#include "UI/KartSessionSettings.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UI/KartPlayerPrefs.h"
#include "UI/KartTrackCatalog.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFrontEnd(TEXT("Kart.FrontEnd"), 1,
		TEXT("1: a normal launch (and Play in the editor) opens the title screen. 0: straight into a race, like -KartNoFrontEnd."));

	/** v2 paint palette (games/kart/kartPaint.ts): pink, cyan, gold, violet; the flag-driven race uses these. */
	const FLinearColor SeatPaint[] = {
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("f0508e"))),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("5ec8e5"))),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("f7c948"))),
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("a67cf0"))),
	};
	/** The web's four lobby bots first, then four more so a full grid has no twins. */
	const TCHAR* BotNames[] = { TEXT("Dante"), TEXT("Priya"), TEXT("Mika"), TEXT("Rosa"), TEXT("Theo"), TEXT("Juno"), TEXT("Kenji"), TEXT("Ada") };

	/** Switches that imply "race now" (test hooks and race options). */
	const TCHAR* SkipSwitches[] = {
		TEXT("KartNoFrontEnd"), TEXT("KartAutoDrive"), TEXT("KartSkipIntro"), TEXT("KartSoloView"), TEXT("KartLegacyWalls"),
		TEXT("KartRoom"), TEXT("KartLobby"), TEXT("KartDebugGrip"), TEXT("KartWebFeel"),
	};
	/** Value flags, matched with their '='. */
	const TCHAR* SkipValues[] = {
		TEXT("KartShots="), TEXT("KartQuitAfter="), TEXT("KartBots="), TEXT("KartLocalPlayers="), TEXT("KartLaps="),
		TEXT("KartVehicle="), TEXT("KartOutfit="), TEXT("KartCloseCam="), TEXT("KartPhoneSeat="), TEXT("KartBotSkill="),
	};
}

void FKartRaceSetup::Sanitize(int32 GridSlots)
{
	GridSlots = FMath::Max(1, GridSlots);
	if (!FKartTrackCatalog::Find(TrackId) && FKartTrackCatalog::All().Num() > 0)
	{
		TrackId = FKartTrackCatalog::All()[0].Id;
	}
	Laps = FMath::Clamp(Laps, 1, MaxLaps);
	LocalPlayers = FMath::Clamp(LocalPlayers, 1, FMath::Min(MaxLocalPlayers, GridSlots));
	Bots = FMath::Clamp(Bots, 0, GridSlots - LocalPlayers);
	BotSkill = FMath::Clamp(BotSkill, 0, 2);
	for (int32 i = Racers.Num(); i < MaxLocalPlayers; ++i)
	{
		Racers.Add(FKartRoster::DefaultChoice(i));
	}
	Racers.SetNum(MaxLocalPlayers);
	for (FKartRacerChoice& Racer : Racers)
	{
		Racer = FKartRoster::Sanitize(Racer);
	}
}

FKartRacePlan FKartRacePlan::Resolve(const FKartRaceSetup* MenuSetup, const TCHAR* Cmd, int32 GridSlots, int32 DefaultBots)
{
	FKartRacePlan Plan;
	GridSlots = FMath::Max(1, GridSlots);
	int32 Bots = MenuSetup ? MenuSetup->Bots : DefaultBots;
	int32 LocalPlayers = MenuSetup ? MenuSetup->LocalPlayers : 1;
	int32 Laps = MenuSetup ? MenuSetup->Laps : 0;
	FParse::Value(Cmd, TEXT("KartBots="), Bots);
	FParse::Value(Cmd, TEXT("KartLocalPlayers="), LocalPlayers);
	int32 FlagLaps = 0;
	if (FParse::Value(Cmd, TEXT("KartLaps="), FlagLaps) && FlagLaps > 0)
	{
		Laps = FlagLaps;
	}
	Plan.LocalPlayers = FMath::Clamp(LocalPlayers, 1, GridSlots);
	Bots = FMath::Clamp(Bots, 0, GridSlots - Plan.LocalPlayers);
	Plan.Laps = FMath::Max(0, Laps);
	Plan.bSoloView = FParse::Param(Cmd, TEXT("KartSoloView")) || (MenuSetup && !MenuSetup->bViewEveryKart);
	Plan.bAutoDrive = FParse::Param(Cmd, TEXT("KartAutoDrive"));
	Plan.bSkipIntro = FParse::Param(Cmd, TEXT("KartSkipIntro"));
	Plan.bLegacyWalls = FParse::Param(Cmd, TEXT("KartLegacyWalls"));
	int32 Skill = MenuSetup ? FMath::Clamp(MenuSetup->BotSkill, 0, 2) : 1;
	FString SkillName;
	if (FParse::Value(Cmd, TEXT("KartBotSkill="), SkillName))
	{
		Skill = SkillName.Equals(TEXT("easy"), ESearchCase::IgnoreCase) ? 0 : SkillName.Equals(TEXT("hard"), ESearchCase::IgnoreCase) ? 2 : 1;
	}
	Plan.BotSkill = Skill == 0 ? EKartBotSkill::Easy : Skill == 2 ? EKartBotSkill::Hard : EKartBotSkill::Normal;

	if (!MenuSetup)
	{
		// The flag-driven race, unchanged: seat paints cycle, every kart gets FKartLook::MakeVaried(seat).
		for (int32 i = 0; i < Plan.LocalPlayers + Bots; ++i)
		{
			FKartSeatSpec& Seat = Plan.Seats.AddDefaulted_GetRef();
			Seat.Kind = i < Plan.LocalPlayers ? EKartSeatKind::Local : EKartSeatKind::Bot;
			Seat.Name = i < Plan.LocalPlayers ? FString::Printf(TEXT("Player %d"), i + 1) : BotNames[(i - Plan.LocalPlayers) % UE_ARRAY_COUNT(BotNames)];
			Seat.Paint = SeatPaint[i % UE_ARRAY_COUNT(SeatPaint)];
			Seat.BotSkill = Plan.BotSkill;
		}
		return Plan;
	}

	// Menu race: players wear their picks; bots take the paints and drivers the players left free.
	TSet<int32> TakenPaints;
	TSet<EKartFace> TakenFaces;
	for (int32 i = 0; i < Plan.LocalPlayers; ++i)
	{
		const FKartRacerChoice Choice = MenuSetup->Racers.IsValidIndex(i) ? MenuSetup->Racers[i] : FKartRoster::DefaultChoice(i);
		FKartSeatSpec& Seat = Plan.Seats.AddDefaulted_GetRef();
		Seat.Kind = EKartSeatKind::Local;
		Seat.Name = Plan.LocalPlayers > 1 ? FString::Printf(TEXT("Player %d"), i + 1) : FString(TEXT("You"));
		Seat.Look = FKartRoster::MakeLook(Choice, Seat.Name);
		Seat.Paint = Seat.Look.Paint;
		Seat.bHasLook = true;
		TakenPaints.Add(FKartRoster::Sanitize(Choice).Paint);
		TakenFaces.Add(Seat.Look.Face);
	}
	const int32 NumPaints = FKartRoster::Paints().Num();
	int32 NextPaint = 0;
	int32 NextRow = 0;
	for (int32 b = 0; b < Bots; ++b)
	{
		while (TakenPaints.Contains(NextPaint % NumPaints) && TakenPaints.Num() < NumPaints)
		{
			++NextPaint;
		}
		const int32 Paint = NextPaint++ % NumPaints;
		TakenPaints.Add(Paint);
		if (TakenPaints.Num() >= NumPaints)
		{
			TakenPaints.Reset(); // a full grid reuses paints
		}
		// A driver the players didn't pick, while there are any left.
		FKartLook Look = FKartLook::MakeVaried(NextRow);
		for (int32 Tries = 0; Tries < 8 && TakenFaces.Contains(Look.Face); ++Tries)
		{
			Look = FKartLook::MakeVaried(++NextRow);
		}
		TakenFaces.Add(Look.Face);
		++NextRow;

		FKartSeatSpec& Seat = Plan.Seats.AddDefaulted_GetRef();
		Seat.Kind = EKartSeatKind::Bot;
		Seat.Name = BotNames[b % UE_ARRAY_COUNT(BotNames)];
		Seat.Paint = FKartRoster::PaintColor(Paint);
		Seat.Look = Look;
		Seat.Look.Paint = Seat.Paint;
		Seat.Look.Name = Seat.Name;
		Seat.bHasLook = true;
		Seat.BotSkill = Plan.BotSkill;
	}
	return Plan;
}

bool KartFrontEndFlags::ShouldSkipFrontEnd(const TCHAR* Cmd)
{
	for (const TCHAR* Switch : SkipSwitches)
	{
		if (FParse::Param(Cmd, Switch))
		{
			return true;
		}
	}
	FString Ignored;
	for (const TCHAR* Value : SkipValues)
	{
		if (FParse::Value(Cmd, Value, Ignored))
		{
			return true;
		}
	}
	return false;
}

bool KartFrontEndFlags::WantsRoom(const TCHAR* Cmd)
{
	return FParse::Param(Cmd, TEXT("KartRoom")) || FParse::Param(Cmd, TEXT("KartLobby"));
}

EKartBoot KartFrontEndFlags::DecideBoot(EKartSessionMode Mode, bool bFirstBoot, const TCHAR* Cmd)
{
	switch (Mode)
	{
	case EKartSessionMode::Party: return EKartBoot::Room;
	case EKartSessionMode::LocalRace: return EKartBoot::LocalRace;
	default: break;
	}
	if (!bFirstBoot)
	{
		return EKartBoot::FrontEnd;
	}
	if (WantsRoom(Cmd))
	{
		return EKartBoot::Room;
	}
	// The screenshot tour drives the menus itself (other flags still shape its race).
	FString Tour;
	if (FParse::Value(Cmd, TEXT("KartUiShots="), Tour))
	{
		return EKartBoot::FrontEnd;
	}
	return ShouldSkipFrontEnd(Cmd) ? EKartBoot::LocalRace : EKartBoot::FrontEnd;
}

UKartSessionSettings* UKartSessionSettings::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UKartSessionSettings>() : nullptr;
}

void UKartSessionSettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Setup = GetDefault<UKartPlayerPrefs>()->LastRace;
	Setup.Sanitize();
}

EKartBoot UKartSessionSettings::DecideBoot(const TCHAR* CommandLine)
{
	const bool bFirstBoot = !bBooted;
	bBooted = true;
	FString Cmd = CommandLine;
	if (bFirstBoot && CVarFrontEnd.GetValueOnGameThread() == 0)
	{
		Cmd += TEXT(" -KartNoFrontEnd");
	}
	const EKartBoot Boot = KartFrontEndFlags::DecideBoot(Mode, bFirstBoot, *Cmd);
	// The command line's choice sticks across the level reloads a race or the room does.
	if (Boot == EKartBoot::Room)
	{
		Mode = EKartSessionMode::Party;
	}
	else if (Boot == EKartBoot::LocalRace && Mode == EKartSessionMode::None)
	{
		Mode = EKartSessionMode::LocalRace;
		bRaceFromMenu = false;
	}
	return Boot;
}

void UKartSessionSettings::SaveSetup()
{
	UKartPlayerPrefs* Prefs = GetMutableDefault<UKartPlayerPrefs>();
	Prefs->LastRace = Setup;
	Prefs->Save();
}

void UKartSessionSettings::StartLocalRace()
{
	Setup.Sanitize();
	SaveSetup();
	Mode = EKartSessionMode::LocalRace;
	bRaceFromMenu = true;
}

void UKartSessionSettings::StartParty()
{
	Mode = EKartSessionMode::Party;
	bRaceFromMenu = false;
}

void UKartSessionSettings::ReturnToMenu(EKartMenuEntry InEntry)
{
	Mode = EKartSessionMode::None;
	bRaceFromMenu = false;
	Entry = InEntry;
	bBooted = true;
}

EKartMenuEntry UKartSessionSettings::ConsumeMenuEntry()
{
	const EKartMenuEntry Out = Entry;
	Entry = EKartMenuEntry::Title;
	return Out;
}

#include "Games/Sumo/SumoGameMode.h"

#include "Games/Sumo/SumoDeviceControls.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Games/Sumo/SumoArena.h"
#include "Games/Sumo/SumoGame.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Sumo;

namespace
{
	const TCHAR* const HouseNames[] = { TEXT("Rosa"), TEXT("Mika"), TEXT("Dante"), TEXT("Priya") };
	/** Every match seed starts here: a room adds its match number, a local launch a slice of the clock. */
	constexpr uint32 BaseSeed = 0x50e0;
	constexpr int64 ClockSeedMask = 0xffff;
	/** A sumo needs an opponent. */
	constexpr int32 MinWrestlers = 2;

	/** House bots of mixed skill, one keen, one steady, one wild: BaseSkill plus a step per bot, cycling. */
	constexpr float HouseBaseSkill = 0.45f;
	constexpr float HouseSkillStep = 0.17f;
	constexpr int32 HouseSkillCycle = 3;
	/** A local seat that plays itself (-SumoAuto) plays sharp. */
	constexpr float AutoLocalSkill = 0.85f;
	constexpr float RoomBotSkill = 0.6f;
	constexpr float SparringSkill = 0.5f;
	/** FKartLook::MakeVaried seeds: the local line-up from 11 (by seat), the room's sparring partner 12. */
	constexpr int32 LocalLookBase = 11;
	constexpr int32 SparringLook = 12;
	const TCHAR* const SparringId = TEXT("house-1");
	/** Room scores leave out the house players (only the room's own seats are ranked). */
	const TCHAR* const HouseIdPrefix = TEXT("house-");

	/** Hits buzz a phone this much (0..1) at least, full at this closing speed (m/s). */
	constexpr double HapticFullStrength = 12.0;
	constexpr double HapticMin = 0.2;

}

FSumoDebugFlags FSumoDebugFlags::Parse(const TCHAR* CommandLine)
{
	FSumoDebugFlags Flags;
#if !UE_BUILD_SHIPPING
	if (CommandLine)
	{
		FParse::Value(CommandLine, TEXT("SumoSkill="), Flags.BotSkill);
		uint32 Seed = 0;
		if (FParse::Value(CommandLine, TEXT("SumoSeed="), Seed))
		{
			Flags.Seed = Seed;
		}
	}
#endif
	return Flags;
}

ASumoGameMode::ASumoGameMode()
{
	ArcadeId = TEXT("sumo");
	CommandLinePrefix = TEXT("Sumo");
	DefaultBots = 2;
}

void ASumoGameMode::StartPlay()
{
	DebugFlags = FSumoDebugFlags::Parse(FCommandLine::Get());
	Super::StartPlay();
}

void ASumoGameMode::SpawnVenue()
{
	UWorld* World = GetWorld();
	TActorIterator<ASumoArena> Placed(World);
	Arena = Placed ? *Placed : World->SpawnActor<ASumoArena>();
}

void ASumoGameMode::BuildRoomSeats(TConstArrayView<FMcSeat> Players)
{
	for (const FMcSeat& Player : Players)
	{
		FSumoSeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? ESumoPilot::Bot : ESumoPilot::Phone;
		Seat.Skill = RoomBotSkill;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	if (Seats.Num() == 1)
	{
		FSumoSeat& Partner = Seats.AddDefaulted_GetRef();
		Partner.Id = SparringId;
		Partner.Name = HouseNames[0];
		Partner.Kind = ESumoPilot::Bot;
		Partner.Skill = SparringSkill;
		Partner.Look = FKartLook::MakeVaried(SparringLook);
	}
	// Each room match replays the same way for everyone watching, and differs from the last.
	const URoomHostSubsystem* Room = GetRoom();
	Seed = BaseSeed + static_cast<uint32>(FMath::Max(0, Room ? Room->GetRoom().MatchNumber : 0));
	if (DebugFlags.Seed.IsSet())
	{
		Seed = DebugFlags.Seed.GetValue();
	}
}

void ASumoGameMode::AdjustLaunchSeats(FArcadeLaunchOptions& InOutLaunch) const
{
	InOutLaunch.EnsureMinimumSeats(MinWrestlers);
}

void ASumoGameMode::BuildLocalSeats(const FArcadeLaunchOptions& InLaunch)
{
	// AdjustLaunchSeats already added the sparring partner a lone wrestler needs.
	const int32 Locals = InLaunch.Locals;
	const int32 Bots = InLaunch.Bots;
	for (int32 i = 0; i < Locals + Bots; ++i)
	{
		const bool bLocal = i < Locals;
		const int32 House = i - Locals;
		FSumoSeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[House % UE_ARRAY_COUNT(HouseNames)];
		// The device seat plays through the phone path (AArcadeGameModeBase::MakeDeviceControls).
		Seat.Kind = bLocal && !InLaunch.bAutoPlay ? (i == 0 && InLaunch.bDeviceSeat ? ESumoPilot::Phone : ESumoPilot::Local) : ESumoPilot::Bot;
		Seat.Skill = bLocal ? AutoLocalSkill : (DebugFlags.BotSkill >= 0.f ? DebugFlags.BotSkill : HouseBaseSkill + HouseSkillStep * (House % HouseSkillCycle));
		Seat.Look = FKartLook::MakeVaried(i + LocalLookBase);
	}
	Seed = DebugFlags.Seed.IsSet() ? DebugFlags.Seed.GetValue() : BaseSeed + static_cast<uint32>(FDateTime::Now().GetTicks() & ClockSeedMask);
}

void ASumoGameMode::SpawnGame()
{
	Game = SpawnArcadeGame<ASumoGame>();
	Game->Setup(Arena, Seats, GetWorld()->GetFirstPlayerController(), Seed);
	Game->OnQuit = MakeQuitCallback();
	Game->OnRematch = MakeRematchCallback();
}

void ASumoGameMode::BindRoomCallbacks(URoomHostSubsystem& Room)
{
	const TWeakObjectPtr<ASumoGameMode> WeakThis(this);
	Game->OnMatchStart = [WeakThis]()
	{
		if (const ASumoGameMode* Mode = WeakThis.Get())
		{
			Mode->StartRoomTurn();
		}
	};
	Game->OnRoundStart = [WeakThis](int32)
	{
		if (const ASumoGameMode* Mode = WeakThis.Get())
		{
			Mode->SendPrompts();
		}
	};
	Game->OnEvent = [WeakThis](const FRingEvent& Event)
	{
		if (const ASumoGameMode* Mode = WeakThis.Get())
		{
			Mode->SendEventFeedback(Event);
		}
	};
	Game->OnFinished = [WeakThis]()
	{
		if (ASumoGameMode* Mode = WeakThis.Get())
		{
			Mode->CompleteRoomMatch();
		}
	};
}

void ASumoGameMode::HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame)
{
	Game->FeedPhone(Seat.PlayerId, Frame);
}

void ASumoGameMode::HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data)
{
	// The motion pad: its big A button dashes (B too, so either thumb works); releases are not needed.
	TCHAR Button = 0;
	if (Event == MovecadeKart::Net::PadPressEvent && Data.IsValid() && MovecadeKart::Net::ReadPadButton(*Data, Button))
	{
		Game->PressDash(FromPlayerId);
	}
}

bool ASumoGameMode::CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const
{
	for (const FPuck& Puck : Game->GetMatch().Pucks)
	{
		if (!Puck.Id.StartsWith(HouseIdPrefix))
		{
			FMcScoreEntry& Entry = OutScores.AddDefaulted_GetRef();
			Entry.PlayerId = Puck.Id;
			Entry.Score = Puck.Score;
		}
	}
	return true;
}

void ASumoGameMode::StartRoomTurn() const
{
	// Simultaneous play: nobody's turn, everyone's countdown.
	if (URoomHostSubsystem* Room = GetRoom())
	{
		Room->AdvanceTurn(FString());
		Room->PublishMatchCountdown(static_cast<int32>(CountdownMs));
	}
}

void ASumoGameMode::SendPrompts() const
{
	URoomHostSubsystem* Room = GetRoom();
	if (!Room || !Game)
	{
		return;
	}
	const TArray<FSumoSeat>& GameSeats = Game->GetSeats();
	const FString Title = FString::Printf(TEXT("Sumo Bumpers · round %d"), Game->GetMatch().Round);
	for (int32 i = 0; i < GameSeats.Num(); ++i)
	{
		if (GameSeats[i].Kind == ESumoPilot::Phone)
		{
			Room->SendPadPrompt(GameSeats[i].Id, Title, TEXT("Tilt to roll · A: dash"), TEXT("DASH"), FString(), MovecadeKart::Net::HexColor(Game->ColorOf(i)), TEXT("tilt"));
		}
	}
}

bool ASumoGameMode::IsPhone(int32 Slot) const
{
	const FSumoSeat* Seat = Game ? Game->SeatOf(Slot) : nullptr;
	return Seat && Seat->Kind == ESumoPilot::Phone;
}

void ASumoGameMode::SendEventFeedback(const FRingEvent& Event) const
{
	URoomHostSubsystem* Room = GetRoom();
	if (!Room || !Game)
	{
		return;
	}
	switch (Event.Kind)
	{
	case EEventKind::Bump:
	case EEventKind::DashHit:
	case EEventKind::ShieldBlock:
		SendHitHaptics(*Room, Event);
		break;
	case EEventKind::RingOut:
		SendRingOutFeedback(*Room, Event);
		break;
	case EEventKind::RoundWin:
		SendRoundWinFeedback(*Room, Event);
		break;
	case EEventKind::PowerPickup:
		SendPowerFeedback(*Room, Event);
		break;
	default:
		break;
	}
}

void ASumoGameMode::SendHitHaptics(URoomHostSubsystem& Room, const FRingEvent& Event) const
{
	const FMatch& M = Game->GetMatch();
	for (const int32 Slot : { Event.A, Event.B })
	{
		if (!IsPhone(Slot) || !M.Pucks.IsValidIndex(Slot))
		{
			continue;
		}
		if (const FMcSeat* Seat = Room.GetRoom().FindByPlayerId(M.Pucks[Slot].Id))
		{
			Room.SendHaptic(Seat->SeatIndex(), static_cast<float>(FMath::Clamp(Event.Strength / HapticFullStrength, HapticMin, 1.0)));
		}
	}
}

void ASumoGameMode::SendRingOutFeedback(URoomHostSubsystem& Room, const FRingEvent& Event) const
{
	const FMatch& M = Game->GetMatch();
	const FPuck* Victim = M.Pucks.IsValidIndex(Event.A) ? &M.Pucks[Event.A] : nullptr;
	const FPuck* Killer = M.Pucks.IsValidIndex(Event.B) ? &M.Pucks[Event.B] : nullptr;
	if (Victim && IsPhone(Event.A))
	{
		const FString Detail = Killer ? FString::Printf(TEXT("%s pushed you in · round %d"), *Killer->Name, M.Round) : FString::Printf(TEXT("You rolled off · round %d"), M.Round);
		Room.SendPlayFeedback(Victim->Id, Killer ? TEXT("Knocked out!") : TEXT("Ring out!"), Detail, TEXT("warning"), TEXT("warning"));
	}
	if (Killer && IsPhone(Event.B))
	{
		Room.SendPlayFeedback(Killer->Id, TEXT("Knockout!"),
			FString::Printf(TEXT("+%d point · %s is in the sea · %d KOs"), KnockoutPoints, Victim ? *Victim->Name : TEXT(""), Killer->Knockouts), TEXT("success"), TEXT("celebration"), true);
	}
}

void ASumoGameMode::SendRoundWinFeedback(URoomHostSubsystem& Room, const FRingEvent& Event) const
{
	const FMatch& M = Game->GetMatch();
	if (!M.Pucks.IsValidIndex(Event.A) || !IsPhone(Event.A))
	{
		return;
	}
	const FPuck& Winner = M.Pucks[Event.A];
	Room.SendPlayFeedback(Winner.Id, TEXT("Round won!"), FString::Printf(TEXT("+%d points · %d of %d round wins"), RoundWinPoints, Winner.RoundWins, WinsForMatch), TEXT("success"),
		TEXT("celebration"), true);
}

void ASumoGameMode::SendPowerFeedback(URoomHostSubsystem& Room, const FRingEvent& Event) const
{
	const FMatch& M = Game->GetMatch();
	if (!M.Pucks.IsValidIndex(Event.A) || !IsPhone(Event.A))
	{
		return;
	}
	const TCHAR* Title = TEXT("Turbo!");
	const TCHAR* Detail = TEXT("Dash recharges fast");
	if (Event.Power == EPower::Heavy)
	{
		Title = TEXT("Heavy!");
		Detail = TEXT("Hard to move, hits like a truck");
	}
	else if (Event.Power == EPower::Shield)
	{
		Title = TEXT("Shield!");
		Detail = TEXT("The next hit bounces off");
	}
	Room.SendPlayFeedback(M.Pucks[Event.A].Id, Title, Detail, TEXT("success"), TEXT("impact"));
}

TSharedPtr<MovecadeKart::Input::IDeviceControlScheme> ASumoGameMode::MakeDeviceControls()
{
	return MakeShared<FSumoDeviceControls>();
}

#include "Games/Samurai/SamuraiGameMode.h"

#include "Art/KartMaterials.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Samurai/SamuraiDojo.h"
#include "Games/Samurai/SamuraiGame.h"
#include "Games/Samurai/SamuraiLog.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"

using namespace MovecadeKart::Samurai;

namespace
{
	const TCHAR* const SeatColors[] = { TEXT("#f0506e"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Rosa"), TEXT("Mika"), TEXT("Dante"), TEXT("Priya") };
	/** A local seat only uses a skill when -SamuraiAuto hands it to the house AI: then it plays sharp. */
	constexpr float LocalAutoSkill = 0.85f;
	/** House bots cycle through three skills by seat (0.45, 0.6, 0.75). */
	constexpr float HouseSkillBase = 0.45f;
	constexpr float HouseSkillStep = 0.15f;
	constexpr int32 HouseSkillLevels = 3;
	/** Room bots all play at this skill. */
	constexpr float RoomBotSkill = 0.6f;
	/** Local seat i wears FKartLook::MakeVaried(i + this). */
	constexpr int32 SeatLookOffset = 4;
	/** A combo this long is a celebration on the phone, not just an impact. */
	constexpr int32 PhoneCelebrationCombo = 10;
}

FSamuraiDebugFlags FSamuraiDebugFlags::Parse(const TCHAR* CommandLine)
{
	FSamuraiDebugFlags Flags;
#if !UE_BUILD_SHIPPING
	if (CommandLine)
	{
		Flags.bPracticeDemo = FParse::Param(CommandLine, TEXT("SamuraiPracticeDemo"));
	}
#endif
	return Flags;
}

ASamuraiGameMode::ASamuraiGameMode()
{
	ArcadeId = TEXT("samurai");
	CommandLinePrefix = TEXT("Samurai");
	DefaultLocals = 1;
	DefaultBots = 1;
}

void ASamuraiGameMode::SpawnVenue()
{
	UWorld* World = GetWorld();
	for (TActorIterator<ASamuraiDojo> It(World); It; ++It)
	{
		Dojo = *It;
		break;
	}
	if (!Dojo)
	{
		Dojo = World->SpawnActor<ASamuraiDojo>();
	}
}

void ASamuraiGameMode::BuildRoomSeats(TConstArrayView<FMcSeat> Players)
{
	for (const FMcSeat& Player : Players)
	{
		FSamuraiSeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? ESwordKind::Bot : ESwordKind::Phone;
		Seat.Skill = RoomBotSkill;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
	// runtime.ts: the round's seed is 0x5a3c + the match number.
	const int32 MatchNumber = GetRoom() ? GetRoom()->GetRoom().MatchNumber : 0;
	Seed = DefaultSeed + static_cast<uint32>(FMath::Max(0, MatchNumber));
	UE_LOG(LogSamurai, Log, TEXT("Room match %d: %d swordsm(e)n, seed %u"), MatchNumber, Seats.Num(), Seed);
}

void ASamuraiGameMode::BuildLocalSeats(const FArcadeLaunchOptions& InLaunch)
{
	for (int32 i = 0; i < InLaunch.Locals + InLaunch.Bots; ++i)
	{
		FSamuraiSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < InLaunch.Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - InLaunch.Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !InLaunch.bAutoPlay ? ESwordKind::Local : ESwordKind::Bot;
		Seat.Skill = bLocal ? LocalAutoSkill : HouseSkillBase + HouseSkillStep * (i % HouseSkillLevels);
		Seat.Color = MovecadeKart::Art::Hex(SeatColors[i % UE_ARRAY_COUNT(SeatColors)]);
		Seat.Look = FKartLook::MakeVaried(i + SeatLookOffset);
	}
}

void ASamuraiGameMode::SpawnGame()
{
	Game = SpawnArcadeGame<ASamuraiGame>();
	Game->Setup(Dojo, Seats, GetWorld()->GetFirstPlayerController(), Seed, FSamuraiDebugFlags::Parse(FCommandLine::Get()));
	Game->OnQuit = MakeQuitCallback();
	Game->OnRematch = MakeRematchCallback();
}

void ASamuraiGameMode::BindRoomCallbacks(URoomHostSubsystem& Room)
{
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = &Room;
	TWeakObjectPtr<ASamuraiGameMode> WeakThis = this;
	// Simultaneous: every phone streams; the engine counts its own 3-2-1.
	Game->OnMatchStart = [WeakRoom]()
	{
		if (URoomHostSubsystem* Host = WeakRoom.Get())
		{
			Host->AdvanceTurn(FString());
			Host->PublishMatchCountdown(static_cast<int32>(CountdownMs));
		}
	};
	Game->OnEffect = [WeakThis, WeakRoom](const FEffect& Effect)
	{
		const ASamuraiGameMode* Mode = WeakThis.Get();
		if (URoomHostSubsystem* Host = WeakRoom.Get(); Mode && Host)
		{
			Mode->SendEffectFeedback(*Host, Effect);
		}
	};
	Game->OnFinished = [WeakThis]()
	{
		if (ASamuraiGameMode* Mode = WeakThis.Get())
		{
			Mode->CompleteRoomMatch();
		}
	};
}

void ASamuraiGameMode::SendEffectFeedback(URoomHostSubsystem& Room, const FEffect& Effect) const
{
	if (Effect.Label.IsEmpty())
	{
		return;
	}
	const FPlayer* Player = Game ? Game->GetMatch().FindPlayer(Effect.PlayerId) : nullptr;
	const bool bBad = Effect.Kind == EEffectKind::Bomb || Effect.Kind == EEffectKind::Miss;
	const bool bCombo = Effect.Kind == EEffectKind::Combo;
	const FString Detail = FString::Printf(TEXT("%s%d points · %d lives · combo %d"), Effect.ScoreDelta > 0 ? TEXT("+") : TEXT(""), Effect.ScoreDelta,
		Player ? Player->Lives : 0, Player ? Player->Combo : 0);
	Room.SendPlayFeedback(Effect.PlayerId, Effect.Label, Detail, bBad ? TEXT("warning") : TEXT("success"),
		bBad ? TEXT("warning") : (bCombo ? TEXT("celebration") : TEXT("impact")), bCombo && Player && Player->Combo >= PhoneCelebrationCombo);
}

void ASamuraiGameMode::HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame)
{
	if (Game)
	{
		Game->FeedPhone(Seat.PlayerId, Frame);
	}
}

bool ASamuraiGameMode::CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const
{
	// Nothing to report during the countdown (every score is still 0).
	if (!Game || Game->GetMatch().Phase == EMatchPhase::Countdown)
	{
		return false;
	}
	for (const FPlayer& Player : Game->GetMatch().Players)
	{
		FMcScoreEntry& Entry = OutScores.AddDefaulted_GetRef();
		Entry.PlayerId = Player.Id;
		Entry.Score = Player.Score;
	}
	return true;
}

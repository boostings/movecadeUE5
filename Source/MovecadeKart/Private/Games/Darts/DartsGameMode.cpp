#include "Games/Darts/DartsGameMode.h"

#include "Art/KartMaterials.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Games/Darts/DartsDebugFlags.h"
#include "Games/Darts/DartsGame.h"
#include "Games/Darts/DartsGameTuning.h"
#include "Games/Darts/DartsPub.h"
#include "Games/Darts/DartsRelease.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"
#include "TimerManager.h"

using namespace MovecadeKart::Darts;
using MovecadeKart::Net::QuoteJson;

namespace
{
	/** The web's lane colours, one per seat. */
	const TCHAR* const SeatColors[] = { TEXT("#f0508e"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Dante"), TEXT("Priya"), TEXT("Mika"), TEXT("Rosa") };
	/** Local people throw sharp (the bots' skill only matters if -DartsAuto hands them to the AI); house bots vary. */
	constexpr float LocalSkill = 0.85f;
	constexpr float HouseSkillBase = 0.45f;
	constexpr float HouseSkillStep = 0.15f;
	/** A room's own bots throw at this skill. */
	constexpr float RoomBotSkill = 0.6f;
	/** Each party game starts its line-up at its own FKartLook::MakeVaried row, so the games don't all show the same faces. */
	constexpr int32 LookSeedBase = 2;

	/** runtime.ts: darts:aim streams at 12.5 Hz; darts:turn repeats at least every 250 ms (a phone that missed one). */
	constexpr float AimIntervalSeconds = 0.08f;
	constexpr double TurnRepeatSeconds = 0.25;

	/** -DartsLocalAuto: grip this long after the hand comes free, hold at least MinHold, let go in the sweet spot. */
	constexpr float LocalAutoIntervalSeconds = 0.02f;
	constexpr double LocalAutoGripDelaySeconds = 1.1;
	constexpr double LocalAutoMinHoldSeconds = 0.4;

	const TCHAR* PhaseName(EMatchPhase Phase)
	{
		switch (Phase)
		{
		case EMatchPhase::Playing: return TEXT("playing");
		case EMatchPhase::Ended: return TEXT("ended");
		default: return TEXT("countdown");
		}
	}

	const TCHAR* JsonBool(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	/** One dart's room traffic: the thrower's toast and haptic, darts:result and darts:replay-outcome. */
	void SendThrowToRoom(URoomHostSubsystem& Room, const FString& PlayerId, const FThrowOutcome& Outcome)
	{
		const FDartLanding& L = Outcome.Landing;
		const bool bMajor = L.bBounty || L.Hit.Ring == ERing::Bull;
		const bool bScored = L.ScoreDelta > 0;
		Room.SendPlayFeedback(PlayerId, Outcome.Label, FString::Printf(TEXT("%d points%s"), L.ScoreDelta, L.bBounty ? TEXT(" · Double points") : TEXT("")),
			bScored ? TEXT("success") : TEXT("warning"), bMajor ? TEXT("celebration") : (bScored ? TEXT("impact") : TEXT("warning")), bMajor);
		Room.SendGameEvent(TEXT("darts:result"), FString::Printf(TEXT("{\"playerId\":%s,\"label\":%s,\"score\":%d,\"bounty\":%s,\"roundBonus\":%d,\"finished\":%s}"),
			*QuoteJson(PlayerId), *QuoteJson(Outcome.Label), L.ScoreDelta, JsonBool(L.bBounty), Outcome.RoundBonus, JsonBool(Outcome.bFinished)));
		Room.SendGameEvent(TEXT("darts:replay-outcome"), FString::Printf(TEXT("{\"playerId\":%s,\"label\":%s,\"score\":%d}"),
			*QuoteJson(PlayerId), *QuoteJson(Outcome.Label), L.ScoreDelta));
	}
}

ADartsGameMode::ADartsGameMode()
{
	ArcadeId = TEXT("darts");
	CommandLinePrefix = TEXT("Darts");
	DefaultLocals = 1;
	DefaultBots = 1;
}

void ADartsGameMode::SpawnVenue()
{
	UWorld* World = GetWorld();
	for (TActorIterator<ADartsPub> It(World); It; ++It)
	{
		Pub = *It;
		break;
	}
	if (!Pub)
	{
		Pub = World->SpawnActor<ADartsPub>();
	}
}

void ADartsGameMode::BuildRoomSeats(TConstArrayView<FMcSeat> Players)
{
	Seats.Reset();
	for (const FMcSeat& Player : Players)
	{
		FDartsSeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EThrowerKind::Bot : EThrowerKind::Phone;
		Seat.Skill = RoomBotSkill;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
}

void ADartsGameMode::BuildLocalSeats(const FArcadeLaunchOptions& InLaunch)
{
	Seats.Reset();
	for (int32 i = 0; i < InLaunch.Locals + InLaunch.Bots; ++i)
	{
		FDartsSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < InLaunch.Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - InLaunch.Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !InLaunch.bAutoPlay ? EThrowerKind::Local : EThrowerKind::Bot;
		Seat.Skill = bLocal ? LocalSkill : HouseSkillBase + HouseSkillStep * (i % 3);
		Seat.Color = MovecadeKart::Art::Hex(SeatColors[i % UE_ARRAY_COUNT(SeatColors)]);
		Seat.Look = FKartLook::MakeVaried(i + LookSeedBase);
	}
}

void ADartsGameMode::SpawnGame()
{
	const FDartsDebugFlags Debug = FDartsDebugFlags::Parse(FCommandLine::Get());
	Game = SpawnArcadeGame<ADartsGame>();
	Game->Setup(Pub, Seats, GetWorld()->GetFirstPlayerController(), Debug.bShotOnBigHit);
	Game->OnQuit = MakeQuitCallback();
	Game->OnRematch = MakeRematchCallback();
	if (Debug.bLocalAuto)
	{
		GetWorldTimerManager().SetTimer(LocalAutoTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { TickLocalAutoPlay(); }),
			LocalAutoIntervalSeconds, /*bLoop*/ true);
	}
}

void ADartsGameMode::BindRoomCallbacks(URoomHostSubsystem& Room)
{
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = &Room;
	// runtime.ts: every seated phone streams (activePlayerId null), once per match; the engine counts its own 3-2-1.
	Game->OnMatchStart = [WeakRoom]()
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->AdvanceTurn(FString());
			R->PublishMatchCountdown(static_cast<int32>(CountdownMs));
		}
	};
	Game->OnThrow = [WeakRoom](const FString& PlayerId, const FThrowOutcome& Outcome)
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			SendThrowToRoom(*R, PlayerId, Outcome);
		}
	};
	Game->OnFinished = [WeakThis = TWeakObjectPtr<ADartsGameMode>(this)]()
	{
		if (ADartsGameMode* Self = WeakThis.Get())
		{
			// The phones see the match end before the final scores land.
			Self->PushTurnAndAim();
			Self->CompleteRoomMatch();
		}
	};
	GetWorldTimerManager().SetTimer(AimTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { PushTurnAndAim(); }), AimIntervalSeconds, /*bLoop*/ true);
}

void ADartsGameMode::HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame)
{
	Game->FeedPhone(Seat.PlayerId, Frame);
}

void ADartsGameMode::HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data)
{
	if (Event != TEXT("darts:release") || !Data.IsValid())
	{
		return;
	}
	if (const TOptional<FDartGesture> Gesture = GestureFromRelease(*Data))
	{
		Game->SubmitRelease(FromPlayerId, *Gesture);
	}
}

bool ADartsGameMode::CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const
{
	// Nothing to report during the engine's own countdown (runtime.ts holds scores until play).
	if (!Game || Game->GetMatch().Phase == EMatchPhase::Countdown)
	{
		return false;
	}
	for (const FDartsPlayer& P : Game->GetMatch().Players)
	{
		FMcScoreEntry& Entry = OutScores.AddDefaulted_GetRef();
		Entry.PlayerId = P.Id;
		Entry.Score = P.Score;
	}
	return true;
}

void ADartsGameMode::PushTurnAndAim()
{
	URoomHostSubsystem* Room = GetRoom();
	if (!Room || !Game)
	{
		return;
	}
	const FDartsMatch& M = Game->GetMatch();
	const FDartsPlayer* Current = M.ActivePlayer();
	const bool bIntro = Game->GetStage() == EDartsStage::Intro;
	const int32 DartsLeft = Current && M.Phase == EMatchPhase::Playing && !bIntro ? FMath::Max(0, DartsPerRound - M.TurnDarts) : 0;
	const int32 Round = Current ? FMath::Min((Current->DartsThrown - M.TurnDarts) / DartsPerRound, RoundCount - 1) : RoundCount;
	const FString Turn = FString::Printf(TEXT("{\"activePlayerId\":%s,\"activePlayerName\":%s,\"dartsLeft\":%d,\"round\":%d,\"phase\":\"%s\"}"),
		Current ? *QuoteJson(Current->Id) : TEXT("null"), Current ? *QuoteJson(Current->Name) : TEXT("null"), DartsLeft, Round, PhaseName(M.Phase));
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (Turn != LastTurnJson || Now - TurnSentAt >= TurnRepeatSeconds)
	{
		LastTurnJson = Turn;
		TurnSentAt = Now;
		Room->SendGameEvent(TEXT("darts:turn"), Turn);
	}
	FVector2D Aim;
	if (Current && M.Phase == EMatchPhase::Playing && Game->GetReticle(Aim))
	{
		Room->SendGameEvent(TEXT("darts:aim"), FString::Printf(TEXT("[{\"p\":%s,\"x\":%.3f,\"y\":%.3f}]"), *QuoteJson(Current->Id), Aim.X, Aim.Y));
	}
}

void ADartsGameMode::TickLocalAutoPlay()
{
#if !UE_BUILD_SHIPPING
	const FDartsSeat* Thrower = Game ? Game->GetThrower() : nullptr;
	if (!Thrower || Thrower->Kind != EThrowerKind::Local || Game->GetMatch().Phase != EMatchPhase::Playing)
	{
		LocalAutoGripAt = -1.0;
		if (Game)
		{
			Game->SetTestGrip(false);
		}
		return;
	}
	const double Now = Game->GetClock();
	const FLocalThrow& Local = Game->GetLocal();
	if (!Local.bGripped)
	{
		LocalAutoGripAt = LocalAutoGripAt < 0.0 ? Now + LocalAutoGripDelaySeconds : LocalAutoGripAt;
		Game->SetTestGrip(Now >= LocalAutoGripAt);
	}
	else if (Tuning::IsSweet(Local.Power) && Now - Local.GripAt > LocalAutoMinHoldSeconds)
	{
		Game->SetTestGrip(false);
		LocalAutoGripAt = -1.0;
	}
#endif
}

void ADartsGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	// The world's timer manager outlives level travel in practice: clear ours (the base clears its own).
	GetWorldTimerManager().ClearTimer(AimTimer);
	GetWorldTimerManager().ClearTimer(LocalAutoTimer);
	Super::EndPlay(Reason);
}

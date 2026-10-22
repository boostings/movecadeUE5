// AFishingGame's anglers: the hands (local keys and pads, phones, the motion pad's button) and the house bots.
// The avatars' poses are in FishingGamePoses.cpp.
#include "Avatar/ArcadeAvatar.h"
#include "GameFramework/PlayerController.h"
#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingGameInternal.h"
#include "Games/Fishing/FishingLake.h"
#include "InputCoreTypes.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Fishing;
using namespace MovecadeKart::Fishing::GameTuning;
using namespace MovecadeKart::Fishing::GameMath;

namespace
{
	// Local hands.
	/** The weakest cast a tap makes (a release at the meter's empty end still casts). */
	constexpr double MinLocalCastPower = 0.04;
	/** A stick counts as cranking past this deflection. */
	constexpr double StickCrankDeadZone = 0.55;
	/** Each alternate press of a crank key pair, and each wheel notch, in revolutions. */
	constexpr double AlternateKeyRevs = 0.25;
	constexpr double WheelNotchRevs = 0.16;
	/** The mouse circling: pixels a frame before it counts, and the most one frame can add (revolutions). */
	constexpr float MouseCrankMinPixels = 2.5f;
	constexpr double MouseCrankMaxRevs = 0.25;
	/** The crank's ceiling (rev/s) and its smoothing time constant (s). */
	constexpr double MaxCrankRate = 4.0;
	constexpr double CrankSmoothingSeconds = 0.22;
	/** The local crank's rev/s to the rules' reel: the phone detector's own scale (FMotionTuning). */
	constexpr double CrankDeadRevPerSec = 0.12;
	constexpr double CrankFullRevPerSec = 1.6;
	/** A phone that has sent nothing for this long (s) stops reeling. */
	constexpr double PhoneStaleSeconds = 0.35;

	// House bots (ms of match time; Skill 0 sloppy .. 1 sharp).
	/** Cast power: 0.45 + 0.5 Skill, give or take 0.12 + 0.3 (1 - Skill). */
	constexpr double BotPowerBase = 0.45;
	constexpr double BotPowerSkill = 0.5;
	constexpr double BotPowerScatter = 0.12;
	constexpr double BotPowerSloppyScatter = 0.3;
	constexpr double BotMinPower = 0.12;
	/** The wind-up: the meter fills over 0.7 s and the cast flies at 0.75 s. */
	constexpr double BotWindupFillSeconds = 0.7;
	constexpr double BotWindupSeconds = 0.75;
	/** Reaction to a bite: 170 ms, plus up to 430 ms for a sloppy bot, plus up to 220 ms of luck. */
	constexpr double BotReactMs = 170.0;
	constexpr double BotReactSloppyMs = 430.0;
	constexpr double BotReactLuckMs = 220.0;
	/** The misjudgement's random walk per step, and its bound (0.03 + 0.2 for the sloppiest). */
	constexpr double BotErrorWalk = 0.02;
	constexpr double BotErrorBound = 0.03;
	constexpr double BotErrorSloppyBound = 0.2;
	/** Easing off for a run: how often it notices (0.3 + 0.65 Skill) and how late (60 ms + up to 300 ms). */
	constexpr double BotEaseOdds = 0.3;
	constexpr double BotEaseSkillOdds = 0.65;
	constexpr double BotEaseDelayMs = 60.0;
	constexpr double BotEaseSloppyDelayMs = 300.0;
	/** "Never": a careless bot keeps cranking through the run. */
	constexpr double BotNeverMs = 1e12;
	/** The tension it aims for: a little over the fish's pull, kept inside the green. */
	constexpr double BotTargetPull = 0.45;
	constexpr double BotTargetOffset = 0.14;
	constexpr double BotTargetLow = 0.5;
	constexpr double BotTargetHigh = 0.7;
	/** How fast its hand follows the reel it wants (per step), easing off and cranking. */
	constexpr double BotEaseFollow = 0.3;
	constexpr double BotCrankFollow = 0.08;
	/** The pause after a catch or a loss before the next cast: 500 ms plus up to 900 x (1.3 - Skill). */
	constexpr double BotRecastMs = 500.0;
	constexpr double BotRecastJitterMs = 900.0;
	constexpr double BotRecastSloppiness = 1.3;

}

// Hands -----------------------------------------------------------------------------------------------------------

int32 AFishingGame::LaneOf(const FString& PlayerId) const
{
	return Lanes.IndexOfByPredicate([&PlayerId](const FLane& L) { return L.PlayerId == PlayerId; });
}

void AFishingGame::ReadHands(float Dt)
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		if (Lane.Kind == EAnglerKind::Local)
		{
			ReadLocal(i, Dt);
		}
		if (Lane.bHolding && Lane.Kind != EAnglerKind::Bot)
		{
			// The meter runs empty, full, empty: let go at the top for the far water.
			Lane.Charge = 0.5 - 0.5 * FMath::Cos(2.0 * UE_DOUBLE_PI * (Clock - Lane.HoldAt) / MeterPeriodSeconds);
		}
	}
}

void AFishingGame::Press(int32 i)
{
	if (Stage != EFishingStage::Match || bPaused || !Lanes.IsValidIndex(i))
	{
		return;
	}
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	switch (A.State)
	{
	case EAnglerState::Idle:
		if (Match.Phase == EMatchPhase::Playing && !Lane.bHolding)
		{
			Lane.bHolding = true;
			Lane.HoldAt = Clock;
			Lane.Charge = 0.0;
		}
		break;
	case EAnglerState::Waiting:
	case EAnglerState::Bite:
		Hook(Match, A.PlayerId);
		DrainEffects();
		break;
	default:
		break;
	}
}

void AFishingGame::Release(int32 i)
{
	if (!Lanes.IsValidIndex(i) || !Lanes[i].bHolding)
	{
		return;
	}
	FLane& Lane = Lanes[i];
	Lane.bHolding = false;
	if (Stage == EFishingStage::Match && !bPaused && Cast(Match, Lane.PlayerId, FMath::Max(MinLocalCastPower, Lane.Charge)))
	{
		DrainEffects();
	}
}

void AFishingGame::PadPress(const FString& PlayerId)
{
	Press(LaneOf(PlayerId));
}

void AFishingGame::PadRelease(const FString& PlayerId)
{
	Release(LaneOf(PlayerId));
}

void AFishingGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	const int32 i = LaneOf(PlayerId);
	if (i == INDEX_NONE)
	{
		return;
	}
	FLane& Lane = Lanes[i];
	const TOptional<FFlick> Flick = Lane.Phone.Motion.Feed(FMotionSample::FromFrame(Frame));
	Lane.Phone.Reel = Lane.Phone.Motion.Reel();
	Lane.Phone.LastFrameAt = Clock;
	if (!Flick || Stage != EFishingStage::Match || bPaused)
	{
		return;
	}
	const FAngler& A = Match.Anglers[i];
	const bool bLineIn = A.State == EAnglerState::Idle;
	if (bLineIn && Flick->Direction == EFlick::Back)
	{
		// The wind-up before the cast: the avatar draws the rod back.
		Lane.Phone.WindupAt = Clock;
	}
	else if (bLineIn && !Lane.bHolding)
	{
		Cast(Match, PlayerId, Flick->Power);
		DrainEffects();
	}
	else if ((A.State == EAnglerState::Waiting || A.State == EAnglerState::Bite) && Flick->Direction != EFlick::Forward)
	{
		// An upward jerk (or a flick of unknown direction from a phone without a gyro) hooks.
		Hook(Match, PlayerId);
		DrainEffects();
	}
}

void AFishingGame::ReadLocal(int32 i, float Dt)
{
	if (!Viewer)
	{
		return;
	}
	FLane& Lane = Lanes[i];
	const APlayerController* PC = Viewer;
	static const FKey CastKeys[] = { EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton, EKeys::Enter, EKeys::Gamepad_RightTrigger };
	bool bJust = false, bDown = false;
	for (const FKey& Key : CastKeys)
	{
		bJust |= PC->WasInputKeyJustPressed(Key);
		bDown |= PC->IsInputKeyDown(Key);
	}
	if (bJust)
	{
		Press(i);
	}
	if (Lane.bHolding && !bDown && Lane.Kind == EAnglerKind::Local)
	{
		Release(i);
	}
	const double Revs = ReadCrankRevs(Lane.Crank);
	const double Rate = Dt > 0.f ? Revs / Dt : 0.0;
	Lane.Crank.Rate += (FMath::Min(Rate, MaxCrankRate) - Lane.Crank.Rate) * (1.0 - FMath::Exp(-Dt / CrankSmoothingSeconds));
}

double AFishingGame::ReadCrankRevs(FLocalCrank& Crank) const
{
	// The crank: the right (or left) stick going round, alternating keys, the mouse wheel, or the mouse circling.
	const APlayerController* PC = Viewer;
	double Revs = 0.0;
	static const TPair<FKey, FKey> Sticks[] = { TPair<FKey, FKey>(EKeys::Gamepad_RightX, EKeys::Gamepad_RightY), TPair<FKey, FKey>(EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY) };
	for (const TPair<FKey, FKey>& Axis : Sticks)
	{
		const FVector2D Stick(PC->GetInputAnalogKeyState(Axis.Key), PC->GetInputAnalogKeyState(Axis.Value));
		if (Stick.Size() > StickCrankDeadZone)
		{
			const double Angle = FMath::Atan2(Stick.Y, Stick.X);
			if (Crank.bStickValid)
			{
				Revs = FMath::Max(Revs, FMath::Abs(FMath::UnwindRadians(Angle - Crank.StickAngle)) / (2.0 * UE_DOUBLE_PI));
			}
			Crank.StickAngle = Angle;
			Crank.bStickValid = true;
			break;
		}
		Crank.bStickValid = false;
	}
	// A quarter turn each time the other key of the same pair goes down.
	static const FKey CrankKeys[][2] = { { EKeys::A, EKeys::D }, { EKeys::Left, EKeys::Right }, { EKeys::Q, EKeys::E }, { EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder } };
	for (int32 Pair = 0; Pair < UE_ARRAY_COUNT(CrankKeys); ++Pair)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (!PC->WasInputKeyJustPressed(CrankKeys[Pair][Side]))
			{
				continue;
			}
			const int32 Key = Pair * 2 + Side;
			if (Crank.LastKey >= 0 && Crank.LastKey != Key && Crank.LastKey / 2 == Pair)
			{
				Revs += AlternateKeyRevs;
			}
			Crank.LastKey = Key;
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) || PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		Revs += WheelNotchRevs;
	}
	float MouseX = 0.f, MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	if (FMath::Sqrt(MouseX * MouseX + MouseY * MouseY) > MouseCrankMinPixels)
	{
		const double Angle = FMath::Atan2(static_cast<double>(MouseY), static_cast<double>(MouseX));
		if (Crank.bMouseValid)
		{
			Revs += FMath::Min(MouseCrankMaxRevs, FMath::Abs(FMath::UnwindRadians(Angle - Crank.MouseAngle)) / (2.0 * UE_DOUBLE_PI));
		}
		Crank.MouseAngle = Angle;
		Crank.bMouseValid = true;
	}
	else
	{
		Crank.bMouseValid = false;
	}
	return Revs;
}

void AFishingGame::FeedReels()
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		const FLane& Lane = Lanes[i];
		const FAngler& A = Match.Anglers[i];
		switch (Lane.Kind)
		{
		case EAnglerKind::Local:
			SetReel(Match, A.PlayerId, FMath::Clamp((Lane.Crank.Rate - CrankDeadRevPerSec) / (CrankFullRevPerSec - CrankDeadRevPerSec), 0.0, MaxReel));
			break;
		case EAnglerKind::Phone:
			SetReel(Match, A.PlayerId, Clock - Lane.Phone.LastFrameAt < PhoneStaleSeconds ? Lane.Phone.Reel : 0.0);
			break;
		default:
			SteerBot(i);
			break;
		}
	}
}

// Bots ------------------------------------------------------------------------------------------------------------

void AFishingGame::SteerBot(int32 i)
{
	if (Match.Phase != EMatchPhase::Playing)
	{
		return;
	}
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	const double Skill = Seats.IsValidIndex(i) ? Seats[i].Skill : 0.6;
	const double Now = Match.ElapsedMs;
	switch (A.State)
	{
	case EAnglerState::Idle:
		SetReel(Match, A.PlayerId, 0.0);
		SteerBotCast(i, Skill);
		break;
	case EAnglerState::Bite:
		if (Lane.Bot.ReactAt < 0.0)
		{
			Lane.Bot.ReactAt = Now + BotReactMs + (1.0 - Skill) * BotReactSloppyMs + BotRng.FRand() * BotReactLuckMs;
		}
		if (Now >= Lane.Bot.ReactAt)
		{
			Lane.Bot.ReactAt = -1.0;
			Hook(Match, A.PlayerId);
		}
		break;
	case EAnglerState::Fighting:
		SteerBotFight(i, Skill);
		break;
	case EAnglerState::Landed:
	case EAnglerState::Lost:
	{
		SetReel(Match, A.PlayerId, 0.0);
		Lane.Bot.Reel = 0.0;
		const double HoldLeftMs = A.State == EAnglerState::Landed ? LandedHoldMs - A.StateMs : LostHoldMs - A.StateMs;
		Lane.Bot.NextActAt = Now + BotRecastMs + BotRng.FRand() * BotRecastJitterMs * (BotRecastSloppiness - Skill) + HoldLeftMs;
		break;
	}
	default:
		SetReel(Match, A.PlayerId, 0.0);
		break;
	}
}

void AFishingGame::SteerBotCast(int32 i, double Skill)
{
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	if (!Lane.bHolding && Match.ElapsedMs >= Lane.Bot.NextActAt)
	{
		// A wind-up, then the cast: sharp bots reach for the far water, sloppy ones scatter.
		Lane.bHolding = true;
		Lane.HoldAt = Clock;
		Lane.Bot.CastPower = FMath::Clamp(BotPowerBase + BotPowerSkill * Skill + BotRng.FRandRange(-1.f, 1.f) * (BotPowerScatter + BotPowerSloppyScatter * (1.0 - Skill)), BotMinPower, 1.0);
	}
	if (!Lane.bHolding)
	{
		return;
	}
	Lane.Charge = Lane.Bot.CastPower * Smooth(0.0, BotWindupFillSeconds, Clock - Lane.HoldAt);
	if (Clock - Lane.HoldAt >= BotWindupSeconds)
	{
		Lane.bHolding = false;
		Cast(Match, A.PlayerId, Lane.Bot.CastPower);
	}
}

void AFishingGame::SteerBotFight(int32 i, double Skill)
{
	FBotMind& Bot = Lanes[i].Bot;
	const FAngler& A = Match.Anglers[i];
	const FFight& F = A.Fight;
	const double Now = Match.ElapsedMs;
	// Its sense of the green drifts, further for a sloppy bot.
	const double ErrorBound = BotErrorSloppyBound * (1.0 - Skill) + BotErrorBound;
	Bot.Error = FMath::Clamp(Bot.Error + (BotRng.FRand() - 0.5) * BotErrorWalk, -ErrorBound, ErrorBound);
	const bool bRunning = F.WarnMs > 0.0 || F.BurstMs > 0.0;
	if (bRunning && Bot.EaseFrom < 0.0)
	{
		// Ease off for the run, after a reaction; a careless bot keeps cranking.
		Bot.EaseFrom = BotRng.FRand() < BotEaseOdds + BotEaseSkillOdds * Skill ? Now + BotEaseDelayMs + (1.0 - Skill) * BotEaseSloppyDelayMs : BotNeverMs;
	}
	if (!bRunning)
	{
		Bot.EaseFrom = -1.0;
	}
	const bool bEase = bRunning && Now >= Bot.EaseFrom;
	// Mid-green, but always a little over the fish's own pull so the line keeps coming in.
	const double Target = FMath::Clamp(F.Pull * BotTargetPull + BotTargetOffset, BotTargetLow, BotTargetHigh) + Bot.Error;
	const double Want = bEase ? 0.0 : ReelForTension(F.Pull, Target);
	Bot.Reel += (Want - Bot.Reel) * (bEase ? BotEaseFollow : BotCrankFollow);
	SetReel(Match, A.PlayerId, Bot.Reel);
}

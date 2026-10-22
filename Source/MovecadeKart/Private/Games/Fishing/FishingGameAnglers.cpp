// AFishingGame's anglers: the hands (local keys and pads, phones, the motion pad's button), the house bots, and the
// avatar's pose for each moment of a cast, a wait and a fight.
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
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

	// The avatar.
	/** Turning round to show a catch (1/s), the yaw it shows at, fanned a little across the lanes. */
	constexpr double TurnRate = 7.0;
	constexpr double ShowYaw = 162.0;
	constexpr double ShowYawFan = 6.0;
	/** A landed fish is shown from this long after landing until this long before the hold ends (s). */
	constexpr double ShowMarginSeconds = 0.35;
	/** A phone's backward flick draws the rod back for this long (s), to this much of a full draw. */
	constexpr double WindupShowSeconds = 0.4;
	constexpr double WindupShowDraw = 0.8;
	/** The whip forward (s) and the rod's spring through it (s). */
	constexpr double CastWhipSeconds = 0.22;
	constexpr double CastSpringSeconds = 0.3;
	/** The lure's arc: 140 cm plus up to 360 cm for a full-power cast. */
	constexpr double CastArcCm = 140.0;
	constexpr double CastArcPowerCm = 360.0;
	/** The reel handle turns 1.6 times a second at a full-speed reel. */
	constexpr double ReelTurnsPerReel = 1.6;
	/** The fish thrashes at the surface inside this distance (m), this many splashes a second. */
	constexpr double ThrashInsideM = 3.5;
	constexpr double ThrashPerSecond = 3.0;
	/** A record's cheer: from 1.2 s to 2.5 s after it (the card's moment). */
	constexpr double RecordCheerSeconds = 2.5;
	/** The avatar's eyes over its feet (cm), for the look direction. */
	constexpr double EyeHeightCm = 165.0;
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

// Poses -----------------------------------------------------------------------------------------------------------

void AFishingGame::UpdateAnglers(float Dt)
{
	const int32 N = LaneCount();
	for (int32 i = 0; i < N; ++i)
	{
		FLane& Lane = Lanes[i];
		const FAngler& A = Match.Anglers[i];
		UArcadeAvatarComponent* Avatar = Avatars[i];
		FLaneFrame Frame;
		Frame.Spot = AFishingLake::AnglerSpot(i, N);
		Frame.InState = Clock - Lane.Show.StateAt;
		Frame.Caught = A.State == EAnglerState::Fighting || A.State == EAnglerState::Landed ? &Species()[A.Fight.Species] : nullptr;
		Frame.bJunk = Frame.Caught && Frame.Caught->Kind == EKind::Junk;
		Frame.bFinished = Stage == EFishingStage::Finished;
		// Landed: turn round to show the camera, then back. At the end everyone turns round.
		const double HoldSeconds = LandedHoldMs / 1000.0;
		Frame.bShowing = (A.State == EAnglerState::Landed && Frame.InState > ShowMarginSeconds && Frame.InState < HoldSeconds - ShowMarginSeconds && !Frame.bJunk) || Frame.bFinished;
		const double WantYaw = Frame.bShowing ? (ShowYaw + (i - (N - 1) / 2.0) * ShowYawFan) : 0.0;
		Lane.Show.TurnYaw += (WantYaw - Lane.Show.TurnYaw) * (1.0 - FMath::Exp(-TurnRate * Dt));
		Avatar->SetWorldLocationAndRotation(Frame.Spot, Facing(Lane.Show.TurnYaw));

		FArcadePoseInput& Pose = Avatar->Pose();
		Pose = FArcadePoseInput();
		Pose.Idle = 1.f;
		Pose.LookWeight = 0.7f;
		Frame.Pose = &Pose;
		PoseForState(i, Frame, Dt);
		if (Frame.bFinished)
		{
			PoseFinished(i, Frame);
		}
		ApplyPose(i, Frame);
		UpdateTackle(i, Frame, Dt);
		UpdateCatch(i, Frame, Dt);
	}
}

void AFishingGame::PoseForState(int32 i, FLaneFrame& Frame, float Dt)
{
	const FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	const bool bFighting = A.State == EAnglerState::Fighting;
	// A gentle sway by default; each state sets its own rod, line and hands below.
	Frame.Pitch = 38.0 + 2.0 * FMath::Sin(Clock * 1.3 + i);
	Frame.Bend = 6.0;
	Frame.Out = AFishingLake::WaterPoint(i, LaneCount(), bFighting ? A.Fight.DistanceM : A.LureM, bFighting ? A.Fight.Lateral : 0.0);
	Frame.Right = FVector(0.55, -0.08, -0.18);
	Frame.Left = FVector(0.48, 0.18, -0.32);
	const double SinceWindup = Clock - Lane.Phone.WindupAt;
	Frame.Charge = Lane.bHolding ? Lane.Charge
		: (SinceWindup < WindupShowSeconds ? Smooth(0.0, 0.15, SinceWindup) * Smooth(WindupShowSeconds, 0.25, SinceWindup) * WindupShowDraw : 0.0);
	// The bobber's float: a spring on the swell.
	Frame.Rest = 1.2 * FMath::Sin(Clock * 1.3 + i * 1.7);
	switch (A.State)
	{
	case EAnglerState::Idle:
	case EAnglerState::Lost:
		PoseIdle(i, Frame);
		break;
	case EAnglerState::Casting:
		PoseCasting(i, Frame);
		break;
	case EAnglerState::Waiting:
	case EAnglerState::Bite:
		PoseWaiting(i, Frame);
		break;
	case EAnglerState::Fighting:
		PoseFighting(i, Frame, Dt);
		break;
	case EAnglerState::Landed:
		PoseLanded(i, Frame);
		break;
	}
}

void AFishingGame::PoseIdle(int32 i, FLaneFrame& Frame)
{
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	FArcadePoseInput& Pose = *Frame.Pose;
	const double Charge = Frame.Charge;
	// Drawing a cast: the rod goes back over the shoulder, the body twists and leans back.
	Frame.Pitch = 40.0 + 92.0 * Charge;
	Frame.Bend = -8.0 * Charge;
	Frame.Right = FVector(0.5 - 0.35 * Charge, -0.05, -0.15 + 0.65 * Charge);
	Frame.Left = FVector(0.45 - 0.1 * Charge, 0.2, -0.3 + 0.35 * Charge);
	Pose.Lean = static_cast<float>(-0.12 * Charge);
	Pose.Twist = static_cast<float>(0.3 * Charge);
	// The bobber hangs under the tip, swinging a little.
	Frame.End = Lane.Show.Tip - FVector(0.0, 0.0, 55.0) + FVector(3.0 * FMath::Sin(Clock * 1.7 + i), 0.0, 0.0);
	Frame.Bob = Frame.End - FVector(0.0, 0.0, 12.0);
	Lane.Show.BobZ = 0.0;
	Lane.Show.BobV = 0.0;
	if (A.State != EAnglerState::Lost)
	{
		return;
	}
	const double LostSeconds = LostHoldMs / 1000.0;
	Pose.Slump = static_cast<float>(Smooth(0.0, 0.2, Frame.InState) * Smooth(LostSeconds, LostSeconds - 0.4, Frame.InState)) * 0.85f;
	if (A.LastLoss == ELoss::Snap)
	{
		// Snapped: the line whips back, then a fresh float is tied on.
		Frame.bLine = Frame.InState > 1.0;
		Frame.bBobber = Frame.InState > 1.0;
		Frame.Bend = -18.0 * Smooth(0.25, 0.0, Frame.InState);
	}
}

void AFishingGame::PoseCasting(int32 i, FLaneFrame& Frame)
{
	const FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	// The whip forward (a quarter second), then the rod follows the line out.
	const double Flight = FMath::Clamp(A.StateMs / FMath::Max(1.0, A.FlightMs), 0.0, 1.0);
	const double Whip = EaseOut(Frame.InState / CastWhipSeconds);
	Frame.Pitch = FMath::Lerp(130.0, 14.0, Whip);
	Frame.Bend = -26.0 * FMath::Sin(UE_DOUBLE_PI * FMath::Clamp(Frame.InState / CastSpringSeconds, 0.0, 1.0));
	Frame.Right = FVector(FMath::Lerp(0.15, 0.78, Whip), -0.02, FMath::Lerp(0.5, 0.05, Whip));
	Frame.Left = FVector(0.5, 0.15, -0.25);
	Frame.Pose->Lean = static_cast<float>(0.22 * Whip);
	// The lure flies an arc from the tip to where the cast lands.
	const FVector Land = AFishingLake::WaterPoint(i, LaneCount(), A.CastM, 0.0);
	const FVector From = Lane.Show.CastFrom.IsNearlyZero() ? Lane.Show.Tip : Lane.Show.CastFrom;
	Frame.Bob = FMath::Lerp(From, Land, Flight) + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * Flight) * (CastArcCm + CastArcPowerCm * A.CastPower));
	Frame.End = Frame.Bob;
	Frame.Sag = 10.0;
}

void AFishingGame::PoseWaiting(int32 i, FLaneFrame& Frame)
{
	const FLane& Lane = Lanes[i];
	const bool bBite = Match.Anglers[i].State == EAnglerState::Bite;
	FArcadePoseInput& Pose = *Frame.Pose;
	// Rod low toward the float; on a bite the tip jitters and the angler crouches, ready to strike.
	Frame.Pitch = (bBite ? 17.0 : 24.0) + 2.0 * FMath::Sin(Clock * 0.9 + i);
	Frame.Bend = bBite ? 14.0 + 4.0 * FMath::Sin(Clock * 30.0) : 7.0;
	Frame.Right = FVector(0.6, -0.05, -0.12);
	Frame.Left = FVector(0.45, 0.18, -0.3);
	Pose.Lean = bBite ? 0.16f : 0.05f;
	Pose.Crouch = bBite ? 0.22f : 0.08f;
	if (bBite)
	{
		// The bobber pulled under.
		Frame.Rest = -15.0 + 3.0 * FMath::Sin(Clock * 22.0);
	}
	Frame.Bob = Frame.Out;
	Frame.End = Frame.Out;
	Frame.Sag = 18.0 + FVector::Dist(Lane.Show.Tip, Frame.Out) * 0.06;
}

void AFishingGame::PoseFighting(int32 i, FLaneFrame& Frame, float Dt)
{
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	const FFight& F = A.Fight;
	FArcadePoseInput& Pose = *Frame.Pose;
	const double Tension = FMath::Clamp(F.Tension, 0.0, 1.3);
	// The strike: a jerk of the rod just after the hook is set.
	const double SinceHook = Clock - Lane.Show.HookedAt;
	const double Jerk = Smooth(0.0, 0.08, SinceHook) * Smooth(0.35, 0.15, SinceHook);
	Frame.Pitch = 34.0 + 14.0 * Tension + 30.0 * Jerk;
	Frame.Bend = 10.0 + 48.0 * FMath::Min(1.1, Tension);
	// The left hand turns the reel handle; the body leans back and crouches into the load.
	Lane.Show.ReelPhase += A.Reel * ReelTurnsPerReel * 2.0 * UE_DOUBLE_PI * Dt;
	Frame.Right = FVector(0.48, -0.04, 0.12 + 0.22 * Tension + 0.35 * Jerk);
	Frame.Left = FVector(0.4, 0.2, -0.05) + FVector(0.0, FMath::Cos(Lane.Show.ReelPhase), FMath::Sin(Lane.Show.ReelPhase)) * 0.11;
	Pose.Lean = static_cast<float>(-0.08 - 0.16 * Tension);
	Pose.Crouch = static_cast<float>(0.22 + 0.25 * Tension);
	Pose.Twist = static_cast<float>(-F.Lateral * 0.25);
	// The line comes up out of the water as the fish nears; it sags until the tension takes it up.
	const double Near = Smooth(4.0, 1.2, F.DistanceM);
	Frame.End = Frame.Out + FVector(0.0, 0.0, -12.0 + 10.0 * Near);
	Frame.Bob = Frame.Out + FVector(-10.0, 6.0 * FMath::Sin(Clock * 9.0), 0.0);
	Frame.Rest = -6.0 + 2.0 * FMath::Sin(Clock * 11.0);
	Frame.Sag = FMath::Max(0.0, 1.0 - Tension / 0.75) * FVector::Dist(Lane.Show.Tip, Frame.End) * 0.14;
	if (A.Reel > 0.1)
	{
		const float Reel = static_cast<float>(FMath::Min(1.0, A.Reel));
		Cues.Play(this, TEXT("fishing.reel"), 0.35f + 0.25f * Reel, 0.9f + 0.5f * Reel);
	}
	// Thrashing at the surface once it's close.
	if (F.DistanceM < ThrashInsideM)
	{
		Lane.Show.ThrashCarry += Dt * ThrashPerSecond;
		if (Lane.Show.ThrashCarry >= 1.0)
		{
			Lane.Show.ThrashCarry -= 1.0;
			Splash(Frame.Out, 0.25 + 0.3 * F.Stamina, false);
		}
	}
}

void AFishingGame::PoseLanded(int32 i, FLaneFrame& Frame)
{
	// The fish comes up to the hands (the line goes slack and vanishes); junk dangles at arm's length.
	Frame.bLine = Frame.InState < 0.5;
	Frame.bBobber = false;
	Frame.bRod = true;
	Frame.Pitch = 70.0;
	Frame.Bend = 4.0;
	Frame.End = HoldPoint(i);
	const double HoldSeconds = LandedHoldMs / 1000.0;
	const double Hold = Smooth(0.25, 0.6, Frame.InState) * Smooth(HoldSeconds, HoldSeconds - 0.3, Frame.InState);
	if (Frame.bJunk)
	{
		Frame.Right = FVector(0.85, 0.05, 0.15);
		Frame.Pose->Slump = static_cast<float>(0.55 * Hold);
	}
	else
	{
		Frame.Right = FVector(0.62, 0.3, -0.08);
		Frame.Left = FVector(0.62, -0.3, -0.08);
		Frame.Pose->Lean = -0.06f;
	}
}

void AFishingGame::PoseFinished(int32 i, FLaneFrame& Frame)
{
	const FAngler& A = Match.Anglers[i];
	FArcadePoseInput& Pose = *Frame.Pose;
	const bool bWin = Match.WinnerIds.Contains(A.PlayerId);
	Frame.bLine = false;
	Frame.bBobber = false;
	if (bWin || A.BiggestSpecies < 0)
	{
		// The winner cheers (holding the best catch high in one hand); an empty bucket slumps.
		Pose.Cheer = bWin ? 0.85f : 0.f;
		Pose.Slump = bWin ? 0.f : 0.35f;
	}
	else
	{
		// Everyone else holds up their best fish in both hands.
		Frame.Right = FVector(0.55, 0.28, 0.3);
		Frame.Left = FVector(0.55, -0.28, 0.3);
	}
}

void AFishingGame::ApplyPose(int32 i, const FLaneFrame& Frame)
{
	const FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	UArcadeAvatarComponent* Avatar = Avatars[i];
	FArcadePoseInput& Pose = *Frame.Pose;
	Pose.Hand[1] = Frame.Right;
	Pose.Hand[0] = Frame.Left;
	const bool bHandsFree = Pose.Cheer > 0.3f;
	Pose.HandWeight[0] = Pose.HandWeight[1] = bHandsFree ? 0.f : 1.f;
	// Look at the float (or the camera when showing off).
	const FVector LookAt = Frame.bShowing ? (Camera ? Camera->GetActorLocation() : Frame.Spot - FVector(500.0, 0.0, -150.0))
		: (A.IsLineOut() ? Frame.Out : Frame.Spot + FVector(300.0, 0.0, 40.0));
	Pose.Look = Avatar->GetComponentTransform().InverseTransformVectorNoScale(LookAt - Avatar->GetComponentLocation() - FVector(0.0, 0.0, EyeHeightCm)).GetSafeNormal();
	const double ReactAge = Clock - Lane.Show.ReactionAt;
	if (Lane.Show.Reaction == EEffectKind::Record && ReactAge < RecordCheerSeconds)
	{
		Pose.Cheer = FMath::Max(Pose.Cheer, static_cast<float>(0.6 * Smooth(1.2, 1.6, ReactAge) * Smooth(2.5, 2.2, ReactAge)));
	}
}

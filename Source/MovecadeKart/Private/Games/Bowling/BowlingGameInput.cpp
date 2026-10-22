// ABowlingGame: who throws the ball. Local players (keys, pads, the mouse), phones (their own release, their sensor
// frames for the live guide) and the house bots.
#include "Games/Bowling/BowlingGame.h"

#include "BowlingGameTuning.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

using namespace MovecadeKart::Bowling;
using namespace MovecadeKart::Bowling::Tuning;

namespace
{
	/** The line and the hook move this far per second at full key or stick. */
	constexpr double AimRate = 0.45;
	constexpr double HookRate = 0.4;
	/** Metres of line per pixel of mouse travel. */
	constexpr double MouseAimPerPixel = 0.0016;
	/** The right stick sets the hook directly past this deflection, easing toward it at this rate. */
	constexpr double HookStickDeadZone = 0.15;
	constexpr double HookStickEase = 6.0;

	/** Mouse: a flick up the screen throws; its speed over this window is the power. */
	constexpr double FlickWindowSeconds = 0.22;
	/** Pixels per second of upward flick for a full-power ball. */
	constexpr double FlickFullSpeed = 1800.0;
	/** A flick this strong takes the meter over from the keys' oscillation. */
	constexpr double FlickThreshold = 0.15;
	/** A flick's power fades at this rate once the hand slows, so a late wobble can't undo a good flick. */
	constexpr double FlickDecay = 2.0;

	/** A phone's swing energy rises at once and settles at this rate per frame, so the arm doesn't flicker. */
	constexpr double PhoneChargeSettle = 0.12;
	/** Swing energy past this starts the phone bowler's backswing. */
	constexpr double PhoneGripCharge = 0.02;

	// The house bots: the hook they throw and how far their wildness (1 - skill) lets each part of the throw stray.
	constexpr double BotStrikeCurve = 0.22;
	constexpr double BotCurveWobble = 0.08;
	constexpr double BotSpareCurve = 0.05;
	constexpr double BotAimWobble = 0.16;
	constexpr double BotPower = 0.78;
	constexpr double BotPowerWobbleLow = -0.25;
	constexpr double BotPowerWobbleHigh = 0.22;
	/** Even a steady bot's power strays this much (the rest scales with its wildness). */
	constexpr double BotPowerSteadyWobble = 0.4;
	constexpr double BotMinPower = 0.2;

	const FKey GripKeys[] = { EKeys::SpaceBar, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton, EKeys::Enter };

	bool IsAnyKeyDown(const APlayerController& PC, TConstArrayView<FKey> Keys)
	{
		for (const FKey& Key : Keys)
		{
			if (PC.IsInputKeyDown(Key))
			{
				return true;
			}
		}
		return false;
	}

	double Axis(const APlayerController& PC, const FKey& Positive, const FKey& PositiveAlt, const FKey& Negative, const FKey& NegativeAlt)
	{
		return (PC.IsInputKeyDown(Positive) || PC.IsInputKeyDown(PositiveAlt) ? 1.0 : 0.0) - (PC.IsInputKeyDown(Negative) || PC.IsInputKeyDown(NegativeAlt) ? 1.0 : 0.0);
	}
}

// ---- Phones -----------------------------------------------------------------------------------------------------

bool ABowlingGame::SubmitRelease(const FString& PlayerId, const FShot& InShot)
{
	const FString* Up = Match.Bowler();
	if (!Up || *Up != PlayerId || (Phase != EBowlingPhase::Aiming && Phase != EBowlingPhase::Intro))
	{
		return false;
	}
	// runtime.ts releaseGestureFromPayload: every field bounded to what the detector can produce.
	FShot Clamped = InShot;
	Clamped.Aim = FMath::Clamp(Clamped.Aim, -MaxAim, MaxAim);
	Clamped.Power = FMath::Clamp(Clamped.Power, 0.0, 1.0);
	Clamped.Curve = FMath::Clamp(Clamped.Curve, -MaxCurve, MaxCurve);
	Clamped.Loft = FMath::Clamp(Clamped.Loft, 0.0, 1.0);
	if (Phase == EBowlingPhase::Intro)
	{
		HeldRelease = Clamped;
		return true;
	}
	Launch(Clamped);
	return true;
}

void ABowlingGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	FThrowDetector& Detector = PhoneDetectors.FindOrAdd(PlayerId);
	Detector.Feed(Frame);
	const FString* Up = Match.Bowler();
	if (!Up || *Up != PlayerId || Phase != EBowlingPhase::Aiming)
	{
		return;
	}
	const double Charge = Detector.Preview().Charge;
	PhoneCharge = Charge > PhoneCharge ? Charge : FMath::Lerp(PhoneCharge, Charge, PhoneChargeSettle);
	if (PhoneCharge > PhoneGripCharge && PhoneGripAt < 0.0)
	{
		PhoneGripAt = Clock;
	}
}

// ---- Local players ----------------------------------------------------------------------------------------------

void ABowlingGame::UpdateLocalInput(float Dt)
{
	const FBowlerSeat* Bowler = GetBowler();
	if (!Viewer || !Bowler || Bowler->Kind != EBowlerKind::Local)
	{
		return;
	}
	// Whoever is up uses any of this machine's inputs: the turns are one at a time.
	const bool bGripDown = IsAnyKeyDown(*Viewer, GripKeys);
	const float MouseY = SteerLocal(Dt);
	if (bGripDown && !Local.bGripped)
	{
		Local.bGripped = true;
		Local.GripAt = Clock;
		Local.bMouseFlick = false;
		MouseTrail.Reset();
	}
	if (Local.bGripped)
	{
		ChargeLocal(Dt, MouseY);
	}
	if (Local.bGripped && !bGripDown)
	{
		FShot Throw;
		Throw.Aim = Local.Aim;
		Throw.Curve = Local.Curve;
		Throw.Power = FMath::Max(MinPower, Local.Power);
		Local.bGripped = false;
		Launch(Throw);
	}
}

float ABowlingGame::SteerLocal(float Dt)
{
	const APlayerController& PC = *Viewer;
	const double AimBefore = Local.Aim, CurveBefore = Local.Curve;
	// The line: A/D or the arrows, the left stick, the mouse.
	double AimInput = Axis(PC, EKeys::D, EKeys::Right, EKeys::A, EKeys::Left);
	AimInput += PC.GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	Local.Aim = FMath::Clamp(Local.Aim + AimInput * AimRate * Dt, -MaxAim, MaxAim);
	float MouseX = 0.f, MouseY = 0.f;
	PC.GetInputMouseDelta(MouseX, MouseY);
	Local.Aim = FMath::Clamp(Local.Aim + MouseX * MouseAimPerPixel, -MaxAim, MaxAim);
	// The hook: Q/E nudge it; the right stick sets it.
	const double HookInput = (PC.IsInputKeyDown(EKeys::E) ? 1.0 : 0.0) - (PC.IsInputKeyDown(EKeys::Q) ? 1.0 : 0.0);
	const double Stick = PC.GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	if (FMath::Abs(Stick) > HookStickDeadZone)
	{
		Local.Curve = FMath::FInterpTo(Local.Curve, Stick * MaxCurve, Dt, HookStickEase);
	}
	else
	{
		Local.Curve = FMath::Clamp(Local.Curve + HookInput * HookRate * Dt, -MaxCurve, MaxCurve);
	}
	bLocalAimed |= Local.Aim != AimBefore;
	bLocalHooked |= Local.Curve != CurveBefore;
	return MouseY;
}

void ABowlingGame::ChargeLocal(float Dt, float MouseY)
{
	MouseTrail.Add(FVector2D(Clock, MouseY));
	MouseTrail.RemoveAll([this](const FVector2D& Sample) { return Sample.X < Clock - FlickWindowSeconds; });
	double Up = 0.0;
	for (const FVector2D& Sample : MouseTrail)
	{
		Up += FMath::Max(0.0, Sample.Y);
	}
	const double FlickPower = FMath::Clamp(Up / FlickWindowSeconds / FlickFullSpeed, 0.0, 1.0);
	if (FlickPower > FlickThreshold)
	{
		Local.bMouseFlick = true;
	}
	Local.Power = Local.bMouseFlick ? FMath::Max(Local.Power * FMath::Exp(-Dt * FlickDecay), FlickPower)
		: 0.5 - 0.5 * FMath::Cos(2.0 * UE_DOUBLE_PI * (Clock - Local.GripAt) / MeterPeriod);
}

// ---- House bots -------------------------------------------------------------------------------------------------

void ABowlingGame::UpdateBot()
{
	const FBowlerSeat* Bowler = GetBowler();
	if (!Bowler || Bowler->Kind != EBowlerKind::Bot)
	{
		return;
	}
	if (BotGripAt < 0.0 && GetPhaseAge() >= BotThinkSeconds - BotWindUpSeconds)
	{
		BotGripAt = Clock;
	}
	if (GetPhaseAge() < BotThinkSeconds)
	{
		return;
	}
	if (Standing.Num() == Pins && DebugFlags.bShowcase)
	{
		Launch(ShowcaseShot());
		return;
	}
	Launch(BotShot(*Bowler));
}

FShot ABowlingGame::BotShot(const FBowlerSeat& Bowler)
{
	// Aim the deck line (aim + curve) at the pocket on a full rack, or at the middle of what's left.
	double AimAt = PocketX;
	if (Standing.Num() < Pins && Standing.Num() > 0)
	{
		double Sum = 0.0;
		for (const int32 Id : Standing)
		{
			Sum += PinPosition(Id).X;
		}
		AimAt = Sum / Standing.Num();
	}
	// The draws stay in this order (curve on a full rack, then aim, then power): the fixed seed replays the same game.
	const double Wild = 1.0 - Bowler.Skill;
	FShot Throw;
	Throw.Curve = Standing.Num() == Pins ? BotStrikeCurve + BotRng.FRandRange(-BotCurveWobble, BotCurveWobble) * Wild : BotSpareCurve;
	Throw.Aim = FMath::Clamp(AimAt - Throw.Curve + BotRng.FRandRange(-BotAimWobble, BotAimWobble) * Wild, -MaxAim, MaxAim);
	Throw.Power = FMath::Clamp(BotPower + BotRng.FRandRange(BotPowerWobbleLow, BotPowerWobbleHigh) * (BotPowerSteadyWobble + Wild), BotMinPower, 1.0);
	return Throw;
}

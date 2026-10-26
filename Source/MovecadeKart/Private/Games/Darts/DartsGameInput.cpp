// ADartsGame: the local thrower's controls (keys, gamepad, mouse) and the house bots.

#include "Games/Darts/DartsGame.h"

#include "GameFramework/PlayerController.h"
#include "Games/Darts/DartsGameTuning.h"
#include "InputCoreTypes.h"
#include "Platform/Mobile/MobileHaptics.h"

using namespace MovecadeKart::Darts;

namespace
{
	// Aim (D8): keys and stick move the reticle at this many aim units a second; the mouse moves it per pixel.
	constexpr double KeyAimSpeed = 0.75;
	constexpr double MouseAimPerPixel = 0.0022;
	/** How fast the measured aim speed follows the hand (per frame), for steadiness. */
	constexpr double AimSpeedSmoothing = 0.3;
	/** An aim speed this fast (units/s) reads as a fully unsteady hand. */
	constexpr double UnsteadyAimSpeed = 2.1;

	/** The keys and pad meter swings 0 -> 1 -> 0 over this period while the dart is gripped. */
	constexpr double MeterPeriodSeconds = 1.5;
	/** A release never throws softer than this (a tap still reaches the board's neighbourhood). */
	constexpr double MinReleasePower = 0.05;

	// The mouse flick: an upward flick over the last FlickWindow throws, its speed is the power.
	constexpr double FlickWindowSeconds = 0.2;
	/** Upward mouse speed (pixels/s) that is a full-power flick. */
	constexpr double FullFlickPixelsPerSecond = 1500.0;
	/** Above this power the grip is a flick, not the meter. */
	constexpr double FlickDetectPower = 0.15;
	/** A flick's power fades at this rate (1/s) once the hand slows, so the peak is what counts. */
	constexpr double FlickPowerDecay = 2.0;
	/** How fast the flick's sideways drift follows the mouse (per frame), and what it becomes as twist. */
	constexpr double FlickSideSmoothing = 0.3;
	constexpr double FlickSpinPerPixel = 25.0;
	constexpr double MaxFlickSpinDegPerSec = 400.0;
	/** The right stick's twist at full deflection, deg/s. */
	constexpr double StickSpinDegPerSec = 180.0;

	// The house bots: skill 0 wild .. 1 sharp.
	/** Sharp bots go for the bounty's treble, steady ones its fat single (at this radius), wild ones the bull. */
	constexpr double SharpSkill = 0.7;
	constexpr double SteadySkill = 0.45;
	constexpr double FatSingleRadius = 0.78;
	/** Aim scatter (aim units, one sigma) and power scatter: a base plus more the wilder the bot. */
	constexpr double AimScatterBase = 0.05;
	constexpr double AimScatterWild = 0.2;
	constexpr double PowerScatterBase = 0.02;
	constexpr double PowerScatterWild = 0.07;
	constexpr double SpinScatterWild = 30.0;
	constexpr double SteadinessBase = 0.85;
	constexpr double SteadinessSkill = 0.15;

	/** A normal-ish deviate from two uniforms (Box-Muller; the bots' scatter). */
	double Gauss(FRandomStream& Rng)
	{
		const double U = FMath::Max(1e-6, static_cast<double>(Rng.FRand()));
		const double V = Rng.FRand();
		return FMath::Sqrt(-2.0 * FMath::Loge(U)) * FMath::Cos(2.0 * UE_DOUBLE_PI * V);
	}

	/** -1, 0 or 1 from a pair of opposing keys. */
	double KeyAxis(const APlayerController& PC, const FKey& Plus, const FKey& PlusAlt, const FKey& Minus, const FKey& MinusAlt)
	{
		return (PC.IsInputKeyDown(Plus) || PC.IsInputKeyDown(PlusAlt) ? 1.0 : 0.0) - (PC.IsInputKeyDown(Minus) || PC.IsInputKeyDown(MinusAlt) ? 1.0 : 0.0);
	}

	bool IsGripHeld(const APlayerController& PC)
	{
		static const FKey GripKeys[] = { EKeys::SpaceBar, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton, EKeys::Enter };
		for (const FKey& Key : GripKeys)
		{
			if (PC.IsInputKeyDown(Key))
			{
				return true;
			}
		}
		return false;
	}
}

void ADartsGame::UpdateLocalInput(float Dt)
{
	const FDartsSeat* Thrower = GetThrower();
	if (!Viewer || !Thrower || Thrower->Kind != EThrowerKind::Local || Match.Phase != EMatchPhase::Playing || Match.TurnDarts >= DartsPerRound)
	{
		return;
	}
	const APlayerController& PC = *Viewer;
	float MouseX = 0.f, MouseY = 0.f;
	PC.GetInputMouseDelta(MouseX, MouseY);
	UpdateLocalAim(PC, Dt, MouseX, MouseY);

	const bool bGripDown = bTestGrip || IsGripHeld(PC);
	if (bGripDown && !Local.bGripped)
	{
		Local.bGripped = true;
		Local.GripAt = Clock;
		Local.bMouseFlick = false;
		Local.FlickSide = 0.0;
		MouseTrail.Reset();
	}
	if (Local.bGripped)
	{
		UpdateLocalPower(Dt, MouseX, MouseY);
	}
	if (Local.bGripped && !bGripDown)
	{
		ReleaseLocalDart(PC, *Thrower);
	}
}

void ADartsGame::UpdateLocalAim(const APlayerController& PC, float Dt, float MouseX, float MouseY)
{
	// Keys, the left stick and (until the grip) the mouse. The reticle's speed is the hand's unsteadiness.
	FVector2D Move(KeyAxis(PC, EKeys::D, EKeys::Right, EKeys::A, EKeys::Left), KeyAxis(PC, EKeys::W, EKeys::Up, EKeys::S, EKeys::Down));
	Move.X += PC.GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	Move.Y += PC.GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	const FVector2D Before = Local.Aim;
	Local.Aim += Move * KeyAimSpeed * Dt;
	if (!Local.bGripped)
	{
		Local.Aim += FVector2D(MouseX, MouseY) * MouseAimPerPixel;
	}
	Local.Aim.X = FMath::Clamp(Local.Aim.X, -1.0, 1.0);
	Local.Aim.Y = FMath::Clamp(Local.Aim.Y, -1.0, 1.0);
	const double Speed = Dt > 0.f ? (Local.Aim - Before).Size() / Dt : 0.0;
	Local.AimSpeed += (Speed - Local.AimSpeed) * AimSpeedSmoothing;
}

void ADartsGame::UpdateLocalPower(float Dt, float MouseX, float MouseY)
{
	// The mouse: a flick up the screen throws, its speed over the last 200 ms is the power, its sideways drift the twist.
	MouseTrail.Add(FVector2D(Clock, MouseY));
	MouseTrail.RemoveAll([this](const FVector2D& Sample) { return Sample.X < Clock - FlickWindowSeconds; });
	double Up = 0.0;
	for (const FVector2D& Sample : MouseTrail)
	{
		Up += FMath::Max(0.0, static_cast<double>(Sample.Y));
	}
	const double FlickPower = FMath::Clamp(Up / FlickWindowSeconds / FullFlickPixelsPerSecond, 0.0, 1.0);
	if (FlickPower > FlickDetectPower)
	{
		Local.bMouseFlick = true;
	}
	Local.FlickSide = FMath::Lerp(Local.FlickSide, static_cast<double>(MouseX), FlickSideSmoothing);
	// Keys and pad: the meter swings with the time held.
	Local.Power = Local.bMouseFlick ? FMath::Max(Local.Power * FMath::Exp(-Dt * FlickPowerDecay), FlickPower)
		: 0.5 - 0.5 * FMath::Cos(2.0 * UE_DOUBLE_PI * (Clock - Local.GripAt) / MeterPeriodSeconds);
}

void ADartsGame::ReleaseLocalDart(const APlayerController& PC, const FDartsSeat& Thrower)
{
	FDartGesture Gesture;
	Gesture.AimX = Local.Aim.X;
	Gesture.AimY = Local.Aim.Y;
	Gesture.Power = FMath::Max(MinReleasePower, Local.Power);
	Gesture.SpinDegPerSec = Local.bMouseFlick ? FMath::Clamp(Local.FlickSide * FlickSpinPerPixel, -MaxFlickSpinDegPerSec, MaxFlickSpinDegPerSec)
		: PC.GetInputAnalogKeyState(EKeys::Gamepad_RightX) * StickSpinDegPerSec;
	Gesture.Steadiness = FMath::Clamp(1.0 - Local.AimSpeed / UnsteadyAimSpeed, 0.0, 1.0);
	Local.bGripped = false;
	Local.Power = 0.0;
	Throw(Thrower.Id, Gesture);
	MovecadeKart::Mobile::FHaptics::PlayForLocalPlayer(this, MovecadeKart::Mobile::EHaptic::Throw);
	// The first dart is away: the coach marks have done their job.
	if (bCoachShown && CoachDoneAt < 0.0)
	{
		CoachDoneAt = Clock;
		bCoachMarksSeenThisSession = true;
	}
}

void ADartsGame::UpdateBot()
{
	const FDartsSeat* Thrower = GetThrower();
	const FDartsPlayer* Player = Match.ActivePlayer();
	if (!Thrower || !Player || Thrower->Kind != EThrowerKind::Bot || Match.Phase != EMatchPhase::Playing
		|| Match.TurnDarts >= DartsPerRound || Clock < BotReadyAt)
	{
		return;
	}
	// Aim for the called shot: the treble when sharp, its fat single when not, the bull when wild.
	const double Skill = Thrower->Skill;
	const double Angle = SectorCenterAngle(Tuning::SectorIndexOfValue(BountyForRound(Player->Round)));
	const double Radius = Skill > SharpSkill ? (TripleInnerRadius + TripleOuterRadius) / 2.0 : (Skill > SteadySkill ? FatSingleRadius : 0.0);
	const FVector2D Target(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
	const double Scatter = AimScatterBase + AimScatterWild * (1.0 - Skill);
	// The draws stay in this order: the bots' darts are the same every match.
	FDartGesture Gesture;
	Gesture.AimX = Target.X / AimReach + Gauss(BotRng) * Scatter;
	Gesture.AimY = Target.Y / AimReach + Gauss(BotRng) * Scatter;
	Gesture.Power = SweetSpot + Gauss(BotRng) * (PowerScatterBase + PowerScatterWild * (1.0 - Skill));
	Gesture.SpinDegPerSec = Gauss(BotRng) * SpinScatterWild * (1.0 - Skill);
	Gesture.Steadiness = SteadinessBase + SteadinessSkill * Skill;
	BotReadyAt = Clock + Tuning::BotNextDelaySeconds;
	Throw(Thrower->Id, Gesture);
}

// ADerbyGame's swings: the local batter's held-and-released button, a phone's swing events and READY button, and
// the house bots' swings. Every path ends in SubmitSwing; the loop (DerbyGame.cpp) judges it when it peaks.

#include "Games/Derby/DerbyGame.h"

#include "GameFramework/PlayerController.h"
#include "Games/Derby/DerbyStage.h"
#include "InputCoreTypes.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Derby;

namespace
{
	/** Any of these loads the swing while held and swings on release. */
	const FKey SwingKeys[] = { EKeys::SpaceBar, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton, EKeys::Enter };
	/** The phone charge meter falls back this much of the way per frame (it jumps up at once). */
	constexpr double PhoneChargeFall = 0.15;
	/** Bots: strength around this plus SkillStrength at full skill, give or take the jitter; clamped to the range. */
	constexpr double BotBaseStrength = 0.5;
	constexpr double BotSkillStrength = 0.45;
	constexpr float BotStrengthJitterLow = -0.15f;
	constexpr float BotStrengthJitterHigh = 0.12f;
	constexpr double BotMinStrength = 0.3;
	constexpr double BotMaxStrength = 1.0;
	/** Guards the Box-Muller logarithm. */
	constexpr double MinUniform = 1e-6;
	/** The swing cue: louder and higher the harder the swing (gain and rate at zero strength, plus per strength). */
	constexpr float SwingCueGain = 0.6f;
	constexpr float SwingCueGainPerStrength = 0.4f;
	constexpr float SwingCueRate = 0.9f;
	constexpr float SwingCueRatePerStrength = 0.25f;
}

void ADerbyGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	// Every phone's detector runs, so a batter's first frames at the plate don't start a swing from stale state.
	FSwingDetector& Detector = Detectors.FindOrAdd(PlayerId);
	const TOptional<FSwingEvent> Event = Detector.Feed(Frame);
	const FDerbySeat* Up = GetBatter();
	if (!Up || Up->Id != PlayerId || Up->Kind != EDerbyKind::Phone)
	{
		return;
	}
	const double Charge = Detector.Charge();
	PhoneCharge = Charge > PhoneCharge ? Charge : FMath::Lerp(PhoneCharge, Charge, PhoneChargeFall);
	if (!Event.IsSet() || bPaused)
	{
		return;
	}
	// The detector reports its peak on its own clock, after the swing came down: back to the game clock, less the trip.
	const double PeakAt = Clock - (Detector.ClockMs() - Event->PeakAtMs) / 1000.0 - Tuning.PhoneLatencySeconds;
	UE_LOG(LogDerby, Log, TEXT("Phone swing %s: %.0f dps, strength %.2f, direction %.2f (%s), %.0f ms ago"), *PlayerId, Event->PeakDps, Event->Strength,
		Event->Direction, Event->Handed > 0.0 ? TEXT("R") : TEXT("L"), (Clock - PeakAt) * 1000.0);
	if (Phase == EDerbyPhase::Pitch && !bSwingTaken)
	{
		SubmitSwing(PeakAt, Event->Direction, FMath::Max(Tuning.PhoneMinStrength, Event->Strength));
	}
	else if (Phase == EDerbyPhase::Set || Phase == EDerbyPhase::Windup || Phase == EDerbyPhase::StepIn)
	{
		// A practice swing: the batter takes it too, nothing counts.
		SwingAnimAt = Clock - Tuning.SwingLeadSeconds;
	}
}

void ADerbyGame::PressButton(const FString& PlayerId)
{
	const FDerbySeat* Up = GetBatter();
	if (!Up || Up->Id != PlayerId || bPaused)
	{
		return;
	}
	if (Phase == EDerbyPhase::Set || Phase == EDerbyPhase::StepIn)
	{
		bReady = true;
	}
	else if (Phase == EDerbyPhase::Pitch && !bSwingTaken)
	{
		// No gyro, or no faith in it: the button swings, a little after the press.
		SubmitSwing(Clock + Tuning.ButtonSwingDelaySeconds, 0.0, Tuning.ButtonSwingStrength);
	}
}

void ADerbyGame::SubmitSwing(double PeakAt, double Aim, double Strength)
{
	bSwingTaken = true;
	SwingPeakAt = PeakAt;
	SwingAim = Aim;
	SwingStrength = Strength;
	SwingAnimAt = PeakAt - Tuning.SwingLeadSeconds;
	const float Hard = static_cast<float>(Strength);
	Cues.Play(this, Cue::Swing, SwingCueGain + SwingCueGainPerStrength * Hard, SwingCueRate + SwingCueRatePerStrength * Hard);
}

bool ADerbyGame::IsLocalSwingHeld() const
{
	for (const FKey& Key : SwingKeys)
	{
		if (Viewer->IsInputKeyDown(Key))
		{
			return true;
		}
	}
	return false;
}

void ADerbyGame::UpdateLocalAim(float Dt)
{
	// The mouse, A/D or the arrows, either stick. Left pulls, right goes the other way.
	float MouseX = 0.f, MouseY = 0.f;
	Viewer->GetInputMouseDelta(MouseX, MouseY);
	const auto Down = [this](const FKey& Key) { return Viewer->IsInputKeyDown(Key); };
	const double Steer = (Down(EKeys::D) || Down(EKeys::Right) ? 1.0 : 0.0) - (Down(EKeys::A) || Down(EKeys::Left) ? 1.0 : 0.0);
	const double Stick = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftX) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	if (FMath::Abs(Stick) > Tuning.StickDeadZone)
	{
		Local.Aim = FMath::FInterpTo(Local.Aim, FMath::Clamp(Stick, -1.0, 1.0), Dt, Tuning.StickAimSpeed);
	}
	Local.Aim = FMath::Clamp(Local.Aim + Steer * Tuning.KeyAimRate * Dt + MouseX * Tuning.MouseAimPerPixel, -1.0, 1.0);
}

void ADerbyGame::UpdateLocal(float Dt)
{
	const FDerbySeat* Up = GetBatter();
	if (!Viewer || !Up || Up->Kind != EDerbyKind::Local || Phase == EDerbyPhase::Finished || Phase == EDerbyPhase::Intro)
	{
		Local.bLoading = false;
		return;
	}
	const bool bDown = IsLocalSwingHeld();
	UpdateLocalAim(Dt);
	const bool bSwinging = Clock - SwingAnimAt < Tuning.SwingSeconds;
	if (bDown && !Local.bLoading && !bSwinging)
	{
		Local.bLoading = true;
		Local.LoadAt = Clock;
	}
	if (Local.bLoading)
	{
		Local.Load = FMath::Clamp((Clock - Local.LoadAt) / Tuning.LoadSeconds, 0.0, 1.0);
	}
	if (!Local.bLoading || bDown)
	{
		return;
	}
	// Released: the swing, as hard as the load.
	Local.bLoading = false;
	const double Strength = Tuning.MinLoadStrength + Tuning.LoadStrength * Local.Load;
	Local.Load = 0.0;
	if (Phase == EDerbyPhase::Pitch && !bSwingTaken)
	{
		SubmitSwing(Clock + Tuning.SwingLeadSeconds, Local.Aim, Strength);
	}
	else if (Phase != EDerbyPhase::Pitch && Phase != EDerbyPhase::Flight)
	{
		// Too soon: a practice cut while the pitcher gets ready.
		SwingAnimAt = Clock;
	}
}

void ADerbyGame::UpdateBot()
{
	const FDerbySeat* Up = GetBatter();
	if (!Up || Up->Kind != EDerbyKind::Bot)
	{
		return;
	}
	const double Skill = FMath::Clamp(static_cast<double>(Up->Skill), 0.0, 1.0);
	if (!(BotRng.FRand() < Tuning.BotSwingChance))
	{
		return;
	}
	// Timing error, normal (Box-Muller): wider for a shaky hitter and for the curve; the changeup fools them early.
	const double U1 = FMath::Max(MinUniform, static_cast<double>(BotRng.FRand()));
	const double U2 = BotRng.FRand();
	const double Gauss = FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * PI * U2);
	const double Sigma = Tuning.BotTimingSigmaMs + Tuning.BotWildSigmaMs * (1.0 - Skill) + (Pitch.Type == EPitchType::Curve ? Tuning.BotCurveSigmaMs : 0.0);
	const double Mean = Pitch.Type == EPitchType::Changeup ? -Tuning.BotChangeupEarlyMs * (1.0 - Skill) : 0.0;
	const double TimingMs = Mean + Gauss * Sigma;
	const double Aim = BotRng.FRandRange(-Tuning.BotAimRange, Tuning.BotAimRange);
	const double Strength = FMath::Clamp(BotBaseStrength + BotSkillStrength * Skill + BotRng.FRandRange(BotStrengthJitterLow, BotStrengthJitterHigh), BotMinStrength, BotMaxStrength);
	SubmitSwing(ArriveAt() + TimingMs / 1000.0, Aim, Strength);
}

// AStackerGame: the builders' palms and drops (phones, the local pad/keys/mouse, the house bots).

#include "Games/Stacker/StackerGame.h"

#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Stacker;

namespace
{
	/** The rules' fixed step (StackerGame.cpp): a bot lets go when the block is within part of one step's travel. */
	constexpr double StepMs = 10.0;

	// The local palm: a full stick holds this tilt; the mouse nudges it (degrees per pixel) and leaves it there.
	constexpr double StickTiltDeg = 30.0;
	constexpr double MouseTiltPerPixel = 0.12;
	constexpr double MouseTiltLimitDeg = 30.0;
	/** balance.ts clamps the palm here too. */
	constexpr double PalmLimitDeg = 35.0;
	/** -StackerLeanTest: when the palm starts tipping hard right. */
	constexpr double LeanTestStartMs = 10000.0;

	// The house bot (pacing.test.ts plays the same way): a palm that counters the wind with a wandering error, and a
	// drop aimed at a mark near the support that gets sloppier with low skill and great height.
	constexpr double BotWanderBase = 1.5;
	constexpr double BotWanderPerMissingSkill = 10.0;
	constexpr double BotWanderStep = 0.08;
	constexpr double BotPitchErrorShare = 0.5;
	constexpr double BotAimSpreadBase = 0.25;
	constexpr double BotAimSpreadPerMissingSkill = 0.9;
	constexpr double BotAimSpreadHighRows = 30.0;
	constexpr double BotAimSpreadHigh = 0.1;
	constexpr double BotAimErrorLimit = 1.2;
	constexpr float BotThinkMinMs = 150.f;
	constexpr float BotThinkMaxMs = 900.f;
	constexpr double BotThinkSkillOffset = 1.15;
	/** Let go when the block is within this share of one step's travel of the mark. */
	constexpr double BotReleaseWindow = 0.75;

	/** runtime.ts 501-508: the palm's normal from the frame's quaternion; roll across the lane, pitch toward the city. */
	void PalmFromFrame(const MovecadeKart::Net::FSensorFrame& Frame, double& OutRoll, double& OutPitch)
	{
		if (!Frame.bHasQuaternion)
		{
			OutRoll = Frame.Gamma;
			OutPitch = Frame.Beta;
			return;
		}
		const double X = Frame.Quaternion.X, Y = Frame.Quaternion.Y, Z = Frame.Quaternion.Z, Wq = Frame.Quaternion.W;
		const double Nx = 2.0 * (X * Z + Wq * Y);
		const double Ny = 2.0 * (Y * Z - Wq * X);
		const double Nz = 1.0 - 2.0 * (X * X + Y * Y);
		OutRoll = FMath::RadiansToDegrees(FMath::Atan2(Nx, Nz));
		OutPitch = FMath::RadiansToDegrees(FMath::Atan2(Ny, Nz));
	}

	bool AnyKeyDown(const APlayerController& Viewer, const FKey& A, const FKey& B)
	{
		return Viewer.IsInputKeyDown(A) || Viewer.IsInputKeyDown(B);
	}

	/** The stick plus the keys on one axis: Positive / Negative key pairs (the letters and the arrows). */
	float KeyAxis(const APlayerController& Viewer, const FKey& Positive, const FKey& PositiveArrow, const FKey& Negative, const FKey& NegativeArrow)
	{
		return (AnyKeyDown(Viewer, Positive, PositiveArrow) ? 1.f : 0.f) - (AnyKeyDown(Viewer, Negative, NegativeArrow) ? 1.f : 0.f);
	}

	bool DropPressed(const APlayerController& Viewer)
	{
		return Viewer.WasInputKeyJustPressed(EKeys::SpaceBar) || Viewer.WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)
			|| Viewer.WasInputKeyJustPressed(EKeys::LeftMouseButton) || Viewer.WasInputKeyJustPressed(EKeys::Enter);
	}
}

void AStackerGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	const int32 i = Lanes.IndexOfByPredicate([&PlayerId](const FLane& L) { return L.PlayerId == PlayerId; });
	if (i == INDEX_NONE)
	{
		return;
	}
	FLane& Lane = Lanes[i];
	PalmFromFrame(Frame, Lane.PhoneRoll, Lane.PhonePitch);
	FBalance& Balance = Match.Towers[i].Balance;
	Balance.Roll = Lane.PhoneRoll;
	Balance.Pitch = Lane.PhonePitch;
	Balance.LastInputMs = Match.ElapsedMs;
}

void AStackerGame::Drop(const FString& PlayerId)
{
	if (Stage != EStackerStage::Match || bPaused)
	{
		return;
	}
	if (DropBlock(Match, PlayerId).IsSet())
	{
		PresentNewEffects();
	}
}

void AStackerGame::SteerLocals()
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		if (Lanes[i].Kind == EBuilderKind::Local)
		{
			SteerLocal(Lanes[i], Match.Towers[i]);
		}
	}
}

void AStackerGame::SteerLocal(FLane& Lane, FTower& Tower) const
{
	// The palm: the left stick or A/D and W/S hold a tilt, the mouse nudges it and leaves it there. Leaning the palm
	// is "roll" on the phone: a stick to the right tips the tower right.
	float StickX = 0.f, StickY = 0.f;
	if (Viewer)
	{
		float MouseX = 0.f, MouseY = 0.f;
		Viewer->GetInputMouseDelta(MouseX, MouseY);
		Lane.MouseRoll = FMath::Clamp(Lane.MouseRoll + MouseX * MouseTiltPerPixel, -MouseTiltLimitDeg, MouseTiltLimitDeg);
		Lane.MousePitch = FMath::Clamp(Lane.MousePitch - MouseY * MouseTiltPerPixel, -MouseTiltLimitDeg, MouseTiltLimitDeg);
		StickX = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftX) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
		StickY = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftY) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
		StickX += KeyAxis(*Viewer, EKeys::D, EKeys::Right, EKeys::A, EKeys::Left);
		StickY += KeyAxis(*Viewer, EKeys::W, EKeys::Up, EKeys::S, EKeys::Down);
		if (DropPressed(*Viewer))
		{
			Lane.bDropQueued = true;
		}
	}
	if (DebugFlags.bLeanTest && Match.ElapsedMs > LeanTestStartMs)
	{
		StickX = 1.f;
	}
	Tower.Balance.Roll = FMath::Clamp(FMath::Clamp(StickX, -1.f, 1.f) * StickTiltDeg + Lane.MouseRoll, -PalmLimitDeg, PalmLimitDeg);
	Tower.Balance.Pitch = FMath::Clamp(FMath::Clamp(StickY, -1.f, 1.f) * StickTiltDeg + Lane.MousePitch, -PalmLimitDeg, PalmLimitDeg);
}

void AStackerGame::SteerBot(FLane& Lane, FTower& Tower, const FStackerSeat& Seat)
{
	// A bot that is done (or between rounds) still "holds" its phone, so it never reads as missing.
	Tower.Balance.LastInputMs = Match.ElapsedMs;
	if (Tower.IsDone() || Match.Phase != EMatchPhase::Playing)
	{
		return;
	}
	const double Skill = Seat.Skill;
	const int32 Height = Tower.Height();
	// The palm: counter the wind with a hand that wanders.
	const FVector2D Wind = WindAt(Match.ElapsedMs, Height);
	const double Wander = (1.0 - Skill) * BotWanderPerMissingSkill + BotWanderBase;
	Lane.PalmError = FMath::Clamp(Lane.PalmError + (BotRng.FRand() - 0.5) * Wander * BotWanderStep, -Wander, Wander);
	Tower.Balance.Roll = -Wind.X + Lane.PalmError;
	Tower.Balance.Pitch = -Wind.Y + Lane.PalmError * BotPitchErrorShare;
	// The drop: pick a mark for this row, then let go as the block crosses it.
	if (Lane.AimedRow != Height)
	{
		Lane.AimedRow = Height;
		const double Spread = BotAimSpreadBase + BotAimSpreadPerMissingSkill * (1.0 - Skill)
			+ BotAimSpreadHigh * FMath::Max(0.0, Height - BotAimSpreadHighRows) / BotAimSpreadHighRows;
		// Two uniform draws: a triangular spread, most aims near the middle.
		const double Draw = static_cast<double>(BotRng.FRandRange(-1.f, 1.f) + BotRng.FRandRange(-1.f, 1.f));
		Lane.AimError = FMath::Clamp(Draw * 0.5 * Spread, -BotAimErrorLimit, BotAimErrorLimit);
		Lane.NextDropAt = Match.ElapsedMs + DropCooldownMs + BotRng.FRandRange(BotThinkMinMs, BotThinkMaxMs) * (BotThinkSkillOffset - Skill);
	}
	if (Match.ElapsedMs < Lane.NextDropAt || Tower.Balance.RecoveryMs > 0.0)
	{
		return;
	}
	const double Support = Tower.Top().Center + BalanceDisplacement(Tower.Balance.LeanX);
	const double Travel = SweepSpeed(Height) * StepMs / 1000.0;
	if (FMath::Abs(Tower.CursorCenter - (Support + Lane.AimError)) <= Travel * BotReleaseWindow)
	{
		Lane.bDropQueued = true;
	}
}

void AStackerGame::DropQueued()
{
	for (FLane& Lane : Lanes)
	{
		if (Lane.bDropQueued)
		{
			Lane.bDropQueued = false;
			DropBlock(Match, Lane.PlayerId);
		}
	}
}

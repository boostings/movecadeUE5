// ABowlingGame: the lane camera (presentation.ts cameraPlan). The address tripod behind the bowler, the impact and
// result shots over the deck; the port adds a drift after the ball, the slow-motion strike cam and an orbit over a
// celebrating deck. Each phase picks a shot; the camera eases toward it and the impact jolt rides on top.
#include "Games/Bowling/BowlingGame.h"

#include "BowlingGameTuning.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Games/Bowling/BowlingAlley.h"

using namespace MovecadeKart::Bowling;
using namespace MovecadeKart::Bowling::Tuning;

/** Where the camera wants to be (web units), its vertical field of view (degrees) and how fast it gets there (1/s). */
struct FBowlingCameraShot
{
	FVector Pos;
	FVector Look;
	double Fov = 39.0;
	double Response = 3.05;
};

namespace
{
	constexpr double DeckZ = ABowlingAlley::DeckZ;
	/** The address tripod, behind and above the bowler. */
	const FVector AddressPos(0.0, 3.65, 6.55);
	const FVector AddressLook(0.0, 0.86, -9.55);
	/** The intro's push starts low over the lane, looking at the rack. */
	const FVector IntroPos(2.4, 2.6, -2.0);
	const FVector IntroLook(0.0, 0.6, -13.6);
	constexpr double IntroResponse = 8.0;
	/** The strike cam takes over this far down a strike-bound roll. */
	constexpr double StrikeCamFrom = 0.62;
	/** A plain roll cuts from the impact shot to the result shot this far into the settle, and holds it into the reset. */
	constexpr double ImpactShare = 0.55;
	constexpr double ResultHoldShare = 0.58;
	/** The lens eases toward a new field of view at this rate, whatever the shot's response. */
	constexpr double FovResponse = 4.0;
	/** The position ease never steps more than this at once (a hitch doesn't snap the camera). */
	constexpr double MaxEaseStep = 1.0 / 20.0;
	/** The jolt's wobble frequencies (rad/s) per axis; the depth axis moves half as much. */
	constexpr double JoltRateX = 71.0;
	constexpr double JoltRateY = 53.0;
	constexpr double JoltRateZ = 61.0;
	constexpr double DefaultAspect = 16.0 / 9.0;

	FBowlingCameraShot AddressShot()
	{
		return { AddressPos, AddressLook };
	}

	FBowlingCameraShot IntroShot(double Age)
	{
		// A slow push down the lane toward the rack before the first ball.
		const double T = Smooth(0.0, 1.0, Age / IntroSeconds);
		return { FMath::Lerp(IntroPos, AddressPos, T), FMath::Lerp(IntroLook, AddressLook, T), 39.0, IntroResponse };
	}

	FBowlingCameraShot RollingShot(double T, double EndX)
	{
		// The tripod leans after the ball.
		return { FVector(0.0, 3.4, 6.2 - 3.6 * Smooth(0.0, 1.0, T)), FVector(EndX * 0.1, 0.7, -10.5), 37.0, 3.05 };
	}

	/** A strike-bound ball: the low cam at the deck as time slows (the roll's end, then the pins flying). */
	FBowlingCameraShot StrikeCamRolling(double EndX)
	{
		return { FVector(EndX * 0.3 - 1.25, 0.75, DeckZ + 3.4), FVector(EndX * 0.5, 0.45, DeckZ - 0.4), 40.0, 4.2 };
	}

	FBowlingCameraShot StrikeCamSettling(double EndX)
	{
		return { FVector(EndX * 0.3 - 1.25, 0.8, DeckZ + 3.2), FVector(EndX * 0.4, 0.55, DeckZ - 0.6), 42.0, 2.0 };
	}

	FBowlingCameraShot ImpactShot(double EndX)
	{
		return { FVector(0.0, 2.55, -6.85), FVector(EndX * 0.14, 0.58, -13.6), 39.0, 3.05 };
	}

	FBowlingCameraShot ResultShot(double EndX)
	{
		return { FVector(0.0, 3.3, -6.3), FVector(EndX * 0.16, 0.48, -13.6), 40.0, 5.2 };
	}

	FBowlingCameraShot CelebrationShot(double Since)
	{
		// A low swing across the deck while the house goes off.
		const double A = -0.55 + 1.1 * Smooth(0.0, CelebrationSeconds + 0.4, Since);
		return { FVector(FMath::Sin(A) * 3.4, 1.45 + 0.4 * Smooth(0.0, 2.0, Since), DeckZ + FMath::Cos(A) * 4.6),
			FVector(0.0, 0.9, DeckZ - 0.6), 44.0, 3.4 };
	}

	FBowlingCameraShot FinishedShot(double Clock)
	{
		// A slow drift over the house behind the results.
		return { FVector(1.6 * FMath::Sin(Clock * 0.15), 3.1, 4.6), FVector(0.0, 1.0, -6.0), 46.0, 1.2 };
	}

	/** three.js gives a vertical field of view; UE's is horizontal, so convert at the viewport's aspect. */
	double HorizontalFov(double VerticalFov, double Aspect)
	{
		return 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFov) / 2.0) * Aspect));
	}
}

FBowlingCameraShot ABowlingGame::PickCameraShot() const
{
	const double EndX = bHasShot ? Shot.EndX * ABowlingAlley::VisualPinScale : 0.0;
	const double Age = GetPhaseAge();
	switch (Phase)
	{
	case EBowlingPhase::Intro:
		return IntroShot(Age);
	case EBowlingPhase::Rolling:
	{
		const double T = FMath::Clamp(Age / RollSeconds, 0.0, 1.0);
		return bStrikeBound && T > StrikeCamFrom ? StrikeCamRolling(EndX) : RollingShot(T, EndX);
	}
	case EBowlingPhase::Settling:
		if (bStrikeBound)
		{
			return StrikeCamSettling(EndX);
		}
		return Age / SettleSeconds <= ImpactShare ? ImpactShot(EndX) : ResultShot(EndX);
	case EBowlingPhase::Reset:
	{
		const double Since = Clock - LastBeatAt;
		if (IsCelebration(LastBeat) && Since < CelebrationSeconds)
		{
			return CelebrationShot(Since);
		}
		return Age / ResetSeconds < ResultHoldShare ? ResultShot(EndX) : AddressShot();
	}
	case EBowlingPhase::Finished:
		return FinishedShot(Clock);
	default:
		return AddressShot();
	}
}

FVector ABowlingGame::CameraJolt() const
{
	// The impact's jolt: a quick decaying wobble on top of the framing (held still while paused).
	const double ShakeAge = Clock - ShakeAt;
	if (ShakeAge < 0.0 || ShakeAge >= ShakeSeconds || bPaused)
	{
		return FVector::ZeroVector;
	}
	const double Amp = ShakeMetres * FMath::Square(1.0 - ShakeAge / ShakeSeconds);
	return FVector(FMath::Sin(ShakeAge * JoltRateX), FMath::Sin(ShakeAge * JoltRateY + 1.3), FMath::Sin(ShakeAge * JoltRateZ + 2.1) * 0.5) * Amp;
}

void ABowlingGame::UpdateCamera(float Dt)
{
	const FBowlingCameraShot Want = PickCameraShot();
	if (!bCamPlaced)
	{
		CamPos = Want.Pos;
		CamLook = Want.Look;
		CamFov = Want.Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-Want.Response * FMath::Min<double>(Dt, MaxEaseStep));
	CamPos += (Want.Pos - CamPos) * K;
	CamLook += (Want.Look - CamLook) * K;
	CamFov += (Want.Fov - CamFov) * (1.0 - FMath::Exp(-FovResponse * Dt));
	const FVector Jolt = CameraJolt();
	const FVector From = LaneToWorld(CamPos.X + Jolt.X, CamPos.Y + Jolt.Y, CamPos.Z + Jolt.Z);
	const FVector To = LaneToWorld(CamLook.X, CamLook.Y, CamLook.Z);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	double Aspect = DefaultAspect;
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		FVector2D Size;
		Viewport->GetViewportSize(Size);
		Aspect = Size.Y > 0.0 ? Size.X / Size.Y : Aspect;
	}
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HorizontalFov(CamFov, Aspect)));
}

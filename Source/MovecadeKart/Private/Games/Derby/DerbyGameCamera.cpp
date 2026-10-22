// ADerbyGame's broadcast camera: the crane in, the hero shot of the batter stepping in, the plate view behind the
// catcher, the high home camera on a long lens following a hit, the pull-back for the fireworks, and the finale
// round the champion. Each phase picks a target shot (metres, vertical field of view) and how fast to ease to it;
// a cut (bCamPlaced false) jumps straight there.

#include "Games/Derby/DerbyGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Games/Derby/DerbyStage.h"

using namespace MovecadeKart::Derby;
using namespace MovecadeKart::Derby::Stage;

namespace
{
	/** The plate view: behind and a touch above the catcher, the batter on the left, the mound in the middle. */
	const FVector PlatePos(-4.6, 0.7, 2.0);
	const FVector PlateLook(14.0, -0.35, 1.55);
	constexpr double PlateFov = 44.0;
	/** How fast the camera eases to its shot (1/s); a cut-like move uses the fast one. */
	constexpr double DefaultResponse = 5.0;
	constexpr double SnapResponse = 30.0;
	/** The pitch: the plate view leans in a little as the ball arrives. */
	const FVector PitchPush(0.35, 0.0, -0.05);
	constexpr double MinPitchFlightSeconds = 0.2;

	/** The intro crane: from high over the grandstand down toward the plate, the whole park lit ahead. */
	const FVector CraneFromPos(-34.0, -26.0, 36.0);
	const FVector CraneToPos(-12.0, 4.0, 6.0);
	const FVector CraneFromLook(110.0, 10.0, 10.0);
	const FVector CraneToLook(18.0, 0.0, 1.5);
	constexpr double CraneFromFov = 58.0;

	/** The hero shot as the batter steps in: in front of them, low, drifting (metres per second). */
	const FVector HeroPos(1.9, 2.6, 1.25);
	const FVector HeroDrift(0.25, -0.15, 0.0);
	const FVector HeroLook(0.0, -0.95, 1.25);
	constexpr double HeroFov = 34.0;

	/** The high home camera on a hit: behind home, swinging with the spray. */
	constexpr double HighHomeX = -22.0;
	constexpr double HighHomeSwing = 8.0;
	constexpr double HighHomeZ = 14.0;
	/** It aims this share of the way from the ball down to the grass under it, the frame tall enough for both. */
	constexpr double GroundInFrame = 0.32;
	constexpr double MinHalfHeight = 15.0;
	constexpr double HalfHeightPerApex = 0.62;
	constexpr double HalfHeightPad = 6.0;
	constexpr double MinFlightFov = 12.0;
	constexpr double MaxFlightFov = 58.0;
	constexpr double FlightResponse = 7.0;
	constexpr double CallResponse = 2.5;
	/** Just off the bat it snaps onto the ball. */
	constexpr double OffTheBatSeconds = 0.25;
	constexpr double OffTheBatResponse = 12.0;
	/** A home run's call: it pulls back and up for the fireworks over the seats. */
	constexpr double PullBackFrom = 0.5;
	constexpr double PullBackTo = 2.2;
	constexpr double PullBackShare = 0.8;
	constexpr double PullBackHeight = 34.0;
	constexpr double PullBackFov = 40.0;
	constexpr double PullBackResponse = 3.0;

	/** The finale: swung back and forth across the champion's front (round the batter's box), the stands behind. */
	constexpr double FinaleCentreY = -0.95;
	constexpr double FinaleRadius = 6.8;
	constexpr double FinaleSwing = 0.75;
	constexpr double FinaleSwingRate = 0.22;
	constexpr double FinaleSwingOffset = 0.35;
	constexpr double FinaleHeight = 2.1;
	const FVector FinaleLook(0.0, -0.95, 1.3);
	constexpr double FinaleFov = 42.0;
	constexpr double FinaleResponse = 2.0;

	/** Easing: the position at most a 20 Hz step (a hitch never jumps it), the target a little faster, the lens at 5/s. */
	constexpr double MaxEaseStep = 1.0 / 20.0;
	constexpr double LookEaseBoost = 1.6;
	constexpr double FovResponse = 5.0;
	/** The shake after a hard contact: it dies away at 8/s, a jolt sideways and up (metres per unit of shake). */
	constexpr double ShakeDecay = 8.0;
	constexpr double ShakeSide = 0.06;
	constexpr double ShakeUp = 0.05;
	constexpr double ShakeSideRate = 63.0;
	constexpr double ShakeUpRate = 51.0;
	constexpr double ShakeUpPhase = 1.3;
	constexpr double DefaultAspect = 16.0 / 9.0;
}

void ADerbyGame::FlightShot(double Age, FVector& Pos, FVector& Look, double& Fov, double& Response) const
{
	const FVector BallM = (Phase == EDerbyPhase::Flight ? HitBall(FlightClock) : HitBall(Outcome.Flight.HangS)) / 100.0;
	const double Spray = FMath::DegreesToRadians(Outcome.Contact.SprayDeg);
	// The broadcast's camera high behind home, on a long lens: it zooms as the ball carries.
	Pos = FVector(HighHomeX, FMath::Sin(Spray) * HighHomeSwing, HighHomeZ);
	// Keep the field under it in the frame: aim between the ball and the grass beneath, wide enough for both.
	Look = FMath::Lerp(BallM, FVector(BallM.X, BallM.Y, 0.0), GroundInFrame);
	const double Distance = FMath::Max(1.0, (Look - Pos).Size());
	const double Half = FMath::Max(MinHalfHeight, BallM.Z * HalfHeightPerApex + HalfHeightPad);
	Fov = FMath::Clamp(2.0 * FMath::RadiansToDegrees(FMath::Atan(Half / Distance)), MinFlightFov, MaxFlightFov);
	Response = Phase == EDerbyPhase::Flight ? FlightResponse : CallResponse;
	if (Phase == EDerbyPhase::Flight && FlightClock < OffTheBatSeconds)
	{
		Response = OffTheBatResponse;
	}
	if (Phase == EDerbyPhase::Result && IsHomeRun(Outcome.Result))
	{
		const double Back = FMath::SmoothStep(PullBackFrom, PullBackTo, Age);
		Look = FMath::Lerp(BallM, FVector(BallM.X * PullBackShare, BallM.Y * PullBackShare, PullBackHeight), Back);
		Fov = FMath::Lerp(Fov, PullBackFov, Back);
		Response = PullBackResponse;
	}
}

void ADerbyGame::UpdateCamera(float Dt)
{
	double Aspect = DefaultAspect;
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		FVector2D Size;
		Viewport->GetViewportSize(Size);
		Aspect = Size.Y > 0.0 ? Size.X / Size.Y : Aspect;
	}
	FVector Pos = PlatePos, Look = PlateLook;
	double Fov = PlateFov, Response = DefaultResponse;
	const double Age = GetPhaseAge();
	switch (Phase)
	{
	case EDerbyPhase::Intro:
	{
		const double T = FMath::SmoothStep(0.0, 1.0, Age / Tuning.IntroSeconds);
		Pos = FMath::Lerp(CraneFromPos, CraneToPos, T);
		Look = FMath::Lerp(CraneFromLook, CraneToLook, T);
		Fov = FMath::Lerp(CraneFromFov, PlateFov, T);
		Response = SnapResponse;
		break;
	}
	case EDerbyPhase::StepIn:
		Pos = HeroPos + HeroDrift * Age;
		Look = HeroLook;
		Fov = HeroFov;
		Response = SnapResponse;
		break;
	case EDerbyPhase::Pitch:
		Pos += PitchPush * FMath::SmoothStep(0.0, 1.0, (Clock - PitchReleasedAt) / FMath::Max(MinPitchFlightSeconds, Pitch.FlightMs / 1000.0));
		break;
	case EDerbyPhase::Flight:
	case EDerbyPhase::Result:
		if (HasHitFlight())
		{
			FlightShot(Age, Pos, Look, Fov, Response);
		}
		break;
	case EDerbyPhase::Finished:
	{
		const double A = UE_DOUBLE_HALF_PI + FinaleSwing * FMath::Sin(Age * FinaleSwingRate) - FinaleSwingOffset;
		Pos = FVector(FMath::Cos(A) * FinaleRadius, FinaleCentreY + FMath::Sin(A) * FinaleRadius, FinaleHeight);
		Look = FinaleLook;
		Fov = FinaleFov;
		Response = FinaleResponse;
		break;
	}
	default:
		break;
	}
	if (!bCamPlaced)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-Response * FMath::Min<double>(Dt, MaxEaseStep));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * FMath::Min(1.0, K * LookEaseBoost);
	CamFov += (Fov - CamFov) * (1.0 - FMath::Exp(-FovResponse * Dt));
	Shake *= FMath::Exp(-ShakeDecay * Dt);
	const FVector Jolt(0.0, FMath::Sin(Clock * ShakeSideRate) * Shake * ShakeSide, FMath::Sin(Clock * ShakeUpRate + ShakeUpPhase) * Shake * ShakeUp);
	const FVector From = ToCm(CamPos + Jolt);
	const FVector To = ToCm(CamLook);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	// The shots are framed by their vertical field of view; the camera component wants the horizontal one.
	const double HorizontalFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CamFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HorizontalFov));
}

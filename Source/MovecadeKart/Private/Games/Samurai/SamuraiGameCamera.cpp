// ASamuraiGame: the round's camera (cameraDirector.ts arena): fitted to the lanes, swept onto the dojo at the
// start, pushed in at the finale, leaned in by Focus and kicked by impacts.

#include "Games/Samurai/SamuraiGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Games/ArcadeFraming.h"
#include "Engine/GameViewportClient.h"
#include "Games/Samurai/SamuraiTuning.h"

using namespace MovecadeKart::Samurai;
using namespace MovecadeKart::Samurai::Tuning;

double ASamuraiGame::ViewportAspect() const
{
	FVector2D Size(16.0, 9.0);
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->GetViewportSize(Size);
	}
	return Size.Y > 0.0 ? Size.X / Size.Y : 16.0 / 9.0;
}

void ASamuraiGame::UpdateCamera(float Dt)
{
	if (!Camera)
	{
		return;
	}
	const double Aspect = ViewportAspect();
	// Back far enough that every lane fits the (vertical) field of view at this aspect.
	const double HalfWidth = (LaneCount() - 1) * LaneHalfSpan + LaneEdgeMargin;
	// Held upright, backing off would put the dojo's walls in the frame: the camera stays where a 16:9 screen has
	// it and opens the lens until the lanes fit across instead (the fruit's arcs get the extra height).
	const bool bPortrait = ArcadeFraming::IsPortrait(Aspect);
	const double FitAspect = bPortrait ? ArcadeFraming::DesignAspect : Aspect;
	const double Fit = HalfWidth / (FMath::Tan(FMath::DegreesToRadians(BaseFov / 2.0)) * FitAspect * FitAspectSlack) + FitExtraDistance;
	const double Distance = FMath::Max(MinDistance, Fit);
	FVector Position(FMath::Sin(Clock * SwayRate) * SwayAmplitude, Height + BobAmplitude * FMath::Sin(Clock * BobRate), Distance);
	FVector Look(0.0, LookY, 0.0);
	const double PlayFov = bPortrait ? FMath::Max(BaseFov, ArcadeFraming::VerticalFovForWidth(HalfWidth, Distance, Aspect, FitAspectSlack)) : BaseFov;
	double Fov = PlayFov;
	if (Stage == ESamuraiStage::Intro)
	{
		const double T = Ease::Smooth(0.0, 1.0, GetStageAge() / IntroSweepSeconds);
		Position = FMath::Lerp(FVector(IntroFromX, IntroFromY, Distance + IntroExtraDistance), Position, T);
		Look = FMath::Lerp(FVector(0.0, IntroLookY, IntroLookZ), Look, T);
		Fov = FMath::Lerp(FMath::Max(IntroFov, PlayFov), PlayFov, T);
	}
	else if (Stage == ESamuraiStage::Finished)
	{
		const double T = Ease::Smooth(0.0, 1.0, GetStageAge() / FinaleSeconds);
		Position = FMath::Lerp(Position, FVector(0.0, FinaleY, Distance * FinaleDistanceScale), T);
		Look = FMath::Lerp(Look, FVector(0.0, FinaleLookY, FinaleLookZ), T);
	}
	// Focus leans in a touch; perfect cuts and bombs kick the lens.
	Fov += FovKick - FocusFovLean * FocusMood;
	if (!bCameraPlaced)
	{
		CameraPosition = Position;
		CameraLook = Look;
		CameraFov = Fov;
		bCameraPlaced = true;
	}
	const double Follow = 1.0 - FMath::Exp(-FollowRate * FMath::Min<double>(Dt, FollowMaxStep));
	CameraPosition += (Position - CameraPosition) * Follow;
	CameraLook += (Look - CameraLook) * Follow;
	CameraFov += (Fov - CameraFov) * (1.0 - FMath::Exp(-FovFollowRate * Dt));
	// impact.ts: shake decays e^-9t, the punch e^-14t (toward the scene).
	Shake *= FMath::Exp(-ShakeDecay * Dt);
	Punch *= FMath::Exp(-PunchDecay * Dt);
	const FVector Jolt(FMath::Sin(Clock * ShakeFrequencyX) * Shake * ShakeAmplitudeX, FMath::Sin(Clock * ShakeFrequencyY + ShakePhaseY) * Shake * ShakeAmplitudeY, -Punch);
	const FVector From = ToWorld(CameraPosition + Jolt);
	const FVector To = ToWorld(CameraLook);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	// The web's field of view is vertical.
	ArcadeFraming::SetVerticalFov(*Camera->GetCameraComponent(), CameraFov);
}

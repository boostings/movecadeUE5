// AKartRaceManager's cameras: the intro flyover over the track's static shots, the results orbit round the winner,
// the per-viewport race views, and hiding karts that block a viewport's own kart.
#include "Race/KartRaceManager.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "KartCoords.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"

using namespace MovecadeKart;

namespace
{
	/** store.ts: 5 intro shots x 1.2 s, cubic blends of 0.8 s; each shot dollies in 0.7 m/s and crabs 0.4 m/s
	 *  (alternating sides), the look leading by 0.2 m/s, starting 0.4 s in. */
	constexpr int32 IntroShots = 5;
	constexpr double IntroShotSeconds = 1.2;
	constexpr float IntroBlendSeconds = 0.8f;
	constexpr double IntroPushCm = 70.0;
	constexpr double IntroCrabCm = 40.0;
	constexpr double IntroLookCrabCm = 20.0;
	constexpr double IntroLeadSeconds = 0.4;
	/** Vertical FOVs (degrees) of the intro and results cameras, at 16:9. */
	constexpr double IntroFov = 60.0;
	constexpr double ResultsFov = 64.0;
	constexpr double ShotAspect = 16.0 / 9.0;

	/** Results: a low, slow orbit round the winner (the web sat high at (6.4, 4.6, 8.4)), 6.5 m out and 2.1 m up at
	 *  0.18 rad/s, close enough to see the driver celebrate, eased in at 4.6/s. The standings card fills the left of
	 *  the screen, so the view turns 22 degrees to put the winner on the right third. */
	constexpr double ResultsOrbitRate = 0.18;
	constexpr double ResultsRadius = 6.5;
	constexpr double ResultsHeight = 2.1;
	constexpr double ResultsLookHeight = 1.2;
	constexpr double ResultsFollowRate = 4.6;
	constexpr double ResultsYawOffset = 22.0;
	/** A cut under a quick warm white flash. */
	constexpr float ResultsFlashSeconds = 0.55f;
	const FLinearColor ResultsFlashColor(1.f, 0.97f, 0.92f);

	/** Occluders: a kart is hidden from a viewport when it sits between that viewport's camera and its own kart. */
	constexpr double OccluderCentreHeight = 80.0;    // cm above the kart's origin: the body, not the wheels
	constexpr double OccluderMaxAlong = 0.92;        // only karts in front of the own kart along the sight line
	constexpr double OccluderRadius = 220.0;         // cm from the sight line

	double HorizontalFov(double VerticalDegrees, double Aspect)
	{
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalDegrees) * 0.5) * Aspect));
	}
}

void AKartRaceManager::SpawnCameras()
{
	for (ACameraActor* Camera : IntroCameras)
	{
		if (Camera)
		{
			Camera->Destroy();
		}
	}
	IntroCameras.Reset();
	for (int32 i = 0; i < FMath::Min(IntroShots, Track->StaticCameras.Num()); ++i)
	{
		const FStaticCamera& Shot = Track->StaticCameras[i];
		const FVector Eye = Coords::ToUE(Shot.Position + FVector(0.0, Track->SuggestedRebaseY, 0.0));
		const FVector Look = Coords::ToUE(Shot.LookAt + FVector(0.0, Track->SuggestedRebaseY, 0.0));
		ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(Eye, (Look - Eye).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(HorizontalFov(IntroFov, ShotAspect));
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		IntroCameras.Add(Camera);
	}
	if (!ResultsCamera)
	{
		ResultsCamera = GetWorld()->SpawnActor<ACameraActor>();
		ResultsCamera->GetCameraComponent()->SetFieldOfView(HorizontalFov(ResultsFov, ShotAspect));
		ResultsCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
}

void AKartRaceManager::UseRaceViews()
{
	for (FKartRacer& Racer : Racers)
	{
		if (Racer.Viewer && Racer.Viewer->GetViewTarget() != Racer.Kart)
		{
			// A cut: the countdown opens on the crane shot (AKartPawn::PlayCountdownSwoop), which carries the motion.
			Racer.Viewer->SetViewTarget(Racer.Kart);
		}
	}
}

void AKartRaceManager::CutToResults()
{
	ResultsCameraPos = FVector::ZeroVector;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	// A cut under a flash: blending between two moving cameras flies through the scenery.
	PC->SetViewTarget(ResultsCamera);
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(0.85f, 0.f, ResultsFlashSeconds, ResultsFlashColor, false, false);
	}
}

void AKartRaceManager::UpdateCameras(float DeltaSeconds)
{
	if (!GetWorld()->GetFirstPlayerController())
	{
		return;
	}
	if (Phase == EKartRacePhase::Intro && IntroCameras.Num() > 0)
	{
		UpdateIntroCameras();
	}
	else if (Phase == EKartRacePhase::Finished && ResultsCamera && Results.Num() > 0)
	{
		UpdateResultsCamera(DeltaSeconds);
	}
}

void AKartRaceManager::UpdateIntroCameras()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const int32 Shot = FMath::Min(FMath::FloorToInt32(PhaseSeconds / IntroShotSeconds), IntroCameras.Num() - 1);
	if (Shot != IntroShot)
	{
		IntroShot = Shot;
		PC->SetViewTargetWithBlend(IntroCameras[Shot], Shot == 0 ? 0.f : IntroBlendSeconds, VTBlend_Cubic);
	}
	// Each shot is a slow dolly (a push in, and a crab that alternates side per shot) instead of a still, like a
	// broadcast flyover. Every camera keeps moving through the blend into the next one.
	for (int32 i = 0; i < IntroCameras.Num(); ++i)
	{
		const FStaticCamera& Base = Track->StaticCameras[i];
		const FVector Rebase(0.0, Track->SuggestedRebaseY, 0.0);
		const FVector Eye = Coords::ToUE(Base.Position + Rebase);
		const FVector Look = Coords::ToUE(Base.LookAt + Rebase);
		const FVector Toward = (Look - Eye).GetSafeNormal();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward).GetSafeNormal() * (i % 2 == 0 ? 1.0 : -1.0);
		const double T = PhaseSeconds - i * IntroShotSeconds + IntroLeadSeconds;
		const FVector Moved = Eye + (Toward * IntroPushCm + Side * IntroCrabCm) * T;
		IntroCameras[i]->SetActorLocationAndRotation(Moved, (Look + Side * IntroLookCrabCm * T - Moved).Rotation());
	}
}

void AKartRaceManager::UpdateResultsCamera(float DeltaSeconds)
{
	const AKartPawn* Winner = Racers[Results[0].Seat].Kart;
	const FVector2D P = Winner->GetKartMovement()->RenderPosition();
	const double Orbit = FMath::Atan2(8.4, 6.4) + ResultsOrbitRate * PhaseSeconds;
	const FVector Target = Coords::ToUE(FVector(P.X + ResultsRadius * FMath::Cos(Orbit), ResultsHeight, P.Y + ResultsRadius * FMath::Sin(Orbit)));
	const FVector Look = Coords::ToUE(P, ResultsLookHeight);
	const double Alpha = 1.0 - FMath::Exp(-ResultsFollowRate * FMath::Min(static_cast<double>(DeltaSeconds), 1.0 / 30.0));
	ResultsCameraPos = ResultsCameraPos.IsZero() ? Target : FMath::Lerp(ResultsCameraPos, Target, Alpha);
	FRotator View = (Look - ResultsCameraPos).Rotation();
	View.Yaw -= ResultsYawOffset;
	ResultsCamera->SetActorLocationAndRotation(ResultsCameraPos, View);
}

void AKartRaceManager::UpdateOccluders()
{
	// A kart bunched between a viewport's chase camera and its own kart blocks the view (the web has the same
	// problem). Hide such karts for that viewport only.
	for (FKartRacer& Racer : Racers)
	{
		APlayerController* PC = Racer.Viewer;
		if (!PC || !PC->PlayerCameraManager)
		{
			continue;
		}
		// Only un-hide the karts this function hid, so other systems can hide actors too.
		for (FKartRacer& Other : Racers)
		{
			PC->HiddenActors.Remove(Other.Kart);
		}
		if (PC->GetViewTarget() != Racer.Kart)
		{
			continue;
		}
		const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
		const FVector Own = Racer.Kart->GetActorLocation();
		const FVector Seg = Own - Eye;
		const double SegLenSq = Seg.SizeSquared();
		if (SegLenSq < 1.0)
		{
			continue;
		}
		for (FKartRacer& Other : Racers)
		{
			if (&Other == &Racer)
			{
				continue;
			}
			const FVector P = Other.Kart->GetActorLocation() + FVector(0.0, 0.0, OccluderCentreHeight);
			const double T = FVector::DotProduct(P - Eye, Seg) / SegLenSq;
			if (T > 0.0 && T < OccluderMaxAlong && FVector::Dist(P, Eye + Seg * T) < OccluderRadius)
			{
				PC->HiddenActors.Add(Other.Kart);
			}
		}
	}
}

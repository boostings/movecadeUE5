// ASumoGame's camera: the play view fitted to the ring that's left, the intro's sweep in from the sea, the push-in on a
// round's winner and the champion's orbit, smoothed, with the hits' shake on top. Shots are in metres (rules plane).

#include "Games/Sumo/SumoGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Games/Sumo/SumoPresentation.h"

using namespace MovecadeKart::Sumo;
using namespace MovecadeKart::Sumo::Presentation;

namespace
{
	constexpr double DefaultAspect = 16.0 / 9.0;
	/** The play view: vertical field of view, pitch down onto the ring, margins round the ring (m). */
	constexpr double PlayFov = 40.0;
	constexpr double PlayPitchDeg = 50.0;
	constexpr double MinFramedRadius = 3.5;
	constexpr double SideMargin = 1.4;
	constexpr double DepthMargin = 1.9;
	/** The view drifts this share of the way toward the standing pucks' centroid, aimed a little short of the middle. */
	constexpr double CentroidFollow = 0.15;
	const FVector PlayLookOffset(-0.8, 0.0, -0.4);

	/** The intro: in low over the water past a tasselled post, rising to the play view (done just before it ends). */
	const FVector IntroFrom(-24.0, -15.0, 1.5);
	const FVector IntroFromLook(4.0, 2.0, 1.2);
	constexpr double IntroFov = 50.0;
	constexpr double IntroSettleSeconds = 0.3;

	/** A round's winner: push in behind and above it. */
	const FVector WinnerPushOffset(-7.0, 0.0, 5.5);
	constexpr double WinnerPushFrom = 0.3;
	constexpr double WinnerPushTo = 1.4;
	constexpr double WinnerPushShare = 0.85;
	/** The winner is framed at chest height (m over its root). */
	constexpr double WinnerChestZ = 0.8;

	/** The champion: a slow low orbit with the moon and the torii behind. */
	constexpr double OrbitStart = -0.35;
	constexpr double OrbitSway = 0.22;
	constexpr double OrbitSwayRate = 0.25;
	constexpr double OrbitRate = 0.05;
	constexpr double OrbitRadius = 6.2;
	constexpr double OrbitHeight = 1.6;
	constexpr double OrbitLookZ = 0.3;
	constexpr double ChampionFov = 42.0;

	/** Smoothing rates (1/s): tight in the intro (it is already a curve), loose in play. */
	constexpr double IntroFollowRate = 12.0;
	constexpr double PlayFollowRate = 4.0;
	constexpr double MaxSmoothingStep = 1.0 / 20.0;

	/** The shake: a jolt sideways and up and a roll, at unrelated frequencies (rad/s) so it never looks periodic. */
	constexpr double ShakeSideRate = 61.0;
	constexpr double ShakeUpRate = 53.0;
	constexpr double ShakeUpPhase = 1.1;
	constexpr double ShakeRollRate = 47.0;
	constexpr double ShakeDecay = 7.0;
	constexpr double ShakeSideM = 0.16;
	constexpr double ShakeUpM = 0.12;
	constexpr double ShakeRollDeg = 1.2;
}

double ASumoGame::ViewAspect() const
{
	FVector2D Size(DefaultAspect, 1.0);
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->GetViewportSize(Size);
	}
	return Size.Y > 0.0 ? Size.X / Size.Y : DefaultAspect;
}

ASumoGame::FShot ASumoGame::PlayShot(double Aspect) const
{
	// From the front and above, fitting the ring that's left (closing in as it shrinks), drifting toward the action.
	const double R = FMath::Max(MinFramedRadius, Match.Radius);
	FVector2D Centroid = FVector2D::ZeroVector;
	int32 Standing = 0;
	for (const FPuck& P : Match.Pucks)
	{
		if (P.bStanding)
		{
			Centroid += P.Pos;
			++Standing;
		}
	}
	Centroid = Standing > 0 ? Centroid / Standing : FVector2D::ZeroVector;
	const double Pitch = FMath::DegreesToRadians(PlayPitchDeg);
	// Fit the ring's width in the horizontal field (with a margin), and its foreshortened depth in the vertical.
	const double HalfH = FMath::DegreesToRadians(PlayFov / 2.0);
	const double HalfW = FMath::Atan(FMath::Tan(HalfH) * Aspect);
	const double Dist = FMath::Max((R + SideMargin) / FMath::Tan(HalfW), (R * FMath::Sin(Pitch) + DepthMargin) / FMath::Tan(HalfH));
	FShot Shot;
	Shot.Look = FVector(Centroid.X * CentroidFollow + PlayLookOffset.X, Centroid.Y * CentroidFollow, PlayLookOffset.Z);
	Shot.Pos = Shot.Look + FVector(-FMath::Cos(Pitch), 0.0, FMath::Sin(Pitch)) * Dist;
	Shot.Fov = PlayFov;
	return Shot;
}

void ASumoGame::ApplyIntroShot(FShot& Shot) const
{
	const double T = SmoothStep(0.0, 1.0, GetStageAge() / (IntroSeconds - IntroSettleSeconds));
	Shot.Pos = FMath::Lerp(IntroFrom, Shot.Pos, T);
	Shot.Look = FMath::Lerp(IntroFromLook, Shot.Look, T);
	Shot.Fov = FMath::Lerp(IntroFov, Shot.Fov, T);
}

void ASumoGame::ApplyWinnerShot(FShot& Shot) const
{
	const bool bChampion = Stage == ESumoStage::Finished;
	const int32 Winner = bChampion ? Match.MatchWinner : Match.RoundWinner;
	if (!Match.Pucks.IsValidIndex(Winner))
	{
		return;
	}
	const FVector At = PuckWorld(Winner) / 100.0 + FVector(0.0, 0.0, WinnerChestZ);
	if (bChampion)
	{
		const double Age = GetStageAge();
		const double A = OrbitStart + OrbitSway * FMath::Sin(Age * OrbitSwayRate) + Age * OrbitRate;
		Shot.Pos = At + FVector(-FMath::Cos(A) * OrbitRadius, FMath::Sin(A) * OrbitRadius, OrbitHeight);
		Shot.Look = At + FVector(0.0, 0.0, OrbitLookZ);
		Shot.Fov = ChampionFov;
		return;
	}
	const double T = SmoothStep(WinnerPushFrom, WinnerPushTo, Clock - RoundOverAt);
	Shot.Pos = FMath::Lerp(Shot.Pos, At + WinnerPushOffset, T * WinnerPushShare);
	Shot.Look = FMath::Lerp(Shot.Look, At, T * WinnerPushShare);
}

void ASumoGame::UpdateCamera(float Dt)
{
	const double Aspect = ViewAspect();
	FShot Shot = PlayShot(Aspect);
	if (Stage == ESumoStage::Intro)
	{
		ApplyIntroShot(Shot);
	}
	else if (Stage == ESumoStage::Finished || (Match.Phase == EPhase::RoundOver && Match.Pucks.IsValidIndex(Match.RoundWinner)))
	{
		ApplyWinnerShot(Shot);
	}
	if (!bCamPlaced)
	{
		CamPos = Shot.Pos;
		CamLook = Shot.Look;
		CamFov = Shot.Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-(Stage == ESumoStage::Intro ? IntroFollowRate : PlayFollowRate) * FMath::Min<double>(Dt, MaxSmoothingStep));
	CamPos += (Shot.Pos - CamPos) * K;
	CamLook += (Shot.Look - CamLook) * K;
	CamFov += (Shot.Fov - CamFov) * K;
	Shake *= FMath::Exp(-ShakeDecay * Dt);
	const FVector Jolt(0.0, FMath::Sin(Clock * ShakeSideRate) * Shake * ShakeSideM, FMath::Sin(Clock * ShakeUpRate + ShakeUpPhase) * Shake * ShakeUpM);
	const FVector From = (CamPos + Jolt) * 100.0;
	const FVector To = CamLook * 100.0;
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation() + FRotator(0.0, 0.0, FMath::Sin(Clock * ShakeRollRate) * Shake * ShakeRollDeg));
	// CamFov is vertical (it fits the ring's depth); the camera takes a horizontal field for this aspect.
	const double HorizontalFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CamFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HorizontalFov));
}

// AFishingGame's camera: behind the dock framing every angler and their water, the intro's swing round from out on
// the lake, the weigh-in's low shot of the anglers with the sunset behind them, and the shake.
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Games/ArcadeFraming.h"
#include "Engine/GameViewportClient.h"
#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingGameInternal.h"
#include "Games/Fishing/FishingLake.h"

using namespace MovecadeKart::Fishing::GameMath;

namespace
{
	/** Vertical field of view (degrees) in play, and the intro's wider start. */
	constexpr double PlayVFov = 40.0;
	constexpr double IntroVFov = 46.0;
	/** Room either side of the outer anglers (cm), and the share of the frame the dock may fill. */
	constexpr double SideMarginCm = 190.0;
	constexpr double FrameFill = 0.88;
	/** The camera never sits closer than this behind the anglers (cm). */
	constexpr double MinBackCm = 560.0;
	/** The anglers' heads: behind the fishing edge and above the deck (cm). */
	constexpr double HeadsBehindEdgeCm = 62.0;
	constexpr double HeadsOverDeckCm = 120.0;
	/** The camera rises a quarter of its distance back, and is pitched 10 degrees down. */
	constexpr double RisePerBack = 0.25;
	constexpr double PitchDownDeg = 10.0;
	/** The intro swings round 150 degrees, out past the anglers' faces, landing 0.4 s before the round. */
	constexpr double IntroSwingDeg = 150.0;
	constexpr double IntroSettleSeconds = 0.4;
	/** The weigh-in: the move in (s) and the slow drift side to side (rad/s, cm). */
	constexpr double FinishMoveSeconds = 1.6;
	constexpr double FinishDriftRate = 0.25;
	constexpr double FinishDriftCm = 60.0;
	/** How fast the camera follows its target (1/s), at most one 20 Hz frame's worth at a time. */
	constexpr double FollowRate = 6.0;
	constexpr double MaxFollowStep = 1.0 / 20.0;
	/** The shake's decay (1/s) and its jitter: frequencies (rad/s) and amplitudes (cm) across and up. */
	constexpr double ShakeDecay = 8.0;
	constexpr double ShakeFreqY = 61.0;
	constexpr double ShakeFreqZ = 53.0;
	constexpr double ShakeAmpY = 6.0;
	constexpr double ShakeAmpZ = 5.0;
}

double AFishingGame::ViewportAspect() const
{
	FVector2D Size(16.0, 9.0);
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->GetViewportSize(Size);
	}
	return Size.Y > 0.0 ? Size.X / Size.Y : 16.0 / 9.0;
}

void AFishingGame::UpdateCamera(float Dt)
{
	const double Aspect = ViewportAspect();
	const int32 N = FMath::Max(1, LaneCount());
	// Far enough back that every lane fits across the frame.
	const double HalfW = (N - 1) / 2.0 * AFishingLake::LaneSpacing + SideMarginCm;
	const double TanH = FMath::Tan(FMath::DegreesToRadians(PlayVFov / 2.0)) * Aspect;
	const double Back = FMath::Max(MinBackCm, HalfW / (TanH * FrameFill));
	const FVector Anglers(AFishingLake::EdgeX - HeadsBehindEdgeCm, 0.0, AFishingLake::DeckZ + HeadsOverDeckCm);
	// The heads a little below the middle, the horizon a quarter of the way down.
	FVector Pos = Anglers + FVector(-Back, 0.0, 10.0 + Back * RisePerBack);
	FVector Look = Pos + FVector(3000.0, 0.0, -3000.0 * FMath::Tan(FMath::DegreesToRadians(PitchDownDeg)));
	double Fov = PlayVFov;
	if (Stage == EFishingStage::Intro)
	{
		// A swing round from out on the lake (the anglers' faces, the sunset behind us) to behind the dock.
		const double T = Smooth(0.0, 1.0, GetStageAge() / (IntroSeconds - IntroSettleSeconds));
		const double Angle = FMath::DegreesToRadians(FMath::Lerp(IntroSwingDeg, 0.0, T));
		const double R = Back + 120.0;
		Pos = Anglers + FVector(-FMath::Cos(Angle) * R, FMath::Sin(Angle) * R * 0.85, FMath::Lerp(60.0, 10.0 + Back * RisePerBack, T));
		Look = FMath::Lerp(Anglers + FVector(0.0, 0.0, -10.0), Look, Smooth(0.35, 1.0, T));
		Fov = FMath::Lerp(IntroVFov, PlayVFov, T);
	}
	else if (Stage == EFishingStage::Finished)
	{
		// The anglers turn round: come in low and look at them with the lake and the sun behind.
		const double T = Smooth(0.0, FinishMoveSeconds, GetStageAge());
		const double Drift = FMath::Sin(GetStageAge() * FinishDriftRate) * FinishDriftCm;
		Pos = FMath::Lerp(Pos, Anglers + FVector(-Back * 0.95, Drift, 30.0), T);
		// Heads in the lower half: the weigh-in card sits over the lake.
		Look = FMath::Lerp(Look, Anglers + FVector(400.0, 0.0, 95.0), T);
	}
	if (!bCamPlaced)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-FollowRate * FMath::Min<double>(Dt, MaxFollowStep));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * K;
	CamFov += (Fov - CamFov) * K;
	Shake *= FMath::Exp(-ShakeDecay * Dt);
	const FVector Jolt(0.0, FMath::Sin(Clock * ShakeFreqY) * Shake * ShakeAmpY, FMath::Sin(Clock * ShakeFreqZ + 1.1) * Shake * ShakeAmpZ);
	const FVector From = CamPos + Jolt;
	Camera->SetActorLocationAndRotation(From, (CamLook - From).Rotation());
	ArcadeFraming::SetVerticalFov(*Camera->GetCameraComponent(), CamFov);
}

// AStackerGame: the altitude beats (a flight of birds, a camera peek) and the camera: intro, play, showcase.

#include "Games/Stacker/StackerGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Games/Stacker/StackerRooftop.h"
#include "StackerScene.h"

using namespace MovecadeKart::Stacker;
using namespace MovecadeKart::Stacker::Scene;

namespace
{
	/** StackerScene.tsx: four birds (eight wings) per beat for 1.8 s, every five rows; the peek lasts 1.2 s from ten. */
	constexpr int32 BirdCount = 4;
	constexpr double BirdBeatSeconds = 1.8;
	constexpr double PeekSeconds = 1.2;
	constexpr int32 HeightMilestone = 5;
	constexpr int32 PeekMilestone = 10;

	/** StackerScene.tsx's flight (web units, seconds): a wing pair per bird crossing over the tallest tower. */
	struct FBirdFlight
	{
		/** The flight wraps across most of the deck's width, never less than this. */
		static constexpr double MinRange = 18.0;
		static constexpr double RangePerDeck = 0.9;
		static constexpr double Speed = 2.4;
		static constexpr double EntranceRun = 5.0;
		static constexpr double Above = 1.2;
		static constexpr double Behind = -3.8;
		static constexpr double Echelon = 0.18;
		static constexpr double FlapBase = 0.22;
		static constexpr double FlapSwing = 0.18;
		static constexpr double FlapRate = 12.0;
		static constexpr double WingOffset = 0.24;
		static constexpr double WingSpan = 0.58;
		static constexpr double WingSpanScale = 0.85;
		static constexpr double WingThick = 0.08;
		static constexpr double WingHeightBase = 0.72;
		static constexpr double WingHeightPerLife = 0.2;
	};

	/** StackerScene.tsx: the home shot [0, 5.6, 10 + span * 2.5] looking at [0, 1.25, 0], 46 degrees tall. */
	constexpr double HomeFov = 46.0;
	constexpr double HomeHeight = 5.6;
	constexpr double HomeDistance = 10.0;
	constexpr double HomeDistancePerLane = 2.5;
	const FVector HomeLook(0.0, 1.25, 0.0);
	/** The lanes must fit: a lane every 8 units (AStackerRooftop::LaneX), the sweep's reach, a margin. */
	constexpr double LanePitch = 8.0;
	constexpr double FitMargin = 0.5;
	constexpr double FitWidthShare = 0.92;
	/** The intro drops from high on the right onto the home shot. */
	const FVector IntroFrom(6.0, 6.5, 24.0);
	const FVector IntroLookFrom(0.0, 2.0, 0.0);
	constexpr double IntroFov = 48.0;
	constexpr double IntroMoveSeconds = 3.0;
	/** The peek: a breath back and up to take in how high it's got. */
	const FVector PeekOffset(0.0, 1.6, 3.2);
	const FVector PeekLookOffset(0.0, -0.35, 0.0);
	constexpr double PeekFov = 4.0;
	/** The showcase: every compressed tower in frame, then an orbit of the winner. */
	constexpr double ShowcaseSideMargin = 3.0;
	constexpr double ShowcaseExtraDistance = 3.0;
	constexpr double ShowcaseFitShare = 0.9;
	constexpr double ShowcaseMinDistance = 9.0;
	constexpr double OrbitStartSeconds = 2.5;
	/** StackerScene.tsx: radius 5.2 + top * .9; a little wider here, where the results card shares the frame. */
	constexpr double OrbitRadius = 6.5;
	constexpr double OrbitRadiusPerTop = 1.15;
	/** The orbit at .32 rad/s, swung .85 rad back and forth across the front so the city stays behind the winner. */
	constexpr double OrbitRate = 0.32;
	constexpr double OrbitSwing = 0.85;
	constexpr double OrbitPushSeconds = 2.0;
	constexpr double OrbitFov = 44.0;
	/** The showcase framing: above and toward the tallest compressed tower; the orbit's centre, height and aim. */
	struct FShowcase
	{
		static constexpr double MinTowerY = 1.0;
		static constexpr double Height = 2.2;
		static constexpr double HeightPerTower = 0.6;
		static constexpr double LookPerTower = 0.45;
		static constexpr double OrbitCentreShare = 0.5;
		static constexpr double OrbitCentreLift = 0.2;
		static constexpr double OrbitHeight = 0.8;
		static constexpr double OrbitHeightPerTop = 0.15;
		/** Look this share of the radius past the winner (they stand left of the standings). */
		static constexpr double OrbitLookAside = 0.24;
	};
	/** The camera's own easing (per second, capped for a hitch) and the shake's decay. */
	constexpr double CameraRate = 7.0;
	constexpr double CameraMaxStep = 1.0 / 20.0;
	constexpr double ShakeDecay = 9.0;
	/** The shake's jolt: two unrelated rates across and up (rad/s), so it reads as a jolt rather than a wobble. */
	struct FJolt
	{
		static constexpr double RateX = 61.0;
		static constexpr double RateY = 53.0;
		static constexpr double PhaseY = 1.1;
		static constexpr double AmountX = 0.12;
		static constexpr double AmountY = 0.1;
	};
	constexpr double DefaultAspect = 16.0 / 9.0;

	/** The horizontal FOV for a vertical one at an aspect: what a camera component with no constraint takes. */
	double HorizontalFov(double VerticalDeg, double Aspect)
	{
		return 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalDeg) / 2.0) * Aspect));
	}

	/** How far back a camera with this vertical FOV must stand to see HalfWidth either side. */
	double FitDistance(double HalfWidth, double FovDeg, double Aspect, double Share)
	{
		return HalfWidth / (FMath::Tan(FMath::DegreesToRadians(FovDeg / 2.0)) * Aspect * Share);
	}
}

void AStackerGame::UpdateBeats(float Dt)
{
	// StackerScene.tsx updateAltitudeBeats: a flight of birds each five rows, a camera peek from row ten.
	const int32 Tallest = TallestHeight();
	const int32 Crossed = Tallest / HeightMilestone * HeightMilestone;
	if (Crossed < Beats.LastMilestone)
	{
		Beats.LastMilestone = Crossed;
	}
	if (Crossed > Beats.LastMilestone && Crossed > 0 && Stage == EStackerStage::Match)
	{
		Beats.LastMilestone = Crossed;
		Beats.BirdLife = BirdBeatSeconds;
		Beats.BirdRow = Tallest;
		if (Crossed >= PeekMilestone)
		{
			Beats.PeekRemaining = PeekSeconds;
		}
	}
	Beats.BirdLife = FMath::Max(0.0, Beats.BirdLife - Dt);
	Beats.PeekRemaining = FMath::Max(0.0, Beats.PeekRemaining - Dt);
	UpdateBirds();
}

void AStackerGame::UpdateBirds()
{
	double HighestScroll = 0.0;
	for (const FLane& Lane : Lanes)
	{
		HighestScroll = FMath::Max(HighestScroll, Lane.Scroll);
	}
	// A member, reused: no allocation per frame.
	TArray<FTransform>& Wings = BirdWings;
	Wings.Reset();
	if (Beats.BirdLife > 0.0 && Stage == EStackerStage::Match)
	{
		using B = FBirdFlight;
		const double Life = Beats.BirdLife / BirdBeatSeconds;
		const double Entrance = 1.0 - Life;
		const double Range = FMath::Max(B::MinRange, (Rooftop ? Rooftop->DeckWidth() : B::MinRange) * B::RangePerDeck);
		for (int32 b = 0; b < BirdCount; ++b)
		{
			// The flight crosses the sky above the tallest tower, a little behind the lanes, in a loose echelon.
			const double X = FMath::Fmod(Clock * B::Speed + b * Range / BirdCount + Entrance * B::EntranceRun, Range) - Range / 2.0;
			const double Y = (Beats.BirdRow - HighestScroll) * RowHeight + B::Above + b * B::Echelon;
			const double Z = B::Behind - b * B::Echelon;
			const double Flap = FMath::RadiansToDegrees(B::FlapBase + FMath::Sin(Clock * B::FlapRate + b) * B::FlapSwing) * 2.0;
			// web 0.58 x .08 x .08 wings, scaled (.85, .72 + life * .2, 1): UE (deep, across, up), cube 1 m.
			const FVector Scale(B::WingThick, B::WingSpan * B::WingSpanScale, B::WingThick * (B::WingHeightBase + Life * B::WingHeightPerLife));
			Wings.Add(FTransform(Tip(-Flap), W(FVector(X - B::WingOffset, Y, Z)), Scale));
			Wings.Add(FTransform(Tip(Flap), W(FVector(X + B::WingOffset, Y, Z)), Scale));
		}
	}
	// Fixed slots, parked between beats; written only while a flight is up (and once more to park it).
	const bool bFlying = Wings.Num() > 0;
	while (Wings.Num() < BirdCount * 2)
	{
		Wings.Add(Parked());
	}
	if (Birds->GetInstanceCount() != Wings.Num())
	{
		Birds->ClearInstances();
		Birds->AddInstances(Wings, false, false, false);
	}
	else if (bFlying || bBirdsOut)
	{
		Birds->BatchUpdateInstancesTransforms(0, Wings, false, true, true);
	}
	bBirdsOut = bFlying;
}

double AStackerGame::ViewportAspect() const
{
	FVector2D Size(16.0, 9.0);
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->GetViewportSize(Size);
	}
	return Size.Y > 0.0 ? Size.X / Size.Y : DefaultAspect;
}

AStackerGame::FCameraShot AStackerGame::FrameShot(double Aspect) const
{
	const int32 Span = LaneSpan();
	if (Stage == EStackerStage::Finished)
	{
		return ShowcaseShot(Aspect, Span);
	}
	FCameraShot Shot;
	Shot.Fov = HomeFov;
	Shot.Pos = FVector(0.0, HomeHeight, HomeDistance + Span * HomeDistancePerLane);
	Shot.Look = HomeLook;
	if (Span > 1)
	{
		// In the arena the lanes must fit the frame, whatever the window's aspect.
		const double HalfWidth = (Span - 1) * (LanePitch / 2.0) + TrackHalfWidth + FitMargin;
		Shot.Pos.Z = FMath::Max(Shot.Pos.Z, BuilderZ + FitDistance(HalfWidth, Shot.Fov, Aspect, FitWidthShare));
	}
	if (Stage == EStackerStage::Intro)
	{
		const double T = Smooth(0.0, 1.0, GetStageAge() / IntroMoveSeconds);
		Shot.Pos = FMath::Lerp(IntroFrom, Shot.Pos, T);
		Shot.Look = FMath::Lerp(IntroLookFrom, Shot.Look, T);
		Shot.Fov = FMath::Lerp(IntroFov, HomeFov, T);
	}
	else if (Beats.PeekRemaining > 0.0)
	{
		// The peek, then straight back to work.
		const double P = FMath::Sin(PI * (1.0 - Beats.PeekRemaining / PeekSeconds));
		const double Ease = P * P * (3.0 - 2.0 * P);
		Shot.Pos += PeekOffset * Ease;
		Shot.Look += PeekLookOffset * Ease;
		Shot.Fov += PeekFov * Ease;
	}
	return Shot;
}

AStackerGame::FCameraShot AStackerGame::ShowcaseShot(double Aspect, int32 Span) const
{
	double TallestY = FShowcase::MinTowerY;
	for (const FTower& T : Match.Towers)
	{
		TallestY = FMath::Max(TallestY, T.Blocks.Num() * CompareRowHeight);
	}
	FCameraShot Shot;
	Shot.Fov = HomeFov;
	const double HalfWidth = (Span - 1) * (LanePitch / 2.0) + ShowcaseSideMargin;
	const double FitZ = FitDistance(HalfWidth, Shot.Fov, Aspect, ShowcaseFitShare) + ShowcaseExtraDistance;
	Shot.Pos = FVector(0.0, FShowcase::Height + TallestY * FShowcase::HeightPerTower, FMath::Max(ShowcaseMinDistance, FitZ));
	Shot.Look = FVector(0.0, TallestY * FShowcase::LookPerTower, 0.0);
	const TArray<int32>& Order = StandingOrder.Get();
	if (GetStageAge() <= OrbitStartSeconds || Order.Num() == 0)
	{
		return Shot;
	}
	// The orbit of the winner (score, then height).
	const FTower& Winner = Match.Towers[Order[0]];
	const double Top = Winner.Blocks.Num() * CompareRowHeight;
	const double Radius = OrbitRadius + Top * OrbitRadiusPerTop;
	const double OrbitAge = GetStageAge() - OrbitStartSeconds;
	const double A = OrbitSwing * FMath::Sin(OrbitAge * OrbitRate);
	const FVector Centre(LaneXOf(Winner.LaneIndex), Top * FShowcase::OrbitCentreShare + FShowcase::OrbitCentreLift, 0.0);
	// The winner stands in the left of the frame (the standings fill the right): look past them to the right.
	const FVector Right(FMath::Cos(A), 0.0, -FMath::Sin(A));
	// A slow push in as the orbit starts, from the line-up into the winner's portrait.
	const double In = Smooth(0.0, OrbitPushSeconds, OrbitAge);
	Shot.Pos = FMath::Lerp(Shot.Pos, Centre + FVector(FMath::Sin(A) * Radius, FShowcase::OrbitHeight + Top * FShowcase::OrbitHeightPerTop, FMath::Cos(A) * Radius), In);
	Shot.Look = FMath::Lerp(Shot.Look, Centre + Right * Radius * FShowcase::OrbitLookAside, In);
	Shot.Fov = FMath::Lerp(HomeFov, OrbitFov, In);
	return Shot;
}

void AStackerGame::UpdateCamera(float Dt)
{
	const double Aspect = ViewportAspect();
	const FCameraShot Shot = FrameShot(Aspect);
	if (!bCamPlaced)
	{
		CamPos = Shot.Pos;
		CamLook = Shot.Look;
		CamFov = Shot.Fov;
		bCamPlaced = true;
	}
	const double K = Approach(CameraRate, FMath::Min<double>(Dt, CameraMaxStep));
	CamPos += (Shot.Pos - CamPos) * K;
	CamLook += (Shot.Look - CamLook) * K;
	CamFov += (Shot.Fov - CamFov) * K;
	Shake *= FMath::Exp(-ShakeDecay * Dt);
	const FVector Jolt(FMath::Sin(Clock * FJolt::RateX) * Shake * FJolt::AmountX, FMath::Sin(Clock * FJolt::RateY + FJolt::PhaseY) * Shake * FJolt::AmountY, 0.0);
	const FVector From = W(CamPos + Jolt);
	const FVector To = W(CamLook);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HorizontalFov(CamFov, Aspect)));
}

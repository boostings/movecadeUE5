// ADerbyGame's props and the ball: the code-built bat, ball and glove, the two avatars, and where the ball is each
// frame (in the pitcher's hand, on its way to the plate, or flying the rules' path) with its fire trail.

#include "Games/Derby/DerbyGame.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Derby/DerbyBallpark.h"
#include "Games/Derby/DerbyStage.h"
#include "NiagaraComponent.h"

using namespace MovecadeKart::Derby;
using namespace MovecadeKart::Derby::Stage;
namespace Fx = MovecadeKart::Fx;

namespace
{
	// ---- The cast.
	constexpr float BatterScale = 0.86f;
	constexpr float PitcherScale = 0.88f;
	/** The pitcher's look never changes: one varied house look. */
	constexpr int32 PitcherLookSeed = 11;
	/** The pitcher on the rubber (a touch behind it, on the mound's top), facing home. */
	const FVector PitcherSpotM(ADerbyBallpark::MoundX + 0.15, 0.0, 0.33);
	/** The batter stands on the box's chalk, facing the plate. */
	const FVector BatterLift(0.0, 0.0, 9.0);

	// ---- The ball.
	/** In the pitcher's throwing hand, a little in front of the bone. */
	const FVector BallInHand(-4.0, 0.0, 0.0);
	const FName ThrowingHand(TEXT("rightHand"));
	/** The ball's spin (degrees per second of the game clock, pitch and yaw). */
	constexpr double BallSpinPitch = -1400.0;
	constexpr double BallSpinYaw = 300.0;
	/** A hit grows with its distance from the camera (one size per this many cm, within the range) to stay readable. */
	constexpr double BallGrowCm = 2500.0;
	constexpr double BallMaxGrow = 4.0;
	/** After the call the ball stays up this long: on the ground after a hit, at the backstop after a miss. */
	constexpr double LandedBallSeconds = 1.8;
	constexpr double MissedBallSeconds = 0.6;

	// ---- The pitch's path (metres): an arc by type, the break late, and on to the backstop past the plate.
	constexpr double FastballArcM = 0.12;
	constexpr double ChangeupArcM = 0.28;
	constexpr double CurveArcM = 0.55;
	/** At full break the ball moves this far away from a right-handed batter and this far down. */
	constexpr double BreakAwayM = 0.42;
	constexpr double BreakDropM = 0.3;
	/** Past the plate the ball carries on at the velocity of its last few percent of flight (from, span), dropping. */
	constexpr double ExitSampleFrom = 0.97;
	constexpr double ExitSampleSpan = 0.03;
	constexpr double PastPlateDropMps = 0.8;
	constexpr double PastPlateMinHeightM = 0.1;

	// ---- The trail: a short tracer on the pitch, a long flame on a hit (particles per second).
	constexpr double PitchTrailRate = 90.0;
	constexpr double HitTrailRate = 220.0;
	const FLinearColor DefaultTrailEnd(1.f, 0.8f, 0.3f);

	/** The trail's fire stream (m/s, seconds, metres): a white-hot wisp rising off the ball, fading to the batter's colour. */
	Fx::FKartFxEmit TrailRecipe(const FVector2D& Life, const FVector2D& Size, double Intensity)
	{
		Fx::FKartFxEmit Emit;
		Emit.Field = Fx::EField::Fire;
		Emit.Direction = FVector::UpVector;
		Emit.Spread = 0.3;
		Emit.Speed = FVector2D(0.0, 0.15);
		Emit.Life = Life;
		Emit.Size = Size;
		Emit.SizeEnd = 0.15;
		Emit.Color = FLinearColor::White;
		Emit.Intensity = Intensity;
		return Emit;
	}
	/** On the pitch: a short, thin tracer. */
	const Fx::FKartFxEmit& PitchTracer() { static const Fx::FKartFxEmit E = TrailRecipe({ 0.12, 0.18 }, { 0.07, 0.1 }, 1.6); return E; }
	/** On a hit: a long, bright flame. */
	const Fx::FKartFxEmit& HitFlame() { static const Fx::FKartFxEmit E = TrailRecipe({ 0.45, 0.7 }, { 0.22, 0.3 }, 3.5); return E; }

	/** A bat along +Z: knob, handle, taper, barrel, rounded end (cm). Slot 0 wood, slot 1 grip tape. */
	UStaticMesh* BuildBat(UObject* Outer)
	{
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Wood"));
		B.Section(TEXT("Grip"));
		B.Lathe(TEXT("Grip"), { FVector2D(0.0, -2.0), FVector2D(2.2, -1.6), FVector2D(2.2, -0.6), FVector2D(1.35, 0.0), FVector2D(1.35, 18.0), FVector2D(1.4, 19.0) }, 12,
			FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		B.Lathe(TEXT("Wood"), { FVector2D(1.4, 18.5), FVector2D(1.6, 30.0), FVector2D(2.6, 50.0), FVector2D(3.3, 64.0), FVector2D(3.35, 80.0), FVector2D(3.0, 83.0),
			FVector2D(1.8, 85.0), FVector2D(0.0, 85.6) }, 14, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DerbyBat"));
	}

	/** A baseball: a sphere with a raised red seam (cm, radius 3.7). Slot 0 leather, slot 1 seam. */
	UStaticMesh* BuildBall(UObject* Outer)
	{
		constexpr int32 Stitches = 24;
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Leather"));
		B.Section(TEXT("Seam"));
		B.Ellipsoid(TEXT("Leather"), FVector::ZeroVector, FVector(3.7), 10, 16, FVector2D::ZeroVector);
		for (int32 i = 0; i < Stitches; ++i)
		{
			// The seam's figure-eight round the ball, as stitches.
			const double T = 2.0 * PI * i / Stitches;
			const FVector P(FMath::Cos(T) * 0.8 + 0.2 * FMath::Cos(3.0 * T), FMath::Sin(T) * 0.8 - 0.2 * FMath::Sin(3.0 * T), 0.6 * FMath::Sin(2.0 * T));
			B.Ellipsoid(TEXT("Seam"), P.GetSafeNormal() * 3.65, FVector(0.6), 3, 5, FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("DerbyBall"));
	}

	/** The pitcher's glove: a mitt and its thumb (cm). */
	UStaticMesh* BuildGlove(UObject* Outer)
	{
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Leather"));
		B.Ellipsoid(TEXT("Leather"), FVector(0.0, 0.0, 6.0), FVector(5.0, 11.0, 13.0), 6, 10, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Leather"), FVector(0.0, 8.0, 2.0), FVector(4.0, 4.0, 7.0), 4, 8, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DerbyGlove"));
	}
}

void ADerbyGame::BuildStage()
{
	using namespace MovecadeKart::Art;
	Ball = AddProp(TEXT("Ball"), BuildBall(this));
	Ball->SetMaterial(0, Plain(this, Hex(TEXT("#f6f3ea")), 0.55, 0.0, 0.35));
	Ball->SetMaterial(1, Plain(this, Hex(TEXT("#c8202b")), 0.6));
	Ball->SetWorldScale3D(FVector(BallScale));
	Bat = AddProp(TEXT("Bat"), BuildBat(this));
	Bat->SetMaterial(0, Plain(this, Hex(TEXT("#c9935a")), 0.35, 0.0));
	Bat->SetMaterial(1, Plain(this, Hex(TEXT("#1c1c22")), 0.8));
	Glove = AddProp(TEXT("Glove"), BuildGlove(this));
	Glove->SetMaterial(0, Plain(this, Hex(TEXT("#7a4524")), 0.7));

	Batter = AddAvatar(TEXT("Batter"), BatterScale);
	Pitcher = AddAvatar(TEXT("Pitcher"), PitcherScale);
	Pitcher->SetLook(FKartLook::MakeVaried(PitcherLookSeed));
	Pitcher->SetWorldLocationAndRotation(ToCm(PitcherSpotM), FRotator(0.0, 180.0, 0.0));
	Batter->SetWorldLocationAndRotation(ADerbyBallpark::BatterSpot() + BatterLift, FRotator(0.0, 90.0, 0.0));
	if (Seats.Num() > 0)
	{
		DressBatter(0);
	}
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		Trail = FxSys->MakeStream(Fx::EField::Fire, nullptr, TEXT("DerbyTrail"));
	}
	UE_CLOG(!Trail, LogDerby, Warning, TEXT("Derby: no FX stream for the ball's trail (KartFx subsystem or its Fire system missing)"));
}

UStaticMeshComponent* ADerbyGame::AddProp(const TCHAR* Name, UStaticMesh* Mesh)
{
	UStaticMeshComponent* Prop = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name));
	Prop->SetStaticMesh(Mesh);
	Prop->SetupAttachment(RootComponent);
	Prop->SetMobility(EComponentMobility::Movable);
	Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Prop->SetCastShadow(true);
	Prop->RegisterComponent();
	return Prop;
}

UArcadeAvatarComponent* ADerbyGame::AddAvatar(const TCHAR* Name, float Scale)
{
	UArcadeAvatarComponent* Avatar = NewObject<UArcadeAvatarComponent>(this, MakeUniqueObjectName(this, UArcadeAvatarComponent::StaticClass(), Name));
	Avatar->AvatarScale = Scale;
	Avatar->SetupAttachment(RootComponent);
	Avatar->RegisterComponent();
	return Avatar;
}

bool ADerbyGame::GetLanding(FVector& Out) const
{
	if (!bHasOutcome || !bLanded || Outcome.Flight.Path.Num() == 0)
	{
		return false;
	}
	Out = HitBall(Outcome.Flight.HangS);
	return true;
}

FVector ADerbyGame::GetBallWorld() const
{
	return Ball ? Ball->GetComponentLocation() : FVector::ZeroVector;
}

bool ADerbyGame::HasHitFlight() const
{
	return bHasOutcome && Outcome.Flight.Path.Num() > 0;
}

FVector ADerbyGame::PitchBall(double T) const
{
	const double Flight = Pitch.FlightMs / 1000.0;
	const double S = Flight > 0.0 ? T / Flight : 1.0;
	const double Arc = Pitch.Type == EPitchType::Fastball ? FastballArcM : (Pitch.Type == EPitchType::Changeup ? ChangeupArcM : CurveArcM);
	const auto At = [this, Arc](double U)
	{
		FVector P = FMath::Lerp(ReleaseM, PlateM, U);
		P.Z += Arc * 4.0 * U * (1.0 - U);
		// The break comes late: away from a right-handed batter and down.
		P.Y += BreakAwayM * Pitch.Break * U * U * U;
		P.Z -= BreakDropM * Pitch.Break * U * U * U;
		return P;
	};
	if (S <= 1.0)
	{
		return ToCm(At(FMath::Max(0.0, S)));
	}
	// Past the plate: on along its last direction to the backstop, dropping.
	const FVector End = At(1.0);
	const FVector Velocity = (At(1.0) - At(ExitSampleFrom)) / (ExitSampleSpan * Flight);
	FVector P = End + Velocity * (T - Flight);
	if (P.X < BackstopM.X)
	{
		P.X = BackstopM.X;
	}
	P.Z = FMath::Max(PastPlateMinHeightM, End.Z - PastPlateDropMps * (T - Flight));
	return ToCm(P);
}

FVector ADerbyGame::HitBall(double T) const
{
	// The rules fly the ball in its vertical plane: X along the ground from the plate, Y up; the spray turns it.
	const FVector2D P = Outcome.Flight.At(T);
	const double A = FMath::DegreesToRadians(Outcome.Contact.SprayDeg);
	const FVector From = ToCm(FVector(PlateM.X, PlateM.Y, 0.0));
	return From + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * P.X * 100.0 + FVector(0.0, 0.0, P.Y * 100.0);
}

void ADerbyGame::UpdateBall()
{
	FVector At = FVector::ZeroVector;
	bool bTrail = false;
	bool bShow = true;
	switch (Phase)
	{
	case EDerbyPhase::Pitch:
	case EDerbyPhase::Result:
		if (HasHitFlight())
		{
			// Where the hit came down.
			At = HitBall(Outcome.Flight.HangS);
			bShow = Phase != EDerbyPhase::Result || GetPhaseAge() < LandedBallSeconds;
		}
		else if (bHasPitch)
		{
			At = PitchBall(Clock - PitchReleasedAt);
			bTrail = Phase == EDerbyPhase::Pitch;
			bShow = Phase == EDerbyPhase::Pitch || GetPhaseAge() < MissedBallSeconds;
		}
		else
		{
			bShow = false;
		}
		break;
	case EDerbyPhase::Flight:
		At = HitBall(FlightClock);
		bTrail = true;
		break;
	default:
		At = Pitcher->BoneWorld(ThrowingHand) + BallInHand;
		break;
	}
	Ball->SetVisibility(bShow);
	Ball->SetWorldLocationAndRotation(At, FRotator(Clock * BallSpinPitch, Clock * BallSpinYaw, 0.0));
	// Readable from the broadcast camera however far it carries: it grows with the distance.
	const bool bHitShot = Phase == EDerbyPhase::Flight || Phase == EDerbyPhase::Result;
	const double Far = Camera ? FVector::Dist(Camera->GetActorLocation(), At) : 0.0;
	Ball->SetWorldScale3D(FVector(BallScale * (bHitShot ? FMath::Clamp(Far / BallGrowCm, 1.0, BallMaxGrow) : 1.0)));
	UpdateTrail(At, bTrail && bShow);
}

void ADerbyGame::UpdateTrail(const FVector& At, bool bTrail)
{
	if (!Trail)
	{
		return;
	}
	Trail->SetWorldLocation(At);
	const FDerbySeat* Up = GetBatter();
	const bool bHit = Phase == EDerbyPhase::Flight;
	Fx::FKartFxEmit Emit = bHit ? HitFlame() : PitchTracer();
	// The tracer burns out in the batter's colour.
	Emit.ColorEnd = Up ? Up->Color : DefaultTrailEnd;
	UKartFxSubsystem::UpdateStream(Trail, Emit, bTrail ? (bHit ? HitTrailRate : PitchTrailRate) : 0.0);
}

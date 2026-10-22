// AKartPawn's pose: the sim's presentation layers (slip yaw, body yaw, lean, hop), the item-driven spin and shrink,
// the sprung chassis from FKartFeel, the paint, the wheels and the steering wheel.
#include "KartPawn.h"

#include "Components/SkeletalMeshComponent.h"
#include "KartCoords.h"
#include "KartMovementComponent.h"
#include "KartPawnTuning.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Pawn;

namespace
{
	/** An impact always registers at least this hard in the feel. */
	constexpr double MinImpact = 0.2;
	/** Driver whiplash kicks: a head-on hit throws the torso back, a glancing one toward the struck side; a mini-turbo
	 *  presses the driver back. */
	constexpr double ImpactJolt = 4.0;
	constexpr double MiniTurboJolt = 2.5;
	/** Revving on the grid: the chassis buzzes (cm, two frequencies) and shudders (degrees) with the engine. */
	constexpr double GridBuzzCm = 0.45;
	constexpr double GridBuzzCm2 = 0.25;
	constexpr double GridShudderDegrees = 0.35;
	/** Squash keeps the volume, but never widens past twice (a quarter of the area). */
	constexpr double MinSquashArea = 0.25;
	/** The lightning shrink eases at this rate; a respawned kart pops back in over RespawnPopSeconds from RespawnPopFrom. */
	constexpr double ShrinkRate = 8.0;
	constexpr double RespawnPopSeconds = 0.5;
	constexpr double RespawnPopFrom = 0.15;
	/** Prism paint: the hue cycles at this many steps (of 256) per second. */
	constexpr double PrismHueRate = 600.0;
	/** Kart.jsx:202-242: fronts steer with yRot * 30, every wheel spins speed * 0.6 rad/s. */
	constexpr double WheelSteerScale = 30.0;
	constexpr double WheelSpinPerSpeed = 0.6;
	/** drivingGrip.ts: the rim turns 1.1 x the clamped steer, damped at 20/s. */
	constexpr double RimTurnScale = 1.1;
	constexpr double RimTurnRate = 20.0;

	/** 0 -> 1 with a springy overshoot. */
	double EaseOutBack(double T)
	{
		constexpr double C1 = 1.70158, C3 = C1 + 1.0;
		const double X = FMath::Clamp(T, 0.0, 1.0) - 1.0;
		return 1.0 + C3 * X * X * X + C1 * X * X;
	}
}

void AKartPawn::UpdateFeel(float DeltaSeconds)
{
	const FKartState& S = Movement->SimState();
	FKartFeelInput In;
	In.DeltaSeconds = DeltaSeconds;
	In.Speed = S.Speed;
	In.Yaw = Movement->RenderYaw();
	In.HopOffset = S.HopOffset;
	In.DriftDirection = S.DriftDirection;
	In.DriftPower = S.DriftPower;
	In.BoostMultiplier = S.BoostMultiplier;
	In.BoostTimeRemaining = S.BoostTimeRemaining;
	In.bOnDirt = S.bOnDirt;
	ReadImpacts(S, In);
	if (S.DriftBoostCount != SeenDriftBoosts)
	{
		SeenDriftBoosts = S.DriftBoostCount;
		JoltBack.Kick(MiniTurboJolt);
	}
	In.DriftBoostCount = S.DriftBoostCount;
	In.TeleportCount = S.TeleportCount;
	const FKartFeelPose& Pose = Feel.Update(In);

	// The model frame: +X is the kart's left, +Y forward, +Z up (see VehicleRoot). Signs come from the axes, so
	// "nose up" and "right side down" stay true whatever the handedness.
	static const double PitchSign = SignToward(FVector::XAxisVector, FVector::YAxisVector, FVector::UpVector);
	static const double RollSign = SignToward(FVector::YAxisVector, -FVector::XAxisVector, -FVector::UpVector);
	const FQuat Chassis = FQuat(FVector::YAxisVector, RollSign * FMath::DegreesToRadians(Pose.ChassisRoll))
		* FQuat(FVector::XAxisVector, PitchSign * FMath::DegreesToRadians(Pose.ChassisPitch));
	// Squash keeps the volume: what the height and length lose, the width gains.
	const double Width = 1.0 / FMath::Sqrt(FMath::Max(MinSquashArea, Pose.SquashUp * Pose.StretchForward));
	const double Time = GetWorld()->GetTimeSeconds();
	const double Buzz = GridRev * (FMath::Sin(Time * 47.0) * GridBuzzCm + FMath::Sin(Time * 71.0 + 1.3) * GridBuzzCm2);
	const FQuat Shudder(FVector::YAxisVector, FMath::DegreesToRadians(GridRev * GridShudderDegrees * FMath::Sin(Time * 39.0)));
	// Set without propagating: UpdateVisual re-poses the slip/body/chassis chain once (PoseVehicle).
	ChassisPivot->SetRelativeLocation_Direct(FVector(0, 0, Pose.ChassisHeave * 100.0 + Buzz));
	ChassisPivot->SetRelativeRotation_Direct((Shudder * Chassis).Rotator());
	ChassisPivot->SetRelativeScale3D_Direct(FVector(Width, Pose.StretchForward, Pose.SquashUp));
}

void AKartPawn::ReadImpacts(const FKartState& S, FKartFeelInput& In)
{
	// Impacts are one-step pulses: react once per event, however many frames (or steps) pass in between.
	const bool bNewWallHit = S.BarrierEventCount != SeenBarrierEvents;
	const bool bNewBump = S.BumpEventCount != SeenBumpEvents;
	SeenBarrierEvents = S.BarrierEventCount;
	SeenBumpEvents = S.BumpEventCount;
	In.WallImpact = bNewWallHit ? FMath::Max(S.BarrierEvent, MinImpact) : 0.0;
	In.BumpImpact = bNewBump ? FMath::Max(S.BumpEvent, MinImpact) : 0.0;
	if ((!bNewWallHit && !bNewBump) || S.ImpactNormal.IsNearlyZero())
	{
		return;
	}
	// The normal points away from what was hit: pointing to the kart's left means the hit was on its right.
	const double Yaw = Movement->RenderYaw();
	const FVector2D Right(FMath::Cos(Yaw), -FMath::Sin(Yaw));
	const FVector2D Ahead(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
	In.WallSide = FVector2D::DotProduct(S.ImpactNormal, Right) < 0.0 ? 1.0 : -1.0;
	// The driver's whiplash: thrown forward then back by a head-on hit, toward the struck side by a glancing one.
	const double Impact = FMath::Max(In.WallImpact, In.BumpImpact);
	const double HeadOn = FMath::Abs(FVector2D::DotProduct(S.ImpactNormal, Ahead));
	JoltBack.Kick(-ImpactJolt * Impact * HeadOn);
	JoltSide.Kick(ImpactJolt * Impact * (1.0 - HeadOn) * In.WallSide);
}

void AKartPawn::PoseVehicle()
{
	// The pivots' relative transforms were set directly this frame; update the chain once from the highest pivot
	// that actually moved (a moved parent re-poses everything under it, so the rest needs nothing).
	for (USceneComponent* Pivot : { SlipPivot.Get(), BodyPivot.Get(), ChassisPivot.Get() })
	{
		const FTransform Before = Pivot->GetComponentTransform();
		Pivot->UpdateComponentToWorld();
		if (!Before.Equals(Pivot->GetComponentTransform(), UE_SMALL_NUMBER))
		{
			break;
		}
	}
}

void AKartPawn::UpdateVisual(float DeltaSeconds)
{
	using Coords::YawToUE;
	const FKartState& S = Movement->SimState();
	SlipPivot->SetRelativeRotation_Direct(FRotator(0, YawToUE(S.SlipYaw) + ItemVisual.SpinDegrees, 0));
	BodyPivot->SetRelativeLocation_Direct(FVector(0, 0, S.HopOffset * 100.0 + ItemVisual.PopCm));
	BodyPivot->SetRelativeRotation_Direct(FRotator(0, YawToUE(S.GroupYaw), FMath::RadiansToDegrees(-S.Lean) + ItemVisual.TumbleDegrees));
	// Lightning shrink eases in and out; a respawned kart pops back in with an overshoot.
	ShrinkScale = FMath::FInterpTo(ShrinkScale, ItemVisual.Scale, DeltaSeconds, ShrinkRate);
	const double SincePop = GetWorld()->GetTimeSeconds() - RespawnPopStart;
	const double Pop = SincePop < RespawnPopSeconds ? FMath::Lerp(RespawnPopFrom, 1.0, EaseOutBack(SincePop / RespawnPopSeconds)) : 1.0;
	BodyPivot->SetRelativeScale3D_Direct(FVector(ShrinkScale * Pop));
	PoseVehicle();

	UpdatePaint();
	if (VehicleRoot->IsVisible() == ItemVisual.bRocket)
	{
		// The rocket shell (UKartFxComponent) replaces the vehicle and driver.
		VehicleRoot->SetVisibility(!ItemVisual.bRocket, true);
		if (!ItemVisual.bRocket)
		{
			Outfit->SetVisibility(Outfit->GetSkeletalMeshAsset() != nullptr);
		}
	}
	UpdateWheels(DeltaSeconds);
}

void AKartPawn::UpdatePaint()
{
	// Prism: the paint cycles through the rainbow (pushed only when the colour changes).
	static const FName PaintParam(TEXT("PaintColor"));
	const double Time = GetWorld()->GetTimeSeconds();
	const FLinearColor Paint = ItemVisual.bAura ? FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(Time * PrismHueRate, 256.0)), 200, 255) : PaintColor;
	if (PaintMaterial && (PaintMaterial != SentPaintMaterial || Paint != SentPaint))
	{
		PaintMaterial->SetVectorParameterValue(PaintParam, Paint);
		SentPaintMaterial = PaintMaterial;
		SentPaint = Paint;
	}
}

void AKartPawn::UpdateWheels(float DeltaSeconds)
{
	using Coords::YawToUE;
	const FKartState& S = Movement->SimState();
	WheelSpin = FMath::Fmod(WheelSpin + S.Speed * WheelSpinPerSpeed * DeltaSeconds, 2.0 * UE_DOUBLE_PI);
	static const double SpinSign = SignToward(FVector::XAxisVector, FVector::UpVector, FVector::YAxisVector); // top rolls forward (+Y)
	const FQuat Spin(FVector::XAxisVector, SpinSign * WheelSpin);
	const FQuat Steer = FRotator(0, YawToUE(S.SteerVisual * WheelSteerScale), 0).Quaternion();
	for (int32 i = 0; i < WheelPivots.Num(); ++i)
	{
		WheelPivots[i]->SetRelativeRotation(i < 2 ? Steer * Spin : Spin);
	}

	// Steering wheel: clockwise to the right; a handlebar turns with the front wheels.
	const double SteerRight = FMath::Clamp(-S.SteerVisual * SteerToRim, -1.0, 1.0);
	RimTurn = FMath::FInterpTo(RimTurn, SteerRight * RimTurnScale, DeltaSeconds, RimTurnRate);
	if (bHandlebar)
	{
		GripPivot->SetRelativeRotation(Steer);
	}
	else
	{
		// Turning right carries the rim's top toward the driver's right (-X in the model frame).
		const FVector Top = FVector::VectorPlaneProject(FVector::UpVector, GripNormal).GetSafeNormal();
		const double Sign = SignToward(GripNormal, Top, -FVector::XAxisVector);
		GripPivot->SetRelativeRotation(FQuat(GripNormal, Sign * RimTurn));
	}
}

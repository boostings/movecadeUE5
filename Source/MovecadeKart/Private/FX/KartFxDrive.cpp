// UKartFxComponent while driving: boost flames, exhaust puffs, drift smoke, sparks and glows, mini-turbo flourishes,
// off-road dust, tyre marks and speed lines (KartFxLayer.tsx, Kart.jsx, kartTrails.ts, Glow.jsx).
#include "FX/KartFxComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "FX/KartFxSubsystem.h"
#include "KartPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "Sim/KartSim.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;

namespace
{
	/** Most effects inherit this share of the kart's velocity, so they trail with it rather than hang in the air. */
	constexpr double DriveInherit = 0.85;
	/** Over a flame's ~0.15 s life the fire field's drag (3/s) eats a fifth of the inherited speed: overshoot it. */
	constexpr double PipeInherit = 1.22;
	/** An item boost (2x and up) gets a longer, fuller flame than a pad or a mini-turbo. */
	constexpr double ItemBoostMultiplier = 2.0;
	constexpr double BoostCoreRate = 50.0;
	constexpr double BoostSparkRate = 40.0;
	/** Kart.jsx:485: exhaust puffs above this speed when not boosting. */
	constexpr double ExhaustPuffMinSpeed = 15.0;
	constexpr double ExhaustPuffRate = 6.0;
	/** Drift sparks per second: the base plus this much per tier. */
	constexpr double DriftSparkBaseRate = 20.0;
	constexpr double DriftSparkTierRate = 16.0;
	/** Dust thins out below this speed (SU). */
	constexpr double DustFullSpeed = 25.0;

	/** Tyre marks: segments laid every SkidSegment cm of travel, reused oldest first. */
	constexpr double SkidSegment = 35.0;
	constexpr double SkidWidth = 22.0;
	constexpr int32 MaxSkidSegments = 240;
	/** Marks while drifting, or while the tail slips this far (rad) at speed; never in the air or on dirt. */
	constexpr double SkidMinSpeed = 10.0;
	constexpr double SkidSlipYaw = 0.16;
	constexpr double SkidSlipMinSpeed = 22.0;

	namespace Param
	{
		const FName Color(TEXT("Color"));
		const FName Intensity(TEXT("Intensity"));
		const FName Now(TEXT("Now"));
	}

	FKartFxEmit BoostFlame(const FVector& Back, bool bItemBoost, const FVector& InheritCm)
	{
		static const FLinearColor Hot = Hex(TEXT("ffc94a"));
		static const FLinearColor Ember = Hex(TEXT("ff3a0a"));
		FKartFxEmit Fire;
		Fire.Field = EField::Fire;
		Fire.Direction = Back;
		Fire.Spread = 0.2;
		Fire.Speed = { 2.5, 4.5 };
		Fire.Life = bItemBoost ? FVector2D(0.12, 0.24) : FVector2D(0.1, 0.2);
		Fire.Size = bItemBoost ? FVector2D(0.36, 0.5) : FVector2D(0.32, 0.44);
		Fire.SizeEnd = 0.3;
		Fire.Color = Hot;
		Fire.ColorEnd = Ember;
		Fire.Intensity = bItemBoost ? 3.4 : 2.8;
		Fire.InheritCm = InheritCm;
		return Fire;
	}

	/** A hot blue-white core inside each flame. */
	FKartFxEmit BoostCoreFlame(const FKartFxEmit& Flame, bool bItemBoost)
	{
		static const FLinearColor White = Hex(TEXT("eaf4ff"));
		static const FLinearColor Blue = Hex(TEXT("5aa8ff"));
		FKartFxEmit Core = Flame;
		Core.Speed = { 1.0, 2.0 };
		Core.Life = { 0.06, 0.1 };
		Core.Size = { 0.22, 0.28 };
		Core.SizeEnd = 0.6;
		Core.Color = White;
		Core.ColorEnd = Blue;
		Core.Intensity = bItemBoost ? 5.0 : 4.0;
		return Core;
	}

	FKartFxEmit BoostSpark(const FVector& Back, const FVector& InheritCm)
	{
		static const FLinearColor Hot = Hex(TEXT("ffe8a8"));
		static const FLinearColor Ember = Hex(TEXT("ff6a2a"));
		FKartFxEmit Spark;
		Spark.Field = EField::Sparks;
		Spark.Direction = Back;
		Spark.Spread = 0.35;
		Spark.Speed = { 5.0, 8.0 };
		Spark.Life = { 0.15, 0.3 };
		Spark.Size = { 0.05, 0.08 };
		Spark.Color = Hot;
		Spark.ColorEnd = Ember;
		Spark.Intensity = 2.4;
		Spark.Jitter = 0.2;
		Spark.InheritCm = InheritCm;
		return Spark;
	}

	FKartFxEmit ExhaustPuffRecipe(const FVector& Back, const FVector& InheritCm)
	{
		static const FLinearColor White = Hex(TEXT("f2f0ee"));
		static const FLinearColor Grey = Hex(TEXT("d0ccc6"));
		FKartFxEmit Puff;
		Puff.Field = EField::Smoke;
		Puff.Direction = Back;
		Puff.Spread = 0.4;
		Puff.Speed = { 0.28, 0.58 };
		Puff.Life = { 0.25, 0.45 };
		Puff.Size = { 0.12, 0.22 };
		Puff.SizeEnd = 2.4;
		Puff.Color = White;
		Puff.ColorEnd = Grey;
		Puff.InheritCm = InheritCm;
		return Puff;
	}

	/** Bigger and hotter sparks at each tier, so the charge reads from behind and across the screen. */
	FKartFxEmit DriftSpark(const FVector& Back, const FDriftLevel& Level, const FVector& InheritCm)
	{
		FKartFxEmit Spark;
		Spark.Field = EField::Sparks;
		Spark.Direction = FVector(Back.X, Back.Y, 0.7).GetSafeNormal();
		Spark.Spread = 0.7;
		Spark.Speed = { 3.0, 6.5 };
		Spark.Life = { 0.12, 0.3 };
		Spark.Size = FVector2D(0.045, 0.08) * (1.0 + 0.22 * Level.Level);
		Spark.Color = FLinearColor::LerpUsingHSV(Level.Color, FLinearColor::White, 0.35f);
		Spark.ColorEnd = Level.Color;
		Spark.Intensity = 3.0 + 0.7 * Level.Level;
		Spark.InheritCm = InheritCm;
		return Spark;
	}

	/** White smoke, tinted toward the drift tier's colour once a mini-turbo is charging. */
	FKartFxEmit DriftSmoke(const FVector& Back, const FDriftLevel& Level, const FVector& InheritCm)
	{
		static const FLinearColor SmokeWhite = Hex(TEXT("f4f3f6"));
		static const FLinearColor SmokeGrey = Hex(TEXT("cfcdd6"));
		FKartFxEmit Smoke;
		Smoke.Field = EField::Smoke;
		Smoke.Direction = FVector(Back.X, Back.Y, 0.5).GetSafeNormal();
		Smoke.Spread = 0.6;
		Smoke.Speed = { 0.6, 1.6 };
		Smoke.Life = { 0.45, 0.8 };
		Smoke.Size = { 0.22, 0.34 };
		Smoke.SizeEnd = 2.8;
		const float Tint = Level.Level > 0 ? 0.12f + 0.04f * Level.Level : 0.f;
		Smoke.Color = FMath::Lerp(SmokeWhite, Level.Color, Tint);
		Smoke.ColorEnd = FMath::Lerp(SmokeGrey, Level.Color, Tint * 0.6f);
		Smoke.InheritCm = InheritCm;
		Smoke.Jitter = 0.1;
		return Smoke;
	}

	/** Chunky tan puffs kicked up behind the rear wheels off the road (KartFxLayer.tsx:247-266). */
	FKartFxEmit OffRoadDust(const FVector& Back, const FVector& InheritCm)
	{
		static const FLinearColor Sand = Hex(TEXT("e3c99c"));
		static const FLinearColor Earth = Hex(TEXT("c9ab7c"));
		FKartFxEmit Dust;
		Dust.Field = EField::Smoke;
		Dust.Direction = FVector(Back.X, Back.Y, 0.8).GetSafeNormal();
		Dust.Spread = 0.7;
		Dust.Speed = { 0.8, 2.2 };
		Dust.Life = { 0.45, 0.8 };
		Dust.Size = { 0.24, 0.4 };
		Dust.SizeEnd = 2.6;
		Dust.Color = Sand;
		Dust.ColorEnd = Earth;
		Dust.InheritCm = InheritCm;
		Dust.Jitter = 0.15;
		return Dust;
	}
}

void UKartFxComponent::UpdateDriveFx(const FKartState& S, double Time, bool bBoosting, bool bRocket)
{
	const AKartPawn* Kart = CastChecked<AKartPawn>(GetOwner());
	const FVector Back = (-Kart->GetActorForwardVector() + FVector(0.0, 0.0, 0.12)).GetSafeNormal();
	UpdateBoostStreams(S, Back, bBoosting, bRocket);
	UpdateExhaustPuffs(FMath::Abs(S.Speed), Back, bBoosting, bRocket);
	const FDriftLevel Level = DriftLevel(S.DriftPower);
	const FKartFxEmit Spark = UpdateDrift(S, Time, Back, Level);
	if (UKartFxSubsystem::Get(this))
	{
		EmitDriftFlourishes(S, Back, Level, Spark);
	}
	UpdateDust(S, Back);
}

void UKartFxComponent::UpdateBoostStreams(const FKartState& S, const FVector& Back, bool bBoosting, bool bRocket)
{
	// KartFxLayer.tsx:159-194: 70/s per pipe. They inherit the kart's whole velocity so each pipe grows a short flame
	// cone instead of a trail of blobs.
	const bool bItemBoost = S.BoostMultiplier >= ItemBoostMultiplier;
	const FKartFxEmit Flame = BoostFlame(Back, bItemBoost, Velocity() * PipeInherit);
	const double Rate = bBoosting && !bRocket ? BoostFlameRate : 0.0;
	for (UNiagaraComponent* C : BoostFire)
	{
		UKartFxSubsystem::UpdateStream(C, Flame, Rate);
	}
	const FKartFxEmit Core = BoostCoreFlame(Flame, bItemBoost);
	for (UNiagaraComponent* C : BoostCore)
	{
		UKartFxSubsystem::UpdateStream(C, Core, Rate > 0.0 ? BoostCoreRate : 0.0);
	}
	UKartFxSubsystem::UpdateStream(BoostSparks, BoostSpark(Back, Velocity()), Rate > 0.0 ? BoostSparkRate : 0.0);
}

void UKartFxComponent::UpdateExhaustPuffs(double Speed, const FVector& Back, bool bBoosting, bool bRocket)
{
	const FKartFxEmit Puff = ExhaustPuffRecipe(Back, Velocity() * DriveInherit * 0.6);
	const double Rate = Speed > ExhaustPuffMinSpeed && !bBoosting && !bRocket ? ExhaustPuffRate : 0.0;
	for (UNiagaraComponent* C : ExhaustPuff)
	{
		UKartFxSubsystem::UpdateStream(C, Puff, Rate);
	}
}

FKartFxEmit UKartFxComponent::UpdateDrift(const FKartState& S, double Time, const FVector& Back, const FDriftLevel& Level)
{
	// KartFxLayer.tsx:197-245.
	const double Speed = FMath::Abs(S.Speed);
	const FVector Inherit = Velocity() * DriveInherit;
	const bool bDrifting = S.DriftDirection != 0.0 && Speed > DriftSmokeMinSpeed && S.HopTime < 0.0;
	FKartFxEmit Spark = DriftSpark(Back, Level, Inherit * 0.6);
	const FKartFxEmit Smoke = DriftSmoke(Back, Level, Inherit * 0.3);
	for (UNiagaraComponent* C : WheelSmoke)
	{
		UKartFxSubsystem::UpdateStream(C, Smoke, bDrifting ? DriftSmokeRate * 0.7 : 0.0);
	}
	// More and faster sparks as the tier climbs.
	const double SparkRate = bDrifting && Level.Level > 0 ? DriftSparkBaseRate + Level.Level * DriftSparkTierRate : 0.0;
	Spark.Speed *= 1.0 + 0.15 * Level.Level;
	for (UNiagaraComponent* C : WheelSparks)
	{
		UKartFxSubsystem::UpdateStream(C, Spark, SparkRate);
	}

	// Glow: white core, tier-coloured rim, on while drifting (Glow.jsx), pulsing faster at higher tiers.
	const bool bGlow = S.DriftDirection != 0.0;
	const double Pulse = 1.0 + 0.2 * FMath::Sin(Time * (22.0 + 6.0 * Level.Level));
	for (int32 i = 0; i < WheelGlow.Num(); ++i)
	{
		WheelGlow[i]->SetVisibility(bGlow);
		if (bGlow && GlowMaterials.IsValidIndex(i))
		{
			const FLinearColor C = Level.Level > 0 ? Level.Color : FLinearColor(0.9f, 0.9f, 1.f);
			GlowMaterials[i]->SetVectorParameterValue(Param::Color, C);
			GlowMaterials[i]->SetScalarParameterValue(Param::Intensity, static_cast<float>((Level.Level > 0 ? 6.0 : 1.2) * Pulse));
			WheelGlow[i]->SetRelativeScale3D(FVector((Level.Level > 0 ? 0.3 + 0.07 * Level.Level : 0.2) * Pulse));
		}
	}
	return Spark;
}

void UKartFxComponent::EmitDriftFlourishes(const FKartState& S, const FVector& Back, const FDriftLevel& Level, const FKartFxEmit& Spark)
{
	const FVector Inherit = Velocity() * DriveInherit;
	TArray<FKartFxEmit> Out;
	// A level-up flick of sparks and a twinkle in the new colour (Glow.jsx setColor -> Spark.emit).
	if (S.DriftDirection != 0.0 && Level.Level > LastDriftLevel.Level)
	{
		for (const UStaticMeshComponent* G : WheelGlow)
		{
			FKartFxEmit Flick = Spark;
			Flick.Position = G->GetComponentLocation();
			Flick.Count = 16;
			Flick.Spread = UE_DOUBLE_PI * 0.6;
			Flick.Direction = FVector::UpVector;
			Flick.Speed = { 2.0, 5.5 };
			Flick.Life = { 0.2, 0.4 };
			Flick.Size = { 0.05, 0.09 };
			Flick.InheritCm = Inherit;
			Out.Add(Flick);
			Out.Add(FlashStar(Flick.Position, 0.25, 0.9, 0.3, FLinearColor::White, Level.Color, 4.0, Velocity()));
		}
	}
	if (S.DriftDirection != 0.0)
	{
		LastDriftLevel = Level;
	}
	else if (S.DriftBoostCount == LastDriftBoostCount)
	{
		LastDriftLevel = FDriftLevel();
	}

	// Mini-turbo release: a ring and sparks in the grade's colour, a gout of flame from both pipes.
	if (S.DriftBoostCount != LastDriftBoostCount)
	{
		const FLinearColor Grade = DriftGradeColor(S.LastDriftBoost);
		// The pipes' midpoint; the kart's own origin if the streams are missing (FX content not built or cooked).
		const FVector Rear = BoostFire.Num() == 2 && BoostFire[0] && BoostFire[1]
			? (BoostFire[0]->GetComponentLocation() + BoostFire[1]->GetComponentLocation()) * 0.5
			: VehicleRoot->GetComponentLocation();
		const FVector Ground = RearGround();
		Out.Add(ShockRing(FVector(Rear.X, Rear.Y, Ground.Z + 10.0), 0.35, 0.8, 4.5, Grade, Grade * 0.6f, 2.4, Velocity()));
		FKartFxEmit Burst = Spark;
		Burst.Position = Rear;
		Burst.Count = 30;
		Burst.Spread = 1.1;
		Burst.Direction = FVector(Back.X, Back.Y, 0.6).GetSafeNormal();
		Burst.Speed = { 4.0, 10.0 };
		Burst.Life = { 0.2, 0.45 };
		Burst.Size = { 0.05, 0.1 };
		Burst.Color = FLinearColor::White;
		Burst.ColorEnd = Grade;
		Burst.Jitter = 0.4;
		Burst.InheritCm = Inherit;
		Out.Add(Burst);
		static const FLinearColor GoutHot = Hex(TEXT("fff0b8"));
		for (const UNiagaraComponent* Pipe : BoostFire)
		{
			if (!Pipe)
			{
				continue; // no FX content: MakeStream returned null
			}
			FKartFxEmit Gout;
			Gout.Field = EField::Fire;
			Gout.Position = Pipe->GetComponentLocation();
			Gout.Count = 12;
			Gout.Direction = Back;
			Gout.Spread = 0.35;
			Gout.Speed = { 3.0, 6.5 };
			Gout.Life = { 0.15, 0.3 };
			Gout.Size = { 0.4, 0.6 };
			Gout.SizeEnd = 0.4;
			Gout.Color = GoutHot;
			Gout.ColorEnd = Grade;
			Gout.Intensity = 3.0;
			Gout.InheritCm = Inherit;
			Out.Add(Gout);
		}
		LastDriftBoostCount = S.DriftBoostCount;
	}
	UKartFxSubsystem::Get(this)->Emit(Out);
}

void UKartFxComponent::UpdateDust(const FKartState& S, const FVector& Back)
{
	const double Speed = FMath::Abs(S.Speed);
	const FKartFxEmit Dust = OffRoadDust(Back, Velocity() * DriveInherit * 0.25);
	const double Rate = S.bOnDirt && Speed > DustMinSpeed ? DustRate * FMath::Clamp(Speed / DustFullSpeed, 0.4, 1.0) : 0.0;
	for (UNiagaraComponent* C : WheelDust)
	{
		UKartFxSubsystem::UpdateStream(C, Dust, Rate);
	}
}

void UKartFxComponent::UpdateSkids(const FKartState& S, double Time)
{
	if (!SkidMarks)
	{
		return;
	}
	const double Speed = FMath::Abs(S.Speed);
	const bool bSkid = S.HopTime < 0.0 && !S.bOnDirt && Speed > SkidMinSpeed &&
		(S.DriftDirection != 0.0 || (FMath::Abs(S.SlipYaw) > SkidSlipYaw && Speed > SkidSlipMinSpeed));
	if (SkidMaterial)
	{
		SkidMaterial->SetScalarParameterValue(Param::Now, static_cast<float>(Time));
	}
	const FTransform Frame = VehicleRoot->GetComponentTransform();
	for (int32 Wheel = 0; Wheel < 2; ++Wheel)
	{
		// Just above the road under the tyre.
		const FVector Contact = Frame.TransformPosition(Anchors.Wheel[Wheel]) + FVector(0.0, 0.0, 1.5);
		if (!bSkid)
		{
			bMarking[Wheel] = false;
			continue;
		}
		if (!bMarking[Wheel])
		{
			bMarking[Wheel] = true;
			LastMark[Wheel] = Contact;
			continue;
		}
		const FVector Step = Contact - LastMark[Wheel];
		if (Step.SizeSquared() < FMath::Square(SkidSegment))
		{
			continue;
		}
		if (Step.SizeSquared() > FMath::Square(SkidSegment * 8.0))
		{
			// A respawn or a frame hitch: start a fresh mark rather than streak across the track.
			LastMark[Wheel] = Contact;
			continue;
		}
		// The Plane is 1 m square in X/Y: stretch X along the step (a little overlap hides the joins).
		const FTransform Segment(Step.Rotation(), (Contact + LastMark[Wheel]) * 0.5, FVector(Step.Size() / 100.0 * 1.15, SkidWidth / 100.0, 1.0));
		int32 Index;
		if (SkidMarks->GetInstanceCount() < MaxSkidSegments)
		{
			Index = SkidMarks->AddInstance(Segment, /*bWorldSpace*/ true);
		}
		else
		{
			Index = NextSkid;
			SkidMarks->UpdateInstanceTransform(Index, Segment, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
		}
		NextSkid = (Index + 1) % MaxSkidSegments;
		// The material fades each segment out by its age: custom data 0 is when it was laid, "Now" the time. Adding,
		// moving and re-stamping are incremental instance updates, so the skid-mark proxy is never rebuilt.
		SkidMarks->SetCustomDataValue(Index, 0, static_cast<float>(Time), /*bMarkRenderStateDirty*/ false);
		LastMark[Wheel] = Contact;
	}
}

void UKartFxComponent::UpdateSpeedLines(const FKartState& S, bool bRocket)
{
	if (!SpeedLines)
	{
		return;
	}
	// Subtle: only above top speed (boosts, slipstream), stronger with the rocket.
	const double Speed = FMath::Abs(S.Speed);
	double Amount = FMath::Clamp((Speed - SpeedLinesFrom) / (SpeedLinesFull - SpeedLinesFrom), 0.0, 1.0);
	if (bRocket)
	{
		Amount = FMath::Max(Amount, 0.8);
	}
	Amount = Amount * Amount * (3.0 - 2.0 * Amount);
	FKartFxEmit Lines;
	Lines.Field = EField::SpeedLines;
	Lines.Size = { 2.2, 4.6 };     // tube radius around the view axis, m
	Lines.Speed = { 38.0, 55.0 };  // m/s rushing back
	Lines.Life = { 0.16, 0.26 };
	Lines.Jitter = 0.03;           // streak width, m
	Lines.Color = FLinearColor(1.f, 1.f, 1.f, static_cast<float>(0.32 * Amount));
	Lines.Intensity = 1.0;
	UKartFxSubsystem::UpdateStream(SpeedLines, Lines, IsViewed() && Amount > 0.02 ? 30.0 + 80.0 * Amount : 0.0);
}

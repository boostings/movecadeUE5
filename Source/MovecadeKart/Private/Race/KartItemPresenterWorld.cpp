// AKartItemPresenter, every frame: the boxes, the flying items and their trails, pop-ups, fireballs, bolts and lights.
#include "Race/KartItemPresenter.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "FX/KartItemFx.h"
#include "FX/KartItemMeshes.h"
#include "KartCoords.h"
#include "KartPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "Race/KartItemPresenterTuning.h"
#include "Race/KartRaceManager.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;
using namespace MovecadeKart::ItemPresenter;

void AKartItemPresenter::UpdateBoxes(double Time, double Now)
{
	const FKartItemWorld& Items = Manager->GetItems();
	TArray<FKartFxEmit> Out;
	// The gem stands on a corner (a cube tipped onto its diagonal) and tumbles slowly.
	static const FQuat Corner(FVector(1.0, -1.0, 0.0).GetSafeNormal(), FMath::Acos(1.0 / FMath::Sqrt(3.0)));
	for (int32 i = 0; i < BoxPositions.Num(); ++i)
	{
		const bool bVisible = Items.IsBoxVisible(i, Now);
		const bool bWasVisible = BoxWasVisible[i];
		if (bVisible && !bWasVisible)
		{
			BoxShownAt[i] = Time;
			BoxRespawnBurst(BoxPositions[i], Out);
		}
		BoxWasVisible[i] = bVisible;
		const double Scale = bVisible ? FMath::Max(0.0, Pop((Time - BoxShownAt[i]) / BoxPopSeconds)) : 0.0;
		const FVector P = BoxPositions[i] + FVector(0.0, 0.0, BoxBobCm * FMath::Sin(BoxBobRate * Time + i * 0.7));
		const FQuat Spin(FVector::UpVector, -BoxSpinRate * Time - i);
		BoxBatches[0].Add(FTransform(Spin, P, FVector(BoxSize * BoxOrbScale / 100.0 * Scale)));
		const FQuat Core = FQuat(FVector::UpVector, -BoxGemSpinRate * Time - i) * Corner * FQuat(FVector(1.0, 1.0, 1.0).GetSafeNormal(), 0.3 * FMath::Sin(Time * 1.3 + i));
		BoxBatches[1].Add(FTransform(Core, P, FVector(BoxSize * BoxGemScale / 100.0 * Scale)));
		// The halo breathes a little.
		BoxBatches[2].Add(FTransform(FQuat::Identity, P, FVector(BoxSize * BoxOrbScale * 2.0 / 100.0 * Scale * (1.0 + 0.04 * FMath::Sin(Time * 3.0 + i)))));
		if (BoxLabels->IsVisible())
		{
			BoxBatches[3].Add(FTransform(Spin, P, FVector(BoxSize / 100.0 * Scale)));
		}
		// The twinkle's options never change, so the stream only needs telling when the box appears or goes (or when
		// it isn't running yet), not every frame.
		UNiagaraComponent* Twinkle = BoxTwinkles.IsValidIndex(i) ? BoxTwinkles[i].Get() : nullptr;
		if (Twinkle && (bVisible != bWasVisible || (bVisible && !Twinkle->IsActive())))
		{
			UKartFxSubsystem::UpdateStream(Twinkle, BoxTwinkleStream(), bVisible ? BoxTwinkleRate : 0.0);
		}
	}
	Commit(Boxes, BoxBatches[0]);
	Commit(BoxCores, BoxBatches[1]);
	Commit(BoxHalos, BoxBatches[2]);
	Commit(BoxLabels, BoxBatches[3]);
	if (UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this))
	{
		Fx->Emit(Out);
	}
}

void AKartItemPresenter::UpdateItems(double Time)
{
	TArray<FKartFxEmit> Out;
	UpdateProjectiles(Time, Out);
	RetireTrails(Time, Out);
	UpdatePopups(Time, Out);
	AddShowcaseItems(Time);
	for (int32 i = 0; i < ItemMeshes.Num(); ++i)
	{
		Commit(ItemMeshes[i], ItemBatches[i]);
	}
	Commit(Glows, GlowBatch);
	Commit(BombGlows, BombGlowBatch);
	if (UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this))
	{
		Fx->Emit(Out);
	}
}

void AKartItemPresenter::UpdateProjectiles(double Time, TArray<FKartFxEmit>& Out)
{
	for (TPair<int32, FTrail>& Pair : Trails)
	{
		Pair.Value.bSeen = false;
	}
	for (const FKartItemProjectile& P : Manager->GetItems().Projectiles())
	{
		if (!MeshesFor(P.Type))
		{
			continue;
		}
		const double Yaw = FMath::Atan2(-P.Vel.X, -P.Vel.Y);
		FVector At;
		FVector TrailAt;
		// Poses from MultiplayerItems.tsx; heights are the sim's (y) plus the web's offsets, in metres.
		switch (P.Type)
		{
		case EKartItem::Banana:
			// Stationary, yaw += 2 rad/s, y + 0.3 (scaled up 1.5x so it reads at speed).
			At = Coords::ToUE(P.Pos, 0.3 + 0.12);
			BatchFor(P.Type).Add(FTransform(FRotator(0.0, FMath::RadiansToDegrees(2.0 * P.Age), 14.0), At, FVector(1.5)));
			TrailAt = At;
			break;
		case EKartItem::Bomb:
		{
			// Tumble x += 5, z += 3 rad/s; glow pulse 1 + sin(t * (3 + 2t)) * 0.3 (MultiplayerItems.tsx:528-537).
			At = Coords::ToUE(P.Pos, P.Y);
			const FRotator Tumble(FMath::RadiansToDegrees(3.0 * P.Age), 0.0, FMath::RadiansToDegrees(5.0 * P.Age));
			const double Pulse = 1.0 + FMath::Sin(P.Age * (3.0 + 2.0 * P.Age)) * 0.3;
			BatchFor(P.Type).Add(FTransform(Tumble, At, FVector(1.0)), static_cast<float>(1.0 + (Pulse - 0.7) * 6.0));
			BombGlowBatch.Add(FTransform(FQuat::Identity, At, FVector(1.2 * Pulse)));
			TrailAt = At + Tumble.RotateVector(BombFuseTip());
			break;
		}
		case EKartItem::SkySeeker:
			// Altitude y + 0.5, roll wobble sin(t*10)*0.1, glow 1.5x pulsing 1 + sin(t*8)*0.2.
			At = Coords::ToUE(P.Pos, P.Y + 0.5);
			BatchFor(P.Type).Add(FTransform(FRotator(0.0, Coords::YawToUE(Yaw), FMath::RadiansToDegrees(0.1 * FMath::Sin(Time * 10.0))), At, FVector(1.15)));
			GlowBatch.Add(FTransform(FQuat::Identity, At, FVector(0.9 * (1.0 + 0.2 * FMath::Sin(Time * 8.0)))));
			TrailAt = At;
			break;
		default:
			// Green spins yaw += 8 rad/s; red faces its velocity. y + 0.3.
			At = Coords::ToUE(P.Pos, 0.3);
			BatchFor(P.Type).Add(FTransform(FRotator(0.0, Coords::YawToUE(Yaw) + (P.Type == EKartItem::Shot ? FMath::RadiansToDegrees(8.0 * P.Age) : 0.0), 0.0), At, FVector(1.0)));
			TrailAt = At;
			break;
		}
		if (UKartFxSubsystem::Get(this))
		{
			FTrail& T = Trails.FindOrAdd(P.Id);
			T.bSeen = true;
			T.DeadSince = -1.0;
			T.Item = P.Type;
			T.LastPosition = At;
			UpdateTrails(P, TrailAt);
		}
	}
}

void AKartItemPresenter::UpdateTrails(const FKartItemProjectile& P, const FVector& TrailAt)
{
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	FTrail& T = Trails.FindChecked(P.Id);
	const bool bBomb = P.Type == EKartItem::Bomb;
	if (!T.Trail && P.Type != EKartItem::Banana)
	{
		T.Trail = Fx->MakeStream(bBomb ? EField::Smoke : EField::Fire, RootComponent, NAME_None);
		TrailComponents.Add(T.Trail);
		if (bBomb || P.Type == EKartItem::SkySeeker || P.Type == EKartItem::Seeker)
		{
			T.Sparks = Fx->MakeStream(EField::Sparks, RootComponent, NAME_None);
			TrailComponents.Add(T.Sparks);
		}
	}
	const FVector Vel = Coords::ToUE(P.Vel, 0.0) + FVector(0.0, 0.0, P.VY * 100.0);
	const bool bSky = P.Type == EKartItem::SkySeeker;
	if (T.Trail)
	{
		T.Trail->SetWorldLocation(TrailAt);
		if (bBomb)
		{
			UKartFxSubsystem::UpdateStream(T.Trail, BombFuseSmoke(), BombFuseSmokeRate);
		}
		else
		{
			UKartFxSubsystem::UpdateStream(T.Trail, ProjectileGlowTrail(ItemColors(P.Type), Vel, bSky), ProjectileGlowTrailRate);
		}
	}
	if (T.Sparks)
	{
		T.Sparks->SetWorldLocation(TrailAt);
		if (bBomb)
		{
			UKartFxSubsystem::UpdateStream(T.Sparks, BombFuseSparks(Vel), BombFuseSparksRate);
		}
		else
		{
			UKartFxSubsystem::UpdateStream(T.Sparks, ProjectileSparks(ItemColors(P.Type).Color, Vel), ProjectileSparksRate(bSky));
		}
	}
}

void AKartItemPresenter::RetireTrails(double Time, TArray<FKartFxEmit>& Out)
{
	// Projectiles that are gone: stop the trails, puff out the ones that simply expired, free them once faded.
	RecentImpacts.RemoveAll([Time](const TPair<FVector, double>& I) { return Time - I.Value > ImpactMemorySeconds; });
	for (auto It = Trails.CreateIterator(); It; ++It)
	{
		FTrail& T = It.Value();
		if (T.bSeen)
		{
			continue;
		}
		if (T.DeadSince < 0.0)
		{
			T.DeadSince = Time;
			UKartFxSubsystem::UpdateStream(T.Trail, FKartFxEmit(), 0.0);
			UKartFxSubsystem::UpdateStream(T.Sparks, FKartFxEmit(), 0.0);
			const bool bImpact = RecentImpacts.ContainsByPredicate([&T](const TPair<FVector, double>& I)
			{
				return FVector::DistSquared2D(I.Key, T.LastPosition) < FMath::Square(ImpactMemoryRadiusCm);
			});
			if (!bImpact && T.Item != EKartItem::Bomb)
			{
				Poof(T.LastPosition, ItemColors(T.Item).Color, T.Item == EKartItem::Banana ? 1.2 : 0.9, Out);
			}
		}
		else if (Time - T.DeadSince > TrailFadeSeconds)
		{
			for (UNiagaraComponent* C : { T.Trail.Get(), T.Sparks.Get() })
			{
				if (C)
				{
					TrailComponents.Remove(C);
					C->DestroyComponent();
				}
			}
			It.RemoveCurrent();
		}
	}
}

void AKartItemPresenter::UpdatePopups(double Time, TArray<FKartFxEmit>& Out)
{
	// The used item springs up over its kart, spins, then bursts in its colours.
	const bool bFx = UKartFxSubsystem::Get(this) != nullptr;
	for (int32 i = Popups.Num() - 1; i >= 0; --i)
	{
		const FPopup& Popup = Popups[i];
		const double Age = Time - Popup.Start;
		const AKartPawn* Kart = KartAt(Popup.Seat);
		if (!Kart || !MeshesFor(Popup.Item) || Age > PopupSeconds)
		{
			if (Kart && bFx)
			{
				PopupBurst(Kart->GetActorLocation() + FVector(0.0, 0.0, 250.0), ItemColors(Popup.Item), Kart->GetVelocity(), Out);
			}
			Popups.RemoveAtSwap(i);
			continue;
		}
		const double Swell = Age > PopupSeconds - PopupSwellSeconds ? 1.0 + (Age - (PopupSeconds - PopupSwellSeconds)) * 3.0 : 1.0;
		const double Scale = Pop(Age / PopupSpringSeconds) * Swell * 2.0;
		const FVector At = Kart->GetActorLocation() + FVector(0.0, 0.0, 170.0 + 80.0 * EaseOut(Age / PopupRiseSeconds));
		BatchFor(Popup.Item).Add(FTransform(FRotator(0.0, Kart->GetActorRotation().Yaw + 90.0 + Age * 540.0, 0.0), At, FVector(FMath::Max(0.01, Scale))));
	}
}

void AKartItemPresenter::UpdateBlasts(double Time)
{
	// Explosion fireballs: three overlapping lobes that swell fast (0.2 s) to ~0.42 x the blast radius, keep
	// expanding slowly and burn out to soot (toon fire colours in M_FX_Fireball), under the puffs of ExplosionBurst.
	ActiveBlasts.RemoveAll([Time](const FBlast& B) { return Time - B.Start > BlastSeconds; });
	for (const FBlast& B : ActiveBlasts)
	{
		const double T = (Time - B.Start) / BlastSeconds;
		const double Peak = B.Radius * 100.0 * BlastLobeScale;
		for (int32 i = 0; i < 3; ++i)
		{
			const double Delay = i * 0.05;
			const double U = FMath::Clamp((T - Delay) / (1.0 - Delay), 0.0, 1.0);
			const double Grow = EaseOut(U / 0.27) * (1.0 + 0.18 * U);
			const double Radius = Peak * (i == 0 ? 1.0 : 0.68) * Grow;
			const FVector Center = B.Center + B.Lobes[i] * (i == 0 ? 0.2 : 1.0) * Grow + FVector(0.0, 0.0, Radius * 0.35 + U * 120.0);
			// The sphere is 1 m across; the custom float is the burn (0..1) the material colours by.
			BlastBatch.Add(FTransform(FQuat(FVector::UpVector, i * 2.1), Center, FVector(FMath::Max(0.01, Radius / 50.0))), static_cast<float>(U));
		}
	}
	Commit(Blasts, BlastBatch);
}

void AKartItemPresenter::UpdateLights(double Time)
{
	// Light flashes: up in LightRiseSeconds, then out over the rest of their time; the light goes back to the pool.
	for (int32 i = 0; i < BlastLights.Num(); ++i)
	{
		UPointLightComponent* Light = BlastLights[i];
		FLightFlash& F = LightFlashes[i];
		if (!Light || F.Start < 0.0)
		{
			continue;
		}
		const double Age = Time - F.Start;
		if (Age > F.Seconds)
		{
			Light->SetIntensity(0.f);
			Light->SetVisibility(false);
			F.Start = -1.0;
			continue;
		}
		const double Env = Age < LightRiseSeconds ? Age / LightRiseSeconds : FMath::Pow(1.0 - (Age - LightRiseSeconds) / (F.Seconds - LightRiseSeconds), 2.0);
		Light->SetIntensity(static_cast<float>(F.Peak * Env));
	}
}

void AKartItemPresenter::FlashLight(const FKartFxLight& Flash, double Time)
{
	// A free light from the pool (the oldest lit one if all are busy), a new one only up to the pool size.
	int32 Slot = LightFlashes.IndexOfByPredicate([](const FLightFlash& F) { return F.Start < 0.0; });
	if (Slot == INDEX_NONE && BlastLights.Num() < LightPool)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("FlashLight")));
		Light->SetupAttachment(RootComponent);
		Light->SetUsingAbsoluteLocation(true);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(0.f);
		Light->SetCastShadows(false);
		Light->SetVolumetricScatteringIntensity(0.f);
		Light->RegisterComponent();
		Slot = BlastLights.Add(Light);
		LightFlashes.AddDefaulted();
	}
	if (Slot == INDEX_NONE)
	{
		Slot = 0;
		for (int32 i = 1; i < LightFlashes.Num(); ++i)
		{
			Slot = LightFlashes[i].Start < LightFlashes[Slot].Start ? i : Slot;
		}
	}
	if (UPointLightComponent* Light = BlastLights[Slot])
	{
		Light->SetWorldLocation(Flash.Position);
		Light->SetLightColor(Flash.Color);
		Light->SetAttenuationRadius(static_cast<float>(Flash.RadiusCm));
		Light->SetIntensity(0.f);
		Light->SetVisibility(true);
	}
	LightFlashes[Slot] = { Time, Flash.PeakCandela, FMath::Max(0.1, Flash.Seconds) };
}

void AKartItemPresenter::UpdateStrikes(double Time)
{
	static const FName FadeParam(TEXT("Fade"));
	for (int32 i = 0; i < Strikes.Num(); ++i)
	{
		const double Age = Time - Strikes[i].Start;
		const bool bOn = Age >= 0.0 && Age < StrikeSeconds;
		StrikeMeshes[i]->SetVisibility(bOn);
		if (bOn && StrikeMaterials[i])
		{
			// Flicker: full, a dip, a second flash, then out.
			const double T = Age / StrikeSeconds;
			const double Flicker = T < 0.25 ? 1.0 : (T < 0.4 ? 0.25 : (T < 0.6 ? 0.9 : FMath::Pow(1.0 - (T - 0.6) / 0.4, 2.0)));
			StrikeMaterials[i]->SetScalarParameterValue(FadeParam, static_cast<float>(Flicker));
		}
	}
}

// UKartFxComponent's one-off effects, each fired once per sim event however many steps passed this frame: wall
// scrapes and kart bumps (kartFxBus 'wall', 'bump'), the respawn pop, landing puffs and boost pad flashes.
#include "FX/KartFxComponent.h"

#include "KartAssetLoad.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFxSubsystem.h"
#include "KartCoords.h"
#include "KartPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Race/KartRaceManager.h"
#include "Sim/KartSim.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;

namespace
{
	const TCHAR* const PadMaterialPath = TEXT("/Game/FX/Materials/M_FX_PadFlash.M_FX_PadFlash");

	/** The sim's kart radius (1.15 m): a wall contact is this far from the kart, toward the wall. */
	constexpr double KartRadiusCm = 115.0;
	constexpr double BumpContactCm = 90.0;
	/** A grazing scrape bursts at this share of a hit's strength, and doesn't shake the camera. */
	constexpr double ScrapeScale = 0.45;
	/** Landings from this speed (SU) and up throw the full puff. */
	constexpr double LandingFullSpeed = 40.0;
	constexpr double PadFlashSeconds = 0.55;

	namespace Param
	{
		const FName Fade(TEXT("Fade"));
		const FName Age(TEXT("Age"));
	}

	/** Sparks out of the kart's nose and a ring under it as a boost pad fires. */
	void PadBurst(const FVector& KartCm, double GroundZ, const FVector& Forward, const FVector& VelocityCm, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Burst;
		Burst.Field = EField::Sparks;
		Burst.Position = KartCm + FVector(0.0, 0.0, 30.0);
		Burst.Count = 26;
		Burst.Direction = (Forward + FVector::UpVector * 0.4).GetSafeNormal();
		Burst.Spread = 0.7;
		Burst.Speed = { 6.0, 14.0 };
		Burst.Life = { 0.25, 0.5 };
		Burst.Size = { 0.06, 0.11 };
		Burst.Color = Hex(TEXT("fff2b0"));
		Burst.ColorEnd = Hex(TEXT("ff8a1a"));
		Burst.Intensity = 2.8;
		Burst.InheritCm = VelocityCm;
		Out.Add(Burst);
		Out.Add(ShockRing(FVector(KartCm.X, KartCm.Y, GroundZ + 10.0), 0.35, 1.0, 4.0, Hex(TEXT("ffd36a")), Hex(TEXT("ff7a1a")), 2.2, VelocityCm * 0.8));
	}
}

void UKartFxComponent::UpdateEvents(const FKartState& S, double Time)
{
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	if (!Fx)
	{
		return;
	}
	const FVector Location = GetOwner()->GetActorLocation();
	const FVector Ground = RearGround();
	TArray<FKartFxEmit> Out;
	EmitImpacts(S, Ground, Out);

	// Respawn: a pop of smoke and twinkles where the kart reappears.
	if (S.TeleportCount != SeenTeleports)
	{
		SeenTeleports = S.TeleportCount;
		Poof(Location + FVector(0.0, 0.0, 60.0), Hex(TEXT("8fd8ff")), 1.6, Out);
	}

	// Landing after a hop: dust under the rear wheels, harder the faster the kart was going.
	const bool bHopping = S.HopTime >= 0.0;
	if (bWasHopping && !bHopping)
	{
		const double Strength = FMath::Clamp(FMath::Abs(S.Speed) / LandingFullSpeed, 0.25, 1.0);
		for (int32 i = 0; i < 2; ++i)
		{
			LandingPuff(VehicleRoot->GetComponentTransform().TransformPosition(Anchors.Wheel[i]), Velocity(), Strength, S.bOnDirt, Out);
		}
	}
	bWasHopping = bHopping;

	EmitPadBoosts(S, Time, Ground, Out);
	Fx->Emit(Out);
}

void UKartFxComponent::EmitImpacts(const FKartState& S, const FVector& Ground, TArray<FKartFxEmit>& Out)
{
	AKartPawn* Kart = CastChecked<AKartPawn>(GetOwner());
	const FVector Location = Kart->GetActorLocation();
	const FVector Normal = Coords::ToUE(S.ImpactNormal, 0.0).GetSafeNormal();
	if (S.BarrierEventCount != SeenBarrierEvents)
	{
		SeenBarrierEvents = S.BarrierEventCount;
		const double Scale = FMath::Clamp(FMath::Max(S.BarrierEvent, S.CollisionIntensity), 0.2, 1.0);
		const bool bScrape = S.bBarrierEventSustained;
		// The normal points away from the wall.
		const FVector Contact = FVector(Location.X, Location.Y, Ground.Z) - Normal * KartRadiusCm;
		const int32 First = Out.Num();
		ImpactBurst(EImpact::Wall, Contact, bScrape ? Scale * ScrapeScale : Scale, Out);
		for (int32 i = First; i < Out.Num(); ++i)
		{
			Out[i].InheritCm = Velocity() * 0.6;
			if (Out[i].Field == EField::Sparks)
			{
				// Spray back along the wall and away from it.
				Out[i].Direction = (Normal + FVector::UpVector * 0.6 - Velocity().GetSafeNormal() * 0.5).GetSafeNormal();
				Out[i].Spread = 0.9;
			}
		}
		if (!bScrape)
		{
			Kart->AddShake(ImpactProfile(EImpact::Wall).Shake * Scale);
		}
	}
	if (S.BumpEventCount != SeenBumpEvents)
	{
		SeenBumpEvents = S.BumpEventCount;
		const double Scale = FMath::Clamp(S.BumpEvent, 0.2, 1.0);
		ImpactBurst(EImpact::Bump, FVector(Location.X, Location.Y, Ground.Z) - Normal * BumpContactCm, Scale, Out);
		Kart->AddShake(ImpactProfile(EImpact::Bump).Shake * Scale);
	}
}

void UKartFxComponent::EmitPadBoosts(const FKartState& S, double Time, const FVector& Ground, TArray<FKartFxEmit>& Out)
{
	// The sim stamps the pad's time when this kart triggers it.
	if (LastPadBoost.Num() != S.PadLastBoostTime.Num())
	{
		LastPadBoost = S.PadLastBoostTime;
	}
	const AKartRaceManager* R = Race.Get();
	const FTrack* Track = R ? R->GetTrack() : nullptr;
	const AActor* Kart = GetOwner();
	for (int32 i = 0; i < S.PadLastBoostTime.Num(); ++i)
	{
		if (S.PadLastBoostTime[i] == LastPadBoost[i])
		{
			continue;
		}
		LastPadBoost[i] = S.PadLastBoostTime[i];
		if (!Track || !Track->BoostPads.IsValidIndex(i))
		{
			continue;
		}
		const FBoostPad& Pad = Track->BoostPads[i];
		const FVector Centre(Coords::ToUE(Pad.Position).X, Coords::ToUE(Pad.Position).Y, Ground.Z + 6.0);
		// The Plane is 1 m square in X/Y; X along the pad, Y across (TrackBuilder). Chevrons run along +V, so turn it.
		PadFlash(i, Track->BoostPads.Num())->SetWorldTransform(FTransform(FRotator(0.0, Coords::YawToUE(Pad.Yaw) + 90.0, 0.0), Centre,
			FVector(Pad.Width * 1.05, Pad.Length * 1.05, 1.0)));
		PadFlashStart[i] = Time;
		PadBurst(Kart->GetActorLocation(), Ground.Z, Kart->GetActorForwardVector(), Velocity(), Out);
	}
}

UStaticMeshComponent* UKartFxComponent::PadFlash(int32 Pad, int32 PadCount)
{
	if (!PadFlashes.IsValidIndex(Pad))
	{
		PadFlashes.SetNum(PadCount);
		PadMaterials.SetNum(PadCount);
		PadFlashStart.Init(-1000.0, PadCount);
	}
	if (!PadFlashes[Pad])
	{
		// Attached to the kart's root but placed in world space.
		AActor* Owner = GetOwner();
		UStaticMeshComponent* C = MakeFxMesh(Owner->GetRootComponent(),
			MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), *FString::Printf(TEXT("FxPadFlash%d"), Pad)),
			LoadKartAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane.Plane"), AssetSource::Engine));
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		if (UMaterialInterface* M = LoadKartAsset<UMaterialInterface>(PadMaterialPath, AssetSource::FxMaterials))
		{
			PadMaterials[Pad] = UMaterialInstanceDynamic::Create(M, this);
			C->SetMaterial(0, PadMaterials[Pad]);
		}
		PadFlashes[Pad] = C;
	}
	return PadFlashes[Pad];
}

void UKartFxComponent::UpdatePadFlashes(double Time)
{
	for (int32 i = 0; i < PadFlashes.Num(); ++i)
	{
		UStaticMeshComponent* C = PadFlashes[i];
		if (!C)
		{
			continue;
		}
		const double Age = Time - PadFlashStart[i];
		const bool bOn = Age >= 0.0 && Age < PadFlashSeconds;
		C->SetVisibility(bOn);
		if (bOn && PadMaterials[i])
		{
			const double T = Age / PadFlashSeconds;
			PadMaterials[i]->SetScalarParameterValue(Param::Fade, static_cast<float>(3.0 * FMath::Pow(1.0 - T, 1.5)));
			PadMaterials[i]->SetScalarParameterValue(Param::Age, static_cast<float>(Age));
		}
	}
}

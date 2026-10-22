// ASumoGame's parts, built once in Setup: the bumper pucks with their riders, lights and FX streams, and the pool of
// power-up pickups the rules' power-ups are shown with.

#include "Games/Sumo/SumoGame.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Avatar/ArcadeAvatar.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Sumo/SumoArena.h"
#include "Games/Sumo/SumoPresentation.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"

using namespace MovecadeKart::Sumo;
using namespace MovecadeKart::Sumo::Presentation;
namespace Art = MovecadeKart::Art;
namespace Fx = MovecadeKart::Fx;
using MovecadeKart::FMeshBuilder;

namespace
{
	constexpr float RiderScale = 0.64f;

	// The puck's light: a pool of its colour on the ring under it.
	constexpr float PuckLightLumens = 260.f;
	constexpr float PuckLightRadiusCm = 380.f;
	constexpr float PuckLightSourceCm = 40.f;
	constexpr double PuckLightZ = 30.0;
	/** The shield bubble round the puck (in puck-mesh units, scaled with the puck). */
	constexpr double ShieldCentreZ = 70.0;
	const FVector ShieldScale(2.15, 2.15, 1.75);
	constexpr float ShieldOpacity = 0.22f;
	constexpr float ShieldGlow = 1.4f;
	/** The trim's glow when the dash is ready (its colour times this; it dims while reloading). */
	constexpr float TrimReadyGlow = 8.f;
	/** Where the dash's fire and the skirt's sparks stream from, cm above the puck's root. */
	constexpr double TrailZ = 25.0;
	constexpr double SkirtSparksZ = 8.0;

	// The pickups: icons hover over a halo on the ring, lit from inside.
	constexpr double IconZ = 95.0;
	constexpr double HaloInnerCm = 40.0;
	constexpr double HaloOuterCm = 62.0;
	constexpr double HaloZ = 2.0;
	constexpr int32 HaloSteps = 48;
	constexpr float HaloOpacity = 0.6f;
	constexpr float BubbleGlow = 2.f;
	constexpr float BubbleOpacity = 0.35f;
	constexpr double PowerLightZ = 90.0;
	constexpr float PowerLightLumens = 300.f;
	constexpr float PowerLightRadiusCm = 320.f;
	/** Each icon's glow: the core (slot 0, turbo and shield) and the rim or bolt (slot 1). */
	constexpr float IconCoreGlow = 3.f;
	constexpr float IconRimGlow = 7.f;
	constexpr float ShieldIconRimGlow = 1.2f;

	/** The icons in EPower order (Heavy, Shield, Turbo): index = EPower - 1. */
	constexpr EPower IconPowers[3] = { EPower::Heavy, EPower::Shield, EPower::Turbo };

	/** One lathed ring of a torus (centre radius R, tube radius T, at height Z), as a closed profile. */
	void Torus(FMeshBuilder& B, FName Slot, double R, double T, double Z, int32 Segments = 40)
	{
		constexpr int32 ProfileSteps = 12;
		TArray<FVector2D> Profile;
		for (int32 i = 0; i <= ProfileSteps; ++i)
		{
			const double A = -HALF_PI + 2.0 * PI * i / ProfileSteps;
			Profile.Add(FVector2D(R + T * FMath::Cos(A), Z + T * FMath::Sin(A)));
		}
		B.Lathe(Slot, Profile, Segments, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
	}

	/**
	 * The bumper puck (UE cm, facing +X): a dark skirt, a fat rubber bumper all round, a glossy painted shell with a
	 * neon trim line, a flat deck on top for the rider and two headlamps at the front. Slots: Paint, Rubber, Base,
	 * Trim (the lamps share the trim).
	 */
	UStaticMesh* BuildPuckMesh(UObject* Outer)
	{
		FMeshBuilder B;
		B.Section(TEXT("Paint"));
		B.Section(TEXT("Rubber"));
		B.Section(TEXT("Base"));
		B.Section(TEXT("Trim"));
		const double R = PuckMeshRadius;
		B.Lathe(TEXT("Base"), { { 0.0, 3.0 }, { R - 14.0, 3.0 }, { R - 9.0, 8.0 }, { R - 10.0, 18.0 }, { 0.0, 18.0 } }, 40, FVector::ZeroVector, FVector::UpVector,
			FVector::OneVector, FVector2D::ZeroVector);
		Torus(B, TEXT("Rubber"), R - 15.0, 15.0, 22.0, 48);
		B.Lathe(TEXT("Paint"), { { R - 17.0, 26.0 }, { R - 15.5, 33.0 }, { R - 19.0, 40.0 }, { R - 30.0, 45.5 }, { R - 42.0, DeckZ }, { 0.0, DeckZ } }, 48,
			FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		Torus(B, TEXT("Trim"), R - 16.2, 1.8, 34.0, 48);
		// The deck's ring and the lamps.
		Torus(B, TEXT("Base"), R - 44.0, 2.4, DeckZ, 32);
		for (const double Side : { -1.0, 1.0 })
		{
			const double A = Side * 0.38;
			B.Ellipsoid(TEXT("Trim"), FVector(FMath::Cos(A) * (R - 17.0), FMath::Sin(A) * (R - 17.0), 36.0), FVector(4.0, 7.0, 4.0), 6, 10, FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("SumoPuck"));
	}

	/** The heavy power-up's plate: an iron ring round the shell. */
	UStaticMesh* BuildWeightMesh(UObject* Outer)
	{
		FMeshBuilder W;
		Torus(W, TEXT("Iron"), PuckMeshRadius - 10.0, 9.0, 38.0, 40);
		return W.Build(Outer, TEXT("SumoWeight"));
	}

	/** A kettlebell: slot 0 the iron body and handle, slot 1 a glowing rim under it. */
	UStaticMesh* BuildHeavyIcon(UObject* Outer)
	{
		FMeshBuilder B;
		B.Ellipsoid(TEXT("Body"), FVector(0.0, 0.0, 0.0), FVector(26.0, 26.0, 24.0), 10, 16, FVector2D::ZeroVector);
		constexpr int32 HandleLinks = 10;
		for (int32 i = 0; i < HandleLinks; ++i)
		{
			const double A0 = PI * i / static_cast<double>(HandleLinks), A1 = PI * (i + 1) / static_cast<double>(HandleLinks);
			B.Limb(TEXT("Body"), FVector(FMath::Cos(A0) * 18.0, 0.0, 18.0 + FMath::Sin(A0) * 20.0), FVector(FMath::Cos(A1) * 18.0, 0.0, 18.0 + FMath::Sin(A1) * 20.0), 5.0, 5.0, 8,
				FVector2D::ZeroVector);
		}
		B.Box(TEXT("Glow"), FVector(0.0, 0.0, -8.0), FVector(27.0, 27.0, 2.0), FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("SumoIconHeavy"));
	}

	/** A bubble (slot 1) round a glowing core (slot 0). */
	UStaticMesh* BuildShieldIcon(UObject* Outer)
	{
		FMeshBuilder B;
		B.Ellipsoid(TEXT("Body"), FVector::ZeroVector, FVector(12.0, 12.0, 12.0), 8, 12, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Glow"), FVector::ZeroVector, FVector(30.0, 30.0, 30.0), 12, 20, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("SumoIconShield"));
	}

	/** A lightning bolt (slot 1; slot 0 is declared first and left empty so the bolt takes the rim's glow). */
	UStaticMesh* BuildTurboIcon(UObject* Outer)
	{
		FMeshBuilder B;
		const FVector Points[] = { { 0.0, 10.0, 34.0 }, { 0.0, -8.0, 4.0 }, { 0.0, 6.0, 2.0 }, { 0.0, -10.0, -32.0 } };
		B.Section(TEXT("Body"));
		for (int32 i = 0; i + 1 < UE_ARRAY_COUNT(Points); ++i)
		{
			B.Limb(TEXT("Glow"), Points[i], Points[i + 1], 6.0, 6.0, 8, FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("SumoIconTurbo"));
	}

	UStaticMesh* BuildHaloMesh(UObject* Outer)
	{
		FMeshBuilder H;
		const int32 Section = H.Section(TEXT("Halo"));
		AddAnnulus(H, Section, HaloInnerCm, HaloOuterCm, HaloZ, HaloSteps);
		return H.Build(Outer, TEXT("SumoHalo"));
	}

	UPointLightComponent* MakeLight(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, double Z, float Lumens, float RadiusCm)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Owner, MakeUniqueObjectName(Owner, UPointLightComponent::StaticClass(), Name));
		Light->SetupAttachment(Parent);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetRelativeLocation(FVector(0.0, 0.0, Z));
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(Lumens);
		Light->SetAttenuationRadius(RadiusCm);
		Light->SetCastShadows(false);
		return Light;
	}
}

UStaticMeshComponent* ASumoGame::MakeMesh(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Relative, bool bShadows)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("SumoMesh")));
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetRelativeTransform(Relative);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->RegisterComponent();
	return C;
}

USceneComponent* ASumoGame::MakePivot(USceneComponent* Parent, const TCHAR* Name)
{
	USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
	Pivot->SetupAttachment(Parent);
	Pivot->SetMobility(EComponentMobility::Movable);
	Pivot->RegisterComponent();
	return Pivot;
}

void ASumoGame::BuildPucks()
{
	PuckMesh = BuildPuckMesh(this);
	UStaticMesh* WeightMesh = BuildWeightMesh(this);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		BuildPuck(i, WeightMesh, Sphere);
	}
}

void ASumoGame::BuildPuck(int32 Slot, UStaticMesh* WeightMesh, UStaticMesh* Sphere)
{
	const FLinearColor Color = ASumoArena::SlotColor(Slot);
	FSumoPuckParts& P = Parts.AddDefaulted_GetRef();
	P.Root = MakePivot(RootComponent, TEXT("Puck"));
	P.Tilt = MakePivot(P.Root, TEXT("PuckTilt"));

	P.Body = MakeMesh(P.Tilt, PuckMesh, FTransform::Identity, true);
	// Paint and Trim are the puck's own: their parameters change every frame.
	P.Paint = Art::Plain(this, Color * 0.85f, 0.18, 0.15, 0.06);
	P.Trim = Art::GlowOpaque(this, Color * TrimReadyGlow);
	P.Body->SetMaterial(0, P.Paint);
	P.Body->SetMaterial(1, Materials.Plain(this, Art::Hex(TEXT("#17141f")), 0.55, 0.0));
	P.Body->SetMaterial(2, Materials.Plain(this, Art::Hex(TEXT("#2b2836")), 0.35, 0.85));
	P.Body->SetMaterial(3, P.Trim);

	P.Weight = MakeMesh(P.Tilt, WeightMesh, FTransform::Identity, true);
	P.Weight->SetMaterial(0, Materials.Plain(this, Art::Hex(TEXT("#3c3f48")), 0.35, 0.9));
	P.Weight->SetVisibility(false);

	// The shield bubble rides on the root, so it stays upright while the puck leans.
	P.ShieldMat = Sphere ? MakeAdditive(this, PowerColor(EPower::Shield) * ShieldGlow, ShieldOpacity) : nullptr;
	if (P.ShieldMat)
	{
		P.Shield = MakeMesh(P.Root, Sphere, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, ShieldCentreZ * PuckScale), ShieldScale * PuckScale), false);
		P.Shield->SetMaterial(0, P.ShieldMat);
		P.Shield->SetVisibility(false);
	}

	UArcadeAvatarComponent* Rider = NewObject<UArcadeAvatarComponent>(this, MakeUniqueObjectName(this, UArcadeAvatarComponent::StaticClass(), TEXT("Rider")));
	Rider->AvatarScale = RiderScale;
	Rider->SetupAttachment(P.Tilt);
	Rider->SetRelativeLocation(FVector(0.0, 0.0, DeckZ));
	Rider->RegisterComponent();
	Rider->SetLook(Seats[Slot].Look);
	P.Rider = Rider;

	P.Light = MakeLight(this, P.Root, TEXT("PuckLight"), PuckLightZ, PuckLightLumens, PuckLightRadiusCm);
	P.Light->SetLightColor(Color);
	P.Light->SetSourceRadius(PuckLightSourceCm);
	P.Light->RegisterComponent();

	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		P.Trail = FxSys->MakeStream(Fx::EField::Fire, P.Root, MakeUniqueObjectName(this, UNiagaraComponent::StaticClass(), TEXT("DashTrail")), FVector(0.0, 0.0, TrailZ));
		P.Sparks = FxSys->MakeStream(Fx::EField::Sparks, P.Root, MakeUniqueObjectName(this, UNiagaraComponent::StaticClass(), TEXT("DashSparks")), FVector(0.0, 0.0, SkirtSparksZ));
	}
}

void ASumoGame::BuildPowerPool()
{
	UStaticMesh* Icons[3] = { BuildHeavyIcon(this), BuildShieldIcon(this), BuildTurboIcon(this) };
	UMaterialInterface* Iron = Materials.Plain(this, Art::Hex(TEXT("#2d3038")), 0.3, 0.9);
	// The pool's looks never change, so every pickup shares them; the halo's opacity pulses, so each has its own.
	UMaterialInterface* Bubble = Materials.FindOrMake(TEXT("sumo shield bubble"),
		[this]() { return MakeAdditive(this, PowerColor(EPower::Shield) * BubbleGlow, BubbleOpacity); });
	UMaterialInterface* HaloMaster = AdditiveMaster();
	UStaticMesh* HaloMesh = HaloMaster ? BuildHaloMesh(this) : nullptr;
	// One more than the rules keep at once: a taken pickup still shrinks away while the next one pops in.
	for (int32 k = 0; k < MaxPowerUps + 1; ++k)
	{
		FSumoPowerParts& P = Powers.AddDefaulted_GetRef();
		P.Root = MakePivot(RootComponent, TEXT("Power"));
		for (int32 i = 0; i < UE_ARRAY_COUNT(IconPowers); ++i)
		{
			const FLinearColor Color = PowerColor(IconPowers[i]);
			const bool bShield = IconPowers[i] == EPower::Shield;
			UStaticMeshComponent* Icon = MakeMesh(P.Root, Icons[i], FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, IconZ)), true);
			Icon->SetMaterial(0, IconPowers[i] == EPower::Heavy ? Iron : Materials.GlowOpaque(this, Color * IconCoreGlow));
			Icon->SetMaterial(1, bShield && Bubble ? Bubble : Materials.GlowOpaque(this, Color * (bShield ? ShieldIconRimGlow : IconRimGlow)));
			Icon->SetVisibility(false);
			P.Icons[i] = Icon;
		}
		if (HaloMesh)
		{
			P.HaloMat = UMaterialInstanceDynamic::Create(HaloMaster, this);
			P.HaloMat->SetScalarParameterValue(TEXT("Opacity"), HaloOpacity);
			P.Halo = MakeMesh(P.Root, HaloMesh, FTransform::Identity, false);
			P.Halo->SetMaterial(0, P.HaloMat);
			P.Halo->SetVisibility(false);
		}
		P.Light = MakeLight(this, P.Root, TEXT("PowerLight"), PowerLightZ, PowerLightLumens, PowerLightRadiusCm);
		P.Light->SetVisibility(false);
		P.Light->RegisterComponent();
	}
}

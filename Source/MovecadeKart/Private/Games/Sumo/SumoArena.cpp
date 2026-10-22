#include "Games/Sumo/SumoArena.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Sumo/SumoRules.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* const ArenaTag = TEXT("SumoArena");
	/** Hot pink, electric cyan, amber, lime: neon that reads on the night ring and on each other. */
	const TCHAR* const SlotColors[] = { TEXT("#ff3f8e"), TEXT("#29d8ff"), TEXT("#ffb21f"), TEXT("#7cf25a") };
	const TCHAR* const TrimColors[] = { TEXT("#41e8ff"), TEXT("#b56bff") };
	constexpr int32 BandSegments[] = { 36, 32, 28, 24, 20, 16, 12, 6 };
	constexpr double MoonDir[] = { 0.82, -0.42, 0.40 };

	/** cm from rule metres. */
	FVector Cm(double X, double Y, double Z) { return FVector(X, Y, Z) * 100.0; }

	/** How deep the ring's underside is at a radius (m): thin at the rim, a keel toward the middle. */
	double Depth(double R)
	{
		return 0.32 + (MovecadeKart::Sumo::ArenaRadius - R) * 0.3;
	}

	double Smooth01(double X)
	{
		const double T = FMath::Clamp(X, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
}

ASumoArena::ASumoArena()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	Sphere = SphereMesh.Object;
	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(RootComponent);
	Moon->SetMobility(EComponentMobility::Movable);
	Rim = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Rim"));
	Rim->SetupAttachment(RootComponent);
	Rim->SetMobility(EComponentMobility::Movable);
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(RootComponent);
	SkyDome->SetStaticMesh(SphereMesh.Object);
	SkyDome->SetRelativeScale3D(FVector(20000.0));
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDome->SetCastShadow(false);
	SkyDome->SetMobility(EComponentMobility::Movable);
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetMobility(EComponentMobility::Movable);
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
}

FLinearColor ASumoArena::SlotColor(int32 Slot)
{
	return MovecadeKart::Art::Hex(SlotColors[((Slot % 4) + 4) % 4]);
}

void ASumoArena::BeginPlay()
{
	Super::BeginPlay();
	// A level loads the editor's build without its bookkeeping (bands, lanterns): rebuild then. A fresh spawn has it.
	if (Bands.Num() == 0)
	{
		Build();
	}
}

void ASumoArena::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetActorTransform(FTransform::Identity);
	Build();
}

void ASumoArena::SetPlayers(int32 Count)
{
	Players = FMath::Clamp(Count, 1, 4);
	for (int32 i = 0; i < TasselMats.Num(); ++i)
	{
		const bool bOn = i < Players;
		if (TasselMats[i])
		{
			TasselMats[i]->SetVectorParameterValue(TEXT("Color"), bOn ? SlotColor(i) * 6.f : MovecadeKart::Art::Hex(TEXT("#3a3550")) * 0.6f);
		}
		if (TasselLights.IsValidIndex(i) && TasselLights[i])
		{
			TasselLights[i]->SetVisibility(bOn);
		}
	}
}

void ASumoArena::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), ArenaTag);
	UE_LOG(LogTemp, Verbose, TEXT("SumoArena: clearing %d components (%d ISMs total on the actor)"), Old.Num(), GetComponents().Num());
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	Groups.Reset();
	BandMeshes.Reset();
	BandTops.Reset();
	BandTrims.Reset();
	BandWarnings.Reset();
	BandWarningMats.Reset();
	CoreDecor.Reset();
	TasselMats.Reset();
	TasselLights.Reset();
	Floaters.Reset();
	FloaterRest.Reset();
	RimLights.Reset();
	LanternRest.Reset();
	Lanterns = nullptr;
	Bands.Reset();
}

void ASumoArena::Build()
{
	Clear();
	BuildLook();
	BuildSky();
	BuildRing();
	BuildKeel();
	BuildSea();
	BuildProps();
	SetPlayers(Players);
}

UStaticMeshComponent* ASumoArena::Mesh(UStaticMesh* InMesh, UMaterialInterface* Material, const FTransform& Transform, bool bShadows, USceneComponent* Parent)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("ArenaMesh")));
	C->SetStaticMesh(InMesh);
	if (Material)
	{
		for (int32 i = 0; i < FMath::Max(1, InMesh ? InMesh->GetStaticMaterials().Num() : 1); ++i)
		{
			C->SetMaterial(i, Material);
		}
	}
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetRelativeTransform(Transform);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(ArenaTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

UInstancedStaticMeshComponent* ASumoArena::Group(UStaticMesh* InMesh, UMaterialInterface* Material, bool bShadows)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d"), *GetNameSafe(InMesh), *GetNameSafe(Material), bShadows ? 1 : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Groups.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("ArenaGroup")));
	C->SetStaticMesh(InMesh);
	C->SetMaterial(0, Material);
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(ArenaTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	Groups.Add(Key, C);
	return C;
}

UPointLightComponent* ASumoArena::Light(const FVector& At, const FLinearColor& Color, float Lumens, float RadiusCm)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("ArenaLight")));
	L->SetupAttachment(RootComponent);
	L->SetMobility(EComponentMobility::Movable);
	L->SetRelativeLocation(At);
	L->SetIntensityUnits(ELightUnits::Lumens);
	L->SetIntensity(Lumens);
	L->SetAttenuationRadius(RadiusCm);
	L->SetLightColor(Color);
	L->SetSourceRadius(20.f);
	L->SetCastShadows(false);
	L->SetVolumetricScatteringIntensity(0.f);
	L->ComponentTags.Add(ArenaTag);
	AddInstanceComponent(L);
	L->RegisterComponent();
	return L;
}

void ASumoArena::BuildLook()
{
	using namespace MovecadeKart::Art;
	const FVector ToMoon = FVector(MoonDir[0], MoonDir[1], MoonDir[2]).GetSafeNormal();
	// The moon low over the far sea behind the ring: a cool key with soft shadows, its glitter path on the water.
	Moon->SetWorldRotation((-ToMoon).Rotation());
	Moon->SetLightColor(Hex(TEXT("#a9c2ff")));
	Moon->SetIntensity(1.1f);
	Moon->SetCastShadows(true);
	Moon->SetAtmosphereSunLight(false);
	Moon->SetForwardShadingPriority(1);
	Moon->SetLightSourceAngle(1.2f);
	// A magenta neon wash from the front right: the city of lanterns behind the camera.
	Rim->SetWorldRotation((-FVector(-0.6, 0.75, 0.35).GetSafeNormal()).Rotation());
	Rim->SetLightColor(Hex(TEXT("#ff6ac1")));
	Rim->SetIntensity(0.3f);
	Rim->SetCastShadows(false);
	Rim->SetAtmosphereSunLight(false);
	Rim->SetForwardShadingPriority(0);
	SkyLight->SetIntensity(0.22f);
	SkyLight->SetLightColor(Hex(TEXT("#8fa4ff")));
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(TEXT("#1a1838")));
	const FLinearColor Horizon = Hex(TEXT("#7a3f86"));
	Fog->SetStartDistance(3500.f);
	Fog->SetFogDensity(0.012f);
	Fog->SetFogHeightFalloff(0.03f);
	Fog->SetFogMaxOpacity(0.6f);
	Fog->SetFogCutoffDistance(250000.f);
	Fog->SetFogInscatteringColor(Horizon * 0.12f);
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#9fb5ff")) * 0.04f);
	Fog->SetDirectionalInscatteringExponent(10.f);
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 1.15f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.5f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.06f, 1.06f, 1.06f, 1.f);
	S.bOverride_ColorOffset = true;
	S.ColorOffset = FVector4(0.004f, 0.0f, 0.012f, 0.f);
}

void ASumoArena::BuildSky()
{
	using namespace MovecadeKart::Art;
	const FLinearColor Top = Hex(TEXT("#03040f")), Mid = Hex(TEXT("#0c0b26")), Horizon = Hex(TEXT("#3b2152"));
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
		Sky->SetVectorParameterValue(TEXT("SkyMid"), Mid);
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#07061a")));
		const FVector ToMoon = FVector(MoonDir[0], MoonDir[1], MoonDir[2]).GetSafeNormal();
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(ToMoon.X, ToMoon.Y, ToMoon.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#c9d6ff")));
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.045f));
		Sky->SetScalarParameterValue(TEXT("SunDisc"), 14.0f);
		Sky->SetScalarParameterValue(TEXT("SunGlow"), 0.06f);
		Sky->SetScalarParameterValue(TEXT("GlowPower"), 26.0f);
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), 0.08f);
		Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0f);
		if (UTexture* Clouds = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
		}
		Sky->SetScalarParameterValue(TEXT("Clouds"), 0.3f);
		Sky->SetScalarParameterValue(TEXT("CloudScale"), 0.3f);
		Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(TEXT("#8c86c9")));
		Sky->SetVectorParameterValue(TEXT("CloudShade"), Hex(TEXT("#1d1a3e")));
		SkyDome->SetMaterial(0, Sky);
	}
	SkyLight->RecaptureSky();
	// Stars on a dome far out (inside the sky sphere), brighter and denser overhead.
	FRandomStream R(0x57a2);
	UInstancedStaticMeshComponent* Stars = Group(Sphere, Glow(this, Hex(TEXT("#dfe6ff")) * 5.f), false);
	for (int32 i = 0; i < 420; ++i)
	{
		const double Az = R.FRandRange(0.0, 2.0 * PI);
		const double El = FMath::Asin(R.FRandRange(0.05f, 1.f));
		const FVector Dir(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
		const double Size = R.FRandRange(4.f, 10.f) * (R.FRand() < 0.08f ? 2.2f : 1.f);
		Stars->AddInstance(FTransform(FRotator::ZeroRotator, Dir * 300000.0, FVector(Size)), false);
	}
	// The moon's halo: a soft additive disc on the sky behind the disc itself.
	const FVector ToMoon = FVector(MoonDir[0], MoonDir[1], MoonDir[2]).GetSafeNormal();
	if (UMaterialInterface* Additive = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackAdditive.M_TrackAdditive"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Soft = UMaterialInstanceDynamic::Create(Additive, this);
		Soft->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("#8b9cff")) * 0.6f);
		Soft->SetScalarParameterValue(TEXT("Opacity"), 0.18f);
		for (const double Scale : { 1.0, 1.8 })
		{
			Mesh(Sphere, Soft, FTransform(FRotator::ZeroRotator, ToMoon * 290000.0, FVector(160.0 * Scale)), false);
		}
	}
}

UStaticMesh* ASumoArena::SectorMesh(double Inner, double Outer, double Span, const TCHAR* Name)
{
	using MovecadeKart::FMeshBuilder;
	FMeshBuilder B;
	const int32 Top = B.Section(TEXT("Top"));
	const int32 Side = B.Section(TEXT("Side"));
	const int32 Trim = B.Section(TEXT("Trim"));
	const bool bCore = Inner < 1e-3;
	const double Gap = 0.025;
	const double R0 = bCore ? 0.0 : Inner + Gap;
	const double R1 = Outer - Gap;
	const double Mid = (Inner + Outer) / 2.0;
	const double Half = Span / 2.0 - (bCore ? 0.0 : Gap / Mid);
	const int32 Steps = FMath::Max(3, FMath::CeilToInt32(Span * Outer / 0.25));
	const FVector Origin = bCore ? FVector::ZeroVector : Cm(Mid, 0.0, 0.0);
	auto P = [&Origin](double R, double A, double Z) { return Cm(R * FMath::Cos(A), R * FMath::Sin(A), Z) - Origin; };
	auto Radial = [](double A) { return FVector(FMath::Cos(A), FMath::Sin(A), 0.0); };
	const FVector2D UV1 = FVector2D::ZeroVector;
	const double Slope = 0.3;
	for (int32 i = 0; i < Steps; ++i)
	{
		const double A0 = -Half + 2.0 * Half * i / Steps;
		const double A1 = -Half + 2.0 * Half * (i + 1) / Steps;
		// The plank top.
		{
			const int32 a = B.Vert(Top, P(R0, A0, 0.0), FVector::UpVector, FVector2D(R0, A0 * Mid), UV1);
			const int32 b = B.Vert(Top, P(R1, A0, 0.0), FVector::UpVector, FVector2D(R1, A0 * Mid), UV1);
			const int32 c = B.Vert(Top, P(R1, A1, 0.0), FVector::UpVector, FVector2D(R1, A1 * Mid), UV1);
			const int32 d = B.Vert(Top, P(R0, A1, 0.0), FVector::UpVector, FVector2D(R0, A1 * Mid), UV1);
			B.Tri(Top, a, b, c);
			B.Tri(Top, a, c, d);
		}
		// The underside slopes down toward the middle (an inverted cone); its normal faces out and down.
		{
			const FVector N0 = (Radial(A0) * Slope - FVector::UpVector).GetSafeNormal();
			const FVector N1 = (Radial(A1) * Slope - FVector::UpVector).GetSafeNormal();
			const int32 a = B.Vert(Side, P(R0, A0, -Depth(R0)), N0, FVector2D::ZeroVector, UV1);
			const int32 b = B.Vert(Side, P(R1, A0, -Depth(R1)), N0, FVector2D::ZeroVector, UV1);
			const int32 c = B.Vert(Side, P(R1, A1, -Depth(R1)), N1, FVector2D::ZeroVector, UV1);
			const int32 d = B.Vert(Side, P(R0, A1, -Depth(R0)), N1, FVector2D::ZeroVector, UV1);
			B.Tri(Side, a, b, c);
			B.Tri(Side, a, c, d);
		}
		// The outer face, and the inner one for a band.
		{
			const int32 a = B.Vert(Side, P(R1, A0, 0.0), Radial(A0), FVector2D::ZeroVector, UV1);
			const int32 b = B.Vert(Side, P(R1, A0, -Depth(R1)), Radial(A0), FVector2D::ZeroVector, UV1);
			const int32 c = B.Vert(Side, P(R1, A1, -Depth(R1)), Radial(A1), FVector2D::ZeroVector, UV1);
			const int32 d = B.Vert(Side, P(R1, A1, 0.0), Radial(A1), FVector2D::ZeroVector, UV1);
			B.Tri(Side, a, b, c);
			B.Tri(Side, a, c, d);
		}
		if (!bCore)
		{
			const int32 a = B.Vert(Side, P(R0, A0, 0.0), -Radial(A0), FVector2D::ZeroVector, UV1);
			const int32 b = B.Vert(Side, P(R0, A0, -Depth(R0)), -Radial(A0), FVector2D::ZeroVector, UV1);
			const int32 c = B.Vert(Side, P(R0, A1, -Depth(R0)), -Radial(A1), FVector2D::ZeroVector, UV1);
			const int32 d = B.Vert(Side, P(R0, A1, 0.0), -Radial(A1), FVector2D::ZeroVector, UV1);
			B.Tri(Side, a, b, c);
			B.Tri(Side, a, c, d);
		}
		// The neon edge: a strip standing proud on the top's outer edge and a line round the outer face.
		{
			const double In = R1 - 0.07, Out = R1 - 0.01;
			const int32 a = B.Vert(Trim, P(In, A0, 0.012), FVector::UpVector, FVector2D::ZeroVector, UV1);
			const int32 b = B.Vert(Trim, P(Out, A0, 0.012), FVector::UpVector, FVector2D::ZeroVector, UV1);
			const int32 c = B.Vert(Trim, P(Out, A1, 0.012), FVector::UpVector, FVector2D::ZeroVector, UV1);
			const int32 d = B.Vert(Trim, P(In, A1, 0.012), FVector::UpVector, FVector2D::ZeroVector, UV1);
			B.Tri(Trim, a, b, c);
			B.Tri(Trim, a, c, d);
			const double Rf = R1 + 0.006;
			const int32 e = B.Vert(Trim, P(Rf, A0, -0.03), Radial(A0), FVector2D::ZeroVector, UV1);
			const int32 f = B.Vert(Trim, P(Rf, A0, -0.09), Radial(A0), FVector2D::ZeroVector, UV1);
			const int32 g = B.Vert(Trim, P(Rf, A1, -0.09), Radial(A1), FVector2D::ZeroVector, UV1);
			const int32 h = B.Vert(Trim, P(Rf, A1, -0.03), Radial(A1), FVector2D::ZeroVector, UV1);
			B.Tri(Trim, e, f, g);
			B.Tri(Trim, e, g, h);
		}
	}
	// The ends of a segment (the cracks between them).
	if (!bCore)
	{
		for (const double A : { -Half, Half })
		{
			const FVector N = FVector(-FMath::Sin(A), FMath::Cos(A), 0.0) * (A > 0.0 ? 1.0 : -1.0);
			const int32 a = B.Vert(Side, P(R0, A, 0.0), N, FVector2D::ZeroVector, UV1);
			const int32 b = B.Vert(Side, P(R1, A, 0.0), N, FVector2D::ZeroVector, UV1);
			const int32 c = B.Vert(Side, P(R1, A, -Depth(R1)), N, FVector2D::ZeroVector, UV1);
			const int32 d = B.Vert(Side, P(R0, A, -Depth(R0)), N, FVector2D::ZeroVector, UV1);
			B.Tri(Side, a, b, c);
			B.Tri(Side, a, c, d);
		}
	}
	(void)Side;
	(void)Trim;
	return B.Build(this, Name);
}

void ASumoArena::BuildRing()
{
	using namespace MovecadeKart::Art;
	using namespace MovecadeKart::Sumo;
	UMaterialInterface* Additive = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackAdditive.M_TrackAdditive"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Rock = Surface(this, { Hex(TEXT("#3a3150")), TEXT("rock_face"), 1.6, 0.55, 0.8, 0.5, 0.85, 0.0, 0.0, true, 0.5 });
	for (int32 b = 0; b < BandCount; ++b)
	{
		FBand& Band = Bands.AddDefaulted_GetRef();
		const bool bCore = b == BandCount - 1;
		Band.Outer = ArenaRadius - b * BandWidth;
		Band.Inner = bCore ? 0.0 : Band.Outer - BandWidth;
		Band.Segments = BandSegments[b];
		const double Span = 2.0 * PI / Band.Segments;
		UStaticMesh* Sector = SectorMesh(Band.Inner, Band.Outer, Span, *FString::Printf(TEXT("SumoBand%d"), b));
		// Lacquered planks in deep indigo, every other band a shade warmer; the clay core is paler.
		const FLinearColor Lacquer = bCore ? Hex(TEXT("#6a2c27")) : Hex(b % 2 ? TEXT("#1d1838") : TEXT("#241c44"));
		UMaterialInstanceDynamic* TopMat = Surface(this, { Lacquer, bCore ? FName(TEXT("gravelly_sand")) : FName(TEXT("dark_wooden_planks")), bCore ? 1.2 : 1.6, 0.45, 0.6, 0.4,
			bCore ? 0.85 : 0.5, 0.0, 0.0, false, 0.6 });
		UMaterialInstanceDynamic* TrimMat = Glow(this, Hex(bCore ? TEXT("#ff4fa8") : TrimColors[b % 2]) * (b == 0 ? 5.f : 3.2f));
		BandTops.Add(TopMat);
		BandTrims.Add(TrimMat);
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("Band")));
		Ism->SetStaticMesh(Sector);
		Ism->SetMaterial(0, TopMat);
		Ism->SetMaterial(1, Rock);
		Ism->SetMaterial(2, TrimMat);
		Ism->SetupAttachment(RootComponent);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCastShadow(true);
		Ism->ComponentTags.Add(ArenaTag);
		AddInstanceComponent(Ism);
		Ism->RegisterComponent();
		BandMeshes.Add(Ism);
		FRandomStream R(0x5100 + b);
		const double WaveStart = R.FRand() * Band.Segments;
		const double Mid = (Band.Inner + Band.Outer) / 2.0;
		for (int32 s = 0; s < Band.Segments; ++s)
		{
			const double A = (s + 0.5) * Span;
			const FTransform T(FRotator(0.0, FMath::RadiansToDegrees(A), 0.0), bCore ? FVector::ZeroVector : Cm(Mid * FMath::Cos(A), Mid * FMath::Sin(A), 0.0));
			Band.Rest.Add(T);
			Ism->AddInstance(T, false);
			FSegmentFall& F = Band.Falls.AddDefaulted_GetRef();
			// The band breaks up as a wave round the ring from a random start, each piece tumbling its own way.
			F.Delay = 0.55 * FMath::Fmod(s + WaveStart, static_cast<double>(Band.Segments)) / Band.Segments + R.FRandRange(0.f, 0.12f);
			F.Axis = FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-0.3f, 0.3f)).GetSafeNormal();
			F.Spin = R.FRandRange(90.f, 260.f) * (R.FRand() < 0.5f ? -1.f : 1.f);
			F.Drift = R.FRandRange(0.4f, 1.6f);
		}
		// The warning wash: an additive annulus laid on the band.
		if (Additive)
		{
			MovecadeKart::FMeshBuilder W;
			const int32 S = W.Section(TEXT("Warn"));
			const int32 Steps = 96;
			for (int32 i = 0; i < Steps; ++i)
			{
				const double A0 = 2.0 * PI * i / Steps, A1 = 2.0 * PI * (i + 1) / Steps;
				const double Ri = FMath::Max(0.0, Band.Inner + 0.03), Ro = Band.Outer - 0.03;
				const int32 a = W.Vert(S, Cm(Ri * FMath::Cos(A0), Ri * FMath::Sin(A0), 0.02), FVector::UpVector, FVector2D(0, 0), FVector2D::ZeroVector);
				const int32 bb = W.Vert(S, Cm(Ro * FMath::Cos(A0), Ro * FMath::Sin(A0), 0.02), FVector::UpVector, FVector2D(1, 0), FVector2D::ZeroVector);
				const int32 c = W.Vert(S, Cm(Ro * FMath::Cos(A1), Ro * FMath::Sin(A1), 0.02), FVector::UpVector, FVector2D(1, 1), FVector2D::ZeroVector);
				const int32 d = W.Vert(S, Cm(Ri * FMath::Cos(A1), Ri * FMath::Sin(A1), 0.02), FVector::UpVector, FVector2D(0, 1), FVector2D::ZeroVector);
				W.Tri(S, a, bb, c);
				W.Tri(S, a, c, d);
			}
			UMaterialInstanceDynamic* WarnMat = UMaterialInstanceDynamic::Create(Additive, this);
			WarnMat->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("#ff2a3a")) * 3.f);
			WarnMat->SetScalarParameterValue(TEXT("Opacity"), 0.f);
			UStaticMeshComponent* Wash = Mesh(W.Build(this, *FString::Printf(TEXT("SumoWarn%d"), b)), WarnMat, FTransform::Identity, false);
			Wash->SetVisibility(false);
			BandWarnings.Add(Wash);
			BandWarningMats.Add(WarnMat);
		}
	}
	// The core's markings: the straw-rope circle (a glowing tawara) and the two white start lines.
	UMaterialInterface* Chalk = Plain(this, Hex(TEXT("#f4efe6")), 0.7, 0.0, 0.35);
	for (const double Y : { -0.55, 0.55 })
	{
		CoreDecor.Add(Mesh(Cube, Chalk, FTransform(FRotator(0.0, 90.0, 0.0), Cm(0.0, Y, 0.012), FVector(0.07, 0.8, 0.012)), false));
	}
	{
		MovecadeKart::FMeshBuilder E;
		const int32 Steps = 64;
		E.Section(TEXT("Rope"));
		for (int32 i = 0; i < Steps; ++i)
		{
			// The rope ring as a chain of short limbs (a torus).
			const double A0 = 2.0 * PI * i / Steps, A1 = 2.0 * PI * (i + 1) / Steps;
			const double R = CoreRadius - 0.12;
			E.Limb(TEXT("Rope"), Cm(R * FMath::Cos(A0), R * FMath::Sin(A0), 0.035), Cm(R * FMath::Cos(A1), R * FMath::Sin(A1), 0.035), 6.0, 6.0, 8, FVector2D::ZeroVector);
		}
		UMaterialInterface* Straw = Plain(this, Hex(TEXT("#d9b56a")), 0.75, 0.0, 0.25);
		CoreDecor.Add(Mesh(E.Build(this, TEXT("SumoRope")), Straw, FTransform::Identity, true));
	}
}

void ASumoArena::BuildKeel()
{
	using namespace MovecadeKart::Art;
	using namespace MovecadeKart::Sumo;
	// Under the core: the rock keel tapering to a point, crystals glowing out of it.
	MovecadeKart::FMeshBuilder K;
	TArray<FVector2D> Profile;
	const double TopZ = -Depth(CoreRadius) * 100.0 + 25.0;
	const double Pts[][2] = { { 0.0, -6.6 }, { 0.35, -6.2 }, { 0.8, -5.5 }, { 1.25, -4.7 }, { 1.65, -3.9 }, { 1.95, -3.1 }, { 2.05, -2.7 } };
	for (const auto& Pt : Pts)
	{
		Profile.Add(FVector2D(Pt[0] * 100.0, Pt[1] * 100.0));
	}
	Profile.Add(FVector2D(2.0 * 100.0, TopZ));
	Profile.Add(FVector2D(0.0, TopZ));
	K.Lathe(TEXT("Rock"), Profile, 20, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
	UMaterialInterface* Rock = Surface(this, { Hex(TEXT("#2f2846")), TEXT("rock_face"), 2.0, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 });
	Mesh(K.Build(this, TEXT("SumoKeel")), Rock, FTransform::Identity, true);
	FRandomStream R(0xc4a1);
	UInstancedStaticMeshComponent* Crystals = Group(Sphere, Glow(this, Hex(TEXT("#5ee9ff")) * 7.f), false);
	UInstancedStaticMeshComponent* Pinks = Group(Cube, Glow(this, Hex(TEXT("#ff58c8")) * 6.f), false);
	for (int32 i = 0; i < 26; ++i)
	{
		const double A = R.FRandRange(0.0, 2.0 * PI);
		const double Z = R.FRandRange(-5.8f, -2.6f);
		const double Rad = FMath::Lerp(0.3, 2.05, FMath::Clamp((Z + 6.6) / 3.9, 0.0, 1.0)) - 0.05;
		const FVector At = Cm(Rad * FMath::Cos(A), Rad * FMath::Sin(A), Z);
		const FRotator Rot(R.FRandRange(-40.f, 40.f), FMath::RadiansToDegrees(A), R.FRandRange(-30.f, 30.f));
		const FVector Size(R.FRandRange(0.12f, 0.3f), R.FRandRange(0.08f, 0.16f), R.FRandRange(0.35f, 0.8f));
		(i % 3 ? Crystals : Pinks)->AddInstance(FTransform(Rot, At, Size), false);
	}
	// Crystal glints on the band undersides near the rim, and the keel's own glow on the water below.
	for (int32 i = 0; i < 30; ++i)
	{
		const double A = R.FRandRange(0.0, 2.0 * PI);
		const double Rad = R.FRandRange(3.0f, 8.6f);
		const FVector At = Cm(Rad * FMath::Cos(A), Rad * FMath::Sin(A), -Depth(Rad) - 0.05);
		Crystals->AddInstance(FTransform(FRotator(R.FRandRange(150.f, 210.f), 0.0, 0.0), At, FVector(0.08, 0.08, R.FRandRange(0.2f, 0.45f))), false);
	}
	Light(Cm(0.0, 0.0, -6.0), Hex(TEXT("#55dfff")), 400.f, 1400.f);
}

void ASumoArena::BuildSea()
{
	using namespace MovecadeKart::Art;
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* Water = nullptr;
	if (UMaterialInterface* WaterMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackWater.M_TrackWater"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* W = UMaterialInstanceDynamic::Create(WaterMaster, this);
		W->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("#05060f")));
		W->SetScalarParameterValue(TEXT("Roughness"), 0.06f);
		W->SetVectorParameterValue(TEXT("Scattering"), FLinearColor(0.004f, 0.006f, 0.022f, 1.f));
		W->SetVectorParameterValue(TEXT("Absorption"), FLinearColor(0.5f, 0.2f, 0.1f, 1.f));
		W->SetScalarParameterValue(TEXT("WaveStrength"), 0.42f);
		W->SetScalarParameterValue(TEXT("WaveTileA"), 7.0f);
		W->SetScalarParameterValue(TEXT("WaveTileB"), 19.0f);
		Water = W;
	}
	if (!Water)
	{
		Water = Plain(this, Hex(TEXT("#070a1c")), 0.08, 0.0);
	}
	if (Plane)
	{
		Mesh(Plane, Water, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, WaterZ), FVector(4000.0, 4000.0, 1.0)), false);
	}
	// A dark seabed far below, so the water has depth to tint.
	Mesh(Cube, Plain(this, Hex(TEXT("#03040a")), 1.0), FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, WaterZ - 3000.0), FVector(4000.0, 4000.0, 1.0)), false);
}

void ASumoArena::BuildProps()
{
	using namespace MovecadeKart::Art;
	using namespace MovecadeKart::Sumo;
	const double Sea = WaterZ / 100.0;
	FRandomStream R(0x7041);
	// The torii in the water behind the ring, off to the right, vermilion with a neon line under its lintel.
	{
		UMaterialInterface* Vermilion = Plain(this, Hex(TEXT("#c23322")), 0.45, 0.0, 0.04);
		UMaterialInterface* Black = Plain(this, Hex(TEXT("#121018")), 0.4, 0.2);
		UMaterialInterface* Neon = Glow(this, Hex(TEXT("#41e8ff")) * 8.f);
		const FVector2D At(27.0, 7.0);
		const double W = 13.0, Hgt = 16.0;
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector Base = Cm(At.X, At.Y + Side * W * 0.36, Sea);
			Mesh(Cylinder, Vermilion, FTransform(FRotator::ZeroRotator, Base + FVector(0.0, 0.0, Hgt * 50.0), FVector(1.1, 1.1, Hgt)), true);
			Mesh(Cylinder, Black, FTransform(FRotator::ZeroRotator, Base + FVector(0.0, 0.0, 60.0), FVector(1.35, 1.35, 1.2)), true);
		}
		const double Top = Sea + Hgt;
		Mesh(Cube, Black, FTransform(FRotator::ZeroRotator, Cm(At.X, At.Y, Top + 0.55), FVector(1.5, W + 2.4, 0.45)), true);
		for (const double Side : { -1.0, 1.0 })
		{
			// The kasagi's upswept ends.
			Mesh(Cube, Black, FTransform(FRotator(0.0, 0.0, Side * 7.0), Cm(At.X, At.Y + Side * (W * 0.5 + 0.9), Top + 0.72), FVector(1.5, 3.2, 0.45)), true);
		}
		Mesh(Cube, Vermilion, FTransform(FRotator::ZeroRotator, Cm(At.X, At.Y, Top), FVector(1.25, W + 1.4, 0.7)), true);
		Mesh(Cube, Vermilion, FTransform(FRotator::ZeroRotator, Cm(At.X, At.Y, Top - 2.4), FVector(0.9, W * 0.92, 0.55)), true);
		Mesh(Cube, Vermilion, FTransform(FRotator::ZeroRotator, Cm(At.X, At.Y, Top - 1.2), FVector(0.6, 0.6, 1.8)), true);
		Mesh(Cube, Neon, FTransform(FRotator::ZeroRotator, Cm(At.X - 0.66, At.Y, Top - 0.38), FVector(0.03, W + 1.3, 0.06)), false);
		Mesh(Cube, Neon, FTransform(FRotator::ZeroRotator, Cm(At.X - 0.47, At.Y, Top - 2.71), FVector(0.03, W * 0.9, 0.05)), false);
		Light(Cm(At.X - 3.0, At.Y, Top - 4.0), Hex(TEXT("#ff8a5a")), 1800.f, 2200.f);
	}
	// Paper lanterns adrift on the swell: warm, scattered round the ring, thicker toward the horizon.
	{
		UInstancedStaticMeshComponent* Paper = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("Lanterns")));
		Paper->SetStaticMesh(Sphere);
		Paper->SetMaterial(0, Glow(this, Hex(TEXT("#ffb263")) * 7.f));
		Paper->SetupAttachment(RootComponent);
		Paper->SetMobility(EComponentMobility::Movable);
		Paper->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Paper->SetCastShadow(false);
		Paper->ComponentTags.Add(ArenaTag);
		AddInstanceComponent(Paper);
		Paper->RegisterComponent();
		Lanterns = Paper;
		for (int32 i = 0; i < 70; ++i)
		{
			const double A = R.FRandRange(-1.3f, 1.3f) + (R.FRand() < 0.25f ? PI : 0.0);
			const double D = 14.0 + FMath::Pow(R.FRand(), 0.7f) * 70.0;
			const FVector At = Cm(D * FMath::Cos(A), D * FMath::Sin(A), Sea + 0.15);
			LanternRest.Add(At);
			Paper->AddInstance(FTransform(FRotator::ZeroRotator, At, FVector(0.42, 0.42, 0.55)), false);
		}
		const FVector2D Lit[] = { { 15.0, -6.0 }, { 18.0, 9.0 }, { -2.0, 15.0 }, { 4.0, -16.0 }, { 30.0, -2.0 } };
		for (const FVector2D& L : Lit)
		{
			Light(Cm(L.X, L.Y, Sea + 1.2), Hex(TEXT("#ff9a4a")), 500.f, 1200.f);
		}
	}
	// Four tasselled posts on floating rocks at the ring's corners (the dohyo's four colours, here the players').
	{
		UMaterialInterface* Rock = Surface(this, { Hex(TEXT("#3a3150")), TEXT("rock_face"), 1.4, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 });
		UMaterialInterface* Lacquer = Plain(this, Hex(TEXT("#16121f")), 0.32, 0.3);
		UMaterialInterface* Gold = Plain(this, Hex(TEXT("#e0b25a")), 0.3, 1.0);
		const double Angles[] = { -0.75 * PI, 0.75 * PI, 0.25 * PI, -0.25 * PI };
		for (int32 i = 0; i < 4; ++i)
		{
			const double A = Angles[i];
			const FVector Base = Cm(11.4 * FMath::Cos(A), 11.4 * FMath::Sin(A), -0.6);
			USceneComponent* Float = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("Floater")));
			Float->SetupAttachment(RootComponent);
			Float->SetMobility(EComponentMobility::Movable);
			Float->SetRelativeLocation(Base);
			Float->ComponentTags.Add(ArenaTag);
			AddInstanceComponent(Float);
			Float->RegisterComponent();
			Floaters.Add(Float);
			FloaterRest.Add(Base);
			MovecadeKart::FMeshBuilder K;
			TArray<FVector2D> Profile = { { 0.0, -260.0 }, { 40.0, -230.0 }, { 95.0, -150.0 }, { 135.0, -60.0 }, { 150.0, -5.0 }, { 140.0, 10.0 }, { 0.0, 12.0 } };
			K.Lathe(TEXT("Rock"), Profile, 10, FVector::ZeroVector, FVector::UpVector, FVector(1.0, 0.85, 1.0), FVector2D::ZeroVector);
			Mesh(K.Build(this, TEXT("SumoPostRock")), Rock, FTransform(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector::ZeroVector), true, Float);
			Mesh(Cylinder, Lacquer, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 190.0), FVector(0.34, 0.34, 3.6)), true, Float);
			Mesh(Cylinder, Gold, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 30.0), FVector(0.48, 0.48, 0.2)), true, Float);
			Mesh(Cylinder, Gold, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 372.0), FVector(0.5, 0.5, 0.1)), true, Float);
			UMaterialInstanceDynamic* Tassel = Glow(this, SlotColor(i) * 6.f);
			TasselMats.Add(Tassel);
			// The lantern on top and the tassel hanging from a cord.
			Mesh(Cylinder, Tassel, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 405.0), FVector(0.42, 0.42, 0.55)), false, Float);
			Mesh(Cube, Lacquer, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 437.0), FVector(0.6, 0.6, 0.08)), true, Float);
			const FVector Out = FVector(FMath::Cos(A), FMath::Sin(A), 0.0);
			Mesh(Cylinder, Lacquer, FTransform(FRotator::ZeroRotator, -Out * 30.0 + FVector(0.0, 0.0, 330.0), FVector(0.03, 0.03, 0.7)), false, Float);
			MovecadeKart::FMeshBuilder T;
			T.Lathe(TEXT("Tassel"), { { 0.0, -48.0 }, { 14.0, -46.0 }, { 11.0, -20.0 }, { 5.0, 0.0 }, { 0.0, 2.0 } }, 10, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
			Mesh(T.Build(this, TEXT("SumoTassel")), Tassel, FTransform(FRotator::ZeroRotator, -Out * 30.0 + FVector(0.0, 0.0, 296.0)), false, Float);
			UPointLightComponent* L = Light(FVector::ZeroVector, SlotColor(i), 260.f, 800.f);
			L->AttachToComponent(Float, FAttachmentTransformRules::KeepRelativeTransform);
			L->SetRelativeLocation(FVector(0.0, 0.0, 400.0));
			TasselLights.Add(L);
		}
	}
	// Small rocks drifting round under the ring, each with a crystal.
	{
		UMaterialInterface* Rock = Surface(this, { Hex(TEXT("#2f2846")), TEXT("rock_face"), 1.2, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 });
		UMaterialInterface* Crystal = Glow(this, Hex(TEXT("#5ee9ff")) * 6.f);
		for (int32 i = 0; i < 6; ++i)
		{
			const double A = (i + 0.3) * 2.0 * PI / 6.0;
			const double D = R.FRandRange(12.5f, 15.5f);
			const FVector Base = Cm(D * FMath::Cos(A), D * FMath::Sin(A), R.FRandRange(-4.5f, -2.0f));
			USceneComponent* Float = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("Floater")));
			Float->SetupAttachment(RootComponent);
			Float->SetMobility(EComponentMobility::Movable);
			Float->SetRelativeLocation(Base);
			Float->ComponentTags.Add(ArenaTag);
			AddInstanceComponent(Float);
			Float->RegisterComponent();
			Floaters.Add(Float);
			FloaterRest.Add(Base);
			const double S = R.FRandRange(0.5f, 1.1f);
			MovecadeKart::FMeshBuilder K;
			K.Lathe(TEXT("Rock"), { { 0.0, -150.0 }, { 60.0, -110.0 }, { 95.0, -40.0 }, { 90.0, 10.0 }, { 0.0, 25.0 } }, 9, FVector::ZeroVector, FVector::UpVector, FVector(1.0, 0.8, 1.0) * S, FVector2D::ZeroVector);
			Mesh(K.Build(this, TEXT("SumoDrift")), Rock, FTransform(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector::ZeroVector), true, Float);
			Mesh(Cube, Crystal, FTransform(FRotator(R.FRandRange(-25.f, 25.f), 30.0, R.FRandRange(-25.f, 25.f)), FVector(0.0, 0.0, 30.0 * S), FVector(0.18, 0.18, 0.55) * S), false, Float);
		}
	}
	// Islands on the horizon: dark hills, one with a pagoda's lit windows.
	{
		UMaterialInterface* Hill = Plain(this, Hex(TEXT("#0c0d22")), 0.9);
		UMaterialInterface* Window = Glow(this, Hex(TEXT("#ffbf6e")) * 5.f);
		const double Bearings[] = { -0.55, -0.25, 0.12, 0.42, 0.7, 2.6, -2.5 };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Bearings); ++i)
		{
			const double D = R.FRandRange(260.f, 420.f);
			const double H = R.FRandRange(14.f, 42.f);
			const double W = R.FRandRange(40.f, 110.f);
			MovecadeKart::FMeshBuilder K;
			K.Lathe(TEXT("Hill"), { { W * 100.0, 0.0 }, { W * 70.0, H * 40.0 }, { W * 35.0, H * 85.0 }, { W * 10.0, H * 100.0 }, { 0.0, H * 102.0 } }, 14, FVector::ZeroVector, FVector::UpVector, FVector(1.0, 0.5, 1.0), FVector2D::ZeroVector);
			const FVector At = Cm(D * FMath::Cos(Bearings[i]), D * FMath::Sin(Bearings[i]), Sea - 0.5);
			Mesh(K.Build(this, TEXT("SumoIsland")), Hill, FTransform(FRotator(0.0, FMath::RadiansToDegrees(Bearings[i]) + 90.0, 0.0), At), false);
			if (i == 1 || i == 3)
			{
				for (int32 Tier = 0; Tier < 5; ++Tier)
				{
					const FVector P = At + FVector(0.0, 0.0, H * 100.0 + 200.0 + Tier * 260.0);
					Mesh(Cube, Hill, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 0.0, 120.0), FVector(9.0 - Tier * 1.3, 9.0 - Tier * 1.3, 0.5)), false);
					Mesh(Cube, Window, FTransform(FRotator::ZeroRotator, P, FVector(5.6 - Tier * 0.8, 5.6 - Tier * 0.8, 1.2)), false);
				}
			}
		}
	}
	// Neon spill over the ring: cyan from the left, magenta from the right, a soft white overhead.
	RimLights.Add(Light(Cm(-2.0, -10.0, 4.0), Hex(TEXT("#43d9ff")), 1600.f, 1800.f));
	RimLights.Add(Light(Cm(-2.0, 10.0, 4.0), Hex(TEXT("#ff4fae")), 1500.f, 1800.f));
	RimLights.Add(Light(Cm(6.0, 0.0, 7.0), Hex(TEXT("#b8b4ff")), 900.f, 2000.f));
}

void ASumoArena::SetRing(int32 ShrinksDone, bool bPending, double InWarnAge)
{
	for (int32 b = 0; b < Bands.Num(); ++b)
	{
		FBand& Band = Bands[b];
		const bool bGone = b < ShrinksDone;
		if (bGone && (Band.State == EBandState::Intact || Band.State == EBandState::Rising))
		{
			Band.State = EBandState::Falling;
			Band.Since = Clock;
			for (FSegmentFall& F : Band.Falls)
			{
				F.bSplashed = false;
			}
		}
		else if (!bGone && (Band.State == EBandState::Gone || Band.State == EBandState::Falling))
		{
			UE_LOG(LogTemp, Verbose, TEXT("SumoArena: band %d rises (was %d) at %.2f"), b, static_cast<int32>(Band.State), Clock);
			Band.State = EBandState::Rising;
			Band.Since = Clock;
		}
	}
	WarnBand = bPending ? ShrinksDone : INDEX_NONE;
	WarnAge = InWarnAge;
}

void ASumoArena::Splash(const FVector& At, double Scale)
{
	namespace Fx = MovecadeKart::Fx;
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	if (!FxSys)
	{
		return;
	}
	TArray<Fx::FKartFxEmit> Out;
	Fx::FKartFxEmit Spray;
	Spray.Field = Fx::EField::Sparks;
	Spray.Position = At;
	Spray.Count = FMath::RoundToInt32(10 * Scale);
	Spray.Spread = 0.5;
	Spray.Speed = FVector2D(3.0, 6.0) * FMath::Sqrt(Scale);
	Spray.Life = FVector2D(0.5, 0.9);
	Spray.Size = FVector2D(0.03, 0.06);
	Spray.Color = MovecadeKart::Art::Hex(TEXT("#cfe6ff"));
	Spray.ColorEnd = MovecadeKart::Art::Hex(TEXT("#4b6bd6"));
	Spray.Intensity = 1.6;
	Spray.Jitter = 0.3 * Scale;
	Out.Add(Spray);
	Fx::FKartFxEmit Foam;
	Foam.Field = Fx::EField::Smoke;
	Foam.Position = At;
	Foam.Count = FMath::RoundToInt32(6 * Scale);
	Foam.Spread = 0.8;
	Foam.Speed = FVector2D(0.6, 1.6);
	Foam.Life = FVector2D(0.6, 1.0);
	Foam.Size = FVector2D(0.25, 0.45) * Scale;
	Foam.SizeEnd = 1.8;
	Foam.Color = MovecadeKart::Art::Hex(TEXT("#b8c8f0"));
	Foam.ColorEnd = MovecadeKart::Art::Hex(TEXT("#3a4a80"));
	Foam.Intensity = 0.7;
	Foam.Jitter = 0.4 * Scale;
	Out.Add(Foam);
	FxSys->Emit(Out);
}

void ASumoArena::AnimateBands()
{
	constexpr double FallTime = 1.6;
	constexpr double RiseTime = 0.9;
	for (int32 b = 0; b < Bands.Num(); ++b)
	{
		FBand& Band = Bands[b];
		UInstancedStaticMeshComponent* Ism = BandMeshes.IsValidIndex(b) ? BandMeshes[b].Get() : nullptr;
		if (!Ism)
		{
			continue;
		}
		const double Age = Clock - Band.Since;
		const bool bCore = b == Bands.Num() - 1;
		if (bCore)
		{
			for (UStaticMeshComponent* C : CoreDecor)
			{
				if (C)
				{
					C->SetVisibility(Band.State == EBandState::Intact);
				}
			}
		}
		if (Band.State == EBandState::Falling || Band.State == EBandState::Rising)
		{
			bool bDone = true;
			for (int32 s = 0; s < Band.Segments; ++s)
			{
				const FSegmentFall& F = Band.Falls[s];
				const FTransform& Rest = Band.Rest[s];
				FTransform T = Rest;
				if (Band.State == EBandState::Falling)
				{
					// A shudder, then the drop: gravity, a drift outward, a tumble; a splash where it meets the sea.
					const double Local = Age - F.Delay;
					bDone &= Local >= FallTime;
					if (Local > -0.25 && Local < 0.0)
					{
						T.AddToTranslation(FVector(0.0, 0.0, FMath::Sin(Clock * 70.0 + s) * 3.0));
					}
					else if (Local >= 0.0)
					{
						const double Drop = 0.5 * 980.0 * 1.4 * Local * Local;
						const FVector Out = Rest.GetLocation().GetSafeNormal2D();
						const FVector Pos = Rest.GetLocation() + (Out.IsNearlyZero() ? FVector(F.Drift, 0.0, 0.0) : Out) * (F.Drift * 120.0 * Local) - FVector(0.0, 0.0, Drop);
						if (Pos.Z < WaterZ && !F.bSplashed)
						{
							Band.Falls[s].bSplashed = true;
							if (s % 2 == 0)
							{
								Splash(FVector(Pos.X, Pos.Y, WaterZ + 10.0), 0.9);
							}
						}
						if (Pos.Z < WaterZ - 300.0)
						{
							T.SetScale3D(FVector(0.001));
						}
						T.SetLocation(Pos);
						T.SetRotation(FQuat(F.Axis, FMath::DegreesToRadians(F.Spin * Local)) * Rest.GetRotation());
					}
				}
				else
				{
					// Back up out of the sea in a wave, overshooting a touch.
					const double Local = FMath::Clamp((Age - F.Delay * 0.6) / RiseTime, 0.0, 1.0);
					bDone &= Local >= 1.0;
					const double K = 1.0 - Local;
					const double Over = FMath::Sin(Local * PI) * 0.12;
					T.SetLocation(Rest.GetLocation() + FVector(0.0, 0.0, (WaterZ - 300.0) * K * K + Over * 40.0));
					T.SetRotation(FQuat(F.Axis, FMath::DegreesToRadians(F.Spin * 0.6 * K)) * Rest.GetRotation());
				}
				Ism->UpdateInstanceTransform(s, T, false, s == Band.Segments - 1, true);
			}
			if (bDone)
			{
				Band.State = Band.State == EBandState::Falling ? EBandState::Gone : EBandState::Intact;
				UE_LOG(LogTemp, Verbose, TEXT("SumoArena: band %d -> %s at %.2f"), b, Band.State == EBandState::Gone ? TEXT("gone") : TEXT("intact"), Clock);
				if (Band.State == EBandState::Intact)
				{
					for (int32 s = 0; s < Band.Segments; ++s)
					{
						Ism->UpdateInstanceTransform(s, Band.Rest[s], false, s == Band.Segments - 1, true);
					}
				}
			}
		}
		// The warned band: the edge and the planks flush red and flicker faster as the drop nears.
		const bool bWarn = b == WarnBand && Band.State == EBandState::Intact;
		const double Urgency = FMath::Clamp(WarnAge / (MovecadeKart::Sumo::ShrinkWarningMs / 1000.0), 0.0, 1.0);
		const double Pulse = bWarn ? 0.5 + 0.5 * FMath::Sin(Clock * (8.0 + 18.0 * Urgency)) : 0.0;
		if (BandTrims.IsValidIndex(b) && BandTrims[b])
		{
			const FLinearColor Base = MovecadeKart::Art::Hex(bCore ? TEXT("#ff4fa8") : TrimColors[b % 2]) * (b == 0 ? 5.f : 3.2f);
			BandTrims[b]->SetVectorParameterValue(TEXT("Color"), bWarn ? FMath::Lerp(MovecadeKart::Art::Hex(TEXT("#ff2030")) * 4.f, MovecadeKart::Art::Hex(TEXT("#ff6040")) * 14.f, static_cast<float>(Pulse)) : Base);
		}
		if (BandWarnings.IsValidIndex(b) && BandWarnings[b])
		{
			BandWarnings[b]->SetVisibility(bWarn);
			if (bWarn && BandWarningMats[b])
			{
				BandWarningMats[b]->SetScalarParameterValue(TEXT("Opacity"), static_cast<float>(0.08 + 0.32 * Pulse * (0.5 + 0.5 * Urgency)));
			}
		}
	}
}

void ASumoArena::AnimateProps(float Dt)
{
	for (int32 i = 0; i < Floaters.Num(); ++i)
	{
		if (Floaters[i] && FloaterRest.IsValidIndex(i))
		{
			const double Bob = FMath::Sin(Clock * 0.7 + i * 1.7) * 18.0;
			Floaters[i]->SetRelativeLocationAndRotation(FloaterRest[i] + FVector(0.0, 0.0, Bob), FRotator(FMath::Sin(Clock * 0.5 + i) * 2.0, 0.0, FMath::Cos(Clock * 0.6 + i) * 2.0));
		}
	}
	if (Lanterns && LanternRest.Num() == Lanterns->GetInstanceCount())
	{
		for (int32 i = 0; i < LanternRest.Num(); ++i)
		{
			const FVector P = LanternRest[i];
			const double Bob = FMath::Sin(Clock * 1.1 + P.X * 0.003 + P.Y * 0.002) * 8.0;
			const FRotator Rock(FMath::Sin(Clock * 0.9 + i) * 6.0, 0.0, FMath::Cos(Clock * 0.8 + i * 0.7) * 6.0);
			Lanterns->UpdateInstanceTransform(i, FTransform(Rock, P + FVector(0.0, 0.0, Bob), FVector(0.42, 0.42, 0.55)), false, i == LanternRest.Num() - 1, true);
		}
	}
	(void)Dt;
}

void ASumoArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	AnimateBands();
	AnimateProps(DeltaSeconds);
}

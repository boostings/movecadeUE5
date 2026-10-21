#include "Games/Stacker/StackerRooftop.h"

#include "Art/KartMaterials.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* const RoofTag = TEXT("StackerRooftop");
	const TCHAR* const LaneColors[] = { TEXT("#6d8bff"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	/** RooftopEnvironment.tsx makeCity: buildings per band, the band's z and parallax. */
	const int32 BandCounts[] = { 18, 23, 28 };
	const double BandZ[] = { -14.0, -29.0, -49.0 };
	const double BandParallax[] = { 0.18, 0.12, 0.07 };
}

AStackerRooftop::AStackerRooftop()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
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

FVector AStackerRooftop::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

FLinearColor AStackerRooftop::LaneColor(int32 Lane)
{
	return MovecadeKart::Art::Hex(LaneColors[((Lane % 4) + 4) % 4]);
}

void AStackerRooftop::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetActorTransform(FTransform::Identity);
	Build();
}

void AStackerRooftop::SetLanes(int32 LaneCount)
{
	LaneCount = FMath::Clamp(LaneCount, 1, 4);
	if (LaneCount != Lanes)
	{
		Lanes = LaneCount;
		Build();
	}
}

void AStackerRooftop::SetScroll(double MaxScroll)
{
	for (int32 i = 0; i < Bands.Num(); ++i)
	{
		if (Bands[i])
		{
			Bands[i]->SetRelativeLocation(ToWorld(FVector(0.0, -MaxScroll * BandParallax[i], 0.0)));
		}
	}
}

void AStackerRooftop::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), RoofTag);
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	Groups.Reset();
	Bands.Reset();
	Flags.Reset();
	Fan = nullptr;
}

UInstancedStaticMeshComponent* AStackerRooftop::Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows, USceneComponent* Parent)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d|%s"), *GetNameSafe(Mesh), *GetNameSafe(Material), bShadows ? 1 : 0, *GetNameSafe(Parent));
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Groups.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("RoofGroup")));
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, Material);
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(RoofTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	Groups.Add(Key, C);
	return C;
}

void AStackerRooftop::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad, bool bShadows, USceneComponent* Parent)
{
	Group(Cube, Material, bShadows, Parent)->AddInstance(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(YawRad), 0.0), ToWorld(C), FVector(S.Z, S.X, S.Y)), false);
}

void AStackerRooftop::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows)
{
	Group(Cylinder, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, 0.0)), FVector(Radius * 2.0, Radius * 2.0, Height)), true);
}

void AStackerRooftop::Build()
{
	Clear();
	BuildLook();
	BuildDeck();
	BuildCity();
	BuildProps();
}

void AStackerRooftop::BuildLook()
{
	using namespace MovecadeKart;
	using namespace MovecadeKart::Art;
	// StackerCanvas.tsx: a golden-hour rooftop; the sun high on the right, a cool rim from behind.
	Sun->SetWorldRotation((-Coords::DirToUE(FVector(11.0, 15.0, 10.0)).GetSafeNormal()).Rotation());
	Sun->SetLightColor(Hex(TEXT("#ffd09a")));
	Sun->SetIntensity(2.6f);
	Sun->SetCastShadows(true);
	Sun->SetAtmosphereSunLight(false);
	Sun->SetForwardShadingPriority(1);
	Rim->SetWorldRotation((-Coords::DirToUE(FVector(10.0, 5.0, -14.0)).GetSafeNormal()).Rotation());
	Rim->SetLightColor(Hex(TEXT("#9fc4ff")));
	Rim->SetIntensity(1.35f);
	Rim->SetCastShadows(false);
	Rim->SetAtmosphereSunLight(false);
	Rim->SetForwardShadingPriority(0);
	SkyLight->SetIntensity(1.4f);
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(TEXT("#7a6a5a")) * 0.5f);
	const FLinearColor Top = Hex(TEXT("#2a5580")), Horizon = Hex(TEXT("#e3ae94"));
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
		Sky->SetVectorParameterValue(TEXT("SkyMid"), FMath::Lerp(Horizon, Top, 0.55f));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#8c7a6a")));
		const FVector SunDir = Coords::DirToUE(FVector(0.55, 0.32, -1.0)).GetSafeNormal();
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#ffe2b8")));
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.6f * 0.035f));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), 0.55f);
		Sky->SetScalarParameterValue(TEXT("GlowPower"), 18.0f);
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), 0.5f);
		Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0f);
		if (UTexture* Clouds = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
		}
		Sky->SetScalarParameterValue(TEXT("Clouds"), 0.4f);
		Sky->SetScalarParameterValue(TEXT("CloudScale"), 0.35f);
		Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(TEXT("#ffe2c4")));
		Sky->SetVectorParameterValue(TEXT("CloudShade"), FMath::Lerp(Horizon, Top, 0.45f) * 0.8f);
		SkyDome->SetMaterial(0, Sky);
	}
	SkyLight->RecaptureSky();
	Fog->SetStartDistance(2600.f);
	Fog->SetFogDensity(0.09f);
	Fog->SetFogHeightFalloff(0.02f);
	Fog->SetFogMaxOpacity(0.8f);
	Fog->SetFogCutoffDistance(900000.f);
	Fog->SetFogInscatteringColor(Horizon * 0.9f);
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#ffd09a")) * 0.3f);
	Fog->SetDirectionalInscatteringExponent(6.f);
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.4f;
}

void AStackerRooftop::BuildDeck()
{
	using namespace MovecadeKart::Art;
	const double W = DeckWidth();
	UMaterialInterface* Deck = Surface(this, { Hex(TEXT("#8d8a84")), TEXT("painted_concrete"), 3.0, 0.2, 0.5, 0.4, 0.85, 0.0, 0.0, false, 1.0 });
	UMaterialInterface* Edge = Surface(this, { Hex(TEXT("#6e6a64")), TEXT("painted_concrete"), 2.0, 0.2, 0.5, 0.4, 0.9, 0.0, 0.0, true, 1.0 });
	UMaterialInterface* Hazard = Plain(this, Hex(TEXT("#e8b931")), 0.6);
	UMaterialInterface* HazardDark = Plain(this, Hex(TEXT("#1e1e22")), 0.6);
	// The deck, its parapet edge, the yellow hazard stripe along the front.
	Box(Deck, FVector(0.0, DeckY - 0.05, 1.0), FVector(W, 0.1, 14.0), 0.0, false);
	Box(Edge, FVector(0.0, -1.03, 1.0), FVector(W, 0.66, 14.0), 0.0, false);
	Box(Edge, FVector(0.0, DeckY + 0.25, 8.0), FVector(W, 0.5, 0.3));
	for (int32 i = 0; i < FMath::CeilToInt32(W / 0.8); ++i)
	{
		Box(i % 2 ? HazardDark : Hazard, FVector(-W / 2.0 + 0.4 + i * 0.8, DeckY + 0.003, 7.3), FVector(0.8, 0.01, 0.3), 0.0, false);
	}
	// A work zone per lane: dashed in the lane's colour, numbered, with the lane's plinth.
	for (int32 Lane = 0; Lane < Lanes; ++Lane)
	{
		const double X = LaneX(Lane, Lanes);
		const FLinearColor C = LaneColor(Lane);
		UMaterialInterface* Dash = Plain(this, C * 0.9f, 0.55);
		for (int32 d = 0; d < 9; ++d)
		{
			Box(Dash, FVector(X - 3.65 + d * 0.91, DeckY + 0.004, 1.0 - 2.4), FVector(0.5, 0.01, 0.08), 0.0, false);
			Box(Dash, FVector(X - 3.65 + d * 0.91, DeckY + 0.004, 1.0 + 2.4), FVector(0.5, 0.01, 0.08), 0.0, false);
		}
		for (int32 d = 0; d < 5; ++d)
		{
			Box(Dash, FVector(X - 3.65, DeckY + 0.004, 1.0 - 2.4 + 0.25 + d * 1.1), FVector(0.08, 0.01, 0.5), 0.0, false);
			Box(Dash, FVector(X + 3.65, DeckY + 0.004, 1.0 - 2.4 + 0.25 + d * 1.1), FVector(0.08, 0.01, 0.5), 0.0, false);
		}
		Box(Plain(this, C * 0.75f, 0.45, 0.1), FVector(X, -0.4 - 0.31, 0.0), FVector(6.78, 0.62, 2.08));
		Box(Plain(this, Hex(TEXT("#2a2c33")), 0.6), FVector(X, -0.4 - 0.005, 0.0), FVector(6.9, 0.02, 2.2), 0.0, false);
		UTextRenderComponent* Number = NewObject<UTextRenderComponent>(this, MakeUniqueObjectName(this, UTextRenderComponent::StaticClass(), TEXT("ZoneNumber")));
		Number->SetupAttachment(RootComponent);
		Number->SetRelativeLocation(ToWorld(FVector(X + 2.8, DeckY + 0.01, 3.0)));
		Number->SetRelativeRotation(FRotator(90.0, 180.0, 0.0));
		Number->SetHorizontalAlignment(EHTA_Center);
		Number->SetVerticalAlignment(EVRTA_TextCenter);
		Number->SetWorldSize(60.f);
		Number->SetTextRenderColor((C * 0.9f).ToFColor(true));
		Number->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Lane + 1)));
		Number->ComponentTags.Add(RoofTag);
		AddInstanceComponent(Number);
		Number->RegisterComponent();
	}
}

void AStackerRooftop::BuildCity()
{
	using namespace MovecadeKart::Art;
	// Three bands of towers, deterministic: heights, setbacks, ledges, antennas and lit window grids.
	FRandomStream R(0x5c17);
	const TCHAR* const Facades[] = { TEXT("#8c95a6"), TEXT("#a39a8f"), TEXT("#7d889b"), TEXT("#b0a294"), TEXT("#6f7c90") };
	UMaterialInterface* Window = Glow(this, Hex(TEXT("#edd3a4")) * 1.9f);
	UMaterialInterface* Dark = Plain(this, Hex(TEXT("#3a4458")), 0.15, 0.4);
	UMaterialInterface* Antenna = Plain(this, Hex(TEXT("#3a3d44")), 0.5, 0.6);
	TArray<UMaterialInterface*> FacadeMats;
	for (const TCHAR* F : Facades)
	{
		FacadeMats.Add(Plain(this, Hex(F), 0.85));
	}
	// The building under the roof: its body runs down out of frame, windows on the face toward the camera.
	{
		const double W = DeckWidth();
		Box(FacadeMats[1], FVector(0.0, -1.36 - 12.5, 1.0), FVector(W - 0.3, 25.0, 13.6), 0.0, false);
		for (int32 r = 0; r < 14; ++r)
		{
			for (int32 c = 0; c < FMath::FloorToInt32(W / 1.6); ++c)
			{
				const bool bLit = R.FRand() < 0.3;
				Box(bLit ? Window : Dark, FVector(-W / 2.0 + 0.8 + c * 1.6, -2.6 - r * 1.7, 7.81), FVector(0.8, 0.95, 0.02), 0.0, false);
			}
		}
	}
	for (int32 Band = 0; Band < 3; ++Band)
	{
		USceneComponent* Parent = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("CityBand")));
		Parent->SetupAttachment(RootComponent);
		Parent->SetMobility(EComponentMobility::Movable);
		Parent->ComponentTags.Add(RoofTag);
		AddInstanceComponent(Parent);
		Parent->RegisterComponent();
		Bands.Add(Parent);
		const int32 N = BandCounts[Band];
		const double Span = 26.0 + Band * 22.0 + DeckWidth();
		for (int32 i = 0; i < N; ++i)
		{
			const double X = -Span / 2.0 + (i + 0.5) * Span / N + R.FRandRange(-0.6, 0.6);
			const double Wd = Span / N * R.FRandRange(0.7, 0.95);
			const double Dp = R.FRandRange(4.0, 8.0);
			// The near band stays under the parapet, the middle rises past it, the far one climbs into the haze.
			const double H = 14.0 + Band * 5.0 + R.FRandRange(0.0, 10.0 + Band * 10.0);
			const double Base = -26.0;
			const double Z = BandZ[Band] - Dp / 2.0;
			Box(FacadeMats[(i + Band) % FacadeMats.Num()], FVector(X, Base + H / 2.0, Z), FVector(Wd, H, Dp), 0.0, Band == 0, Parent);
			// A setback on the taller ones, a ledge, a rooftop antenna.
			if (H > 18.0 && R.FRand() < 0.6)
			{
				Box(FacadeMats[(i + 2) % FacadeMats.Num()], FVector(X, Base + H + 2.0, Z), FVector(Wd * 0.7, 4.0, Dp * 0.7), 0.0, false, Parent);
			}
			Box(Dark, FVector(X, Base + H + 0.08, Z), FVector(Wd * 1.02, 0.16, Dp * 1.02), 0.0, false, Parent);
			if (R.FRand() < 0.35)
			{
				Box(Antenna, FVector(X + Wd * 0.2, Base + H + 1.6, Z), FVector(0.08, 3.2, 0.08), 0.0, false, Parent);
			}
			// Windows: a grid on the face toward the deck, some lit.
			const int32 Cols = FMath::Max(2, FMath::FloorToInt32(Wd / 0.9));
			const int32 Rows = FMath::Max(2, FMath::FloorToInt32(H / 1.2));
			for (int32 r = 0; r < Rows; ++r)
			{
				for (int32 c = 0; c < Cols; ++c)
				{
					const bool bLit = R.FRand() < 0.26;
					const double WX = X - Wd / 2.0 + (c + 0.5) * Wd / Cols;
					const double WY = Base + 1.0 + r * 1.2;
					if (WY > Base + H - 0.6)
					{
						continue;
					}
					Box(bLit ? Window : Dark, FVector(WX, WY, BandZ[Band] + 0.01), FVector(Wd / Cols * 0.5, 0.6, 0.02), 0.0, false, Parent);
				}
			}
		}
	}
}

void AStackerRooftop::BuildProps()
{
	using namespace MovecadeKart::Art;
	const double W = DeckWidth();
	UMaterialInterface* Crane = Plain(this, Hex(TEXT("#d39a32")), 0.5, 0.2);
	UMaterialInterface* Steel = Plain(this, Hex(TEXT("#4a4d55")), 0.4, 0.8);
	UMaterialInterface* Rail = Plain(this, Hex(TEXT("#c9ccd2")), 0.35, 0.9);
	UMaterialInterface* Cone = Plain(this, Hex(TEXT("#f26b2c")), 0.5);
	UMaterialInterface* Wood = Surface(this, { Hex(TEXT("#a07a52")), TEXT("dark_wooden_planks"), 0.5, 0.4, 0.6, 0.4, 0.8 });
	UMaterialInterface* Cardboard = Plain(this, Hex(TEXT("#b58b5a")), 0.8);
	UMaterialInterface* Hvac = Plain(this, Hex(TEXT("#b9bcc2")), 0.45, 0.5);
	// The crane behind the deck: a lattice mast, the jib, the counter-jib and a hook on its cable.
	const FVector CraneBase(-W / 2.0 - 2.0, DeckY, -13.0);
	for (const double Dx : { -0.45, 0.45 })
	{
		for (const double Dz : { -0.45, 0.45 })
		{
			Box(Crane, CraneBase + FVector(Dx, 6.0, Dz), FVector(0.12, 12.0, 0.12));
		}
	}
	for (int32 i = 0; i < 12; ++i)
	{
		Box(Crane, CraneBase + FVector(0.0, 0.5 + i, 0.45), FVector(0.9, 0.08, 0.08), 0.0, false);
		Box(Crane, CraneBase + FVector(0.0, 0.5 + i, -0.45), FVector(0.9, 0.08, 0.08), 0.0, false);
	}
	Box(Crane, CraneBase + FVector(6.0, 11.4, 0.0), FVector(12.0, 0.5, 0.6));
	Box(Crane, CraneBase + FVector(-2.5, 11.4, 0.0), FVector(5.0, 0.5, 0.6));
	Box(Steel, CraneBase + FVector(-4.2, 10.9, 0.0), FVector(1.4, 1.0, 1.0));
	Box(Steel, CraneBase + FVector(0.0, 12.3, 0.0), FVector(0.2, 1.8, 0.2));
	Box(Steel, CraneBase + FVector(9.0, 8.4, 0.0), FVector(0.03, 6.0, 0.03), 0.0, false);
	Box(Steel, CraneBase + FVector(9.0, 5.3, 0.0), FVector(0.4, 0.3, 0.3));
	// The railing along the back of the deck and down the sides.
	for (const double Y : { 0.45, 0.95 })
	{
		Box(Rail, FVector(0.0, DeckY + Y, -3.5), FVector(W - 0.4, 0.05, 0.05), 0.0, false);
		for (const double Side : { -1.0, 1.0 })
		{
			Box(Rail, FVector(Side * (W / 2.0 - 0.2), DeckY + Y, 2.25), FVector(0.05, 0.05, 11.5), 0.0, false);
		}
	}
	for (int32 i = 0; i <= FMath::CeilToInt32(W / 2.0); ++i)
	{
		Box(Rail, FVector(-W / 2.0 + 0.2 + i * (W - 0.4) / FMath::CeilToInt32(W / 2.0), DeckY + 0.5, -3.5), FVector(0.05, 1.0, 0.05), 0.0, false);
	}
	// Cones, a cable spool, a pallet with boxes, the HVAC unit with its fan, two flags.
	const double Side = W / 2.0 - 1.6;
	Post(Cone, FVector(-Side, DeckY, 4.6), 0.18, 0.55);
	Post(Cone, FVector(-Side + 0.7, DeckY, 5.2), 0.18, 0.55);
	Box(Wood, FVector(Side, DeckY + 0.07, 4.0), FVector(1.2, 0.14, 1.0));
	Box(Cardboard, FVector(Side - 0.25, DeckY + 0.42, 4.0), FVector(0.55, 0.56, 0.6));
	Box(Cardboard, FVector(Side + 0.3, DeckY + 0.36, 3.85), FVector(0.45, 0.44, 0.5));
	Box(Cardboard, FVector(Side + 0.05, DeckY + 0.86, 3.95), FVector(0.42, 0.32, 0.44));
	Group(Cylinder, Wood, true)->AddInstance(FTransform(FRotator(0.0, 0.0, 90.0), ToWorld(FVector(-Side + 0.3, DeckY + 0.45, 2.2)), FVector(0.9, 0.9, 0.5)), false);
	Group(Cylinder, Steel, true)->AddInstance(FTransform(FRotator(0.0, 0.0, 90.0), ToWorld(FVector(-Side + 0.3, DeckY + 0.45, 2.2)), FVector(0.4, 0.4, 0.52)), false);
	const FVector HvacAt(Side - 0.6, DeckY, -2.0);
	Box(Hvac, HvacAt + FVector(0.0, 0.55, 0.0), FVector(1.8, 1.1, 1.2));
	Box(Steel, HvacAt + FVector(0.0, 1.12, 0.0), FVector(1.0, 0.04, 1.0), 0.0, false);
	Fan = NewObject<USceneComponent>(this, TEXT("HvacFan"));
	Fan->SetupAttachment(RootComponent);
	Fan->SetMobility(EComponentMobility::Movable);
	Fan->SetRelativeLocation(ToWorld(HvacAt + FVector(0.0, 1.16, 0.0)));
	Fan->ComponentTags.Add(RoofTag);
	AddInstanceComponent(Fan);
	Fan->RegisterComponent();
	for (int32 b = 0; b < 4; ++b)
	{
		UStaticMeshComponent* Blade = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("FanBlade")));
		Blade->SetStaticMesh(Cube);
		Blade->SetMaterial(0, Steel);
		Blade->SetupAttachment(Fan);
		Blade->SetRelativeRotation(FRotator(0.0, b * 90.0, 0.0));
		Blade->SetRelativeLocation(FRotator(0.0, b * 90.0, 0.0).RotateVector(FVector(20.0, 0.0, 0.0)));
		Blade->SetRelativeScale3D(FVector(0.4, 0.12, 0.02));
		Blade->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Blade->ComponentTags.Add(RoofTag);
		AddInstanceComponent(Blade);
		Blade->RegisterComponent();
	}
	const TCHAR* const FlagColors[] = { TEXT("#d86654"), TEXT("#4c8990") };
	for (int32 f = 0; f < 2; ++f)
	{
		const FVector Pole(f == 0 ? -W / 2.0 + 0.6 : W / 2.0 - 0.6, DeckY, -3.2);
		Post(Rail, Pole, 0.04, 3.2);
		USceneComponent* Hinge = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("Flag")));
		Hinge->SetupAttachment(RootComponent);
		Hinge->SetMobility(EComponentMobility::Movable);
		Hinge->SetRelativeLocation(ToWorld(Pole + FVector(0.0, 2.85, 0.0)));
		Hinge->ComponentTags.Add(RoofTag);
		AddInstanceComponent(Hinge);
		Hinge->RegisterComponent();
		UStaticMeshComponent* Cloth = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("FlagCloth")));
		Cloth->SetStaticMesh(Cube);
		Cloth->SetMaterial(0, Plain(this, Hex(FlagColors[f]), 0.8));
		Cloth->SetupAttachment(Hinge);
		Cloth->SetRelativeLocation(FVector(0.0, 50.0, -20.0));
		Cloth->SetRelativeScale3D(FVector(0.02, 1.0, 0.6));
		Cloth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Cloth->ComponentTags.Add(RoofTag);
		AddInstanceComponent(Cloth);
		Cloth->RegisterComponent();
		Flags.Add(Hinge);
	}
}

void AStackerRooftop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	if (Fan)
	{
		Fan->SetRelativeRotation(FRotator(0.0, FMath::RadiansToDegrees(Clock * 2.4), 0.0));
	}
	for (int32 f = 0; f < Flags.Num(); ++f)
	{
		if (Flags[f])
		{
			Flags[f]->SetRelativeRotation(FRotator(0.0, 20.0 + 14.0 * FMath::Sin(Clock * 1.7 + f * 1.3), 4.0 * FMath::Sin(Clock * 3.1 + f)));
		}
	}
}

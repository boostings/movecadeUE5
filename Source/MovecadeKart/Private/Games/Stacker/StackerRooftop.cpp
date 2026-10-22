#include "Games/Stacker/StackerRooftop.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
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
	/** RooftopEnvironment.tsx: four clouds at these x, y 9 / 11 alternating, z -62, the group drifting sin(.025t)*2.2. */
	const double CloudX[] = { -36.0, -12.0, 18.0, 43.0 };

	/** A window: one quad on a facade, facing the deck (web +z), in UE cm. */
	void WindowQuad(MovecadeKart::FMeshBuilder& B, int32 Section, const FVector& WebCentre, double HalfW, double HalfH)
	{
		const FVector C = MovecadeKart::Coords::ToUE(WebCentre);
		const FVector N = MovecadeKart::Coords::DirToUE(FVector(0.0, 0.0, 1.0));
		const double Y = HalfW * 100.0, Z = HalfH * 100.0;
		const int32 A = B.Vert(Section, C + FVector(0.0, -Y, -Z), N, FVector2D(0.0, 1.0), FVector2D::ZeroVector);
		const int32 Bb = B.Vert(Section, C + FVector(0.0, Y, -Z), N, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		const int32 Cc = B.Vert(Section, C + FVector(0.0, Y, Z), N, FVector2D(1.0, 0.0), FVector2D::ZeroVector);
		const int32 D = B.Vert(Section, C + FVector(0.0, -Y, Z), N, FVector2D(0.0, 0.0), FVector2D::ZeroVector);
		B.Tri(Section, A, Bb, Cc);
		B.Tri(Section, A, Cc, D);
	}
}

AStackerRooftop::AStackerRooftop()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// The deck, the building, the crane and the props never move: Static, so their draws are cached.
	RootComponent->SetMobility(EComponentMobility::Static);
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
	// The sky never changes during a round: one capture after the build, not one every frame.
	SkyLight->bRealTimeCapture = false;
	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(RootComponent);
	SkyDome->SetStaticMesh(SphereMesh.Object);
	SkyDome->SetRelativeScale3D(FVector(20000.0));
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDome->SetCastShadow(false);
	SkyDome->SetMobility(EComponentMobility::Static);
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
	if (!GetActorTransform().Equals(FTransform::Identity))
	{
		SetActorTransform(FTransform::Identity);
	}
	// In the editor the venue previews itself. A game builds once, for the real lane count (SetLanes), instead of
	// building for one lane here and again for four a moment later.
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		Build();
	}
}

void AStackerRooftop::BeginPlay()
{
	Super::BeginPlay();
	AppliedScroll = -1.0;
}

void AStackerRooftop::SetLanes(int32 LaneCount)
{
	LaneCount = FMath::Clamp(LaneCount, 1, 4);
	if (LaneCount != Lanes || !bBuilt)
	{
		Lanes = LaneCount;
		Build();
	}
}

void AStackerRooftop::SetScroll(double MaxScroll)
{
	// The game settles its scroll exactly; anything under a fiftieth of a row is not worth moving ~10 batches for.
	if (FMath::Abs(MaxScroll - AppliedScroll) < 0.002)
	{
		return;
	}
	AppliedScroll = MaxScroll;
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
	Clouds = nullptr;
	AppliedScroll = -1.0;
}

EComponentMobility::Type AStackerRooftop::MobilityUnder(const USceneComponent* Parent) const
{
	return Parent && Parent->Mobility == EComponentMobility::Movable ? EComponentMobility::Movable : EComponentMobility::Static;
}

UMaterialInterface* AStackerRooftop::Mat(const FLinearColor& Color, double Roughness, double Metallic, double Emissive)
{
	const FString Key = FString::Printf(TEXT("%s|%.3f|%.3f|%.3f"), *Color.ToString(), Roughness, Metallic, Emissive);
	if (TObjectPtr<UMaterialInterface>* Found = Materials.Find(Key))
	{
		return *Found;
	}
	UMaterialInterface* M = MovecadeKart::Art::Plain(this, Color, Roughness, Metallic, Emissive);
	Materials.Add(Key, M);
	return M;
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
	C->SetMobility(MobilityUnder(Parent));
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(RoofTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	Groups.Add(Key, C);
	return C;
}

UStaticMeshComponent* AStackerRooftop::Part(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Mats, bool bShadows, USceneComponent* Parent)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("RoofPart")));
	C->SetStaticMesh(Mesh);
	for (int32 i = 0; i < Mats.Num(); ++i)
	{
		C->SetMaterial(i, Mats[i]);
	}
	C->SetMobility(MobilityUnder(Parent));
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(RoofTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

USceneComponent* AStackerRooftop::Pivot(const TCHAR* Name, const FVector& WebAt)
{
	USceneComponent* P = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
	P->SetMobility(EComponentMobility::Movable);
	P->SetupAttachment(RootComponent);
	P->SetRelativeLocation(ToWorld(WebAt));
	P->ComponentTags.Add(RoofTag);
	AddInstanceComponent(P);
	P->RegisterComponent();
	return P;
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
	BuildSky();
	BuildProps();
	bBuilt = true;
	// One capture of the finished scene (the sky light is not real-time).
	SkyLight->RecaptureSky();
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
	if (!SkyMaterial)
	{
		if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			SkyMaterial = UMaterialInstanceDynamic::Create(SkyMaster, this);
		}
	}
	if (UMaterialInstanceDynamic* Sky = SkyMaterial)
	{
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
		Sky->SetVectorParameterValue(TEXT("SkyMid"), FMath::Lerp(Horizon, Top, 0.4f));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#8c7a6a")));
		const FVector SunDir = Coords::DirToUE(FVector(0.36, 0.085, -1.0)).GetSafeNormal();
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#ffcf94")));
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.035f));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), 1.1f);
		Sky->SetScalarParameterValue(TEXT("GlowPower"), 9.0f);
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), 0.9f);
		Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0f);
		if (UTexture* CloudMap = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), CloudMap);
		}
		Sky->SetScalarParameterValue(TEXT("Clouds"), 0.4f);
		Sky->SetScalarParameterValue(TEXT("CloudScale"), 0.35f);
		Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(TEXT("#ffe2c4")));
		Sky->SetVectorParameterValue(TEXT("CloudShade"), FMath::Lerp(Horizon, Top, 0.45f) * 0.8f);
		SkyDome->SetMaterial(0, Sky);
	}
	Fog->SetStartDistance(2600.f);
	Fog->SetFogDensity(0.07f);
	Fog->SetFogHeightFalloff(0.02f);
	Fog->SetFogMaxOpacity(0.8f);
	Fog->SetFogCutoffDistance(900000.f);
	Fog->SetFogInscatteringColor(Horizon * 0.75f);
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#ffc27a")) * 0.6f);
	Fog->SetDirectionalInscatteringExponent(6.f);
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.4f;
	// StackerCanvas.tsx: bloom .8 for the sun, the windows and the sparks; a touch of warmth and punch.
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.8f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.08, 1.06, 1.0, 1.0);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.06, 1.06, 1.06, 1.0);
}

void AStackerRooftop::BuildDeck()
{
	using namespace MovecadeKart::Art;
	const double W = DeckWidth();
	auto CachedSurface = [this](const TCHAR* Key, const FSurfaceLook& Look)
	{
		if (TObjectPtr<UMaterialInterface>* Found = Materials.Find(Key))
		{
			return Found->Get();
		}
		UMaterialInterface* M = Surface(this, Look);
		Materials.Add(Key, M);
		return M;
	};
	UMaterialInterface* Deck = CachedSurface(TEXT("deck"), { Hex(TEXT("#8d8a84")), TEXT("painted_concrete"), 3.0, 0.2, 0.5, 0.4, 0.85, 0.0, 0.0, false, 1.0 });
	UMaterialInterface* Edge = CachedSurface(TEXT("edge"), { Hex(TEXT("#6e6a64")), TEXT("painted_concrete"), 2.0, 0.2, 0.5, 0.4, 0.9, 0.0, 0.0, true, 1.0 });
	UMaterialInterface* Hazard = Mat(Hex(TEXT("#e8b931")), 0.6);
	UMaterialInterface* HazardDark = Mat(Hex(TEXT("#1e1e22")), 0.6);
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
		UMaterialInterface* Dash = Mat(C * 0.9f, 0.55);
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
		// The plinth rises from the deck to the foundation's underside, so the first block sits on it.
		Box(Mat(C * 0.75f, 0.45, 0.1), FVector(X, (DeckY + 0.03) / 2.0, 0.0), FVector(6.78, 0.03 - DeckY, 2.08));
		Box(Mat(Hex(TEXT("#2a2c33")), 0.6), FVector(X, 0.02, 0.0), FVector(6.9, 0.02, 2.2), 0.0, false);
		UTextRenderComponent* Number = NewObject<UTextRenderComponent>(this, MakeUniqueObjectName(this, UTextRenderComponent::StaticClass(), TEXT("ZoneNumber")));
		Number->SetMobility(EComponentMobility::Static);
		Number->SetupAttachment(RootComponent);
		Number->SetRelativeLocation(ToWorld(FVector(X + 2.8, DeckY + 0.01, 3.0)));
		Number->SetRelativeRotation(FRotator(90.0, 180.0, 0.0));
		Number->SetHorizontalAlignment(EHTA_Center);
		Number->SetVerticalAlignment(EVRTA_TextCenter);
		Number->SetWorldSize(60.f);
		Number->SetTextRenderColor((C * 0.9f).ToFColor(true));
		Number->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Lane + 1)));
		Number->SetCastShadow(false);
		Number->ComponentTags.Add(RoofTag);
		AddInstanceComponent(Number);
		Number->RegisterComponent();
	}
}

void AStackerRooftop::BuildCity()
{
	using namespace MovecadeKart::Art;
	using MovecadeKart::FMeshBuilder;
	// Three bands of towers, deterministic: heights, setbacks, ledges, antennas and lit window grids.
	FRandomStream R(0x5c17);
	const TCHAR* const Facades[] = { TEXT("#8c95a6"), TEXT("#a39a8f"), TEXT("#7d889b"), TEXT("#b0a294"), TEXT("#6f7c90") };
	// The windows are opaque and emissive (not translucent), so they sit in the fog with the facades.
	UMaterialInterface* Window = Mat(Hex(TEXT("#edd3a4")), 0.4, 0.0, 1.9);
	UMaterialInterface* Dark = Mat(Hex(TEXT("#3a4458")), 0.15, 0.4);
	UMaterialInterface* Antenna = Mat(Hex(TEXT("#3a3d44")), 0.5, 0.6);
	TArray<UMaterialInterface*> FacadeMats;
	for (const TCHAR* F : Facades)
	{
		FacadeMats.Add(Mat(Hex(F), 0.85));
	}
	const TArray<UMaterialInterface*> WindowMats = { Window, Dark };
	// The building under the roof: its body runs down out of frame, windows on the face toward the camera.
	{
		const double W = DeckWidth();
		Box(FacadeMats[1], FVector(0.0, -1.36 - 12.5, 1.0), FVector(W - 0.3, 25.0, 13.6), 0.0, false);
		FMeshBuilder B;
		const int32 Lit = B.Section(TEXT("Lit"));
		const int32 Unlit = B.Section(TEXT("Dark"));
		for (int32 r = 0; r < 14; ++r)
		{
			for (int32 c = 0; c < FMath::FloorToInt32(W / 1.6); ++c)
			{
				const bool bLit = R.FRand() < 0.3;
				WindowQuad(B, bLit ? Lit : Unlit, FVector(-W / 2.0 + 0.8 + c * 1.6, -2.6 - r * 1.7, 7.82), 0.4, 0.475);
			}
		}
		Part(B.Build(this, TEXT("RoofWindows")), WindowMats, false);
	}
	for (int32 Band = 0; Band < 3; ++Band)
	{
		USceneComponent* Parent = Pivot(TEXT("CityBand"), FVector::ZeroVector);
		Bands.Add(Parent);
		FMeshBuilder B;
		const int32 Lit = B.Section(TEXT("Lit"));
		const int32 Unlit = B.Section(TEXT("Dark"));
		const int32 N = BandCounts[Band];
		const double Span = 26.0 + Band * 22.0 + DeckWidth();
		for (int32 i = 0; i < N; ++i)
		{
			const double X = -Span / 2.0 + (i + 0.5) * Span / N + R.FRandRange(-0.6, 0.6);
			const double Wd = Span / N * R.FRandRange(0.7, 0.95);
			const double Dp = R.FRandRange(4.0, 8.0);
			// The near band stays under the parapet, the middle rises past it, the far one climbs into the haze, all
			// low enough to leave the golden sky and its clouds above them (more of it as the stacks climb).
			const double H = 14.0 + Band * 4.0 + R.FRandRange(0.0, 6.0 + Band * 5.0);
			const double Base = -26.0;
			const double Z = BandZ[Band] - Dp / 2.0;
			// No shadows from the city: it is beyond the sun's useful shadow range and every move would invalidate it.
			Box(FacadeMats[(i + Band) % FacadeMats.Num()], FVector(X, Base + H / 2.0, Z), FVector(Wd, H, Dp), 0.0, false, Parent);
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
			// Windows: a grid on the face toward the deck, some lit, baked into the band's one window mesh.
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
					WindowQuad(B, bLit ? Lit : Unlit, FVector(WX, WY, BandZ[Band] + 0.02), Wd / Cols * 0.25, 0.3);
				}
			}
		}
		Part(B.Build(this, *FString::Printf(TEXT("CityWindows%d"), Band)), WindowMats, false, Parent);
	}
}

void AStackerRooftop::BuildSky()
{
	using namespace MovecadeKart::Art;
	using MovecadeKart::FMeshBuilder;
	// RooftopEnvironment.tsx: four soft clouds in the haze beyond the far band, three puffs each, drifting together.
	Clouds = Pivot(TEXT("Clouds"), FVector::ZeroVector);
	FMeshBuilder B;
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector At(CloudX[i], 9.0 + (i % 2) * 2.0, -62.0);
		auto Puff = [&B, &At](const FVector& Offset, const FVector& Scale)
		{
			// Web scale (x across, y up, z deep) to UE radii (X deep, Y across, Z up), cm.
			B.Ellipsoid(TEXT("Cloud"), MovecadeKart::Coords::ToUE(At + Offset), FVector(Scale.Z, Scale.X, Scale.Y) * 100.0, 10, 18, FVector2D::ZeroVector);
		};
		Puff(FVector::ZeroVector, FVector(8.0, 1.15, 2.6));
		Puff(FVector(-2.0, 0.5, 0.2), FVector(4.2, 1.9, 2.5));
		Puff(FVector(5.0, -0.15, 0.0), FVector(3.5, 1.1, 2.0));
		Puff(FVector(2.2, 0.75, 0.4), FVector(2.6, 1.3, 1.8));
	}
	// Lit by the low sun with a little of their own warmth, so the undersides stay peach rather than grey.
	Part(B.Build(this, TEXT("StackerClouds")), { Mat(Hex(TEXT("#f1d4bf")), 1.0, 0.0, 0.35) }, false, Clouds);
}

void AStackerRooftop::BuildProps()
{
	using namespace MovecadeKart::Art;
	const double W = DeckWidth();
	UMaterialInterface* Crane = Mat(Hex(TEXT("#d39a32")), 0.5, 0.2);
	UMaterialInterface* Steel = Mat(Hex(TEXT("#4a4d55")), 0.4, 0.8);
	UMaterialInterface* Rail = Mat(Hex(TEXT("#c9ccd2")), 0.35, 0.9);
	UMaterialInterface* Cone = Mat(Hex(TEXT("#f26b2c")), 0.5);
	UMaterialInterface* Wood = nullptr;
	if (TObjectPtr<UMaterialInterface>* Found = Materials.Find(TEXT("wood")))
	{
		Wood = *Found;
	}
	else
	{
		Wood = Surface(this, { Hex(TEXT("#a07a52")), TEXT("dark_wooden_planks"), 0.5, 0.4, 0.6, 0.4, 0.8 });
		Materials.Add(TEXT("wood"), Wood);
	}
	UMaterialInterface* Cardboard = Mat(Hex(TEXT("#b58b5a")), 0.8);
	UMaterialInterface* Hvac = Mat(Hex(TEXT("#b9bcc2")), 0.45, 0.5);
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
	// The moving parts cast no shadows: a spinning or waving caster would invalidate the cached shadow every frame.
	Fan = Pivot(TEXT("HvacFan"), HvacAt + FVector(0.0, 1.16, 0.0));
	for (int32 b = 0; b < 4; ++b)
	{
		UStaticMeshComponent* Blade = Part(Cube, { Steel }, false, Fan);
		Blade->SetRelativeRotation(FRotator(0.0, b * 90.0, 0.0));
		Blade->SetRelativeLocation(FRotator(0.0, b * 90.0, 0.0).RotateVector(FVector(20.0, 0.0, 0.0)));
		Blade->SetRelativeScale3D(FVector(0.4, 0.12, 0.02));
	}
	const TCHAR* const FlagColors[] = { TEXT("#d86654"), TEXT("#4c8990") };
	for (int32 f = 0; f < 2; ++f)
	{
		const FVector Pole(f == 0 ? -W / 2.0 + 0.6 : W / 2.0 - 0.6, DeckY, -3.2);
		Post(Rail, Pole, 0.04, 3.2);
		USceneComponent* Hinge = Pivot(TEXT("Flag"), Pole + FVector(0.0, 2.85, 0.0));
		UStaticMeshComponent* Cloth = Part(Cube, { Mat(Hex(FlagColors[f]), 0.8) }, false, Hinge);
		Cloth->SetRelativeLocation(FVector(0.0, 50.0, -20.0));
		Cloth->SetRelativeScale3D(FVector(0.02, 1.0, 0.6));
		Flags.Add(Hinge);
	}
}

void AStackerRooftop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		Build();
	}
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
	if (Clouds)
	{
		Clouds->SetRelativeLocation(ToWorld(FVector(FMath::Sin(Clock * 0.025) * 2.2, 0.0, 0.0)));
	}
}

#include "Games/Samurai/SamuraiDojo.h"

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
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* const DojoTag = TEXT("SamuraiDojo");
	constexpr double LaneSpacing = 3.6;
	constexpr double TatamiZ = 0.15;
	constexpr double WallZ = -3.6;
	constexpr double LanternY = 4.55;
	constexpr double LanternZ = -1.3;
	/** samuraiStage.tsx: sunPosition [13, 5.6, -3], the sky's sun [.47, .195, -1]. */
	const FVector SunFrom(13.0, 5.6, -3.0);
	const FVector SkySun(0.47, 0.195, -1.0);
}

ASamuraiDojo::ASamuraiDojo()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	Sphere = SphereMesh.Object;

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
	PetalRng.Initialize(0x9e7a1);
}

FVector ASamuraiDojo::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

double ASamuraiDojo::StageWidth() const
{
	return FMath::Max(13.0, Lanes * LaneSpacing + 4.0);
}

void ASamuraiDojo::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetActorTransform(FTransform::Identity);
	Build();
}

void ASamuraiDojo::SetLanes(int32 LaneCount)
{
	LaneCount = FMath::Clamp(LaneCount, 1, 4);
	if (LaneCount != Lanes)
	{
		Lanes = LaneCount;
		Build();
	}
}

void ASamuraiDojo::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), DojoTag);
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	Groups.Reset();
	Lanterns.Reset();
}

UInstancedStaticMeshComponent* ASamuraiDojo::Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d"), *GetNameSafe(Mesh), *GetNameSafe(Material), bShadows ? 1 : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Groups.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("DojoGroup")));
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, Material);
	C->SetupAttachment(RootComponent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(DojoTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	Groups.Add(Key, C);
	return C;
}

void ASamuraiDojo::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad, bool bShadows)
{
	Group(Cube, Material, bShadows)->AddInstance(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(YawRad), 0.0), ToWorld(C), FVector(S.Z, S.X, S.Y)), true);
}

void ASamuraiDojo::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows)
{
	Group(Cylinder, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, 0.0)), FVector(Radius * 2.0, Radius * 2.0, Height)), true);
}

UStaticMeshComponent* ASamuraiDojo::Part(UStaticMesh* Mesh, const FTransform& World, bool bShadows)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("DojoPart")));
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(RootComponent);
	C->SetRelativeTransform(World);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(DojoTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

void ASamuraiDojo::Build()
{
	Clear();
	using MovecadeKart::FMeshBuilder;
	if (!LanternMesh)
	{
		// A chochin: ribbed paper about a black top and bottom.
		{
			FMeshBuilder B;
			TArray<FVector2D> Paper;
			for (int32 i = 0; i <= 14; ++i)
			{
				const double T = UE_DOUBLE_PI * i / 14.0;
				Paper.Add(FVector2D(FMath::Sin(T) * 34.0 + (i > 0 && i < 14 && i % 2 == 0 ? 1.2 : 0.0), -FMath::Cos(T) * 46.0));
			}
			B.Lathe(TEXT("Paper"), Paper, 24, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
			B.Cylinder(TEXT("Cap"), FVector(0, 0, 40), FVector(0, 0, 50), 20.0, 18, FVector2D::ZeroVector);
			B.Cylinder(TEXT("Cap"), FVector(0, 0, -50), FVector(0, 0, -40), 20.0, 18, FVector2D::ZeroVector);
			LanternMesh = B.Build(this, TEXT("DojoLantern"));
		}
		// A layered ridge: a jagged silhouette 120 m wide, facing the camera.
		{
			FMeshBuilder B;
			const int32 S = B.Section(TEXT("Ridge"));
			const int32 Steps = 48;
			for (int32 i = 0; i < Steps; ++i)
			{
				const double Y0 = FMath::Lerp(-6000.0, 6000.0, static_cast<double>(i) / Steps);
				const double Y1 = FMath::Lerp(-6000.0, 6000.0, static_cast<double>(i + 1) / Steps);
				auto Height = [](double Y) { return 600.0 + 520.0 * FMath::Sin(Y * 0.0011) + 260.0 * FMath::Sin(Y * 0.0031 + 1.3) + 120.0 * FMath::Sin(Y * 0.0083 + 0.4); };
				const FVector N(-1, 0, 0);
				const int32 a = B.Vert(S, FVector(0, Y0, -200), N, FVector2D(0, 1), FVector2D::ZeroVector);
				const int32 b = B.Vert(S, FVector(0, Y1, -200), N, FVector2D(1, 1), FVector2D::ZeroVector);
				const int32 c = B.Vert(S, FVector(0, Y1, Height(Y1)), N, FVector2D(1, 0), FVector2D::ZeroVector);
				const int32 d = B.Vert(S, FVector(0, Y0, Height(Y0)), N, FVector2D(0, 0), FVector2D::ZeroVector);
				B.Tri(S, a, b, c);
				B.Tri(S, a, c, d);
			}
			RidgeMesh = B.Build(this, TEXT("DojoRidge"));
		}
		// A garden pine: a trunk and four flat tiers.
		{
			FMeshBuilder B;
			B.Limb(TEXT("Trunk"), FVector(0, 0, 0), FVector(8, 0, 320), 16.0, 9.0, 10, FVector2D::ZeroVector);
			const double Tiers[][3] = { { 150, 120, 26 }, { 210, 100, 24 }, { 265, 78, 22 }, { 312, 54, 22 } };
			for (const auto& T : Tiers)
			{
				B.Lathe(TEXT("Needles"), { { 0, T[0] - T[2] * 0.4 }, { T[1], T[0] - T[2] * 0.2 }, { T[1] * 0.8, T[0] + T[2] * 0.5 }, { 0, T[0] + T[2] } }, 14,
					FVector(T[1] * 0.12, 0, 0), FVector::UpVector, FVector(1.0, 0.8, 1.0), FVector2D::ZeroVector);
			}
			PineMesh = B.Build(this, TEXT("DojoPine"));
		}
		// A stepping stone: a flattened, lumpy disc.
		{
			FMeshBuilder B;
			B.Lathe(TEXT("Stone"), { { 0, -6 }, { 34, -6 }, { 40, 0 }, { 36, 5 }, { 18, 8 }, { 0, 8.5 } }, 12, FVector::ZeroVector, FVector::UpVector, FVector(1.0, 0.78, 1.0), FVector2D::ZeroVector);
			StoneMesh = B.Build(this, TEXT("DojoStone"));
		}
		// The kasagi: the torii's top beam, its ends sweeping up.
		{
			FMeshBuilder B;
			const int32 Steps = 24;
			const int32 S = B.Section(TEXT("Beam"));
			for (int32 i = 0; i < Steps; ++i)
			{
				const double U0 = FMath::Lerp(-1.0, 1.0, static_cast<double>(i) / Steps), U1 = FMath::Lerp(-1.0, 1.0, static_cast<double>(i + 1) / Steps);
				auto Rise = [](double U) { return 34.0 * U * U * U * U; };
				for (int32 Face = 0; Face < 4; ++Face)
				{
					// Front, back, top, bottom of a 30 x 52 section along Y (half length 100: scaled per torii).
					const double H0 = Rise(U0), H1 = Rise(U1);
					FVector P[4];
					FVector N;
					switch (Face)
					{
					case 0: N = FVector(-1, 0, 0); P[0] = FVector(-26, U0 * 100, H0 - 15); P[1] = FVector(-26, U1 * 100, H1 - 15); P[2] = FVector(-26, U1 * 100, H1 + 15); P[3] = FVector(-26, U0 * 100, H0 + 15); break;
					case 1: N = FVector(1, 0, 0); P[0] = FVector(26, U0 * 100, H0 - 15); P[1] = FVector(26, U1 * 100, H1 - 15); P[2] = FVector(26, U1 * 100, H1 + 15); P[3] = FVector(26, U0 * 100, H0 + 15); break;
					case 2: N = FVector(0, 0, 1); P[0] = FVector(-26, U0 * 100, H0 + 15); P[1] = FVector(-26, U1 * 100, H1 + 15); P[2] = FVector(26, U1 * 100, H1 + 15); P[3] = FVector(26, U0 * 100, H0 + 15); break;
					default: N = FVector(0, 0, -1); P[0] = FVector(-26, U0 * 100, H0 - 15); P[1] = FVector(-26, U1 * 100, H1 - 15); P[2] = FVector(26, U1 * 100, H1 - 15); P[3] = FVector(26, U0 * 100, H0 - 15); break;
					}
					const int32 a = B.Vert(S, P[0], N, FVector2D(0, 0), FVector2D::ZeroVector);
					const int32 b = B.Vert(S, P[1], N, FVector2D(1, 0), FVector2D::ZeroVector);
					const int32 c = B.Vert(S, P[2], N, FVector2D(1, 1), FVector2D::ZeroVector);
					const int32 d = B.Vert(S, P[3], N, FVector2D(0, 1), FVector2D::ZeroVector);
					B.Tri(S, a, b, c);
					B.Tri(S, a, c, d);
				}
			}
			KasagiMesh = B.Build(this, TEXT("DojoKasagi"));
		}
	}
	BuildLook();
	BuildGround();
	BuildStage();
	BuildTorii();
	BuildWall();
	BuildGarden();
	BuildDistance();
	BuildLanterns();
}

void ASamuraiDojo::BuildLook()
{
	using namespace MovecadeKart;
	using namespace MovecadeKart::Art;
	// samuraiStage.tsx SAMURAI_STAGE_MOOD: a low warm sun from the right and behind, a rose rim from the left.
	Sun->SetWorldRotation((-Coords::DirToUE(SunFrom).GetSafeNormal()).Rotation());
	Sun->SetLightColor(Hex(TEXT("#ffc27c")));
	Sun->SetIntensity(3.1f);
	Sun->SetCastShadows(true);
	Sun->SetAtmosphereSunLight(false);
	Rim->SetWorldRotation((-Coords::DirToUE(FVector(-9.0, 5.0, -10.0)).GetSafeNormal()).Rotation());
	Rim->SetLightColor(Hex(TEXT("#f08c86")));
	Rim->SetIntensity(1.25f);
	Rim->SetCastShadows(false);
	Rim->SetAtmosphereSunLight(false);
	// The sun is the one forward shading, translucency and fog use.
	Sun->SetForwardShadingPriority(1);
	Rim->SetForwardShadingPriority(0);
	SkyLight->SetIntensity(1.3f);
	SkyLight->SetLightColor(FMath::Lerp(FLinearColor::White, Hex(TEXT("#9dbbe0")), 0.35f));
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(TEXT("#6b6150")) * 0.5f);

	const FLinearColor Top = Hex(TEXT("#3f5f93")), Horizon = Hex(TEXT("#f0b889"));
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
		Sky->SetVectorParameterValue(TEXT("SkyMid"), FMath::Lerp(Horizon, Top, 0.55f));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#6b6150")));
		const FVector SunDir = Coords::DirToUE(SkySun).GetSafeNormal();
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#ffd6ae")));
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.6 * 0.042));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), 0.6);
		Sky->SetScalarParameterValue(TEXT("GlowPower"), 18.0);
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), 0.55);
		Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0);
		if (UTexture* Clouds = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
		}
		Sky->SetScalarParameterValue(TEXT("Clouds"), 0.34);
		Sky->SetScalarParameterValue(TEXT("CloudScale"), 0.35);
		Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(TEXT("#ffd6ae")));
		Sky->SetVectorParameterValue(TEXT("CloudShade"), FMath::Lerp(Horizon, Top, 0.4f) * 0.8f);
		SkyDome->SetMaterial(0, Sky);
	}
	SkyLight->RecaptureSky();
	// Haze from 22 m, in the horizon's peach; the sun side scatters warmer.
	Fog->SetStartDistance(2200.f);
	Fog->SetFogDensity(0.05f);
	Fog->SetFogHeightFalloff(0.02f);
	Fog->SetFogMaxOpacity(0.85f);
	Fog->SetFogCutoffDistance(900000.f);
	Fog->SetFogInscatteringColor(Horizon * 0.9f);
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#ffc27c")) * 0.3f);
	Fog->SetDirectionalInscatteringExponent(6.f);
	// The mood's grade: a warm gain, a cool lift.
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(1.06, 1.0, 0.9, 1.0);
	S.bOverride_ColorOffset = true;
	S.ColorOffset = FVector4(0.0, 0.01 / 3.0, 0.03 / 3.0, 0.0);
}

void ASamuraiDojo::BuildGround()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Earth = Surface(this, { Hex(TEXT("#4a3c2c")), TEXT("gravelly_sand"), 2.0, 0.5, 0.6, 0.4, 0.95, 0.0, 0.0, false, 0.4 });
	UMaterialInterface* Gravel = Surface(this, { Hex(TEXT("#b5aa95")), TEXT("gravelly_sand"), 0.8, 0.6, 0.8, 0.45, 0.92, 0.0, 0.0, false, 0.7 });
	UMaterialInterface* Moss = Surface(this, { Hex(TEXT("#3d5a33")), TEXT("leafy_grass"), 1.2, 0.5, 0.6, 0.4, 0.95, 0.0, 0.0, false, 0.2 });
	Box(Earth, FVector(0.0, -0.24, -6.0), FVector(90.0, 0.2, 40.0), 0.0, false);
	Box(Gravel, FVector(0.0, -0.13, -12.0), FVector(StageWidth() + 6.0, 0.02, 9.0), 0.0, false);
	// Raked lines across the gravel.
	UMaterialInterface* Rake = Surface(this, { Hex(TEXT("#9a907e")), TEXT("gravelly_sand"), 0.8, 0.6, 0.8, 0.45, 0.95, 0.0, 0.0, false, 0.7 });
	for (int32 i = 0; i < 12; ++i)
	{
		Box(Rake, FVector(0.0, -0.118, -8.2 - i * 0.62), FVector(StageWidth() + 5.0, 0.01, 0.07), 0.0, false);
	}
	for (const double Side : { -1.0, 1.0 })
	{
		Box(Moss, FVector(Side * (StageWidth() * 0.5 + 4.0), -0.12, -6.0), FVector(8.0, 0.03, 20.0), 0.0, false);
	}
}

void ASamuraiDojo::BuildStage()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Cedar = Surface(this, { Hex(TEXT("#8a5a3a")), TEXT("dark_wooden_planks"), 1.2, 0.5, 0.65, 0.4, 0.55, 0.0, 0.0, false });
	UMaterialInterface* CedarSide = Surface(this, { Hex(TEXT("#5e3a22")), TEXT("dark_wooden_planks"), 0.8, 0.4, 0.6, 0.4, 0.6 });
	UMaterialInterface* Tatami[] = {
		Surface(this, { Hex(TEXT("#c9b77e")), TEXT("leafy_grass"), 0.45, 0.35, 0.5, 0.3, 0.8, 0.0, 0.0, false, 1.0 }),
		Surface(this, { Hex(TEXT("#bfae74")), TEXT("leafy_grass"), 0.45, 0.35, 0.5, 0.3, 0.8, 0.0, 0.0, false, 1.0 }),
	};
	UMaterialInterface* Binding[] = { Plain(this, Hex(TEXT("#1f3b2b")), 0.6), Plain(this, Hex(TEXT("#2b1d33")), 0.6) };
	const double W = StageWidth();
	// The floor: a raised cedar stage, its front edge toward the camera.
	Box(Cedar, FVector(0.0, -0.075, 0.3), FVector(W, 0.15, 8.0));
	Box(CedarSide, FVector(0.0, -0.18, 4.3), FVector(W + 0.1, 0.14, 0.2));
	for (int32 Lane = 0; Lane < Lanes; ++Lane)
	{
		const double X = (Lane - (Lanes - 1) / 2.0) * LaneSpacing;
		Box(Tatami[Lane % 2], FVector(X, 0.0, TatamiZ), FVector(LaneSpacing - 0.28, 0.04, 7.3));
		Box(Binding[Lane % 2], FVector(X - 1.66, 0.022, TatamiZ), FVector(0.07, 0.012, 7.3), 0.0, false);
		Box(Binding[Lane % 2], FVector(X + 1.66, 0.022, TatamiZ), FVector(0.07, 0.012, 7.3), 0.0, false);
		Box(Binding[Lane % 2], FVector(X, 0.022, TatamiZ - 3.61), FVector(LaneSpacing - 0.28, 0.012, 0.07), 0.0, false);
		Box(Binding[Lane % 2], FVector(X, 0.022, TatamiZ + 3.61), FVector(LaneSpacing - 0.28, 0.012, 0.07), 0.0, false);
	}
}

void ASamuraiDojo::BuildTorii()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Vermilion = Plain(this, Hex(TEXT("#d23c2a")), 0.42);
	UMaterialInterface* Lacquer = Plain(this, Hex(TEXT("#16120f")), 0.3, 0.1);
	const double Half = Lanes * 1.8 + 0.55;
	for (const double Side : { -1.0, 1.0 })
	{
		Post(Vermilion, FVector(Side * Half, 0.0, 0.0), 0.17, 4.2);
		Post(Lacquer, FVector(Side * Half, 0.0, 0.0), 0.2, 0.32);
	}
	// Shimaki and nuki (the beams), the kasagi on top with its upswept ends, the tablet between.
	Box(Vermilion, FVector(0.0, 4.02, 0.0), FVector(Half * 2.0 + 0.9, 0.24, 0.42));
	Box(Vermilion, FVector(0.0, 3.3, 0.0), FVector((Half * 2.0 + 0.9) * 0.88, 0.2, 0.32));
	UStaticMeshComponent* Kasagi = Part(KasagiMesh, FTransform(FRotator::ZeroRotator, ToWorld(FVector(0.0, 4.36, 0.0)), FVector(1.0, (Half + 0.85), 1.0)));
	Kasagi->SetMaterial(0, Lacquer);
	Box(Vermilion, FVector(0.0, 3.66, 0.0), FVector(0.22, 0.52, 0.26));
	Box(Lacquer, FVector(0.0, 3.68, 0.14), FVector(0.5, 0.42, 0.04));
}

void ASamuraiDojo::BuildWall()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Cedar = Surface(this, { Hex(TEXT("#6e4428")), TEXT("dark_wooden_planks"), 0.9, 0.45, 0.6, 0.4, 0.6 });
	UMaterialInterface* Paper = Plain(this, Hex(TEXT("#f3e6c8")), 0.9, 0.0, 0.18);
	UMaterialInterface* Kumiko = Plain(this, Hex(TEXT("#3b2414")), 0.6);
	const double W = StageWidth();
	// The beam over the wall, and posts every 2.4 m that leave the middle open onto the garden.
	Box(Cedar, FVector(0.0, 5.55, WallZ), FVector(W + 1.0, 0.34, 0.36));
	Box(Cedar, FVector(0.0, 0.25, WallZ), FVector(W + 1.0, 0.18, 0.3));
	const int32 Bays = FMath::Max(2, FMath::RoundToInt32(W / 2.4) / 2) * 2;
	const double Bay = W / Bays;
	for (int32 i = 0; i <= Bays; ++i)
	{
		const double X = -W / 2.0 + i * Bay;
		if (FMath::Abs(X) < Bay * 0.6)
		{
			continue;
		}
		Box(Cedar, FVector(X, 2.75, WallZ), FVector(0.2, 5.5, 0.22));
	}
	// Shoji between the posts (not across the open middle): glowing paper behind a kumiko lattice.
	for (int32 i = 0; i < Bays; ++i)
	{
		const double X = -W / 2.0 + (i + 0.5) * Bay;
		if (FMath::Abs(X) < Bay)
		{
			continue;
		}
		Box(Paper, FVector(X, 2.3, WallZ + 0.05), FVector(Bay - 0.24, 3.6, 0.02), 0.0, false);
		for (int32 r = 0; r <= 6; ++r)
		{
			Box(Kumiko, FVector(X, 0.5 + r * 0.6, WallZ + 0.08), FVector(Bay - 0.24, 0.035, 0.03), 0.0, false);
		}
		for (int32 c = 1; c < 3; ++c)
		{
			Box(Kumiko, FVector(X - (Bay - 0.24) / 2.0 + c * (Bay - 0.24) / 3.0, 2.3, WallZ + 0.08), FVector(0.035, 3.6, 0.03), 0.0, false);
		}
	}
}

void ASamuraiDojo::BuildGarden()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Stone = Surface(this, { Hex(TEXT("#7d7a72")), TEXT("rock_face"), 0.6, 0.6, 0.9, 0.45, 0.9 });
	UMaterialInterface* StoneDark = Surface(this, { Hex(TEXT("#5d5a54")), TEXT("rock_face"), 0.5, 0.6, 0.9, 0.45, 0.9 });
	UMaterialInterface* Glow = MovecadeKart::Art::Glow(this, Hex(TEXT("#ffb35c")) * 3.f);
	UMaterialInterface* Bamboo = Plain(this, Hex(TEXT("#5fa357")), 0.45);
	UMaterialInterface* BambooNode = Plain(this, Hex(TEXT("#3f7a3a")), 0.5);
	UMaterialInterface* Leaf = Plain(this, Hex(TEXT("#4b8f44")), 0.6);
	const double W = StageWidth();
	// Stepping stones through the gravel, both sides of the open middle.
	for (const double Side : { -1.0, 1.0 })
	{
		for (int32 i = 0; i < 3; ++i)
		{
			UStaticMeshComponent* S = Part(StoneMesh, FTransform(FRotator(0.0, i * 37.0, 0.0), ToWorld(FVector(Side * (1.6 + i * 0.18), -0.03, -5.2 - i * 2.1)), FVector(1.0 - i * 0.08)));
			S->SetMaterial(0, Stone);
		}
		// A stone lantern (toro) either side: base, shaft, the lit fire box, the roof and its jewel.
		const FVector T(Side * 2.35, -0.13, -7.4);
		Post(StoneDark, T, 0.42, 0.22);
		Post(Stone, T + FVector(0.0, 0.22, 0.0), 0.16, 0.9);
		Box(Stone, T + FVector(0.0, 1.2, 0.0), FVector(0.66, 0.12, 0.66));
		Box(Stone, T + FVector(0.0, 1.5, 0.0), FVector(0.5, 0.46, 0.5));
		Box(Glow, T + FVector(0.0, 1.5, 0.255), FVector(0.24, 0.26, 0.01), 0.0, false);
		Box(StoneDark, T + FVector(0.0, 1.82, 0.0), FVector(0.92, 0.14, 0.92), UE_DOUBLE_PI / 4.0);
		Box(StoneDark, T + FVector(0.0, 1.94, 0.0), FVector(0.5, 0.12, 0.5), UE_DOUBLE_PI / 4.0);
		Post(Stone, T + FVector(0.0, 2.0, 0.0), 0.08, 0.16);
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("ToroLight")));
			L->SetupAttachment(RootComponent);
			L->SetMobility(EComponentMobility::Movable);
			L->SetRelativeLocation(ToWorld(T + FVector(0.0, 1.5, 0.4)));
			L->SetIntensityUnits(ELightUnits::Lumens);
			L->SetIntensity(400.f);
			L->SetAttenuationRadius(350.f);
			L->SetLightColor(Hex(TEXT("#ffb35c")));
			L->SetCastShadows(false);
			L->ComponentTags.Add(DojoTag);
			AddInstanceComponent(L);
			L->RegisterComponent();
		}
		// Bamboo at the stage's edges, in front of the wall.
		const double BX = Side * (W / 2.0 - 0.25);
		for (int32 i = 0; i < 5; ++i)
		{
			const double X = BX - Side * i * 0.16;
			const double H = 3.2 + (i % 3) * 0.6;
			const double Z = -1.75 + (i % 2) * 0.22;
			Post(Bamboo, FVector(X, 0.0, Z), 0.055, H);
			for (double Y = 0.7; Y < H; Y += 0.72)
			{
				Post(BambooNode, FVector(X, Y, Z), 0.064, 0.04, false);
			}
			for (int32 k = 0; k < 3; ++k)
			{
				const double Y = H - 0.3 - k * 0.5;
				Box(Leaf, FVector(X - Side * 0.25, Y, Z + 0.05), FVector(0.5, 0.025, 0.09), Side * (0.5 + k * 0.4), false);
			}
		}
		// Garden pines behind the wall.
		for (int32 i = 0; i < 2; ++i)
		{
			UStaticMeshComponent* P = Part(PineMesh, FTransform(FRotator(0.0, i * 70.0 + (Side > 0 ? 20.0 : 0.0), 0.0), ToWorld(FVector(Side * (W * 0.37 + i * 2.2), -0.13, -5.2 - i * 3.0)), FVector(1.15 - i * 0.2)));
			P->SetMaterial(0, Plain(this, Hex(TEXT("#4a3426")), 0.85));
			P->SetMaterial(1, Plain(this, Hex(TEXT("#2f4a32")), 0.85));
		}
	}
}

void ASamuraiDojo::BuildDistance()
{
	using namespace MovecadeKart::Art;
	// Ridges fading into the haze: dusky blue to violet, each farther and taller.
	const TCHAR* const Colors[] = { TEXT("#3a4a5e"), TEXT("#4d5873"), TEXT("#6a6a88") };
	for (int32 i = 0; i < 3; ++i)
	{
		const double Z = -32.0 - i * 18.0;
		UStaticMeshComponent* R = Part(RidgeMesh, FTransform(FRotator(0.0, 0.0, 0.0), ToWorld(FVector(i * 9.0 - 9.0, -0.5, Z)), FVector(1.0, 1.0 + i * 0.25, 0.75 + i * 0.35)), false);
		R->SetMaterial(0, Plain(this, Hex(Colors[i]), 0.95));
	}
}

void ASamuraiDojo::BuildLanterns()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Paper = Plain(this, Hex(TEXT("#e23d33")), 0.8, 0.0, 0.9);
	UMaterialInterface* Cap = Plain(this, Hex(TEXT("#141110")), 0.4);
	UMaterialInterface* Cord = Plain(this, Hex(TEXT("#141110")), 0.8);
	const double W = StageWidth();
	const double Xs[] = { -W * 0.3, 0.0, W * 0.3 };
	for (int32 i = 0; i < 3; ++i)
	{
		USceneComponent* Pivot = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("LanternPivot")));
		Pivot->SetupAttachment(RootComponent);
		Pivot->SetMobility(EComponentMobility::Movable);
		Pivot->SetRelativeLocation(ToWorld(FVector(Xs[i], 5.55, LanternZ)));
		Pivot->ComponentTags.Add(DojoTag);
		AddInstanceComponent(Pivot);
		Pivot->RegisterComponent();
		Lanterns.Add(Pivot);
		UStaticMeshComponent* L = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("Lantern")));
		L->SetStaticMesh(LanternMesh);
		L->SetMaterial(0, Paper);
		L->SetMaterial(1, Cap);
		L->SetupAttachment(Pivot);
		L->SetRelativeLocation(FVector(0.0, 0.0, -(5.55 - LanternY) * 100.0));
		L->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		L->ComponentTags.Add(DojoTag);
		AddInstanceComponent(L);
		L->RegisterComponent();
		UStaticMeshComponent* Line = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("LanternCord")));
		Line->SetStaticMesh(Cylinder);
		Line->SetMaterial(0, Cord);
		Line->SetupAttachment(Pivot);
		Line->SetRelativeLocation(FVector(0.0, 0.0, -25.0));
		Line->SetRelativeScale3D(FVector(0.015, 0.015, 0.5));
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->ComponentTags.Add(DojoTag);
		AddInstanceComponent(Line);
		Line->RegisterComponent();
		UPointLightComponent* Glow = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("LanternLight")));
		Glow->SetupAttachment(L);
		Glow->SetMobility(EComponentMobility::Movable);
		Glow->SetIntensityUnits(ELightUnits::Lumens);
		Glow->SetIntensity(900.f);
		Glow->SetAttenuationRadius(520.f);
		Glow->SetLightColor(Hex(TEXT("#ff9a5a")));
		Glow->SetCastShadows(false);
		Glow->ComponentTags.Add(DojoTag);
		AddInstanceComponent(Glow);
		Glow->RegisterComponent();
	}
}

void ASamuraiDojo::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	// The lanterns sway on their cords, each on its own beat.
	for (int32 i = 0; i < Lanterns.Num(); ++i)
	{
		if (Lanterns[i])
		{
			const double Swing = FMath::Sin(Clock * 0.9 + i * 1.7) * 4.0 + FMath::Sin(Clock * 2.3 + i) * 0.8;
			Lanterns[i]->SetRelativeRotation(FRotator(0.0, 0.0, Swing));
		}
	}
	// Petals drift across the stage.
	if (UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this))
	{
		PetalCarry += DeltaSeconds * 7.0;
		TArray<MovecadeKart::Fx::FKartFxEmit> Out;
		while (PetalCarry >= 1.0)
		{
			PetalCarry -= 1.0;
			MovecadeKart::Fx::FKartFxEmit E;
			E.Field = MovecadeKart::Fx::EField::Confetti;
			const double W = StageWidth();
			E.Position = ToWorld(FVector(PetalRng.FRandRange(-W * 0.6, W * 0.6), PetalRng.FRandRange(4.5, 6.0), PetalRng.FRandRange(-3.0, 3.0)));
			E.Count = 1;
			E.Direction = ToWorld(FVector(-0.6, -0.2, 0.3)).GetSafeNormal();
			E.Spread = 0.6;
			E.Speed = FVector2D(0.3, 0.8);
			E.Life = FVector2D(3.5, 5.0);
			E.Size = FVector2D(0.035, 0.05);
			E.SizeEnd = 0.9;
			E.Color = MovecadeKart::Art::Hex(TEXT("#f6b6c8"));
			E.ColorEnd = MovecadeKart::Art::Hex(TEXT("#f8d3dd"));
			Out.Add(E);
		}
		if (Out.Num() > 0)
		{
			Fx->Emit(Out);
		}
	}
}

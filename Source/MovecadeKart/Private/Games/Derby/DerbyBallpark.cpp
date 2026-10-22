#include "Games/Derby/DerbyBallpark.h"

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
#include "Games/Derby/DerbyRules.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* const ParkTag = TEXT("DerbyBallpark");
	constexpr int32 CrowdSections = 12;
	const TCHAR* const Shirts[] = { TEXT("#d8433b"), TEXT("#f2f0ea"), TEXT("#2f5fc4"), TEXT("#f2b23f"), TEXT("#1f2a44"), TEXT("#3fc9c2") };
	const TCHAR* const Skins[] = { TEXT("#e9b48f"), TEXT("#8a5a3c") };

	double FenceAt(double SprayDeg)
	{
		return MovecadeKart::Derby::FenceDistanceM(SprayDeg);
	}

	/** A fan: a rounded body and a head, slot 0 the shirt, slot 1 the skin (cm, standing on the origin). */
	UStaticMesh* BuildFan(UObject* Outer)
	{
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Shirt"));
		B.Section(TEXT("Skin"));
		B.Ellipsoid(TEXT("Shirt"), FVector(0.0, 0.0, 62.0), FVector(17.0, 22.0, 36.0), 6, 8, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Skin"), FVector(0.0, 0.0, 112.0), FVector(12.0, 12.0, 14.0), 5, 8, FVector2D::ZeroVector);
		// Arms up in a V on half of them would need a second mesh; a raised fist reads from the field.
		B.Limb(TEXT("Shirt"), FVector(0.0, 18.0, 82.0), FVector(4.0, 30.0, 128.0), 5.0, 4.5, 6, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DerbyFan"));
	}
}

ADerbyBallpark::ADerbyBallpark()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	Sphere = SphereMesh.Object;
	Floods = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Floods"));
	Floods->SetupAttachment(RootComponent);
	Floods->SetMobility(EComponentMobility::Movable);
	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(RootComponent);
	Moon->SetMobility(EComponentMobility::Movable);
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
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

FVector ADerbyBallpark::Ground(double SprayDeg, double DistanceM, double HeightM)
{
	const double A = FMath::DegreesToRadians(SprayDeg);
	return M(FVector(FMath::Cos(A) * DistanceM, FMath::Sin(A) * DistanceM, HeightM));
}

void ADerbyBallpark::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetActorTransform(FTransform::Identity);
	Build();
}

void ADerbyBallpark::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), ParkTag);
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	Groups.Reset();
	Crowd.Reset();
	BoardLines.Reset();
	BoardAccent = nullptr;
}

USceneComponent* ADerbyBallpark::Pivot(const TCHAR* Name)
{
	USceneComponent* P = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
	P->SetupAttachment(RootComponent);
	P->SetMobility(EComponentMobility::Movable);
	P->ComponentTags.Add(ParkTag);
	AddInstanceComponent(P);
	P->RegisterComponent();
	return P;
}

UInstancedStaticMeshComponent* ADerbyBallpark::Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows, USceneComponent* Parent, UMaterialInterface* Material2)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%s|%d|%s"), *GetNameSafe(Mesh), *GetNameSafe(Material), *GetNameSafe(Material2), bShadows ? 1 : 0, *GetNameSafe(Parent));
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Groups.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("ParkGroup")));
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, Material);
	if (Material2)
	{
		C->SetMaterial(1, Material2);
	}
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(ParkTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	Groups.Add(Key, C);
	return C;
}

void ADerbyBallpark::Box(UMaterialInterface* Material, const FVector& CentreM, const FVector& SizeM, double YawDeg, bool bShadows, USceneComponent* Parent)
{
	Group(Cube, Material, bShadows, Parent)->AddInstance(FTransform(FRotator(0.0, YawDeg, 0.0), M(CentreM), SizeM), false);
}

void ADerbyBallpark::Disc(UMaterialInterface* Material, const FVector& CentreM, double RadiusM, double HeightM, bool bShadows)
{
	Group(Cylinder, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, M(CentreM + FVector(0.0, 0.0, HeightM / 2.0)), FVector(RadiusM * 2.0, RadiusM * 2.0, HeightM)), false);
}

UTextRenderComponent* ADerbyBallpark::Text(const FString& String, const FVector& AtM, double YawDeg, float WorldSizeCm, const FLinearColor& Color)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this, MakeUniqueObjectName(this, UTextRenderComponent::StaticClass(), TEXT("ParkText")));
	T->SetupAttachment(RootComponent);
	T->SetRelativeLocation(M(AtM));
	T->SetRelativeRotation(FRotator(0.0, YawDeg, 0.0));
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetWorldSize(WorldSizeCm);
	T->SetTextRenderColor(Color.ToFColor(true));
	T->SetText(FText::FromString(String));
	T->SetCastShadow(false);
	T->ComponentTags.Add(ParkTag);
	AddInstanceComponent(T);
	T->RegisterComponent();
	return T;
}

void ADerbyBallpark::Build()
{
	Clear();
	BuildLook();
	BuildSky();
	BuildField();
	BuildWall();
	BuildStands();
	BuildTowers();
	BuildBoard();
}

void ADerbyBallpark::BuildLook()
{
	using namespace MovecadeKart::Art;
	// The floods: one warm key from high behind the plate, as the towers add up to; a cold moon fill from the other side.
	Floods->SetWorldRotation(FRotator(-52.0, 18.0, 0.0));
	Floods->SetLightColor(Hex(TEXT("#fff0da")));
	Floods->SetIntensity(3.2f);
	Floods->SetCastShadows(true);
	Floods->SetAtmosphereSunLight(false);
	Floods->SetForwardShadingPriority(1);
	Moon->SetWorldRotation(FRotator(-35.0, -150.0, 0.0));
	Moon->SetLightColor(Hex(TEXT("#7f9cff")));
	Moon->SetIntensity(0.7f);
	Moon->SetCastShadows(false);
	Moon->SetAtmosphereSunLight(false);
	Moon->SetForwardShadingPriority(0);
	SkyLight->bRealTimeCapture = false;
	SkyLight->SetIntensity(0.9f);
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(TEXT("#1d2a1c")));
	Fog->SetStartDistance(9000.f);
	Fog->SetFogDensity(0.035f);
	Fog->SetFogHeightFalloff(0.06f);
	Fog->SetFogMaxOpacity(0.6f);
	Fog->SetFogInscatteringColor(Hex(TEXT("#16224a")));
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#ffd9a8")) * 0.15f);
	Fog->SetDirectionalInscatteringExponent(8.f);
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = 0.6f;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = 1.4f;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 1.1f;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = 1.0f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.45f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.06f, 1.06f, 1.06f, 1.f);
}

void ADerbyBallpark::BuildSky()
{
	using namespace MovecadeKart::Art;
	const FLinearColor Top = Hex(TEXT("#03050c")), Horizon = Hex(TEXT("#1c2a55"));
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
		Sky->SetVectorParameterValue(TEXT("SkyMid"), FMath::Lerp(Horizon, Top, 0.6f));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#05070c")));
		const FVector MoonDir = FVector(-0.45, 0.62, 0.42).GetSafeNormal();
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(MoonDir.X, MoonDir.Y, MoonDir.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#dfe7ff")));
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.022f));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), 0.25f);
		Sky->SetScalarParameterValue(TEXT("GlowPower"), 24.0f);
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), 0.8f);
		Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0f);
		if (UTexture* Clouds = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
		}
		Sky->SetScalarParameterValue(TEXT("Clouds"), 0.18f);
		Sky->SetScalarParameterValue(TEXT("CloudScale"), 0.3f);
		Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(TEXT("#2c3a66")));
		Sky->SetVectorParameterValue(TEXT("CloudShade"), Hex(TEXT("#0a0f22")));
		SkyDome->SetMaterial(0, Sky);
	}
	SkyLight->RecaptureSky();
	// Stars on the upper dome, deterministic.
	FRandomStream R(0xba5e);
	UMaterialInterface* Star = Glow(this, FLinearColor(1.f, 0.97f, 0.9f) * 6.f);
	UInstancedStaticMeshComponent* Stars = Group(Sphere, Star, false);
	for (int32 i = 0; i < 420; ++i)
	{
		const double Yaw = R.FRandRange(0.0, 2.0 * PI);
		const double Elev = FMath::Asin(R.FRandRange(0.12f, 0.98f));
		const FVector Dir(FMath::Cos(Elev) * FMath::Cos(Yaw), FMath::Cos(Elev) * FMath::Sin(Yaw), FMath::Sin(Elev));
		const double Size = R.FRandRange(1.4f, 3.6f) * (R.FRand() < 0.08f ? 2.0 : 1.0);
		Stars->AddInstance(FTransform(FRotator::ZeroRotator, M(Dir * 1800.0), FVector(Size)), false);
	}
	// The moon.
	Group(Sphere, Glow(this, Hex(TEXT("#f4f1e6")) * 4.f), false)->AddInstance(FTransform(FRotator::ZeroRotator, M(FVector(-0.45, 0.62, 0.42).GetSafeNormal() * 1700.0), FVector(70.0)), false);
}

void ADerbyBallpark::BuildField()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Grass = Surface(this, { Hex(TEXT("#2f6b2a")), TEXT("leafy_grass"), 4.0, 0.25, 0.5, 0.4, 0.9, 0.0, 0.0, false, 0.6 });
	UMaterialInterface* GrassLight = Surface(this, { Hex(TEXT("#3d8033")), TEXT("leafy_grass"), 4.0, 0.25, 0.5, 0.4, 0.9, 0.0, 0.0, false, 0.6 });
	UMaterialInterface* Dirt = Surface(this, { Hex(TEXT("#a8673b")), TEXT("gravelly_sand"), 2.0, 0.3, 0.5, 0.4, 0.95, 0.0, 0.0, false, 0.3 });
	UMaterialInterface* Track = Surface(this, { Hex(TEXT("#8a5332")), TEXT("gravelly_sand"), 2.0, 0.3, 0.5, 0.4, 0.95, 0.0, 0.0, false, 0.3 });
	UMaterialInterface* Concourse = Surface(this, { Hex(TEXT("#3a3d46")), TEXT("painted_concrete"), 4.0, 0.2, 0.5, 0.4, 0.9 });
	UMaterialInterface* Chalk = Plain(this, Hex(TEXT("#f4f4ee")), 0.7, 0.0, 0.15);
	UMaterialInterface* Rubber = Plain(this, Hex(TEXT("#f4f4ee")), 0.5);

	// The ground beyond, the outfield lawn (dark), its mown stripes, the infield skin, its grass, the circles.
	Disc(Concourse, FVector(0.0, 0.0, -0.3), 420.0, 0.2);
	Disc(Grass, FVector(0.0, 0.0, -0.1), 128.0, 0.1);
	const double StripeW = 6.0;
	for (int32 k = -17; k <= 17; ++k)
	{
		if (k % 2 == 0)
		{
			continue;
		}
		const double Y = k * StripeW;
		// From the foul line (or the plate) to the warning track: the fence along this lateral.
		double X = FMath::Max(FMath::Abs(Y), 0.0);
		double End = X;
		for (double Probe = X; Probe < 130.0; Probe += 0.5)
		{
			const double Spray = FMath::RadiansToDegrees(FMath::Atan2(Y, Probe));
			if (FMath::Sqrt(Probe * Probe + Y * Y) >= FenceAt(Spray) - 4.6)
			{
				break;
			}
			End = Probe;
		}
		const double Start = FMath::Max(X + 1.0, 4.0);
		if (End > Start + 1.0)
		{
			Box(GrassLight, FVector((Start + End) / 2.0, Y, 0.0), FVector(End - Start, StripeW, 0.01));
		}
	}
	// The warning track along the wall.
	for (double A = -46.0; A < 46.0; A += 1.5)
	{
		const double Mid = A + 0.75;
		const double R = FenceAt(Mid) - 2.3;
		Box(Track, FVector(FMath::Cos(FMath::DegreesToRadians(Mid)) * R, FMath::Sin(FMath::DegreesToRadians(Mid)) * R, 0.012),
			FVector(4.6, R * FMath::DegreesToRadians(1.6), 0.02), Mid);
	}
	Disc(Dirt, FVector(17.0, 0.0, 0.0), 29.0, 0.03);
	Box(Grass, FVector(19.4, 0.0, 0.0), FVector(23.5, 23.5, 0.07), 45.0);
	Disc(Dirt, FVector(0.0, 0.0, 0.0), 4.2, 0.09);
	Disc(Dirt, FVector(MoundX, 0.0, 0.0), 2.75, 0.25);
	Disc(Dirt, FVector(MoundX, 0.0, 0.0), 1.8, 0.33);
	Box(Rubber, FVector(MoundX, 0.0, 0.34), FVector(0.15, 0.6, 0.04));
	// Base paths: dirt along the lines between the bags.
	const FVector Bases[] = { FVector(0.0, 0.0, 0.0), FVector(19.40, 19.40, 0.0), FVector(38.79, 0.0, 0.0), FVector(19.40, -19.40, 0.0) };
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector A = Bases[i], B = Bases[(i + 1) % 4];
		const FVector D = B - A;
		Box(Dirt, (A + B) / 2.0 + FVector(0.0, 0.0, 0.08), FVector(D.Size(), 1.9, 0.02), FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)));
	}
	for (int32 i = 1; i < 4; ++i)
	{
		Disc(Dirt, Bases[i], 2.2, 0.095);
		Box(Rubber, Bases[i] + FVector(0.0, 0.0, 0.12), FVector(0.38, 0.38, 0.08), 45.0, true);
	}
	// Home plate, the batter's boxes, the foul lines out to the poles.
	Box(Rubber, FVector(0.22, 0.0, 0.1), FVector(0.43, 0.43, 0.02));
	for (const double Side : { -1.0, 1.0 })
	{
		const double Cy = Side * 0.95;
		Box(Chalk, FVector(0.25, Cy - 0.61, 0.101), FVector(1.83, 0.06, 0.01));
		Box(Chalk, FVector(0.25, Cy + 0.61, 0.101), FVector(1.83, 0.06, 0.01));
		Box(Chalk, FVector(0.25 - 0.915, Cy, 0.101), FVector(0.06, 1.22, 0.01));
		Box(Chalk, FVector(0.25 + 0.915, Cy, 0.101), FVector(0.06, 1.22, 0.01));
		const double L = FenceAt(45.0);
		const double A = FMath::DegreesToRadians(Side * 45.0);
		Box(Chalk, FVector(FMath::Cos(A) * (L / 2.0 + 1.0), FMath::Sin(A) * (L / 2.0 + 1.0), 0.1), FVector(L - 2.0, 0.12, 0.01), Side * 45.0);
	}
	// The backstop behind home and the low wall along the foul lines.
	UMaterialInterface* Pad = Plain(this, Hex(TEXT("#16331f")), 0.7);
	for (double A = 120.0; A <= 240.0; A += 4.0)
	{
		const double R = 18.0;
		Box(Pad, FVector(FMath::Cos(FMath::DegreesToRadians(A + 2.0)) * R, FMath::Sin(FMath::DegreesToRadians(A + 2.0)) * R, 1.0), FVector(0.4, R * FMath::DegreesToRadians(4.1), 2.0), A + 2.0);
	}
}

void ADerbyBallpark::BuildWall()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Pad = Plain(this, Hex(TEXT("#1b4a2c")), 0.75);
	UMaterialInterface* Cap = Plain(this, Hex(TEXT("#f2c230")), 0.5, 0.0, 0.25);
	UMaterialInterface* Pole = Plain(this, Hex(TEXT("#f2c230")), 0.4, 0.2, 0.6);
	UMaterialInterface* Eye = Plain(this, Hex(TEXT("#0f2318")), 0.9);
	using MovecadeKart::Derby::FenceHeightM;
	for (double A = -45.0; A < 45.0; A += 1.5)
	{
		const double Mid = A + 0.75;
		const double R = FenceAt(Mid) + 0.2;
		const double Rad = FMath::DegreesToRadians(Mid);
		const double Len = R * FMath::DegreesToRadians(1.56);
		Box(Pad, FVector(FMath::Cos(Rad) * R, FMath::Sin(Rad) * R, FenceHeightM / 2.0), FVector(0.4, Len, FenceHeightM), Mid, true);
		Box(Cap, FVector(FMath::Cos(Rad) * (R - 0.05), FMath::Sin(Rad) * (R - 0.05), FenceHeightM + 0.06), FVector(0.5, Len, 0.12), Mid);
	}
	for (const double Side : { -1.0, 1.0 })
	{
		const FVector Base = Ground(Side * 45.0, FenceAt(45.0) + 0.3) / 100.0;
		Group(Cylinder, Pole, true)->AddInstance(FTransform(FRotator::ZeroRotator, M(Base + FVector(0.0, 0.0, 12.5)), FVector(0.5, 0.5, 25.0)), false);
		// The screen on the pole's fair side.
		Box(Pole, Base + FVector(-0.3, -Side * 0.7, 14.0), FVector(0.05, 1.2, 18.0), Side * 45.0);
	}
	// The batter's eye in dead centre.
	for (double A = -9.0; A < 9.0; A += 3.0)
	{
		const double Mid = A + 1.5;
		const double R = FenceAt(Mid) + 4.0;
		const double Rad = FMath::DegreesToRadians(Mid);
		Box(Eye, FVector(FMath::Cos(Rad) * R, FMath::Sin(Rad) * R, 5.0), FVector(1.0, R * FMath::DegreesToRadians(3.1), 10.0), Mid);
	}
	// Distance markers and the sponsors' panels on the wall, facing home.
	const FLinearColor Marker = Hex(TEXT("#f4f4ee"));
	const double Marks[] = { -45.0, -22.5, 0.0, 22.5, 45.0 };
	for (const double Mark : Marks)
	{
		const double Ft = FMath::RoundToDouble(FenceAt(Mark) / MovecadeKart::Derby::MetresPerFoot / 5.0) * 5.0;
		// The line markers sit a little inside the poles.
		const double At = FMath::Abs(Mark) > 44.0 ? Mark - FMath::Sign(Mark) * 2.0 : Mark;
		Text(FString::Printf(TEXT("%d"), static_cast<int32>(Ft)), Ground(At, FenceAt(At) - 0.05) / 100.0 + FVector(0.0, 0.0, 1.6), At + 180.0, 150.f, Marker);
	}
	const TCHAR* const Ads[] = { TEXT("MOVECADE"), TEXT("SWING!"), TEXT("MOVECADE"), TEXT("PARTY MODE"), TEXT("MOVECADE"), TEXT("GO DEEP") };
	const FLinearColor AdColors[] = { Hex(TEXT("#f2b23f")), Hex(TEXT("#3fc9c2")), Hex(TEXT("#ff6b5a")), Hex(TEXT("#a083ff")), Hex(TEXT("#f2b23f")), Hex(TEXT("#3fc9c2")) };
	const double AdAt[] = { -37.0, -30.0, -12.0, 12.0, 30.0, 37.0 };
	for (int32 i = 0; i < 6; ++i)
	{
		const double R = FenceAt(AdAt[i]) - 0.05;
		Text(Ads[i], Ground(AdAt[i], R) / 100.0 + FVector(0.0, 0.0, 1.4), AdAt[i] + 180.0, 110.f, AdColors[i]);
	}
}

void ADerbyBallpark::BuildStands()
{
	using namespace MovecadeKart::Art;
	FanMesh = BuildFan(this);
	UMaterialInterface* Riser = Surface(this, { Hex(TEXT("#4b505c")), TEXT("painted_concrete"), 3.0, 0.2, 0.5, 0.4, 0.9 });
	UMaterialInterface* Seat = Plain(this, Hex(TEXT("#24408e")), 0.6);
	UMaterialInterface* Facade = Plain(this, Hex(TEXT("#22252e")), 0.8);
	UMaterialInterface* Trim = Glow(this, Hex(TEXT("#3fc9c2")) * 2.4f);
	TArray<UMaterialInterface*> ShirtMats, SkinMats;
	for (const TCHAR* S : Shirts)
	{
		ShirtMats.Add(Plain(this, Hex(S), 0.7));
	}
	for (const TCHAR* S : Skins)
	{
		SkinMats.Add(Plain(this, Hex(S), 0.6));
	}
	TArray<UInstancedStaticMeshComponent*> Sections;
	for (int32 i = 0; i < CrowdSections; ++i)
	{
		USceneComponent* P = Pivot(TEXT("CrowdSection"));
		Crowd.Add(P);
		Sections.Add(Group(FanMesh, ShirtMats[i % ShirtMats.Num()], false, P, SkinMats[(i / ShirtMats.Num()) % SkinMats.Num()]));
	}
	FRandomStream R(0xfa25);
	auto AddFan = [&](const FVector& FeetM, double YawDeg)
	{
		if (R.FRand() < 0.18f)
		{
			return;
		}
		const int32 S = R.RandRange(0, CrowdSections - 1);
		const double Scale = R.FRandRange(0.85f, 1.05f);
		const double Yaw = YawDeg + R.FRandRange(-25.f, 25.f);
		Sections[S]->AddInstance(FTransform(FRotator(0.0, Yaw, 0.0), M(FeetM), FVector(Scale)), false);
	};
	// Bleachers beyond the wall, arcs of risers stepping up and back, the batter's eye left clear in centre.
	const int32 Rows = 16;
	for (double A = -50.0; A < 50.0; A += 2.5)
	{
		const double Mid = A + 1.25;
		if (FMath::Abs(Mid) < 10.0)
		{
			continue;
		}
		const double Rad = FMath::DegreesToRadians(Mid);
		const FVector Dir(FMath::Cos(Rad), FMath::Sin(Rad), 0.0);
		const double Base = FenceAt(FMath::Clamp(Mid, -45.0, 45.0)) + 6.0;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const double Rr = Base + Row * 1.1;
			const double Z = 1.6 + Row * 0.62;
			const double Len = Rr * FMath::DegreesToRadians(2.56);
			Box(Riser, Dir * (Rr + 0.55) + FVector(0.0, 0.0, Z / 2.0), FVector(1.1, Len, Z), Mid);
			Box(Seat, Dir * (Rr + 0.75) + FVector(0.0, 0.0, Z + 0.2), FVector(0.4, Len * 0.96, 0.4), Mid);
			const int32 Count = FMath::FloorToInt32(Len / 0.75);
			for (int32 k = 0; k < Count; ++k)
			{
				const double Lat = (k + 0.5) / Count - 0.5;
				const double Ar = Rad + Lat * FMath::DegreesToRadians(2.5);
				AddFan(FVector(FMath::Cos(Ar), FMath::Sin(Ar), 0.0) * (Rr + 0.45) + FVector(0.0, 0.0, Z), Mid + 180.0);
			}
		}
		// The back wall and its neon trim.
		const double Back = Base + Rows * 1.1 + 0.6;
		const double Top = 1.6 + Rows * 0.62;
		Box(Facade, Dir * Back + FVector(0.0, 0.0, (Top + 3.0) / 2.0), FVector(1.0, Back * FMath::DegreesToRadians(2.56), Top + 3.0), Mid);
		Box(Trim, Dir * (Back - 0.55) + FVector(0.0, 0.0, Top + 2.4), FVector(0.1, Back * FMath::DegreesToRadians(2.56), 0.25), Mid);
	}
	// The grandstand round home, from first base's side behind the plate to third's.
	for (double A = 52.0; A < 308.0; A += 3.0)
	{
		const double Mid = A + 1.5;
		const double Rad = FMath::DegreesToRadians(Mid);
		const FVector Dir(FMath::Cos(Rad), FMath::Sin(Rad), 0.0);
		// Down the lines the stand sits farther out (foul ground), behind the plate it hugs the backstop.
		const double Behind = FMath::Abs(FMath::FindDeltaAngleDegrees(Mid, 180.0));
		const double Base = Behind < 60.0 ? 19.5 : FMath::Lerp(19.5, 34.0, FMath::Clamp((Behind - 60.0) / 70.0, 0.0, 1.0));
		const int32 GRows = 18;
		for (int32 Row = 0; Row < GRows; ++Row)
		{
			const double Rr = Base + Row * 1.1;
			const double Z = 1.4 + Row * 0.66;
			const double Len = Rr * FMath::DegreesToRadians(3.06);
			Box(Riser, Dir * (Rr + 0.55) + FVector(0.0, 0.0, Z / 2.0), FVector(1.1, Len, Z), Mid);
			Box(Seat, Dir * (Rr + 0.75) + FVector(0.0, 0.0, Z + 0.2), FVector(0.4, Len * 0.96, 0.4), Mid);
			const int32 Count = FMath::FloorToInt32(Len / 0.75);
			for (int32 k = 0; k < Count; ++k)
			{
				const double Lat = (k + 0.5) / Count - 0.5;
				const double Ar = Rad + Lat * FMath::DegreesToRadians(3.0);
				AddFan(FVector(FMath::Cos(Ar), FMath::Sin(Ar), 0.0) * (Rr + 0.45) + FVector(0.0, 0.0, Z), Mid + 180.0);
			}
		}
		const double Back = Base + GRows * 1.1 + 0.6;
		const double Top = 1.4 + GRows * 0.66;
		Box(Facade, Dir * Back + FVector(0.0, 0.0, (Top + 6.0) / 2.0), FVector(1.0, Back * FMath::DegreesToRadians(3.06), Top + 6.0), Mid);
		// A roof over the upper rows.
		Box(Facade, Dir * (Back - 5.0) + FVector(0.0, 0.0, Top + 6.0), FVector(11.0, Back * FMath::DegreesToRadians(3.1), 0.5), Mid);
		Box(Trim, Dir * (Back - 10.4) + FVector(0.0, 0.0, Top + 5.7), FVector(0.15, Back * FMath::DegreesToRadians(3.06), 0.2), Mid);
	}
	// Down the lines: stands parallel to the foul lines, between the grandstand and the bleachers.
	for (const double Side : { -1.0, 1.0 })
	{
		const double LineRad = FMath::DegreesToRadians(Side * 45.0);
		const FVector Along(FMath::Cos(LineRad), FMath::Sin(LineRad), 0.0);
		const FVector Out = FVector(-FMath::Sin(LineRad), FMath::Cos(LineRad), 0.0) * Side;
		const double Yaw = Side * 45.0;
		for (double S = 40.0; S < 100.0; S += 4.0)
		{
			for (int32 Row = 0; Row < 12; ++Row)
			{
				const double D = 12.0 + Row * 1.1;
				const double Z = 1.4 + Row * 0.62;
				const FVector C = Along * (S + 2.0) + Out * (D + 0.55);
				Box(Riser, C + FVector(0.0, 0.0, Z / 2.0), FVector(4.0, 1.1, Z), Yaw);
				for (int32 k = 0; k < 5; ++k)
				{
					AddFan(Along * (S + 0.4 + k * 0.8) + Out * (D + 0.45) + FVector(0.0, 0.0, Z), Yaw + (Side > 0 ? -90.0 : 90.0));
				}
			}
			const FVector Back = Along * (S + 2.0) + Out * (12.0 + 12 * 1.1 + 0.6);
			Box(Facade, Back + FVector(0.0, 0.0, 6.0), FVector(4.0, 1.0, 12.0), Yaw);
		}
	}
}

void ADerbyBallpark::BuildTowers()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Steel = Plain(this, Hex(TEXT("#2a2d35")), 0.5, 0.7);
	UMaterialInterface* Lamp = Glow(this, Hex(TEXT("#fff3d6")) * 18.f);
	UMaterialInterface* Halo = Glow(this, Hex(TEXT("#ffe2a8")) * 1.2f);
	const FVector2D Spots[] = { FVector2D(28.0, 168.0), FVector2D(-28.0, 168.0), FVector2D(62.0, 142.0), FVector2D(-62.0, 142.0), FVector2D(128.0, 64.0), FVector2D(-128.0, 64.0) };
	for (const FVector2D& S : Spots)
	{
		const FVector Base = Ground(S.X, S.Y) / 100.0;
		const double H = 46.0;
		Box(Steel, Base + FVector(0.0, 0.0, H / 2.0), FVector(1.4, 1.4, H), S.X, true);
		// The head faces the middle of the field.
		const FVector Aim = FVector(45.0, 0.0, 0.0) - Base;
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Aim.Y, Aim.X));
		const FVector Face(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0);
		const FVector Side(-Face.Y, Face.X, 0.0);
		const FVector Head = Base + FVector(0.0, 0.0, H + 3.5);
		Box(Steel, Head, FVector(1.0, 13.0, 8.0), Yaw, true);
		for (int32 r = 0; r < 3; ++r)
		{
			for (int32 c = 0; c < 5; ++c)
			{
				const FVector P = Head + Face * 0.55 + Side * ((c - 2) * 2.5) + FVector(0.0, 0.0, (r - 1) * 2.5);
				Box(Lamp, P, FVector(0.2, 2.0, 2.0), Yaw);
			}
		}
		// The frame's catwalk under the lamps.
		Box(Halo, Head + Face * 0.6 + FVector(0.0, 0.0, -4.4), FVector(0.15, 12.0, 0.15), Yaw);
	}
}

void ADerbyBallpark::BuildBoard()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Frame = Plain(this, Hex(TEXT("#1a1d24")), 0.6, 0.4);
	UMaterialInterface* Screen = Glow(this, Hex(TEXT("#050812")) * 1.f);
	BoardAccent = Glow(this, Hex(TEXT("#f2b23f")) * 3.f);
	const double X = FenceAt(0.0) + 42.0;
	const double Z = 26.0;
	// Two legs, the frame, the dark screen, an accent bar top and bottom.
	for (const double Y : { -12.0, 12.0 })
	{
		Box(Frame, FVector(X + 1.0, Y, Z / 2.0 - 4.0), FVector(1.5, 1.5, Z), 0.0, true);
	}
	Box(Frame, FVector(X + 0.6, 0.0, Z), FVector(1.2, 38.0, 16.0), 0.0, true);
	Box(Screen, FVector(X, 0.0, Z), FVector(0.1, 36.0, 14.0));
	Group(Cube, BoardAccent, false)->AddInstance(FTransform(FRotator::ZeroRotator, M(FVector(X - 0.06, 0.0, Z + 6.6)), FVector(0.05, 36.0, 0.5)), false);
	Group(Cube, BoardAccent, false)->AddInstance(FTransform(FRotator::ZeroRotator, M(FVector(X - 0.06, 0.0, Z - 6.6)), FVector(0.05, 36.0, 0.5)), false);
	Text(TEXT("HOME RUN DERBY"), FVector(X + 0.3, 0.0, Z + 9.4), 180.0, 280.f, Hex(TEXT("#f2b23f")));
	BoardLines.Add(Text(TEXT("NOW BATTING"), FVector(X - 0.2, 0.0, Z + 3.4), 180.0, 330.f, FLinearColor::White));
	BoardLines.Add(Text(TEXT(""), FVector(X - 0.2, 0.0, Z - 0.6), 180.0, 420.f, Hex(TEXT("#f2b23f"))));
	BoardLines.Add(Text(TEXT(""), FVector(X - 0.2, 0.0, Z - 4.4), 180.0, 280.f, Hex(TEXT("#3fc9c2"))));
	for (UTextRenderComponent* T : BoardLines)
	{
		if (UMaterialInterface* Unlit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultTextMaterialTranslucent.DefaultTextMaterialTranslucent"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			T->SetTextMaterial(Unlit);
		}
	}
}

void ADerbyBallpark::SetBoard(const FString& Line1, const FString& Line2, const FString& Line3, const FLinearColor& Accent)
{
	const FString Lines[] = { Line1, Line2, Line3 };
	for (int32 i = 0; i < BoardLines.Num() && i < 3; ++i)
	{
		if (BoardLines[i])
		{
			BoardLines[i]->SetText(FText::FromString(Lines[i]));
		}
	}
	if (BoardLines.IsValidIndex(1) && BoardLines[1])
	{
		BoardLines[1]->SetTextRenderColor(FMath::Lerp(Accent, FLinearColor::White, 0.25f).ToFColor(true));
	}
	if (BoardAccent)
	{
		BoardAccent->SetVectorParameterValue(TEXT("Color"), Accent * 3.f);
	}
}

void ADerbyBallpark::FireworkSpots(TArray<FVector>& Out) const
{
	for (const double A : { -32.0, -14.0, 0.0, 14.0, 32.0 })
	{
		Out.Add(Ground(A, FenceAt(A) + 30.0, 48.0 + FMath::Abs(A) * 0.2));
	}
}

void ADerbyBallpark::Roar(float Level)
{
	Excitement = FMath::Max(Excitement, FMath::Clamp(Level, 0.f, 1.f));
}

void ADerbyBallpark::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	// The crowd settles back to a murmur over a few seconds.
	Excitement = FMath::Max(0.12f, Excitement - DeltaSeconds * 0.22f);
	for (int32 i = 0; i < Crowd.Num(); ++i)
	{
		if (!Crowd[i])
		{
			continue;
		}
		const double Beat = 7.5 + (i % 5) * 0.9;
		const double Hop = FMath::Max(0.0, FMath::Sin(Clock * Beat + i * 1.7));
		const double Z = Excitement * Excitement * 38.0 * Hop + 1.5 * FMath::Sin(Clock * 1.3 + i);
		Crowd[i]->SetRelativeLocation(FVector(0.0, 0.0, Z));
	}
}

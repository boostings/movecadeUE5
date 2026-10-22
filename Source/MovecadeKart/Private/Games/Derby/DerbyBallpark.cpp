#include "Games/Derby/DerbyBallpark.h"

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
#include "Games/Derby/DerbyBallparkParts.h"
#include "Games/Derby/DerbyLog.h"
#include "Games/Derby/DerbyRules.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogDerby);

using namespace MovecadeKart::Derby::Ballpark;

namespace
{
	/** The sky dome's scale over the unit sphere: well past everything, inside the far plane. */
	constexpr double SkyDomeScale = 20000.0;
	const TCHAR* const SkyMaterialPath = TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky");
	const TCHAR* const CloudsPath = TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds");
	/** Where the moon hangs (and the sky material's "sun" glows): high over left-centre. */
	const FVector MoonDirection(-0.45, 0.62, 0.42);
	/** Stars on the upper dome (deterministic), their sizes, and the moon's disc (metres out on the dome). */
	constexpr int32 StarSeed = 0xba5e;
	constexpr int32 StarCount = 420;
	constexpr double StarDomeM = 1800.0;
	constexpr double MoonDomeM = 1700.0;
	constexpr double MoonSize = 70.0;

	/** The crowd: it settles back to a murmur at this rate (per second), never under the floor. */
	constexpr float CrowdSettleRate = 0.22f;
	constexpr float CrowdMurmur = 0.12f;
	/** Each section hops on its own beat (rad/s, by section) and phase, as high as the excitement squared allows (cm). */
	constexpr double HopBeat = 7.5;
	constexpr double HopBeatStep = 0.9;
	constexpr int32 HopBeats = 5;
	constexpr double HopPhase = 1.7;
	constexpr double HopHeightCm = 38.0;
	/** Under the hops, a slow sway (cm, rad/s). */
	constexpr double SwayCm = 1.5;
	constexpr double SwayRate = 1.3;

	/** The scoreboard beyond centre field: metres past the fence, its screen's centre height. */
	constexpr double BoardBehindFenceM = 42.0;
	constexpr double BoardZ = 26.0;
	/** The board's accent bars glow at this multiple of the batter's colour. */
	constexpr float BoardAccentGlow = 3.f;
	/** The batter's name line, warmed toward white. */
	constexpr float BoardNameWhiten = 0.25f;
	constexpr int32 BoardLineCount = 3;
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
	SkyDome->SetRelativeScale3D(FVector(SkyDomeScale));
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
	// The park is built round the world origin (the game's plate): it never moves.
	SetActorTransform(FTransform::Identity);
	Build();
}

void ADerbyBallpark::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), PartTag);
	for (UActorComponent* Part : Old)
	{
		RemoveInstanceComponent(Part);
		Part->DestroyComponent();
	}
	Groups.Reset();
	Materials.Reset();
	Crowd.Reset();
	BoardLines.Reset();
	BoardAccent = nullptr;
}

void ADerbyBallpark::AddPart(USceneComponent* Part)
{
	Part->ComponentTags.Add(PartTag);
	AddInstanceComponent(Part);
	Part->RegisterComponent();
}

USceneComponent* ADerbyBallpark::Pivot(const TCHAR* Name)
{
	USceneComponent* P = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
	P->SetupAttachment(RootComponent);
	P->SetMobility(EComponentMobility::Movable);
	AddPart(P);
	return P;
}

void ADerbyBallpark::Box(UMaterialInterface* Material, const FVector& CentreM, const FVector& SizeM, double YawDeg, bool bShadows, USceneComponent* Parent)
{
	// The engine cube is 1 m across: its scale is the size in metres.
	Groups.Find(Cube, Material, bShadows, Parent)->AddInstance(FTransform(FRotator(0.0, YawDeg, 0.0), M(CentreM), SizeM), false);
}

void ADerbyBallpark::Disc(UMaterialInterface* Material, const FVector& CentreM, double RadiusM, double HeightM, bool bShadows)
{
	// The engine cylinder is 1 m across and 1 m tall, centred on its origin.
	const FTransform Transform(FRotator::ZeroRotator, M(CentreM + FVector(0.0, 0.0, HeightM / 2.0)), FVector(RadiusM * 2.0, RadiusM * 2.0, HeightM));
	Groups.Find(Cylinder, Material, bShadows)->AddInstance(Transform, false);
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
	AddPart(T);
	return T;
}

void ADerbyBallpark::Build()
{
	Clear();
	// Movable: the crowd sections bounce, and every group hangs off the movable root.
	Groups.Init(this, TEXT("ParkGroup"), PartTag, EComponentMobility::Movable);
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
	// A blue night haze past the stands, lit warm toward the floods.
	Fog->SetStartDistance(9000.f);
	Fog->SetFogDensity(0.035f);
	Fog->SetFogHeightFalloff(0.06f);
	Fog->SetFogMaxOpacity(0.6f);
	Fog->SetFogInscatteringColor(Hex(TEXT("#16224a")));
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#ffd9a8")) * 0.15f);
	Fog->SetDirectionalInscatteringExponent(8.f);
	// Without a sun, auto exposure drifts: hold it in a narrow band, and bloom the lamps.
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
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, SkyMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
		Sky->SetVectorParameterValue(TEXT("SkyMid"), FMath::Lerp(Horizon, Top, 0.6f));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#05070c")));
		const FVector MoonDir = MoonDirection.GetSafeNormal();
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(MoonDir.X, MoonDir.Y, MoonDir.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#dfe7ff")));
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.022f));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), 0.25f);
		Sky->SetScalarParameterValue(TEXT("GlowPower"), 24.0f);
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), 0.8f);
		Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0f);
		if (UTexture* Clouds = LoadObject<UTexture>(nullptr, CloudsPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
		}
		Sky->SetScalarParameterValue(TEXT("Clouds"), 0.18f);
		Sky->SetScalarParameterValue(TEXT("CloudScale"), 0.3f);
		Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(TEXT("#2c3a66")));
		Sky->SetVectorParameterValue(TEXT("CloudShade"), Hex(TEXT("#0a0f22")));
		SkyDome->SetMaterial(0, Sky);
	}
	else
	{
		UE_LOG(LogDerby, Warning, TEXT("DerbyBallpark: %s is missing; the sky stays the default"), SkyMaterialPath);
	}
	SkyLight->RecaptureSky();
	FRandomStream R(StarSeed);
	UInstancedStaticMeshComponent* Stars = Groups.Find(Sphere, Materials.GlowOpaque(this, FLinearColor(1.f, 0.97f, 0.9f) * 6.f), false);
	for (int32 i = 0; i < StarCount; ++i)
	{
		const double Yaw = R.FRandRange(0.0, 2.0 * PI);
		const double Elev = FMath::Asin(R.FRandRange(0.12f, 0.98f));
		const FVector Dir(FMath::Cos(Elev) * FMath::Cos(Yaw), FMath::Cos(Elev) * FMath::Sin(Yaw), FMath::Sin(Elev));
		// One in twelve or so a bright one.
		const double Size = R.FRandRange(1.4f, 3.6f) * (R.FRand() < 0.08f ? 2.0 : 1.0);
		Stars->AddInstance(FTransform(FRotator::ZeroRotator, M(Dir * StarDomeM), FVector(Size)), false);
	}
	Groups.Find(Sphere, Materials.GlowOpaque(this, Hex(TEXT("#f4f1e6")) * 4.f), false)
		->AddInstance(FTransform(FRotator::ZeroRotator, M(MoonDirection.GetSafeNormal() * MoonDomeM), FVector(MoonSize)), false);
}

void ADerbyBallpark::BuildBoard()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Frame = Materials.Plain(this, Hex(TEXT("#1a1d24")), 0.6, 0.4);
	UMaterialInterface* Screen = Materials.GlowOpaque(this, Hex(TEXT("#050812")) * 1.f);
	BoardAccent = GlowOpaque(this, Hex(TEXT("#f2b23f")) * BoardAccentGlow);
	const double X = FenceAt(0.0) + BoardBehindFenceM;
	const double Z = BoardZ;
	// Two legs, the frame, the dark screen, an accent bar top and bottom.
	for (const double Y : { -12.0, 12.0 })
	{
		Box(Frame, FVector(X + 1.0, Y, Z / 2.0 - 4.0), FVector(1.5, 1.5, Z), 0.0, true);
	}
	Box(Frame, FVector(X + 0.6, 0.0, Z), FVector(1.2, 38.0, 16.0), 0.0, true);
	Box(Screen, FVector(X, 0.0, Z), FVector(0.1, 36.0, 14.0));
	Box(BoardAccent, FVector(X - 0.06, 0.0, Z + 6.6), FVector(0.05, 36.0, 0.5));
	Box(BoardAccent, FVector(X - 0.06, 0.0, Z - 6.6), FVector(0.05, 36.0, 0.5));
	// The title, then the three lines SetBoard writes (the third in teal), all facing home.
	Text(TEXT("HOME RUN DERBY"), FVector(X + 0.3, 0.0, Z + 9.4), 180.0, 280.f, Hex(TEXT("#f2b23f")));
	BoardLines.Add(Text(TEXT("NOW BATTING"), FVector(X - 0.2, 0.0, Z + 3.4), 180.0, 330.f, FLinearColor::White));
	BoardLines.Add(Text(TEXT(""), FVector(X - 0.2, 0.0, Z - 0.6), 180.0, 420.f, Hex(TEXT("#f2b23f"))));
	BoardLines.Add(Text(TEXT(""), FVector(X - 0.2, 0.0, Z - 4.4), 180.0, 280.f, Hex(TEXT("#3fc9c2"))));
}

void ADerbyBallpark::SetBoard(const FString& Line1, const FString& Line2, const FString& Line3, const FLinearColor& Accent)
{
	const FString* const Lines[BoardLineCount] = { &Line1, &Line2, &Line3 };
	for (int32 i = 0; i < BoardLines.Num() && i < BoardLineCount; ++i)
	{
		if (BoardLines[i])
		{
			BoardLines[i]->SetText(FText::FromString(*Lines[i]));
		}
	}
	if (BoardLines.IsValidIndex(1) && BoardLines[1])
	{
		BoardLines[1]->SetTextRenderColor(FMath::Lerp(Accent, FLinearColor::White, BoardNameWhiten).ToFColor(true));
	}
	if (BoardAccent)
	{
		BoardAccent->SetVectorParameterValue(TEXT("Color"), Accent * BoardAccentGlow);
	}
}

void ADerbyBallpark::FireworkSpots(TArray<FVector>& Out) const
{
	// Over the bleachers either side of the batter's eye and over the board, higher toward the lines.
	for (const double A : { -32.0, -14.0, 0.0, 14.0, 32.0 })
	{
		Out.Add(Ground(A, FenceAt(A) + 16.0, 34.0 + FMath::Abs(A) * 0.15));
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
	Excitement = FMath::Max(CrowdMurmur, Excitement - DeltaSeconds * CrowdSettleRate);
	for (int32 i = 0; i < Crowd.Num(); ++i)
	{
		if (!Crowd[i])
		{
			continue;
		}
		const double Beat = HopBeat + (i % HopBeats) * HopBeatStep;
		const double Hop = FMath::Max(0.0, FMath::Sin(Clock * Beat + i * HopPhase));
		const double Z = Excitement * Excitement * HopHeightCm * Hop + SwayCm * FMath::Sin(Clock * SwayRate + i);
		Crowd[i]->SetRelativeLocation(FVector(0.0, 0.0, Z));
	}
}

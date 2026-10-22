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
	/** The night's light: the floods' key and the moon's fill (degrees), the sky light, the haze and the grade. */
	struct FNightRig
	{
		double FloodsPitch;
		double FloodsYaw;
		const TCHAR* FloodsColor;
		float FloodsIntensity;
		double MoonPitch;
		double MoonYaw;
		const TCHAR* MoonColor;
		float MoonIntensity;
		float SkyLightIntensity;
		const TCHAR* GroundBounceColor;
		float FogStartCm;
		float FogDensity;
		float FogHeightFalloff;
		float FogMaxOpacity;
		const TCHAR* FogColor;
		const TCHAR* FogGlowColor;
		float FogGlow;
		float FogGlowExponent;
		float ExposureMin;
		float ExposureMax;
		float BloomIntensity;
		float BloomThreshold;
		float Vignette;
		float Saturation;
		float Contrast;
	};
	const FNightRig Night{ .FloodsPitch = -52.0, .FloodsYaw = 18.0, .FloodsColor = TEXT("#fff0da"), .FloodsIntensity = 3.2f,
		.MoonPitch = -35.0, .MoonYaw = -150.0, .MoonColor = TEXT("#7f9cff"), .MoonIntensity = 0.7f,
		.SkyLightIntensity = 0.9f, .GroundBounceColor = TEXT("#1d2a1c"),
		.FogStartCm = 9000.f, .FogDensity = 0.035f, .FogHeightFalloff = 0.06f, .FogMaxOpacity = 0.6f, .FogColor = TEXT("#16224a"),
		.FogGlowColor = TEXT("#ffd9a8"), .FogGlow = 0.15f, .FogGlowExponent = 8.f,
		.ExposureMin = 0.6f, .ExposureMax = 1.4f, .BloomIntensity = 1.1f, .BloomThreshold = 1.0f, .Vignette = 0.45f, .Saturation = 1.08f, .Contrast = 1.06f };

	/** The sky dome's scale over the unit sphere: well past everything, inside the far plane. */
	constexpr double SkyDomeScale = 20000.0;
	const TCHAR* const SkyMaterialPath = TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky");
	const TCHAR* const CloudsPath = TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds");
	/** Where the moon hangs (and the sky material's "sun" glows): high over left-centre. */
	const FVector MoonDirection(-0.45, 0.62, 0.42);
	/** Stars on the upper dome (deterministic), a few bright ones, and the moon's disc (metres out on the dome). */
	struct FStarField
	{
		int32 Seed;
		int32 Count;
		double DomeM;
		float MinSinElevation;
		float MaxSinElevation;
		float MinSize;
		float MaxSize;
		float BrightChance;
		double BrightScale;
		float Glow;
		double MoonDomeM;
		double MoonSize;
		float MoonGlow;
	};
	constexpr FStarField Stars{ .Seed = 0xba5e, .Count = 420, .DomeM = 1800.0, .MinSinElevation = 0.12f, .MaxSinElevation = 0.98f, .MinSize = 1.4f,
		.MaxSize = 3.6f, .BrightChance = 0.08f, .BrightScale = 2.0, .Glow = 6.f, .MoonDomeM = 1700.0, .MoonSize = 70.0, .MoonGlow = 4.f };

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

	/** The scoreboard beyond centre field (metres; X back from its screen, Z from the screen's centre). */
	struct FBoardSpec
	{
		double BehindFenceM;
		double CentreZ;
		/** Two legs either side, a little behind the screen, standing on the ground. */
		double LegY;
		double LegBack;
		double LegSize;
		double LegSink;
		double FrameBack;
		double FrameDepth;
		double FrameWidth;
		double FrameHeight;
		double ScreenDepth;
		double ScreenWidth;
		double ScreenHeight;
		/** The accent bars along the screen's top and bottom edges, just proud of it. */
		double AccentFront;
		double AccentDepth;
		double AccentHeight;
		double AccentZ;
	};
	constexpr FBoardSpec Board{ .BehindFenceM = 42.0, .CentreZ = 26.0, .LegY = 12.0, .LegBack = 1.0, .LegSize = 1.5, .LegSink = 4.0,
		.FrameBack = 0.6, .FrameDepth = 1.2, .FrameWidth = 38.0, .FrameHeight = 16.0, .ScreenDepth = 0.1, .ScreenWidth = 36.0, .ScreenHeight = 14.0,
		.AccentFront = 0.06, .AccentDepth = 0.05, .AccentHeight = 0.5, .AccentZ = 6.6 };

	/** The board's lettering: the title above the screen, then the three lines SetBoard writes (bLine). */
	struct FBoardText
	{
		const TCHAR* Initial;
		double OffsetX;
		double OffsetZ;
		float SizeCm;
		/** Null: white. */
		const TCHAR* Color;
		bool bLine;
	};
	const FBoardText BoardTexts[] = {
		{ TEXT("HOME RUN DERBY"), 0.3, 9.4, 280.f, TEXT("#f2b23f"), false },
		{ TEXT("NOW BATTING"), -0.2, 3.4, 330.f, nullptr, true },
		{ TEXT(""), -0.2, -0.6, 420.f, TEXT("#f2b23f"), true },
		{ TEXT(""), -0.2, -4.4, 280.f, TEXT("#3fc9c2"), true },
	};
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
	FindGroup(Cube, Material, bShadows, Parent)->AddInstance(FTransform(FRotator(0.0, YawDeg, 0.0), M(CentreM), SizeM), false);
}

void ADerbyBallpark::Disc(UMaterialInterface* Material, const FVector& CentreM, double RadiusM, double HeightM, bool bShadows)
{
	// The engine cylinder is 1 m across and 1 m tall, centred on its origin.
	const FTransform Transform(FRotator::ZeroRotator, M(CentreM + FVector(0.0, 0.0, HeightM / 2.0)), FVector(RadiusM * 2.0, RadiusM * 2.0, HeightM));
	FindGroup(Cylinder, Material, bShadows)->AddInstance(Transform, false);
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
	WarnIfShapesMissing();
	// Movable: the crowd sections bounce, and every group hangs off the movable root.
	Groups.Init(this, TEXT("ParkGroup"), PartTag, EComponentMobility::Movable);
	PartsWithoutMaterial = 0;
	BuildLook();
	BuildSky();
	BuildField();
	BuildWall();
	BuildStands();
	BuildTowers();
	BuildBoard();
	// Art warns once per missing master and falls back (Surface to Plain, GlowOpaque to Glow); only a look with no
	// master at all comes back null, and its parts draw with the engine's default material.
	UE_CLOG(PartsWithoutMaterial > 0, LogDerby, Warning, TEXT("DerbyBallpark: %d part(s) built without a material (see LogKartMaterials)"),
		PartsWithoutMaterial);
}

void ADerbyBallpark::WarnIfShapesMissing() const
{
	// The engine's basic shapes are every instanced part; without them the park is built empty.
	if (!Cube || !Cylinder || !Sphere)
	{
		UE_LOG(LogDerby, Warning, TEXT("DerbyBallpark: engine basic shapes missing (cube %d, cylinder %d, sphere %d); the park will be mostly empty"),
			Cube != nullptr, Cylinder != nullptr, Sphere != nullptr);
	}
}

UInstancedStaticMeshComponent* ADerbyBallpark::FindGroup(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows, USceneComponent* Parent,
	UMaterialInterface* Material2)
{
	PartsWithoutMaterial += Material ? 0 : 1;
	return Groups.Find(Mesh, Material, bShadows, Parent, Material2);
}

void ADerbyBallpark::BuildLook()
{
	using namespace MovecadeKart::Art;
	const FNightRig& L = Night;
	// The floods: one warm key from high behind the plate, as the towers add up to; a cold moon fill from the other side.
	Floods->SetWorldRotation(FRotator(L.FloodsPitch, L.FloodsYaw, 0.0));
	Floods->SetLightColor(Hex(L.FloodsColor));
	Floods->SetIntensity(L.FloodsIntensity);
	Floods->SetCastShadows(true);
	Floods->SetAtmosphereSunLight(false);
	Floods->SetForwardShadingPriority(1);
	Moon->SetWorldRotation(FRotator(L.MoonPitch, L.MoonYaw, 0.0));
	Moon->SetLightColor(Hex(L.MoonColor));
	Moon->SetIntensity(L.MoonIntensity);
	Moon->SetCastShadows(false);
	Moon->SetAtmosphereSunLight(false);
	Moon->SetForwardShadingPriority(0);
	SkyLight->bRealTimeCapture = false;
	SkyLight->SetIntensity(L.SkyLightIntensity);
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(L.GroundBounceColor));
	// A blue night haze past the stands, lit warm toward the floods.
	Fog->SetStartDistance(L.FogStartCm);
	Fog->SetFogDensity(L.FogDensity);
	Fog->SetFogHeightFalloff(L.FogHeightFalloff);
	Fog->SetFogMaxOpacity(L.FogMaxOpacity);
	Fog->SetFogInscatteringColor(Hex(L.FogColor));
	Fog->SetDirectionalInscatteringColor(Hex(L.FogGlowColor) * L.FogGlow);
	Fog->SetDirectionalInscatteringExponent(L.FogGlowExponent);
	// Without a sun, auto exposure drifts: hold it in a narrow band, and bloom the lamps.
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = L.ExposureMin;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = L.ExposureMax;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = L.BloomIntensity;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = L.BloomThreshold;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = L.Vignette;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(L.Saturation, L.Saturation, L.Saturation, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(L.Contrast, L.Contrast, L.Contrast, 1.f);
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
		else
		{
			UE_LOG(LogDerby, Warning, TEXT("DerbyBallpark: %s is missing; the sky has no clouds"), CloudsPath);
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
	const FStarField& F = Stars;
	FRandomStream R(F.Seed);
	UInstancedStaticMeshComponent* StarGroup = FindGroup(Sphere, Materials.GlowOpaque(this, FLinearColor(1.f, 0.97f, 0.9f) * F.Glow), false);
	for (int32 i = 0; i < F.Count; ++i)
	{
		const double Yaw = R.FRandRange(0.0, 2.0 * PI);
		// Uniform over the dome's area between the two sines of elevation.
		const double Elev = FMath::Asin(R.FRandRange(F.MinSinElevation, F.MaxSinElevation));
		const FVector Dir(FMath::Cos(Elev) * FMath::Cos(Yaw), FMath::Cos(Elev) * FMath::Sin(Yaw), FMath::Sin(Elev));
		const double Size = R.FRandRange(F.MinSize, F.MaxSize) * (R.FRand() < F.BrightChance ? F.BrightScale : 1.0);
		StarGroup->AddInstance(FTransform(FRotator::ZeroRotator, M(Dir * F.DomeM), FVector(Size)), false);
	}
	FindGroup(Sphere, Materials.GlowOpaque(this, Hex(TEXT("#f4f1e6")) * F.MoonGlow), false)
		->AddInstance(FTransform(FRotator::ZeroRotator, M(MoonDirection.GetSafeNormal() * F.MoonDomeM), FVector(F.MoonSize)), false);
}

void ADerbyBallpark::BuildBoard()
{
	using namespace MovecadeKart::Art;
	const FBoardSpec& B = Board;
	UMaterialInterface* Frame = Materials.Plain(this, Hex(TEXT("#1a1d24")), 0.6, 0.4);
	UMaterialInterface* Screen = Materials.GlowOpaque(this, Hex(TEXT("#050812")) * 1.f);
	BoardAccent = GlowOpaque(this, Hex(TEXT("#f2b23f")) * BoardAccentGlow);
	const double X = FenceAt(0.0) + B.BehindFenceM;
	const double Z = B.CentreZ;
	// Two legs, the frame, the dark screen, an accent bar top and bottom.
	for (const double Y : { -B.LegY, B.LegY })
	{
		Box(Frame, FVector(X + B.LegBack, Y, Z / 2.0 - B.LegSink), FVector(B.LegSize, B.LegSize, Z), 0.0, true);
	}
	Box(Frame, FVector(X + B.FrameBack, 0.0, Z), FVector(B.FrameDepth, B.FrameWidth, B.FrameHeight), 0.0, true);
	Box(Screen, FVector(X, 0.0, Z), FVector(B.ScreenDepth, B.ScreenWidth, B.ScreenHeight));
	for (const double Bar : { B.AccentZ, -B.AccentZ })
	{
		Box(BoardAccent, FVector(X - B.AccentFront, 0.0, Z + Bar), FVector(B.AccentDepth, B.ScreenWidth, B.AccentHeight));
	}
	// The title, then the three lines SetBoard writes, all facing home.
	for (const FBoardText& Line : BoardTexts)
	{
		const FLinearColor Color = Line.Color ? Hex(Line.Color) : FLinearColor::White;
		UTextRenderComponent* Rendered = Text(Line.Initial, FVector(X + Line.OffsetX, 0.0, Z + Line.OffsetZ), 180.0, Line.SizeCm, Color);
		if (Line.bLine)
		{
			BoardLines.Add(Rendered);
		}
	}
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

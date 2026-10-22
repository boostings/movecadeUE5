// ASumoArena's lighting and grade, the night sky, the ring of falling bands round its clay core, the rock keel under
// it and the sea.

#include "Games/Sumo/SumoArena.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Games/Sumo/SumoArenaLayout.h"
#include "Games/Sumo/SumoPresentation.h"
#include "Games/Sumo/SumoRules.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace MovecadeKart::Sumo;
using namespace MovecadeKart::Sumo::ArenaLayout;
using namespace MovecadeKart::Art;
using MovecadeKart::FMeshBuilder;

namespace
{
	/** The stars: a dome far out inside the sky sphere, denser overhead, a few bright ones. */
	constexpr int32 StarCount = 420;
	constexpr double StarDomeCm = 300000.0;
	constexpr float StarMinCm = 4.f;
	constexpr float StarMaxCm = 10.f;
	constexpr float BrightStarChance = 0.08f;
	constexpr float BrightStarScale = 2.2f;
	/** The moon's halo: two soft additive discs on the sky behind the moon's disc. */
	constexpr double MoonHaloCm = 290000.0;
	constexpr double MoonHaloSize = 160.0;

	/** The cracks between a band's segments (m) and the underside's slope. */
	constexpr double SegmentGap = 0.025;
	constexpr double UndersideSlope = 0.3;
	/** A segment's arc is cut into steps about this long (m) at its outer edge. */
	constexpr double SegmentStepM = 0.25;
	/** A band breaks up as a wave round the ring from a random start (over this share of a second), plus jitter. */
	constexpr double BreakWaveSeconds = 0.55;
	constexpr float BreakJitterSeconds = 0.12f;
	constexpr int32 WarningSteps = 96;
	/** The core's rope ring (m inside its edge) and the start lines' half spacing (m). */
	constexpr double RopeInsetM = 0.12;
	constexpr int32 RopeLinks = 64;
	constexpr double StartLineY = 0.55;

	/** The keel's crystals and the glints on the band undersides. */
	constexpr int32 KeelCrystals = 26;
	constexpr int32 UndersideGlints = 30;
}

void ASumoArena::BuildLook()
{
	// The moon low over the far sea behind the ring: a cool key with soft shadows, its glitter path on the water.
	Moon->SetWorldRotation((-MoonDirection).Rotation());
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
	// A touch of blue-violet lift in the blacks.
	S.ColorOffset = FVector4(0.004f, 0.0f, 0.012f, 0.f);
}

void ASumoArena::BuildSky()
{
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Hex(TEXT("#03040f")));
		Sky->SetVectorParameterValue(TEXT("SkyMid"), Hex(TEXT("#0c0b26")));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Hex(TEXT("#3b2152")));
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#07061a")));
		// The moon is the sky shader's sun: a small hard disc with a faint glow.
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(MoonDirection.X, MoonDirection.Y, MoonDirection.Z, 0.0));
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
	FRandomStream R(0x57a2);
	UInstancedStaticMeshComponent* Stars = Groups.Find(Sphere, Materials.GlowOpaque(this, Hex(TEXT("#dfe6ff")) * 5.f), false);
	for (int32 i = 0; i < StarCount; ++i)
	{
		const double Az = R.FRandRange(0.0, 2.0 * PI);
		const double El = FMath::Asin(R.FRandRange(0.05f, 1.f));
		const FVector Dir(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
		const double Size = R.FRandRange(StarMinCm, StarMaxCm) * (R.FRand() < BrightStarChance ? BrightStarScale : 1.f);
		Stars->AddInstance(FTransform(FRotator::ZeroRotator, Dir * StarDomeCm, FVector(Size)), false);
	}
	if (UMaterialInstanceDynamic* Soft = MovecadeKart::Sumo::Presentation::MakeAdditive(this, Hex(TEXT("#8b9cff")) * 0.6f, 0.18f))
	{
		for (const double Scale : { 1.0, 1.8 })
		{
			Mesh(Sphere, Soft, FTransform(FRotator::ZeroRotator, MoonDirection * MoonHaloCm, FVector(MoonHaloSize * Scale)), false);
		}
	}
}

UStaticMesh* ASumoArena::SectorMesh(double Inner, double Outer, double Span, const TCHAR* Name)
{
	// One segment of a band (or the whole core), its origin at the segment's middle so it can tumble about it.
	FMeshBuilder B;
	const int32 Top = B.Section(TEXT("Top"));
	const int32 Side = B.Section(TEXT("Side"));
	const int32 Trim = B.Section(TEXT("Trim"));
	const bool bCore = Inner < 1e-3;
	const double R0 = bCore ? 0.0 : Inner + SegmentGap;
	const double R1 = Outer - SegmentGap;
	const double Mid = (Inner + Outer) / 2.0;
	const double Half = Span / 2.0 - (bCore ? 0.0 : SegmentGap / Mid);
	const int32 Steps = FMath::Max(3, FMath::CeilToInt32(Span * Outer / SegmentStepM));
	const FVector Origin = bCore ? FVector::ZeroVector : Cm(Mid, 0.0, 0.0);
	auto P = [&Origin](double R, double A, double Z) { return Cm(R * FMath::Cos(A), R * FMath::Sin(A), Z) - Origin; };
	auto Radial = [](double A) { return FVector(FMath::Cos(A), FMath::Sin(A), 0.0); };
	// A quad a-b-c-d (two triangles) into a section.
	auto Quad = [&B](int32 Section, const FVector& Pa, const FVector& Pb, const FVector& Pc, const FVector& Pd, const FVector& Na, const FVector& Nc,
		const FVector2D& Ua = FVector2D::ZeroVector, const FVector2D& Ub = FVector2D::ZeroVector, const FVector2D& Uc = FVector2D::ZeroVector, const FVector2D& Ud = FVector2D::ZeroVector)
	{
		const int32 a = B.Vert(Section, Pa, Na, Ua, FVector2D::ZeroVector);
		const int32 b = B.Vert(Section, Pb, Na, Ub, FVector2D::ZeroVector);
		const int32 c = B.Vert(Section, Pc, Nc, Uc, FVector2D::ZeroVector);
		const int32 d = B.Vert(Section, Pd, Nc, Ud, FVector2D::ZeroVector);
		B.Tri(Section, a, b, c);
		B.Tri(Section, a, c, d);
	};
	for (int32 i = 0; i < Steps; ++i)
	{
		const double A0 = -Half + 2.0 * Half * i / Steps;
		const double A1 = -Half + 2.0 * Half * (i + 1) / Steps;
		// The plank top (UVs in metres, round the arc at the band's middle).
		Quad(Top, P(R0, A0, 0.0), P(R1, A0, 0.0), P(R1, A1, 0.0), P(R0, A1, 0.0), FVector::UpVector, FVector::UpVector,
			FVector2D(R0, A0 * Mid), FVector2D(R1, A0 * Mid), FVector2D(R1, A1 * Mid), FVector2D(R0, A1 * Mid));
		// The underside slopes down toward the middle (an inverted cone); its normal faces out and down.
		const FVector N0 = (Radial(A0) * UndersideSlope - FVector::UpVector).GetSafeNormal();
		const FVector N1 = (Radial(A1) * UndersideSlope - FVector::UpVector).GetSafeNormal();
		Quad(Side, P(R0, A0, -Depth(R0)), P(R1, A0, -Depth(R1)), P(R1, A1, -Depth(R1)), P(R0, A1, -Depth(R0)), N0, N1);
		// The outer face, and the inner one for a band.
		Quad(Side, P(R1, A0, 0.0), P(R1, A0, -Depth(R1)), P(R1, A1, -Depth(R1)), P(R1, A1, 0.0), Radial(A0), Radial(A1));
		if (!bCore)
		{
			Quad(Side, P(R0, A0, 0.0), P(R0, A0, -Depth(R0)), P(R0, A1, -Depth(R0)), P(R0, A1, 0.0), -Radial(A0), -Radial(A1));
		}
		// The neon edge: a strip standing proud on the top's outer edge and a line round the outer face.
		const double In = R1 - 0.07, Out = R1 - 0.01;
		Quad(Trim, P(In, A0, 0.012), P(Out, A0, 0.012), P(Out, A1, 0.012), P(In, A1, 0.012), FVector::UpVector, FVector::UpVector);
		const double Rf = R1 + 0.006;
		Quad(Trim, P(Rf, A0, -0.03), P(Rf, A0, -0.09), P(Rf, A1, -0.09), P(Rf, A1, -0.03), Radial(A0), Radial(A1));
	}
	// The ends of a segment (the cracks between them).
	if (!bCore)
	{
		for (const double A : { -Half, Half })
		{
			const FVector N = FVector(-FMath::Sin(A), FMath::Cos(A), 0.0) * (A > 0.0 ? 1.0 : -1.0);
			Quad(Side, P(R0, A, 0.0), P(R1, A, 0.0), P(R1, A, -Depth(R1)), P(R0, A, -Depth(R0)), N, N);
		}
	}
	return B.Build(this, Name);
}

void ASumoArena::BuildRing()
{
	UMaterialInterface* Rock = Materials.Surface(this, { Hex(TEXT("#3a3150")), TEXT("rock_face"), 1.6, 0.55, 0.8, 0.5, 0.85, 0.0, 0.0, true, 0.5 });
	for (int32 b = 0; b < BandCount; ++b)
	{
		BuildBand(b, Rock);
	}
	BuildCoreMarkings();
}

void ASumoArena::BuildBand(int32 Index, UMaterialInterface* Rock)
{
	FBand& Band = Bands.AddDefaulted_GetRef();
	const bool bCore = Index == BandCount - 1;
	Band.Outer = ArenaRadius - Index * BandWidth;
	Band.Inner = bCore ? 0.0 : Band.Outer - BandWidth;
	Band.Segments = BandSegments[Index];
	Band.TrimColor = Hex(bCore ? CoreTrimColor : TrimColors[Index % 2]) * (Index == 0 ? OuterTrimGlow : TrimGlow);
	const double Span = 2.0 * PI / Band.Segments;
	UStaticMesh* Sector = SectorMesh(Band.Inner, Band.Outer, Span, *FString::Printf(TEXT("SumoBand%d"), Index));
	// Lacquered planks in deep indigo, every other band a shade warmer; the clay core is paler.
	const FLinearColor Lacquer = bCore ? Hex(TEXT("#6a2c27")) : Hex(Index % 2 ? TEXT("#1d1838") : TEXT("#241c44"));
	UMaterialInterface* TopMat = Materials.Surface(this, { Lacquer, bCore ? FName(TEXT("gravelly_sand")) : FName(TEXT("dark_wooden_planks")), bCore ? 1.2 : 1.6, 0.45, 0.6, 0.4,
		bCore ? 0.85 : 0.5, 0.0, 0.0, false, 0.6 });
	// The edge is the band's own: it turns red while the band is warned.
	UMaterialInstanceDynamic* TrimMat = GlowOpaque(this, Band.TrimColor);
	BandTrims.Add(TrimMat);
	UInstancedStaticMeshComponent* Ism = OwnInstances(Sector, TEXT("Band"), true);
	Ism->SetMaterial(0, TopMat);
	Ism->SetMaterial(1, Rock);
	Ism->SetMaterial(2, TrimMat);
	BandMeshes.Add(Ism);
	FRandomStream R(0x5100 + Index);
	const double WaveStart = R.FRand() * Band.Segments;
	const double Mid = (Band.Inner + Band.Outer) / 2.0;
	for (int32 s = 0; s < Band.Segments; ++s)
	{
		const double A = (s + 0.5) * Span;
		const FTransform T(FRotator(0.0, FMath::RadiansToDegrees(A), 0.0), bCore ? FVector::ZeroVector : Cm(Mid * FMath::Cos(A), Mid * FMath::Sin(A), 0.0));
		Band.Rest.Add(T);
		Ism->AddInstance(T, false);
		// Each piece tumbles its own way, on its own beat of the wave.
		FSegmentFall& F = Band.Falls.AddDefaulted_GetRef();
		F.Delay = BreakWaveSeconds * FMath::Fmod(s + WaveStart, static_cast<double>(Band.Segments)) / Band.Segments + R.FRandRange(0.f, BreakJitterSeconds);
		F.Axis = FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-0.3f, 0.3f)).GetSafeNormal();
		F.Spin = R.FRandRange(90.f, 260.f) * (R.FRand() < 0.5f ? -1.f : 1.f);
		F.Drift = R.FRandRange(0.4f, 1.6f);
	}
	BuildWarningWash(Band, Index);
}

void ASumoArena::BuildWarningWash(const FBand& Band, int32 Index)
{
	// An additive annulus laid on the band, shown while it is warned.
	UMaterialInstanceDynamic* WarnMat = MovecadeKart::Sumo::Presentation::MakeAdditive(this, Hex(TEXT("#ff2a3a")) * 3.f, 0.f);
	if (!WarnMat)
	{
		return;
	}
	FMeshBuilder W;
	const int32 Section = W.Section(TEXT("Warn"));
	MovecadeKart::Sumo::Presentation::AddAnnulus(W, Section, FMath::Max(0.0, Band.Inner + 0.03), Band.Outer - 0.03, 0.02, WarningSteps, 100.0);
	UStaticMeshComponent* Wash = Mesh(W.Build(this, *FString::Printf(TEXT("SumoWarn%d"), Index)), WarnMat, FTransform::Identity, false);
	Wash->SetVisibility(false);
	BandWarnings.Add(Wash);
	BandWarningMats.Add(WarnMat);
}

void ASumoArena::BuildCoreMarkings()
{
	// The core's markings: the straw-rope circle (a glowing tawara) and the two white start lines.
	UMaterialInterface* Chalk = Materials.Plain(this, Hex(TEXT("#f4efe6")), 0.7, 0.0, 0.35);
	for (const double Y : { -StartLineY, StartLineY })
	{
		CoreDecor.Add(Mesh(Cube, Chalk, FTransform(FRotator(0.0, 90.0, 0.0), Cm(0.0, Y, 0.012), FVector(0.07, 0.8, 0.012)), false));
	}
	FMeshBuilder E;
	E.Section(TEXT("Rope"));
	const double R = CoreRadius - RopeInsetM;
	for (int32 i = 0; i < RopeLinks; ++i)
	{
		// The rope ring as a chain of short limbs (a torus).
		const double A0 = 2.0 * PI * i / RopeLinks, A1 = 2.0 * PI * (i + 1) / RopeLinks;
		E.Limb(TEXT("Rope"), Cm(R * FMath::Cos(A0), R * FMath::Sin(A0), 0.035), Cm(R * FMath::Cos(A1), R * FMath::Sin(A1), 0.035), 6.0, 6.0, 8, FVector2D::ZeroVector);
	}
	UMaterialInterface* Straw = Materials.Plain(this, Hex(TEXT("#d9b56a")), 0.75, 0.0, 0.25);
	CoreDecor.Add(Mesh(E.Build(this, TEXT("SumoRope")), Straw, FTransform::Identity, true));
}

void ASumoArena::BuildKeel()
{
	// Under the core: the rock keel tapering to a point, crystals glowing out of it.
	FMeshBuilder K;
	TArray<FVector2D> Profile;
	const double TopZ = -Depth(CoreRadius) * 100.0 + 25.0;
	const double Points[][2] = { { 0.0, -6.6 }, { 0.35, -6.2 }, { 0.8, -5.5 }, { 1.25, -4.7 }, { 1.65, -3.9 }, { 1.95, -3.1 }, { 2.05, -2.7 } };
	for (const auto& Point : Points)
	{
		Profile.Add(FVector2D(Point[0] * 100.0, Point[1] * 100.0));
	}
	Profile.Add(FVector2D(2.0 * 100.0, TopZ));
	Profile.Add(FVector2D(0.0, TopZ));
	K.Lathe(TEXT("Rock"), Profile, 20, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
	UMaterialInterface* Rock = Materials.Surface(this, { Hex(TEXT("#2f2846")), TEXT("rock_face"), 2.0, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 });
	Mesh(K.Build(this, TEXT("SumoKeel")), Rock, FTransform::Identity, true);
	FRandomStream R(0xc4a1);
	UInstancedStaticMeshComponent* Crystals = Groups.Find(Sphere, Materials.GlowOpaque(this, Hex(TEXT("#5ee9ff")) * 7.f), false);
	UInstancedStaticMeshComponent* Pinks = Groups.Find(Cube, Materials.GlowOpaque(this, Hex(TEXT("#ff58c8")) * 6.f), false);
	for (int32 i = 0; i < KeelCrystals; ++i)
	{
		// Along the keel's surface (its radius at that height, from the profile), a little inside it.
		const double A = R.FRandRange(0.0, 2.0 * PI);
		const double Z = R.FRandRange(-5.8f, -2.6f);
		const double Rad = FMath::Lerp(0.3, 2.05, FMath::Clamp((Z + 6.6) / 3.9, 0.0, 1.0)) - 0.05;
		const FVector At = Cm(Rad * FMath::Cos(A), Rad * FMath::Sin(A), Z);
		const FRotator Rot(R.FRandRange(-40.f, 40.f), FMath::RadiansToDegrees(A), R.FRandRange(-30.f, 30.f));
		const FVector Size(R.FRandRange(0.12f, 0.3f), R.FRandRange(0.08f, 0.16f), R.FRandRange(0.35f, 0.8f));
		(i % 3 ? Crystals : Pinks)->AddInstance(FTransform(Rot, At, Size), false);
	}
	// Crystal glints on the band undersides near the rim, and the keel's own glow on the water below.
	for (int32 i = 0; i < UndersideGlints; ++i)
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
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* Water = nullptr;
	if (UMaterialInterface* WaterMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackWater.M_TrackWater"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		// Near-black and glassy, so the moon, the neon and the lanterns mirror in it.
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
		UE_LOG(LogSumo, Warning, TEXT("SumoArena: M_TrackWater is missing; the sea is a plain dark plane"));
		Water = Materials.Plain(this, Hex(TEXT("#070a1c")), 0.08, 0.0);
	}
	if (Plane)
	{
		Mesh(Plane, Water, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, WaterZ), FVector(4000.0, 4000.0, 1.0)), false);
	}
	// A dark seabed far below, so the water has depth to tint.
	Mesh(Cube, Materials.Plain(this, Hex(TEXT("#03040a")), 1.0), FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, WaterZ - 3000.0), FVector(4000.0, 4000.0, 1.0)), false);
}

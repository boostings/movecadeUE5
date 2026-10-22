// ASumoArena's lighting and grade, the night sky, the ring of falling bands round its clay core, the rock keel under
// it and the sea. The looks are labelled tables at the top (applied as they are), the shapes named in metres.

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
namespace Presentation = MovecadeKart::Sumo::Presentation;
using MovecadeKart::FMeshBuilder;
using MovecadeKart::Sumo::Presentation::CachedPlain;
using MovecadeKart::Sumo::Presentation::FPlainLook;

namespace
{
	/** A material parameter and its value, applied in a loop. */
	struct FColorParam
	{
		const TCHAR* Name;
		const TCHAR* Hex;
	};
	struct FScalarParam
	{
		const TCHAR* Name;
		float Value;
	};
	struct FLinearParam
	{
		const TCHAR* Name;
		FLinearColor Value;
	};

	/**
	 * The night: the moon low over the far sea behind the ring (a cool key with soft shadows, its glitter path on the
	 * water), a magenta neon wash from the front right (the city of lanterns behind the camera), a dim blue sky light,
	 * violet fog toward the horizon, and a grade with bloom, a vignette and a touch of blue-violet in the blacks.
	 */
	namespace NightLook
	{
		const TCHAR* const MoonColor = TEXT("#a9c2ff");
		constexpr float MoonIntensity = 1.1f;
		constexpr float MoonSourceAngle = 1.2f;
		const FVector RimDirection(-0.6, 0.75, 0.35);
		const TCHAR* const RimColor = TEXT("#ff6ac1");
		constexpr float RimIntensity = 0.3f;
		constexpr float SkyIntensity = 0.22f;
		const TCHAR* const SkyColor = TEXT("#8fa4ff");
		const TCHAR* const LowerHemisphereColor = TEXT("#1a1838");
		const TCHAR* const FogColor = TEXT("#7a3f86");
		constexpr float FogInscatter = 0.12f;
		const TCHAR* const FogMoonColor = TEXT("#9fb5ff");
		constexpr float FogMoonInscatter = 0.04f;
		constexpr float FogMoonExponent = 10.f;
		constexpr float FogStartCm = 3500.f;
		constexpr float FogDensity = 0.012f;
		constexpr float FogHeightFalloff = 0.03f;
		constexpr float FogMaxOpacity = 0.6f;
		constexpr float FogCutoffCm = 250000.f;
		constexpr float Bloom = 1.15f;
		constexpr float Vignette = 0.5f;
		constexpr float Saturation = 1.08f;
		constexpr float Contrast = 1.06f;
		const FVector4 BlackLift(0.004f, 0.0f, 0.012f, 0.f);
	}

	/** The sky dome (M_TrackSky): a deep gradient, the moon as its sun (a small hard disc, a faint glow), thin clouds. */
	const FColorParam SkyColors[] = {
		{ TEXT("SkyTop"), TEXT("#03040f") },
		{ TEXT("SkyMid"), TEXT("#0c0b26") },
		{ TEXT("SkyHorizon"), TEXT("#3b2152") },
		{ TEXT("SkyBelow"), TEXT("#07061a") },
		{ TEXT("SunColor"), TEXT("#c9d6ff") },
		{ TEXT("CloudLit"), TEXT("#8c86c9") },
		{ TEXT("CloudShade"), TEXT("#1d1a3e") },
	};
	const FScalarParam SkyScalars[] = {
		{ TEXT("SunCos"), FMath::Cos(0.045f) },
		{ TEXT("SunDisc"), 14.0f },
		{ TEXT("SunGlow"), 0.06f },
		{ TEXT("GlowPower"), 26.0f },
		{ TEXT("HorizonGlow"), 0.08f },
		{ TEXT("Brightness"), 1.0f },
		{ TEXT("Clouds"), 0.3f },
		{ TEXT("CloudScale"), 0.3f },
	};
	const TCHAR* const SkyPath = TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky");
	const TCHAR* const CloudsPath = TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds");

	/** The stars: a dome far out inside the sky sphere, denser overhead, a few bright ones. */
	namespace Stars
	{
		constexpr int32 Count = 420;
		const TCHAR* const Color = TEXT("#dfe6ff");
		constexpr float Glow = 5.f;
		constexpr double DomeCm = 300000.0;
		constexpr float LowestSine = 0.05f;
		constexpr float MinCm = 4.f;
		constexpr float MaxCm = 10.f;
		constexpr float BrightChance = 0.08f;
		constexpr float BrightScale = 2.2f;
	}
	/** The moon's halo: two soft additive discs on the sky behind the moon's disc. */
	namespace MoonHalo
	{
		const TCHAR* const Color = TEXT("#8b9cff");
		constexpr float Glow = 0.6f;
		constexpr float Opacity = 0.18f;
		constexpr double DistanceCm = 290000.0;
		constexpr double Size = 160.0;
		constexpr double Scales[] = { 1.0, 1.8 };
	}

	/** One band segment's shape (m): the cracks between segments, the underside's slope, the arc's step length. */
	namespace Segment
	{
		constexpr double Gap = 0.025;
		constexpr double UndersideSlope = 0.3;
		constexpr double StepM = 0.25;
		/** The neon edge: a strip standing proud on the top's outer edge and a line round the outer face. */
		constexpr double StripInset = 0.07;
		constexpr double StripOuterInset = 0.01;
		constexpr double StripRise = 0.012;
		constexpr double LineProud = 0.006;
		constexpr double LineTop = -0.03;
		constexpr double LineBottom = -0.09;
	}

	/** The bands' looks: lacquered planks in deep indigo, every other band a shade warmer; the clay core paler. */
	namespace BandLook
	{
		const FSurfaceLook PlankLooks[] = {
			{ Hex(TEXT("#241c44")), TEXT("dark_wooden_planks"), 1.6, 0.45, 0.6, 0.4, 0.5, 0.0, 0.0, false, 0.6 },
			{ Hex(TEXT("#1d1838")), TEXT("dark_wooden_planks"), 1.6, 0.45, 0.6, 0.4, 0.5, 0.0, 0.0, false, 0.6 },
		};
		const FSurfaceLook CoreLook{ Hex(TEXT("#6a2c27")), TEXT("gravelly_sand"), 1.2, 0.45, 0.6, 0.4, 0.85, 0.0, 0.0, false, 0.6 };
		const FSurfaceLook RockLook{ Hex(TEXT("#3a3150")), TEXT("rock_face"), 1.6, 0.55, 0.8, 0.5, 0.85, 0.0, 0.0, true, 0.5 };
		/** A band breaks up as a wave round the ring from a random start (over this many seconds), plus jitter. */
		constexpr double BreakWaveSeconds = 0.55;
		constexpr float BreakJitterSeconds = 0.12f;
		constexpr float AxisTilt = 0.3f;
		constexpr float MinSpinDeg = 90.f;
		constexpr float MaxSpinDeg = 260.f;
		constexpr float MinDrift = 0.4f;
		constexpr float MaxDrift = 1.6f;
		/** The warning wash: red, laid just inside the band's edges, just above its top (m). */
		const TCHAR* const WarnColor = TEXT("#ff2a3a");
		constexpr float WarnGlow = 3.f;
		constexpr double WarnInset = 0.03;
		constexpr double WarnRise = 0.02;
		constexpr int32 WarnSteps = 96;
	}

	/** The core's markings: the straw-rope circle (a glowing tawara) and the two white start lines (m). */
	namespace CoreMarks
	{
		const FPlainLook Chalk{ TEXT("#f4efe6"), 0.7, 0.0, 0.35 };
		constexpr double StartLineY = 0.55;
		constexpr double StartLineRise = 0.012;
		const FVector StartLineScale(0.07, 0.8, 0.012);
		const FPlainLook Straw{ TEXT("#d9b56a"), 0.75, 0.0, 0.25 };
		constexpr double RopeInset = 0.12;
		constexpr double RopeRise = 0.035;
		constexpr double RopeThickCm = 6.0;
		constexpr int32 RopeLinks = 64;
		constexpr int32 RopeSides = 8;
	}

	/** The keel under the core: a lathed rock tapering to a point (m: radius, height), crystals glowing out of it. */
	namespace Keel
	{
		const FVector2D Profile[] = { { 0.0, -6.6 }, { 0.35, -6.2 }, { 0.8, -5.5 }, { 1.25, -4.7 }, { 1.65, -3.9 }, { 1.95, -3.1 }, { 2.05, -2.7 } };
		constexpr double TopRadius = 2.0;
		/** The keel's top tucks this far (cm) up into the core's underside. */
		constexpr double TuckCm = 25.0;
		constexpr int32 Sides = 20;
		const FSurfaceLook RockLook{ Hex(TEXT("#2f2846")), TEXT("rock_face"), 2.0, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 };
		/** Crystals (two in three cyan, the rest pink) along the keel between these depths, a little inside its skin. */
		constexpr int32 Crystals = 26;
		const TCHAR* const CyanColor = TEXT("#5ee9ff");
		constexpr float CyanGlow = 7.f;
		const TCHAR* const PinkColor = TEXT("#ff58c8");
		constexpr float PinkGlow = 6.f;
		constexpr float CrystalLow = -5.8f;
		constexpr float CrystalHigh = -2.6f;
		/** The keel's radius from its tip (TipRadius at TipDepth) to its shoulder (ShoulderRadius, Span higher). */
		constexpr double TipRadius = 0.3;
		constexpr double ShoulderRadius = 2.05;
		constexpr double TipDepth = 6.6;
		constexpr double Span = 3.9;
		constexpr double Inset = 0.05;
		constexpr float CrystalPitchDeg = 40.f;
		constexpr float CrystalRollDeg = 30.f;
		/** Crystal sizes (shape scales): width, depth and length ranges. */
		constexpr float CrystalMinWidth = 0.12f;
		constexpr float CrystalMaxWidth = 0.3f;
		constexpr float CrystalMinDepth = 0.08f;
		constexpr float CrystalMaxDepth = 0.16f;
		constexpr float CrystalMinLength = 0.35f;
		constexpr float CrystalMaxLength = 0.8f;
		/** Glints hanging under the bands near the rim. */
		constexpr int32 Glints = 30;
		constexpr float GlintInner = 3.0f;
		constexpr float GlintOuter = 8.6f;
		constexpr double GlintDrop = 0.05;
		constexpr float GlintMinPitchDeg = 150.f;
		constexpr float GlintMaxPitchDeg = 210.f;
		constexpr double GlintWidth = 0.08;
		constexpr float GlintMinLength = 0.2f;
		constexpr float GlintMaxLength = 0.45f;
		/** Its own glow on the water below. */
		const FVector LightAtM(0.0, 0.0, -6.0);
		const TCHAR* const LightColor = TEXT("#55dfff");
		constexpr float LightLumens = 400.f;
		constexpr float LightRadiusCm = 1400.f;
	}

	/** The sea: near-black and glassy, so the moon, the neon and the lanterns mirror in it; a dark bed far below. */
	namespace Sea
	{
		const TCHAR* const WaterPath = TEXT("/Game/Tracks/Shared/Materials/M_TrackWater.M_TrackWater");
		const TCHAR* const PlanePath = TEXT("/Engine/BasicShapes/Plane.Plane");
		const TCHAR* const WaterColor = TEXT("#05060f");
		const FScalarParam WaterScalars[] = {
			{ TEXT("Roughness"), 0.06f },
			{ TEXT("WaveStrength"), 0.42f },
			{ TEXT("WaveTileA"), 7.0f },
			{ TEXT("WaveTileB"), 19.0f },
		};
		const FLinearParam WaterColors[] = {
			{ TEXT("Scattering"), FLinearColor(0.004f, 0.006f, 0.022f, 1.f) },
			{ TEXT("Absorption"), FLinearColor(0.5f, 0.2f, 0.1f, 1.f) },
		};
		const FPlainLook Fallback{ TEXT("#070a1c"), 0.08, 0.0 };
		const FPlainLook Bed{ TEXT("#03040a"), 1.0 };
		constexpr double BedDepthCm = 3000.0;
		const FVector Extent(4000.0, 4000.0, 1.0);
	}
}

void ASumoArena::BuildLook()
{
	using namespace NightLook;
	Moon->SetWorldRotation((-MoonDirection).Rotation());
	Moon->SetLightColor(Hex(MoonColor));
	Moon->SetIntensity(MoonIntensity);
	Moon->SetCastShadows(true);
	Moon->SetAtmosphereSunLight(false);
	Moon->SetForwardShadingPriority(1);
	Moon->SetLightSourceAngle(MoonSourceAngle);
	Rim->SetWorldRotation((-RimDirection.GetSafeNormal()).Rotation());
	Rim->SetLightColor(Hex(RimColor));
	Rim->SetIntensity(RimIntensity);
	Rim->SetCastShadows(false);
	Rim->SetAtmosphereSunLight(false);
	Rim->SetForwardShadingPriority(0);
	SkyLight->SetIntensity(SkyIntensity);
	SkyLight->SetLightColor(Hex(SkyColor));
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(LowerHemisphereColor));
	Fog->SetStartDistance(FogStartCm);
	Fog->SetFogDensity(FogDensity);
	Fog->SetFogHeightFalloff(FogHeightFalloff);
	Fog->SetFogMaxOpacity(FogMaxOpacity);
	Fog->SetFogCutoffDistance(FogCutoffCm);
	Fog->SetFogInscatteringColor(Hex(FogColor) * FogInscatter);
	Fog->SetDirectionalInscatteringColor(Hex(FogMoonColor) * FogMoonInscatter);
	Fog->SetDirectionalInscatteringExponent(FogMoonExponent);
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = Bloom;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = Vignette;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(Contrast, Contrast, Contrast, 1.f);
	S.bOverride_ColorOffset = true;
	S.ColorOffset = BlackLift;
}

void ASumoArena::BuildSky()
{
	if (UMaterialInterface* SkyMaster = Presentation::LoadAsset<UMaterialInterface>(SkyPath, TEXT("the sky dome stays unshaded")))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		for (const FColorParam& Param : SkyColors)
		{
			Sky->SetVectorParameterValue(Param.Name, Hex(Param.Hex));
		}
		for (const FScalarParam& Param : SkyScalars)
		{
			Sky->SetScalarParameterValue(Param.Name, Param.Value);
		}
		// The moon is the sky shader's sun.
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(MoonDirection.X, MoonDirection.Y, MoonDirection.Z, 0.0));
		if (UTexture* Clouds = Presentation::LoadAsset<UTexture>(CloudsPath, TEXT("a cloudless sky")))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
		}
		SkyDome->SetMaterial(0, Sky);
	}
	SkyLight->RecaptureSky();
	FRandomStream R(0x57a2);
	UInstancedStaticMeshComponent* StarGroup = Groups.Find(Sphere, Materials.GlowOpaque(this, Hex(Stars::Color) * Stars::Glow), false);
	for (int32 i = 0; i < Stars::Count; ++i)
	{
		const double Az = R.FRandRange(0.0, 2.0 * PI);
		// Uniform in the sine of the elevation: denser overhead, none at the horizon.
		const double El = FMath::Asin(R.FRandRange(Stars::LowestSine, 1.f));
		const FVector Dir(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
		const double Size = R.FRandRange(Stars::MinCm, Stars::MaxCm) * (R.FRand() < Stars::BrightChance ? Stars::BrightScale : 1.f);
		StarGroup->AddInstance(FTransform(FRotator::ZeroRotator, Dir * Stars::DomeCm, FVector(Size)), false);
	}
	if (UMaterialInstanceDynamic* Soft = Presentation::MakeAdditive(this, Hex(MoonHalo::Color) * MoonHalo::Glow, MoonHalo::Opacity))
	{
		for (const double Scale : MoonHalo::Scales)
		{
			Mesh(Sphere, Soft, FTransform(FRotator::ZeroRotator, MoonDirection * MoonHalo::DistanceCm, FVector(MoonHalo::Size * Scale)), false);
		}
	}
}

UStaticMesh* ASumoArena::SectorMesh(double Inner, double Outer, double Span, const TCHAR* Name)
{
	using namespace Segment;
	// One segment of a band (or the whole core), its origin at the segment's middle so it can tumble about it.
	FMeshBuilder B;
	const int32 Top = B.Section(TEXT("Top"));
	const int32 Side = B.Section(TEXT("Side"));
	const int32 Trim = B.Section(TEXT("Trim"));
	const bool bCore = Inner < 1e-3;
	const double R0 = bCore ? 0.0 : Inner + Gap;
	const double R1 = Outer - Gap;
	const double Mid = (Inner + Outer) / 2.0;
	const double Half = Span / 2.0 - (bCore ? 0.0 : Gap / Mid);
	const int32 Steps = FMath::Max(3, FMath::CeilToInt32(Span * Outer / StepM));
	const FVector Origin = bCore ? FVector::ZeroVector : Cm(Mid, 0.0, 0.0);
	auto P = [&Origin](double R, double A, double Z) { return Cm(R * FMath::Cos(A), R * FMath::Sin(A), Z) - Origin; };
	auto Radial = [](double A) { return FVector(FMath::Cos(A), FMath::Sin(A), 0.0); };
	// A quad a-b-c-d (two triangles) into a section; a and b take the first normal, c and d the second.
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
		const double In = R1 - StripInset, Out = R1 - StripOuterInset;
		Quad(Trim, P(In, A0, StripRise), P(Out, A0, StripRise), P(Out, A1, StripRise), P(In, A1, StripRise), FVector::UpVector, FVector::UpVector);
		const double Rf = R1 + LineProud;
		Quad(Trim, P(Rf, A0, LineTop), P(Rf, A0, LineBottom), P(Rf, A1, LineBottom), P(Rf, A1, LineTop), Radial(A0), Radial(A1));
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
	UMaterialInterface* Rock = Materials.Surface(this, BandLook::RockLook);
	for (int32 b = 0; b < BandCount; ++b)
	{
		BuildBand(b, Rock);
	}
	BuildCoreMarkings();
}

void ASumoArena::BuildBand(int32 Index, UMaterialInterface* Rock)
{
	using namespace BandLook;
	FBand& Band = Bands.AddDefaulted_GetRef();
	const bool bCore = Index == BandCount - 1;
	Band.Outer = ArenaRadius - Index * BandWidth;
	Band.Inner = bCore ? 0.0 : Band.Outer - BandWidth;
	Band.Segments = BandSegments[Index];
	Band.TrimColor = Hex(bCore ? CoreTrimColor : TrimColors[Index % 2]) * (Index == 0 ? OuterTrimGlow : TrimGlow);
	const double Span = 2.0 * PI / Band.Segments;
	UStaticMesh* Sector = SectorMesh(Band.Inner, Band.Outer, Span, *FString::Printf(TEXT("SumoBand%d"), Index));
	UMaterialInterface* TopMat = Materials.Surface(this, bCore ? CoreLook : PlankLooks[Index % 2]);
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
		F.Axis = FVector(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f), R.FRandRange(-AxisTilt, AxisTilt)).GetSafeNormal();
		F.Spin = R.FRandRange(MinSpinDeg, MaxSpinDeg) * (R.FRand() < 0.5f ? -1.f : 1.f);
		F.Drift = R.FRandRange(MinDrift, MaxDrift);
	}
	BuildWarningWash(Band, Index);
}

void ASumoArena::BuildWarningWash(const FBand& Band, int32 Index)
{
	using namespace BandLook;
	// An additive annulus laid on the band, shown while it is warned.
	UMaterialInstanceDynamic* WarnMat = Presentation::MakeAdditive(this, Hex(WarnColor) * WarnGlow, 0.f);
	if (!WarnMat)
	{
		return;
	}
	FMeshBuilder W;
	const int32 Section = W.Section(TEXT("Warn"));
	Presentation::AddAnnulus(W, Section, FMath::Max(0.0, Band.Inner + WarnInset), Band.Outer - WarnInset, WarnRise, WarnSteps, 100.0);
	UStaticMeshComponent* Wash = Mesh(W.Build(this, *FString::Printf(TEXT("SumoWarn%d"), Index)), WarnMat, FTransform::Identity, false);
	Wash->SetVisibility(false);
	BandWarnings.Add(Wash);
	BandWarningMats.Add(WarnMat);
}

void ASumoArena::BuildCoreMarkings()
{
	using namespace CoreMarks;
	UMaterialInterface* ChalkMat = CachedPlain(Materials, this, Chalk);
	for (const double Y : { -StartLineY, StartLineY })
	{
		CoreDecor.Add(Mesh(Cube, ChalkMat, FTransform(FRotator(0.0, 90.0, 0.0), Cm(0.0, Y, StartLineRise), StartLineScale), false));
	}
	FMeshBuilder E;
	E.Section(TEXT("Rope"));
	const double R = CoreRadius - RopeInset;
	for (int32 i = 0; i < RopeLinks; ++i)
	{
		// The rope ring as a chain of short limbs (a torus).
		const double A0 = 2.0 * PI * i / RopeLinks, A1 = 2.0 * PI * (i + 1) / RopeLinks;
		E.Limb(TEXT("Rope"), Cm(R * FMath::Cos(A0), R * FMath::Sin(A0), RopeRise), Cm(R * FMath::Cos(A1), R * FMath::Sin(A1), RopeRise), RopeThickCm, RopeThickCm, RopeSides,
			FVector2D::ZeroVector);
	}
	CoreDecor.Add(Mesh(E.Build(this, TEXT("SumoRope")), CachedPlain(Materials, this, Straw), FTransform::Identity, true));
}

void ASumoArena::BuildKeel()
{
	using namespace Keel;
	FMeshBuilder K;
	TArray<FVector2D> Outline;
	const double TopZ = -Depth(CoreRadius) * 100.0 + TuckCm;
	for (const FVector2D& Point : Profile)
	{
		Outline.Add(FVector2D(Point.X * 100.0, Point.Y * 100.0));
	}
	Outline.Add(FVector2D(TopRadius * 100.0, TopZ));
	Outline.Add(FVector2D(0.0, TopZ));
	K.Lathe(TEXT("Rock"), Outline, Sides, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
	Mesh(K.Build(this, TEXT("SumoKeel")), Materials.Surface(this, RockLook), FTransform::Identity, true);
	FRandomStream R(0xc4a1);
	UInstancedStaticMeshComponent* Cyan = Groups.Find(Sphere, Materials.GlowOpaque(this, Hex(CyanColor) * CyanGlow), false);
	UInstancedStaticMeshComponent* Pink = Groups.Find(Cube, Materials.GlowOpaque(this, Hex(PinkColor) * PinkGlow), false);
	for (int32 i = 0; i < Crystals; ++i)
	{
		const double A = R.FRandRange(0.0, 2.0 * PI);
		const double Z = R.FRandRange(CrystalLow, CrystalHigh);
		const double Rad = FMath::Lerp(TipRadius, ShoulderRadius, FMath::Clamp((Z + TipDepth) / Span, 0.0, 1.0)) - Inset;
		const FVector At = Cm(Rad * FMath::Cos(A), Rad * FMath::Sin(A), Z);
		const FRotator Rot(R.FRandRange(-CrystalPitchDeg, CrystalPitchDeg), FMath::RadiansToDegrees(A), R.FRandRange(-CrystalRollDeg, CrystalRollDeg));
		const FVector Size(R.FRandRange(CrystalMinWidth, CrystalMaxWidth), R.FRandRange(CrystalMinDepth, CrystalMaxDepth), R.FRandRange(CrystalMinLength, CrystalMaxLength));
		(i % 3 ? Cyan : Pink)->AddInstance(FTransform(Rot, At, Size), false);
	}
	for (int32 i = 0; i < Glints; ++i)
	{
		const double A = R.FRandRange(0.0, 2.0 * PI);
		const double Rad = R.FRandRange(GlintInner, GlintOuter);
		const FVector At = Cm(Rad * FMath::Cos(A), Rad * FMath::Sin(A), -Depth(Rad) - GlintDrop);
		Cyan->AddInstance(FTransform(FRotator(R.FRandRange(GlintMinPitchDeg, GlintMaxPitchDeg), 0.0, 0.0), At, FVector(GlintWidth, GlintWidth, R.FRandRange(GlintMinLength, GlintMaxLength))), false);
	}
	Light(Cm(LightAtM.X, LightAtM.Y, LightAtM.Z), Hex(LightColor), LightLumens, LightRadiusCm);
}

void ASumoArena::BuildSea()
{
	using namespace Sea;
	UStaticMesh* Plane = Presentation::LoadAsset<UStaticMesh>(PlanePath, TEXT("no sea surface"));
	UMaterialInterface* Water = nullptr;
	if (UMaterialInterface* WaterMaster = Presentation::LoadAsset<UMaterialInterface>(WaterPath, TEXT("the sea is a plain dark plane")))
	{
		UMaterialInstanceDynamic* W = UMaterialInstanceDynamic::Create(WaterMaster, this);
		W->SetVectorParameterValue(TEXT("Color"), Hex(WaterColor));
		for (const FScalarParam& Param : WaterScalars)
		{
			W->SetScalarParameterValue(Param.Name, Param.Value);
		}
		for (const FLinearParam& Param : WaterColors)
		{
			W->SetVectorParameterValue(Param.Name, Param.Value);
		}
		Water = W;
	}
	if (!Water)
	{
		Water = CachedPlain(Materials, this, Fallback);
	}
	if (Plane)
	{
		Mesh(Plane, Water, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, WaterZ), Extent), false);
	}
	Mesh(Cube, CachedPlain(Materials, this, Bed), FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, WaterZ - BedDepthCm), Extent), false);
}

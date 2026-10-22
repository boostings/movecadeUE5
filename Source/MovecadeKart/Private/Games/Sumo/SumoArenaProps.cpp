// ASumoArena's props round the ring: the torii in the water, the drifting paper lanterns, the four tasselled posts in
// the players' colours, small rocks drifting under the ring, the islands on the horizon and the neon spill lights.
// Each prop's dimensions are a labelled table at the top (metres unless they say cm; scales are the engine shapes').

#include "Games/Sumo/SumoArena.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Sumo/SumoArenaLayout.h"
#include "Games/Sumo/SumoPresentation.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace MovecadeKart::Sumo::ArenaLayout;
using namespace MovecadeKart::Art;
using MovecadeKart::FMeshBuilder;
using MovecadeKart::Sumo::Presentation::CachedPlain;
using MovecadeKart::Sumo::Presentation::FPlainLook;

namespace
{
	/** The sea's surface in the rules' metres. */
	constexpr double SeaM = ASumoArena::WaterZ / 100.0;

	/** The torii: behind the ring, off to the right, vermilion with black caps and a neon line under each lintel. */
	namespace Torii
	{
		const FVector2D At(27.0, 7.0);
		constexpr double Width = 13.0;
		constexpr double Height = 16.0;
		const FPlainLook Vermilion{ TEXT("#c23322"), 0.45, 0.0, 0.04 };
		const FPlainLook Black{ TEXT("#121018"), 0.4, 0.2 };
		const TCHAR* const NeonColor = TEXT("#41e8ff");
		constexpr float NeonGlow = 8.f;
		/** The pillars stand this share of the width either side of the middle, with a black foot (cm up, scale). */
		constexpr double PillarSpread = 0.36;
		constexpr double PillarRadius = 1.1;
		constexpr double FootZCm = 60.0;
		const FVector FootScale(1.35, 1.35, 1.2);
		/** The kasagi (top beam) over the top, its upswept ends, the shimaki under it, the nuki lower down, the strut. */
		constexpr double KasagiZ = 0.55;
		const FVector KasagiScale(1.5, 2.4, 0.45);
		constexpr double KasagiEndOut = 0.9;
		constexpr double KasagiEndZ = 0.72;
		constexpr double KasagiEndTiltDeg = 7.0;
		const FVector KasagiEndScale(1.5, 3.2, 0.45);
		const FVector ShimakiScale(1.25, 1.4, 0.7);
		constexpr double NukiDrop = 2.4;
		constexpr double NukiWidthShare = 0.92;
		const FVector NukiScale(0.9, 0.0, 0.55);
		constexpr double StrutDrop = 1.2;
		const FVector StrutScale(0.6, 0.6, 1.8);
		/** The neon lines: under the shimaki (overhanging the width) and under the nuki (a share of it). */
		constexpr double NeonTopFront = 0.66;
		constexpr double NeonTopDrop = 0.38;
		const FVector NeonTopScale(0.03, 1.3, 0.06);
		constexpr double NeonLowFront = 0.47;
		constexpr double NeonLowDrop = 2.71;
		constexpr double NeonLowWidthShare = 0.9;
		const FVector NeonLowScale(0.03, 0.0, 0.05);
		/** A warm light on its face. */
		constexpr double LightFront = 3.0;
		constexpr double LightDrop = 4.0;
		const TCHAR* const LightColor = TEXT("#ff8a5a");
		constexpr float LightLumens = 1800.f;
		constexpr float LightRadiusCm = 2200.f;
	}

	/** The lanterns: mostly ahead of the camera, a quarter behind it, thicker toward the horizon. */
	namespace Lantern
	{
		constexpr int32 Count = 70;
		const TCHAR* const Color = TEXT("#ffb263");
		constexpr float Brightness = 7.f;
		constexpr float BearingSpread = 1.3f;
		constexpr float BehindChance = 0.25f;
		constexpr double NearM = 14.0;
		constexpr double SpreadM = 70.0;
		constexpr float Falloff = 0.7f;
		constexpr double FloatM = 0.15;
		/** A few lanterns throw real light on the water. */
		const FVector2D Lit[] = { { 15.0, -6.0 }, { 18.0, 9.0 }, { -2.0, 15.0 }, { 4.0, -16.0 }, { 30.0, -2.0 } };
		constexpr double LightM = 1.2;
		const TCHAR* const LightColor = TEXT("#ff9a4a");
		constexpr float LightLumens = 500.f;
		constexpr float LightRadiusCm = 1200.f;
	}

	/** The posts at the ring's corners (the dohyo's four colours, here the players'), on floating rocks. In cm on the rock. */
	namespace Post
	{
		constexpr double RadiusM = 11.4;
		constexpr double FloatM = -0.6;
		constexpr double Angles[] = { -0.75 * PI, 0.75 * PI, 0.25 * PI, -0.25 * PI };
		const FSurfaceLook RockLook{ Hex(TEXT("#3a3150")), TEXT("rock_face"), 1.4, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 };
		const FPlainLook Lacquer{ TEXT("#16121f"), 0.32, 0.3 };
		const FPlainLook Gold{ TEXT("#e0b25a"), 0.3, 1.0 };
		/** The rock: a lathed cone, squashed a little across. */
		const TArray<FVector2D> RockProfile = { { 0.0, -260.0 }, { 40.0, -230.0 }, { 95.0, -150.0 }, { 135.0, -60.0 }, { 150.0, -5.0 }, { 140.0, 10.0 }, { 0.0, 12.0 } };
		constexpr int32 RockSides = 10;
		const FVector RockScale(1.0, 0.85, 1.0);
		/** The pole, its gold collars, the lamp (lit in the seat's colour) and its cap. */
		const FVector PoleAt(0.0, 0.0, 190.0);
		const FVector PoleScale(0.34, 0.34, 3.6);
		const FVector LowCollarAt(0.0, 0.0, 30.0);
		const FVector LowCollarScale(0.48, 0.48, 0.2);
		const FVector HighCollarAt(0.0, 0.0, 372.0);
		const FVector HighCollarScale(0.5, 0.5, 0.1);
		constexpr float LampGlow = 6.f;
		const FVector LampAt(0.0, 0.0, 405.0);
		const FVector LampScale(0.42, 0.42, 0.55);
		const FVector CapAt(0.0, 0.0, 437.0);
		const FVector CapScale(0.6, 0.6, 0.08);
		/** The tassel hangs on a cord this far out from the pole, toward the sea. */
		constexpr double TasselOutCm = 30.0;
		const FVector CordAt(0.0, 0.0, 330.0);
		const FVector CordScale(0.03, 0.03, 0.7);
		const TArray<FVector2D> TasselProfile = { { 0.0, -48.0 }, { 14.0, -46.0 }, { 11.0, -20.0 }, { 5.0, 0.0 }, { 0.0, 2.0 } };
		constexpr int32 TasselSides = 10;
		const FVector TasselAt(0.0, 0.0, 296.0);
		const FVector LightAt(0.0, 0.0, 400.0);
		constexpr float LightLumens = 260.f;
		constexpr float LightRadiusCm = 800.f;
	}

	/** Small rocks drifting round under the ring, each with a crystal. */
	namespace DriftRock
	{
		constexpr int32 Count = 6;
		constexpr double PhaseShare = 0.3;
		constexpr float MinRadiusM = 12.5f;
		constexpr float MaxRadiusM = 15.5f;
		constexpr float LowM = -4.5f;
		constexpr float HighM = -2.0f;
		constexpr float MinScale = 0.5f;
		constexpr float MaxScale = 1.1f;
		const FSurfaceLook RockLook{ Hex(TEXT("#2f2846")), TEXT("rock_face"), 1.2, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 };
		const TCHAR* const CrystalColor = TEXT("#5ee9ff");
		constexpr float CrystalGlow = 6.f;
		const TArray<FVector2D> RockProfile = { { 0.0, -150.0 }, { 60.0, -110.0 }, { 95.0, -40.0 }, { 90.0, 10.0 }, { 0.0, 25.0 } };
		constexpr int32 RockSides = 9;
		const FVector RockScale(1.0, 0.8, 1.0);
		constexpr float CrystalTiltDeg = 25.f;
		constexpr double CrystalYawDeg = 30.0;
		constexpr double CrystalZCm = 30.0;
		const FVector CrystalScale(0.18, 0.18, 0.55);
	}

	/** Islands on the horizon (bearings in radians, 0 straight ahead); two carry a pagoda with lit windows. */
	namespace Island
	{
		constexpr double Bearings[] = { -0.55, -0.25, 0.12, 0.42, 0.7, 2.6, -2.5 };
		constexpr int32 PagodaA = 1;
		constexpr int32 PagodaB = 3;
		constexpr float MinDistanceM = 260.f;
		constexpr float MaxDistanceM = 420.f;
		constexpr float MinHeightM = 14.f;
		constexpr float MaxHeightM = 42.f;
		constexpr float MinWidthM = 40.f;
		constexpr float MaxWidthM = 110.f;
		constexpr double SinkM = 0.5;
		const FPlainLook HillLook{ TEXT("#0c0d22"), 0.9 };
		const TCHAR* const WindowColor = TEXT("#ffbf6e");
		constexpr float WindowGlow = 5.f;
		constexpr int32 HillSides = 14;
		const FVector HillScale(1.0, 0.5, 1.0);
		/** A pagoda: tiers stacked on the summit, each a roof over a lit band, narrowing upward (cm, shape scales). */
		constexpr int32 PagodaTiers = 5;
		constexpr double PagodaBaseCm = 200.0;
		constexpr double TierCm = 260.0;
		constexpr double RoofCm = 120.0;
		constexpr double RoofWidth = 9.0;
		constexpr double RoofStep = 1.3;
		constexpr double RoofThick = 0.5;
		constexpr double WindowWidth = 5.6;
		constexpr double WindowStep = 0.8;
		constexpr double WindowHeight = 1.2;
	}

	/** Neon spill over the ring: cyan from the left, magenta from the right, a soft white overhead. */
	struct FSpillLight
	{
		FVector AtM;
		const TCHAR* Color;
		float Lumens;
		float RadiusCm;
	};
	const FSpillLight SpillLights[] = {
		{ FVector(-2.0, -10.0, 4.0), TEXT("#43d9ff"), 1600.f, 1800.f },
		{ FVector(-2.0, 10.0, 4.0), TEXT("#ff4fae"), 1500.f, 1800.f },
		{ FVector(6.0, 0.0, 7.0), TEXT("#b8b4ff"), 900.f, 2000.f },
	};
}

void ASumoArena::BuildProps()
{
	FRandomStream R(0x7041);
	BuildTorii();
	BuildLanterns(R);
	BuildPosts(R);
	BuildDriftRocks(R);
	BuildIslands(R);
	for (const FSpillLight& Spill : SpillLights)
	{
		RimLights.Add(Light(Cm(Spill.AtM.X, Spill.AtM.Y, Spill.AtM.Z), Hex(Spill.Color), Spill.Lumens, Spill.RadiusCm));
	}
}

void ASumoArena::BuildTorii()
{
	using namespace Torii;
	UMaterialInterface* VermilionMat = CachedPlain(Materials, this, Vermilion);
	UMaterialInterface* BlackMat = CachedPlain(Materials, this, Black);
	UMaterialInterface* Neon = Materials.GlowOpaque(this, Hex(NeonColor) * NeonGlow);
	const double W = Width;
	for (const double Side : { -1.0, 1.0 })
	{
		const FVector Base = Cm(At.X, At.Y + Side * W * PillarSpread, SeaM);
		Mesh(Cylinder, VermilionMat, FTransform(FRotator::ZeroRotator, Base + FVector(0.0, 0.0, Height * 50.0), FVector(PillarRadius, PillarRadius, Height)), true);
		Mesh(Cylinder, BlackMat, FTransform(FRotator::ZeroRotator, Base + FVector(0.0, 0.0, FootZCm), FootScale), true);
	}
	const double Top = SeaM + Height;
	Mesh(Cube, BlackMat, FTransform(FRotator::ZeroRotator, Cm(At.X, At.Y, Top + KasagiZ), FVector(KasagiScale.X, W + KasagiScale.Y, KasagiScale.Z)), true);
	for (const double Side : { -1.0, 1.0 })
	{
		// The kasagi's upswept ends.
		Mesh(Cube, BlackMat, FTransform(FRotator(0.0, 0.0, Side * KasagiEndTiltDeg), Cm(At.X, At.Y + Side * (W * 0.5 + KasagiEndOut), Top + KasagiEndZ), KasagiEndScale), true);
	}
	Mesh(Cube, VermilionMat, FTransform(FRotator::ZeroRotator, Cm(At.X, At.Y, Top), FVector(ShimakiScale.X, W + ShimakiScale.Y, ShimakiScale.Z)), true);
	Mesh(Cube, VermilionMat, FTransform(FRotator::ZeroRotator, Cm(At.X, At.Y, Top - NukiDrop), FVector(NukiScale.X, W * NukiWidthShare, NukiScale.Z)), true);
	Mesh(Cube, VermilionMat, FTransform(FRotator::ZeroRotator, Cm(At.X, At.Y, Top - StrutDrop), StrutScale), true);
	Mesh(Cube, Neon, FTransform(FRotator::ZeroRotator, Cm(At.X - NeonTopFront, At.Y, Top - NeonTopDrop), FVector(NeonTopScale.X, W + NeonTopScale.Y, NeonTopScale.Z)), false);
	Mesh(Cube, Neon, FTransform(FRotator::ZeroRotator, Cm(At.X - NeonLowFront, At.Y, Top - NeonLowDrop), FVector(NeonLowScale.X, W * NeonLowWidthShare, NeonLowScale.Z)), false);
	Light(Cm(At.X - LightFront, At.Y, Top - LightDrop), Hex(LightColor), LightLumens, LightRadiusCm);
}

void ASumoArena::BuildLanterns(FRandomStream& R)
{
	using namespace Lantern;
	// Their own instances (not a shared group): AnimateProps bobs each one.
	Lanterns = OwnInstances(Sphere, TEXT("Lanterns"), false);
	Lanterns->SetMaterial(0, Materials.GlowOpaque(this, Hex(Color) * Brightness));
	for (int32 i = 0; i < Count; ++i)
	{
		const double A = R.FRandRange(-BearingSpread, BearingSpread) + (R.FRand() < BehindChance ? PI : 0.0);
		const double D = NearM + FMath::Pow(R.FRand(), Falloff) * SpreadM;
		const FVector At = Cm(D * FMath::Cos(A), D * FMath::Sin(A), SeaM + FloatM);
		LanternRest.Add(At);
		Lanterns->AddInstance(FTransform(FRotator::ZeroRotator, At, LanternScale), false);
	}
	for (const FVector2D& L : Lit)
	{
		Light(Cm(L.X, L.Y, SeaM + LightM), Hex(LightColor), LightLumens, LightRadiusCm);
	}
}

void ASumoArena::BuildPosts(FRandomStream& R)
{
	using namespace Post;
	UMaterialInterface* Rock = Materials.Surface(this, RockLook);
	UMaterialInterface* LacquerMat = CachedPlain(Materials, this, Lacquer);
	UMaterialInterface* GoldMat = CachedPlain(Materials, this, Gold);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Angles); ++i)
	{
		const double A = Angles[i];
		USceneComponent* Float = Floater(Cm(RadiusM * FMath::Cos(A), RadiusM * FMath::Sin(A), FloatM));
		FMeshBuilder K;
		K.Lathe(TEXT("Rock"), RockProfile, RockSides, FVector::ZeroVector, FVector::UpVector, RockScale, FVector2D::ZeroVector);
		Mesh(K.Build(this, TEXT("SumoPostRock")), Rock, FTransform(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector::ZeroVector), true, Float);
		Mesh(Cylinder, LacquerMat, FTransform(FRotator::ZeroRotator, PoleAt, PoleScale), true, Float);
		Mesh(Cylinder, GoldMat, FTransform(FRotator::ZeroRotator, LowCollarAt, LowCollarScale), true, Float);
		Mesh(Cylinder, GoldMat, FTransform(FRotator::ZeroRotator, HighCollarAt, HighCollarScale), true, Float);
		// The post's own glow: SetPlayers lights it in its seat's colour or dims it.
		UMaterialInstanceDynamic* Tassel = GlowOpaque(this, ASumoArena::SlotColor(i) * LampGlow);
		TasselMats.Add(Tassel);
		Mesh(Cylinder, Tassel, FTransform(FRotator::ZeroRotator, LampAt, LampScale), false, Float);
		Mesh(Cube, LacquerMat, FTransform(FRotator::ZeroRotator, CapAt, CapScale), true, Float);
		const FVector Out = FVector(FMath::Cos(A), FMath::Sin(A), 0.0);
		Mesh(Cylinder, LacquerMat, FTransform(FRotator::ZeroRotator, -Out * TasselOutCm + CordAt, CordScale), false, Float);
		FMeshBuilder T;
		T.Lathe(TEXT("Tassel"), TasselProfile, TasselSides, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		Mesh(T.Build(this, TEXT("SumoTassel")), Tassel, FTransform(FRotator::ZeroRotator, -Out * TasselOutCm + TasselAt), false, Float);
		TasselLights.Add(Light(LightAt, ASumoArena::SlotColor(i), LightLumens, LightRadiusCm, Float));
	}
}

void ASumoArena::BuildDriftRocks(FRandomStream& R)
{
	using namespace DriftRock;
	UMaterialInterface* Rock = Materials.Surface(this, RockLook);
	UMaterialInterface* Crystal = Materials.GlowOpaque(this, Hex(CrystalColor) * CrystalGlow);
	for (int32 i = 0; i < Count; ++i)
	{
		// Evenly round the ring, at a random distance and depth.
		const double A = (i + PhaseShare) * 2.0 * PI / static_cast<double>(Count);
		const double D = R.FRandRange(MinRadiusM, MaxRadiusM);
		USceneComponent* Float = Floater(Cm(D * FMath::Cos(A), D * FMath::Sin(A), R.FRandRange(LowM, HighM)));
		const double S = R.FRandRange(MinScale, MaxScale);
		FMeshBuilder K;
		K.Lathe(TEXT("Rock"), RockProfile, RockSides, FVector::ZeroVector, FVector::UpVector, RockScale * S, FVector2D::ZeroVector);
		Mesh(K.Build(this, TEXT("SumoDrift")), Rock, FTransform(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector::ZeroVector), true, Float);
		Mesh(Cube, Crystal, FTransform(FRotator(R.FRandRange(-CrystalTiltDeg, CrystalTiltDeg), CrystalYawDeg, R.FRandRange(-CrystalTiltDeg, CrystalTiltDeg)), FVector(0.0, 0.0, CrystalZCm * S),
			CrystalScale * S), false, Float);
	}
}

void ASumoArena::BuildIslands(FRandomStream& R)
{
	using namespace Island;
	UMaterialInterface* Hill = CachedPlain(Materials, this, HillLook);
	UMaterialInterface* Window = Materials.GlowOpaque(this, Hex(WindowColor) * WindowGlow);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Bearings); ++i)
	{
		const double D = R.FRandRange(MinDistanceM, MaxDistanceM);
		const double H = R.FRandRange(MinHeightM, MaxHeightM);
		const double W = R.FRandRange(MinWidthM, MaxWidthM);
		// A soft hill: wide at the waterline, rounding to a summit (cm).
		FMeshBuilder K;
		K.Lathe(TEXT("Hill"), { { W * 100.0, 0.0 }, { W * 70.0, H * 40.0 }, { W * 35.0, H * 85.0 }, { W * 10.0, H * 100.0 }, { 0.0, H * 102.0 } }, HillSides, FVector::ZeroVector,
			FVector::UpVector, HillScale, FVector2D::ZeroVector);
		const FVector At = Cm(D * FMath::Cos(Bearings[i]), D * FMath::Sin(Bearings[i]), SeaM - SinkM);
		Mesh(K.Build(this, TEXT("SumoIsland")), Hill, FTransform(FRotator(0.0, FMath::RadiansToDegrees(Bearings[i]) + 90.0, 0.0), At), false);
		if (i != PagodaA && i != PagodaB)
		{
			continue;
		}
		for (int32 Tier = 0; Tier < PagodaTiers; ++Tier)
		{
			const FVector P = At + FVector(0.0, 0.0, H * 100.0 + PagodaBaseCm + Tier * TierCm);
			Mesh(Cube, Hill, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 0.0, RoofCm), FVector(RoofWidth - Tier * RoofStep, RoofWidth - Tier * RoofStep, RoofThick)), false);
			Mesh(Cube, Window, FTransform(FRotator::ZeroRotator, P, FVector(WindowWidth - Tier * WindowStep, WindowWidth - Tier * WindowStep, WindowHeight)), false);
		}
	}
}

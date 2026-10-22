// ASumoArena's props round the ring: the torii in the water, the drifting paper lanterns, the four tasselled posts in
// the players' colours, small rocks drifting under the ring, the islands on the horizon and the neon spill lights.

#include "Games/Sumo/SumoArena.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Sumo/SumoArenaLayout.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace MovecadeKart::Sumo::ArenaLayout;
using namespace MovecadeKart::Art;
using MovecadeKart::FMeshBuilder;

namespace
{
	/** The sea's surface in the rules' metres. */
	constexpr double SeaM = ASumoArena::WaterZ / 100.0;

	/** The torii: behind the ring, off to the right (m), its posts' spread and its height. */
	const FVector2D ToriiAt(27.0, 7.0);
	constexpr double ToriiWidth = 13.0;
	constexpr double ToriiHeight = 16.0;

	/** The lanterns: mostly ahead of the camera, a quarter behind it, thicker toward the horizon. */
	constexpr int32 LanternCount = 70;
	constexpr float LanternBearingSpread = 1.3f;
	constexpr float LanternBehindChance = 0.25f;
	constexpr double LanternNearM = 14.0;
	constexpr double LanternSpreadM = 70.0;
	constexpr float LanternFalloff = 0.7f;
	const FVector LanternScale(0.42, 0.42, 0.55);
	/** A few lanterns throw real light on the water (m). */
	const FVector2D LitLanterns[] = { { 15.0, -6.0 }, { 18.0, 9.0 }, { -2.0, 15.0 }, { 4.0, -16.0 }, { 30.0, -2.0 } };

	/** The posts at the ring's corners (the dohyo's four colours, here the players'), on rocks (m from the centre). */
	constexpr double PostRadiusM = 11.4;
	constexpr double PostAngles[] = { -0.75 * PI, 0.75 * PI, 0.25 * PI, -0.25 * PI };

	/** Small rocks drifting round under the ring, each with a crystal. */
	constexpr int32 DriftRocks = 6;

	/** The islands' bearings (radians, 0 straight ahead); two carry a pagoda. */
	constexpr double IslandBearings[] = { -0.55, -0.25, 0.12, 0.42, 0.7, 2.6, -2.5 };
	constexpr int32 PagodaTiers = 5;
}

void ASumoArena::BuildProps()
{
	FRandomStream R(0x7041);
	BuildTorii();
	BuildLanterns(R);
	BuildPosts(R);
	BuildDriftRocks(R);
	BuildIslands(R);
	// Neon spill over the ring: cyan from the left, magenta from the right, a soft white overhead.
	RimLights.Add(Light(Cm(-2.0, -10.0, 4.0), Hex(TEXT("#43d9ff")), 1600.f, 1800.f));
	RimLights.Add(Light(Cm(-2.0, 10.0, 4.0), Hex(TEXT("#ff4fae")), 1500.f, 1800.f));
	RimLights.Add(Light(Cm(6.0, 0.0, 7.0), Hex(TEXT("#b8b4ff")), 900.f, 2000.f));
}

void ASumoArena::BuildTorii()
{
	// Vermilion with black caps, a neon line under each lintel, a warm light on its face.
	UMaterialInterface* Vermilion = Materials.Plain(this, Hex(TEXT("#c23322")), 0.45, 0.0, 0.04);
	UMaterialInterface* Black = Materials.Plain(this, Hex(TEXT("#121018")), 0.4, 0.2);
	UMaterialInterface* Neon = Materials.GlowOpaque(this, Hex(TEXT("#41e8ff")) * 8.f);
	const FVector2D At = ToriiAt;
	const double W = ToriiWidth;
	const double Hgt = ToriiHeight;
	for (const double Side : { -1.0, 1.0 })
	{
		const FVector Base = Cm(At.X, At.Y + Side * W * 0.36, SeaM);
		Mesh(Cylinder, Vermilion, FTransform(FRotator::ZeroRotator, Base + FVector(0.0, 0.0, Hgt * 50.0), FVector(1.1, 1.1, Hgt)), true);
		Mesh(Cylinder, Black, FTransform(FRotator::ZeroRotator, Base + FVector(0.0, 0.0, 60.0), FVector(1.35, 1.35, 1.2)), true);
	}
	const double Top = SeaM + Hgt;
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

void ASumoArena::BuildLanterns(FRandomStream& R)
{
	// Their own instances (not a shared group): AnimateProps bobs each one.
	Lanterns = OwnInstances(Sphere, TEXT("Lanterns"), false);
	Lanterns->SetMaterial(0, Materials.GlowOpaque(this, Hex(TEXT("#ffb263")) * 7.f));
	for (int32 i = 0; i < LanternCount; ++i)
	{
		const double A = R.FRandRange(-LanternBearingSpread, LanternBearingSpread) + (R.FRand() < LanternBehindChance ? PI : 0.0);
		const double D = LanternNearM + FMath::Pow(R.FRand(), LanternFalloff) * LanternSpreadM;
		const FVector At = Cm(D * FMath::Cos(A), D * FMath::Sin(A), SeaM + 0.15);
		LanternRest.Add(At);
		Lanterns->AddInstance(FTransform(FRotator::ZeroRotator, At, LanternScale), false);
	}
	for (const FVector2D& L : LitLanterns)
	{
		Light(Cm(L.X, L.Y, SeaM + 1.2), Hex(TEXT("#ff9a4a")), 500.f, 1200.f);
	}
}

void ASumoArena::BuildPosts(FRandomStream& R)
{
	UMaterialInterface* Rock = Materials.Surface(this, { Hex(TEXT("#3a3150")), TEXT("rock_face"), 1.4, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 });
	UMaterialInterface* Lacquer = Materials.Plain(this, Hex(TEXT("#16121f")), 0.32, 0.3);
	UMaterialInterface* Gold = Materials.Plain(this, Hex(TEXT("#e0b25a")), 0.3, 1.0);
	for (int32 i = 0; i < UE_ARRAY_COUNT(PostAngles); ++i)
	{
		const double A = PostAngles[i];
		USceneComponent* Float = Floater(Cm(PostRadiusM * FMath::Cos(A), PostRadiusM * FMath::Sin(A), -0.6));
		FMeshBuilder K;
		const TArray<FVector2D> Profile = { { 0.0, -260.0 }, { 40.0, -230.0 }, { 95.0, -150.0 }, { 135.0, -60.0 }, { 150.0, -5.0 }, { 140.0, 10.0 }, { 0.0, 12.0 } };
		K.Lathe(TEXT("Rock"), Profile, 10, FVector::ZeroVector, FVector::UpVector, FVector(1.0, 0.85, 1.0), FVector2D::ZeroVector);
		Mesh(K.Build(this, TEXT("SumoPostRock")), Rock, FTransform(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector::ZeroVector), true, Float);
		Mesh(Cylinder, Lacquer, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 190.0), FVector(0.34, 0.34, 3.6)), true, Float);
		Mesh(Cylinder, Gold, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 30.0), FVector(0.48, 0.48, 0.2)), true, Float);
		Mesh(Cylinder, Gold, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 372.0), FVector(0.5, 0.5, 0.1)), true, Float);
		// The post's own glow: SetPlayers lights it in its seat's colour or dims it.
		UMaterialInstanceDynamic* Tassel = GlowOpaque(this, ASumoArena::SlotColor(i) * 6.f);
		TasselMats.Add(Tassel);
		// The lantern on top and the tassel hanging from a cord.
		Mesh(Cylinder, Tassel, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 405.0), FVector(0.42, 0.42, 0.55)), false, Float);
		Mesh(Cube, Lacquer, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 437.0), FVector(0.6, 0.6, 0.08)), true, Float);
		const FVector Out = FVector(FMath::Cos(A), FMath::Sin(A), 0.0);
		Mesh(Cylinder, Lacquer, FTransform(FRotator::ZeroRotator, -Out * 30.0 + FVector(0.0, 0.0, 330.0), FVector(0.03, 0.03, 0.7)), false, Float);
		FMeshBuilder T;
		T.Lathe(TEXT("Tassel"), { { 0.0, -48.0 }, { 14.0, -46.0 }, { 11.0, -20.0 }, { 5.0, 0.0 }, { 0.0, 2.0 } }, 10, FVector::ZeroVector, FVector::UpVector, FVector::OneVector,
			FVector2D::ZeroVector);
		Mesh(T.Build(this, TEXT("SumoTassel")), Tassel, FTransform(FRotator::ZeroRotator, -Out * 30.0 + FVector(0.0, 0.0, 296.0)), false, Float);
		TasselLights.Add(Light(FVector(0.0, 0.0, 400.0), ASumoArena::SlotColor(i), 260.f, 800.f, Float));
	}
}

void ASumoArena::BuildDriftRocks(FRandomStream& R)
{
	UMaterialInterface* Rock = Materials.Surface(this, { Hex(TEXT("#2f2846")), TEXT("rock_face"), 1.2, 0.55, 0.9, 0.5, 0.9, 0.0, 0.0, true, 0.5 });
	UMaterialInterface* Crystal = Materials.GlowOpaque(this, Hex(TEXT("#5ee9ff")) * 6.f);
	for (int32 i = 0; i < DriftRocks; ++i)
	{
		const double A = (i + 0.3) * 2.0 * PI / 6.0;
		const double D = R.FRandRange(12.5f, 15.5f);
		USceneComponent* Float = Floater(Cm(D * FMath::Cos(A), D * FMath::Sin(A), R.FRandRange(-4.5f, -2.0f)));
		const double S = R.FRandRange(0.5f, 1.1f);
		FMeshBuilder K;
		K.Lathe(TEXT("Rock"), { { 0.0, -150.0 }, { 60.0, -110.0 }, { 95.0, -40.0 }, { 90.0, 10.0 }, { 0.0, 25.0 } }, 9, FVector::ZeroVector, FVector::UpVector, FVector(1.0, 0.8, 1.0) * S,
			FVector2D::ZeroVector);
		Mesh(K.Build(this, TEXT("SumoDrift")), Rock, FTransform(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector::ZeroVector), true, Float);
		Mesh(Cube, Crystal, FTransform(FRotator(R.FRandRange(-25.f, 25.f), 30.0, R.FRandRange(-25.f, 25.f)), FVector(0.0, 0.0, 30.0 * S), FVector(0.18, 0.18, 0.55) * S), false, Float);
	}
}

void ASumoArena::BuildIslands(FRandomStream& R)
{
	// Dark hills on the horizon, two with a pagoda's lit windows.
	UMaterialInterface* Hill = Materials.Plain(this, Hex(TEXT("#0c0d22")), 0.9);
	UMaterialInterface* Window = Materials.GlowOpaque(this, Hex(TEXT("#ffbf6e")) * 5.f);
	for (int32 i = 0; i < UE_ARRAY_COUNT(IslandBearings); ++i)
	{
		const double D = R.FRandRange(260.f, 420.f);
		const double H = R.FRandRange(14.f, 42.f);
		const double W = R.FRandRange(40.f, 110.f);
		FMeshBuilder K;
		K.Lathe(TEXT("Hill"), { { W * 100.0, 0.0 }, { W * 70.0, H * 40.0 }, { W * 35.0, H * 85.0 }, { W * 10.0, H * 100.0 }, { 0.0, H * 102.0 } }, 14, FVector::ZeroVector,
			FVector::UpVector, FVector(1.0, 0.5, 1.0), FVector2D::ZeroVector);
		const FVector At = Cm(D * FMath::Cos(IslandBearings[i]), D * FMath::Sin(IslandBearings[i]), SeaM - 0.5);
		Mesh(K.Build(this, TEXT("SumoIsland")), Hill, FTransform(FRotator(0.0, FMath::RadiansToDegrees(IslandBearings[i]) + 90.0, 0.0), At), false);
		if (i != 1 && i != 3)
		{
			continue;
		}
		for (int32 Tier = 0; Tier < PagodaTiers; ++Tier)
		{
			const FVector P = At + FVector(0.0, 0.0, H * 100.0 + 200.0 + Tier * 260.0);
			Mesh(Cube, Hill, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 0.0, 120.0), FVector(9.0 - Tier * 1.3, 9.0 - Tier * 1.3, 0.5)), false);
			Mesh(Cube, Window, FTransform(FRotator::ZeroRotator, P, FVector(5.6 - Tier * 0.8, 5.6 - Tier * 0.8, 1.2)), false);
		}
	}
}

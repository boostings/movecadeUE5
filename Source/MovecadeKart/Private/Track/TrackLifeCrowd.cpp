// ATrackLife's crowds and butterflies: the grandstand prefab rebuilt without its block people with a spectator on
// every seat place (they watch, jump and wave from the shared Cheer parameter), and butterflies over the lawn's
// flower clumps that scatter when a kart comes close.
#include "Track/TrackLife.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Track/TrackLifeMeshes.h"
#include "Track/TrackLifeShared.h"
#include "TrackBuilder.h"

using namespace MovecadeKart;
using namespace MovecadeKart::LifeShared;

namespace
{
	/** Spectators are ~1.8 m: beyond 160 m they are a few pixels, and their animated vertices still cost. */
	constexpr int32 CrowdCullStartCm = 13000;
	constexpr int32 CrowdCullEndCm = 16000;
	/** Per spectator: shirt RGB, three random phases, world origin. */
	constexpr int32 CrowdCustomFloats = 9;
	constexpr int32 CrowdSeed = 0xc40d;
	constexpr int32 ButterflySeed = 0xb077;

	/** Butterflies flee a kart closer than ScareRadiusCm going faster than ScareSpeed (SU), for 3.5-5.5 s. */
	constexpr double ScareRadiusCm = 900.0;
	constexpr double ScareSpeed = 3.0;
	constexpr double FleeSpeedCm = 480.0;
	/** Wings beat at this rate (the material's custom data 3), faster when scared. */
	constexpr float CalmWingRate = 14.f;
	constexpr float ScaredWingRate = 28.f;
	constexpr double ButterflyScale = 1.9;

	/** Shirts for the crowd: saturated party colours, a few neutrals. */
	const TCHAR* const ShirtColors[] = {
		TEXT("e8455a"), TEXT("ff8a3d"), TEXT("ffd23f"), TEXT("4cc76a"), TEXT("2fb3e0"), TEXT("4d6ad8"), TEXT("8b5cf6"),
		TEXT("ff6bd6"), TEXT("f4f1ea"), TEXT("2d3142"), TEXT("ff5f8f"), TEXT("19c2b0"),
	};
	const TCHAR* const WingColors[] = { TEXT("ff9a3d"), TEXT("ffd23f"), TEXT("6fd0ff"), TEXT("ff7ad9"), TEXT("b48cff"), TEXT("fdfbf5") };

	/** A grandstand part's material hint, in the scenery's own terms (ATrackBuilder::MaterialForHint). */
	TSharedPtr<FJsonObject> Hint(const TCHAR* Shader, const TCHAR* Color, double Roughness, const TCHAR* Detail = nullptr, double TileM = 2.0)
	{
		TSharedPtr<FJsonObject> H = MakeShared<FJsonObject>();
		H->SetStringField(TEXT("shader"), Shader);
		H->SetStringField(TEXT("color"), Color);
		H->SetNumberField(TEXT("roughness"), Roughness);
		if (Detail)
		{
			H->SetStringField(TEXT("detail"), Detail);
			H->SetBoolField(TEXT("triplanar"), true);
			H->SetNumberField(TEXT("tileM"), TileM);
			H->SetNumberField(TEXT("albedo"), 0.3);
			H->SetNumberField(TEXT("normal"), 0.6);
			H->SetNumberField(TEXT("rough"), 0.45);
		}
		return H;
	}
}

void ATrackLife::BuildCrowds(ATrackBuilder* Builder)
{
	UMaterialInterface* CrowdMaster = LoadLife<UMaterialInterface>(TEXT("M_TrackCrowd"));
	const TArray<FTransform> Stands = Builder->TakeOverPrefab(TEXT("grandstand"));
	if (!CrowdMaster || Stands.Num() == 0)
	{
		return;
	}
	UInstancedStaticMeshComponent* Shell = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Grandstands"));
	Shell->SetStaticMesh(Life::BuildGrandstandMesh(this));
	Shell->SetupAttachment(RootComponent);
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const TPair<FName, TSharedPtr<FJsonObject>> Slots[] = {
		{ TEXT("Base"), Hint(TEXT("surface"), TEXT("#9aa3ad"), 0.85, TEXT("painted_concrete"), 2.4) },
		{ TEXT("Tiers"), Hint(TEXT("surface"), TEXT("#d3d7de"), 0.8, TEXT("painted_concrete"), 2.0) },
		{ TEXT("SeatsA"), Hint(TEXT("vertexColor"), TEXT("#6d4bd8"), 0.45) },
		{ TEXT("SeatsB"), Hint(TEXT("vertexColor"), TEXT("#4d6ad8"), 0.45) },
		{ TEXT("Back"), Hint(TEXT("surface"), TEXT("#b7bec8"), 0.8, TEXT("plastered_wall_02"), 2.2) },
		{ TEXT("Roof"), Hint(TEXT("surface"), TEXT("#f4f1ea"), 0.6, TEXT("painted_concrete"), 1.6) },
		{ TEXT("Trim"), Hint(TEXT("vertexColor"), TEXT("#8b5cf6"), 0.35) },
	};
	for (const auto& Slot : Slots)
	{
		const int32 Index = Shell->GetMaterialIndex(Slot.Key);
		if (Index != INDEX_NONE)
		{
			Shell->SetMaterial(Index, Builder->MaterialForHint(Slot.Value));
		}
	}
	Shell->RegisterComponent();
	Shell->AddInstances(Stands, false, true);

	Crowd = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Crowd"));
	Crowd->SetStaticMesh(Life::BuildSpectatorMesh(this));
	Crowd->SetMaterial(0, CrowdMaster);
	Crowd->SetupAttachment(RootComponent);
	Crowd->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Crowd->NumCustomDataFloats = CrowdCustomFloats;
	Crowd->SetCullDistances(CrowdCullStartCm, CrowdCullEndCm);
	Crowd->RegisterComponent();
	const TArray<Life::FSpectatorSpot> Spots = Life::GrandstandSpectators();
	FRandomStream R(CrowdSeed);
	TArray<FTransform> Xforms;
	TArray<float> Data;
	for (const FTransform& Stand : Stands)
	{
		for (const Life::FSpectatorSpot& Spot : Spots)
		{
			const double Size = R.FRandRange(0.9, 1.08);
			// Spots are in the stand's mesh axes; the spectator keeps its own size whatever the stand's scale.
			FTransform World = FTransform(FRotator(0.0, Spot.Yaw + R.FRandRange(-12.0, 12.0), 0.0), Spot.Position) * Stand;
			World.SetScale3D(FVector(Size));
			Xforms.Add(World);
			const FLinearColor Shirt = Srgb(ShirtColors[R.RandHelper(UE_ARRAY_COUNT(ShirtColors))]);
			const FVector O = World.GetLocation();
			Data.Append({ Shirt.R, Shirt.G, Shirt.B, static_cast<float>(FMath::Pow(R.FRand(), 1.4)), R.FRand(), R.FRand(),
				static_cast<float>(O.X), static_cast<float>(O.Y), static_cast<float>(O.Z) });
		}
	}
	Crowd->AddInstances(Xforms, false, true);
	for (int32 i = 0; i < Xforms.Num(); ++i)
	{
		Crowd->SetCustomData(i, TArrayView<const float>(Data.GetData() + i * CrowdCustomFloats, CrowdCustomFloats), i == Xforms.Num() - 1);
	}
	Spectators = Xforms.Num();
}

void ATrackLife::BuildButterflies()
{
	UMaterialInterface* Master = LoadLife<UMaterialInterface>(TEXT("M_TrackButterfly"));
	if (!Master || Butterflies.Num() == 0)
	{
		Butterflies.Reset();
		return;
	}
	ButterflyMesh = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Butterflies"));
	ButterflyMesh->SetStaticMesh(Life::BuildButterflyMesh(this));
	ButterflyMesh->SetMaterial(0, Master);
	ButterflyMesh->SetupAttachment(RootComponent);
	ButterflyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ButterflyMesh->SetMobility(EComponentMobility::Movable);
	ButterflyMesh->NumCustomDataFloats = 4;
	ButterflyMesh->SetCastShadow(false);
	ButterflyMesh->RegisterComponent();
	FRandomStream R(ButterflySeed);
	for (int32 i = 0; i < Butterflies.Num(); ++i)
	{
		ButterflyMesh->AddInstance(FTransform(Butterflies[i].Position), true);
		const FLinearColor Wing = Srgb(WingColors[R.RandHelper(UE_ARRAY_COUNT(WingColors))]);
		ButterflyMesh->SetCustomData(i, { Wing.R, Wing.G, Wing.B, Butterflies[i].Rate }, i == Butterflies.Num() - 1);
	}
}

void ATrackLife::UpdateButterflies(float DeltaSeconds)
{
	if (!ButterflyMesh || Butterflies.Num() == 0 || DeltaSeconds <= 0.f)
	{
		return;
	}
	const double T = GetWorld()->GetTimeSeconds();
	TArray<FTransform>& Xforms = ButterflyXforms; // reused every frame
	Xforms.Reset(Butterflies.Num());
	for (int32 i = 0; i < Butterflies.Num(); ++i)
	{
		FButterfly& Fly = Butterflies[i];
		// A kart rushing close scares it up and away.
		for (const FKartView& K : Karts)
		{
			const FVector Away = Fly.Position - K.Position;
			if (K.Speed > ScareSpeed && Away.SizeSquared2D() < FMath::Square(ScareRadiusCm))
			{
				Fly.Scared = FMath::Max(Fly.Scared, 3.5 + FMath::Frac(Fly.Phase) * 2.0);
				Fly.Velocity = FMath::Lerp(Fly.Velocity, (Away.GetSafeNormal2D() + FVector(0.0, 0.0, 0.9)).GetSafeNormal() * 520.0, 0.25);
			}
		}
		FVector Desired;
		if (Fly.Scared > 0.0)
		{
			Fly.Scared -= DeltaSeconds;
			Desired = Fly.Velocity.GetSafeNormal() * FleeSpeedCm;
		}
		else
		{
			// Wander round the clump: a slow, lazy figure, with a flutter on top.
			const double P = Fly.Phase;
			const FVector Target = Fly.Home + FVector(FMath::Sin(T * 0.7 + P) * 140.0, FMath::Cos(T * 0.53 + P * 1.3) * 140.0, 55.0 + 40.0 * FMath::Sin(T * 1.1 + P));
			Desired = (Target - Fly.Position) * 0.9;
			if (Desired.SizeSquared() > FMath::Square(170.0))
			{
				Desired = Desired.GetSafeNormal() * 170.0;
			}
			Desired += FVector(FMath::Sin(T * 9.0 + P * 3.0), FMath::Cos(T * 7.0 + P), FMath::Sin(T * 11.0 + P * 2.0)) * 60.0;
		}
		Fly.Velocity = FMath::Lerp(Fly.Velocity, Desired, 1.0 - FMath::Exp(-DeltaSeconds * 3.0));
		Fly.Position += Fly.Velocity * DeltaSeconds;
		Fly.Position.Z = FMath::Max(Fly.Position.Z, Fly.Home.Z + 15.0);
		const float Rate = Fly.Scared > 0.0 ? ScaredWingRate : CalmWingRate;
		if (Rate != Fly.Rate)
		{
			Fly.Rate = Rate;
			ButterflyMesh->SetCustomDataValue(i, 3, Rate, false);
		}
		const FRotator Heading(FMath::Clamp(Fly.Velocity.Z * 0.05, -25.0, 25.0), Fly.Velocity.Rotation().Yaw, 0.0);
		Xforms.Add(FTransform(Heading, Fly.Position, FVector(ButterflyScale)));
	}
	// No render state rebuild: the instance data manager streams the moved instances.
	ButterflyMesh->BatchUpdateInstancesTransforms(0, Xforms, true, false, true);
}

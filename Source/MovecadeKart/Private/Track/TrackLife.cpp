#include "Track/TrackLife.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "KartCoords.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "NiagaraComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Race/KartRaceManager.h"
#include "Track/TrackLifeMeshes.h"
#include "TrackBuilder.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrackLife, Log, All);

namespace
{
	const TCHAR* const LifeDir = TEXT("/Game/Tracks/Shared/Life/");
	constexpr int32 MaxKarts = 8;
	constexpr int32 TrampleResolution = 1024;
	/** Seconds the flattened grass takes to spring most of the way back, and the push to fade. */
	constexpr double FlatSeconds = 3.5;
	constexpr double PushSeconds = 2.5;
	/** The disc a kart presses into the lawn (its sim radius is 1.15 m). */
	constexpr double StampRadiusCm = 170.0;
	/** Grass is drawn this far from each view; the material sinks it into the lawn over the last stretch. */
	constexpr double GrassFadeStartCm = 4500.0;
	constexpr double GrassCullCm = 6500.0;
	/** The dense fill layer under the tufts, close to each view only. */
	constexpr double GrassFillFadeCm = 1700.0;
	constexpr double GrassFillCullCm = 2600.0;

	template <typename T>
	T* LoadLife(const TCHAR* Name)
	{
		return LoadObject<T>(nullptr, *FString::Printf(TEXT("%s%s.%s"), LifeDir, Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	FLinearColor Srgb(const TCHAR* Hex)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

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

	/** Shirts for the crowd: saturated party colours, a few neutrals. */
	const TCHAR* const ShirtColors[] = {
		TEXT("e8455a"), TEXT("ff8a3d"), TEXT("ffd23f"), TEXT("4cc76a"), TEXT("2fb3e0"), TEXT("4d6ad8"), TEXT("8b5cf6"),
		TEXT("ff6bd6"), TEXT("f4f1ea"), TEXT("2d3142"), TEXT("ff5f8f"), TEXT("19c2b0"),
	};
	const TCHAR* const PetalColors[] = { TEXT("fdfbf5"), TEXT("fdfbf5"), TEXT("ffe066"), TEXT("ff9ad5"), TEXT("c9a7ff"), TEXT("ffffff") };
	const TCHAR* const WingColors[] = { TEXT("ff9a3d"), TEXT("ffd23f"), TEXT("6fd0ff"), TEXT("ff7ad9"), TEXT("b48cff"), TEXT("fdfbf5") };
	const TCHAR* const FireworkColors[] = { TEXT("ff6bd6"), TEXT("55e0ff"), TEXT("ffd34d"), TEXT("8dff7a"), TEXT("fff3e0"), TEXT("a77bff"), TEXT("ff7b5c") };

	/** Data/Tracks/<id>.lawn.json (Tools/ue/bake_lawn_heights.py): the lawn's top surface, UE cm over three.js x, z. */
	struct FLawnHeights
	{
		double Cell = 0.0;
		FVector2D Min = FVector2D::ZeroVector;
		int32 Cols = 0;
		int32 Rows = 0;
		TArray<int16> Heights;
		static constexpr int16 NoData = -32768;

		bool Load(const FString& TrackId)
		{
			FString Text;
			TSharedPtr<FJsonObject> Json;
			if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks"), TrackId + TEXT(".lawn.json")))
				|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
			{
				return false;
			}
			Cell = Json->GetNumberField(TEXT("cellM"));
			Min = FVector2D(Json->GetNumberField(TEXT("minX")), Json->GetNumberField(TEXT("minZ")));
			Cols = static_cast<int32>(Json->GetNumberField(TEXT("cols")));
			Rows = static_cast<int32>(Json->GetNumberField(TEXT("rows")));
			TArray<uint8> Bytes;
			if (Cell <= 0.0 || !FBase64::Decode(Json->GetStringField(TEXT("heightsCm")), Bytes) || Bytes.Num() != Cols * Rows * 2)
			{
				return false;
			}
			Heights.SetNumUninitialized(Cols * Rows);
			FMemory::Memcpy(Heights.GetData(), Bytes.GetData(), Bytes.Num()); // little-endian int16, like every target
			return true;
		}

		/** Bilinear height under a three.js (x, z) point, over the cells that have lawn; false off the lawn. */
		bool Sample(const FVector2D& P, double& OutCm) const
		{
			const FVector2D G = (P - Min) / Cell - FVector2D(0.5);
			const int32 X0 = FMath::FloorToInt32(G.X), Y0 = FMath::FloorToInt32(G.Y);
			const double FX = G.X - X0, FY = G.Y - Y0;
			double Sum = 0.0, Weight = 0.0;
			for (int32 k = 0; k < 4; ++k)
			{
				const int32 X = X0 + (k & 1), Y = Y0 + (k >> 1);
				if (X < 0 || Y < 0 || X >= Cols || Y >= Rows || Heights[Y * Cols + X] == NoData)
				{
					continue;
				}
				const double W = ((k & 1) ? FX : 1.0 - FX) * ((k >> 1) ? FY : 1.0 - FY) + 1e-4;
				Sum += Heights[Y * Cols + X] * W;
				Weight += W;
			}
			if (Weight <= 0.0)
			{
				return false;
			}
			OutCm = Sum / Weight;
			return true;
		}
	};
}

ATrackLife::ATrackLife()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Rng.Initialize(0x7ac4);
}

bool ATrackLife::IsEnabled()
{
	return !FParse::Param(FCommandLine::Get(), TEXT("KartNoLife"));
}

void ATrackLife::Init(ATrackBuilder* Builder)
{
	const TSharedPtr<const MovecadeKart::FTrack> Track = Builder ? Builder->GetTrack() : nullptr;
	if (!Track.IsValid())
	{
		return;
	}
	Collection = LoadLife<UMaterialParameterCollection>(TEXT("MPC_TrackLife"));
	if (!Collection)
	{
		UE_LOG(LogTrackLife, Warning, TEXT("No track life assets; run Tools/ue/make_track_life.py"));
		return;
	}
	Karts.SetNum(MaxKarts);

	// The finish line, for the fireworks: the finish checkpoint and the road's right there.
	for (const MovecadeKart::FCheckpoint& Cp : Track->Checkpoints)
	{
		if (!Cp.bFinish)
		{
			continue;
		}
		FVector2D Nearest;
		double Offset = 0.0;
		Track->NearestCentre(Cp.Position, Nearest, Offset);
		const int32 Index = Track->NearestWaypoint(Cp.Position);
		const FVector2D Next = Track->Waypoints.IsValidIndex(Index) ? Track->Waypoints[(Index + 1) % Track->Waypoints.Num()] : Cp.Position + FVector2D(0.0, -1.0);
		FinishCm = MovecadeKart::Coords::ToUE(Nearest, Track->GroundY + Track->SuggestedRebaseY);
		const FVector Forward = (MovecadeKart::Coords::ToUE(Next) - MovecadeKart::Coords::ToUE(Cp.Position)).GetSafeNormal2D();
		FinishRight = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		bFinishFirework = true;
		break;
	}

	FString Cam;
	TArray<FString> Bits;
	if (FParse::Param(FCommandLine::Get(), TEXT("KartLifeDemo")) && FParse::Value(FCommandLine::Get(), TEXT("KartTrackCam="), Cam, false) && Cam.ParseIntoArray(Bits, TEXT(",")) == 6)
	{
		bDemo = true;
		DemoCentre = MovecadeKart::Coords::ToUE(FVector(FCString::Atod(*Bits[3]), FCString::Atod(*Bits[4]) + Track->SuggestedRebaseY, FCString::Atod(*Bits[5])));
	}

	BuildGrass(Builder, *Track);
	BuildCrowds(Builder);
	BuildButterflies(*Track);
	Builder->EnableFoliageWind();
	UE_LOG(LogTrackLife, Log, TEXT("%s: %d grass tufts, %d spectators, %d butterflies"), *Track->Id, GrassTufts, Spectators, Butterflies.Num());
}

// ---- Grass ------------------------------------------------------------------------------------------------

void ATrackLife::BuildGrass(ATrackBuilder* Builder, const MovecadeKart::FTrack& Track)
{
	using namespace MovecadeKart;
	const FDirtGrid& Dirt = Track.DirtGrid;
	UMaterialInterface* GrassMaster = LoadLife<UMaterialInterface>(TEXT("M_TrackGrass"));
	UTextureRenderTarget2D* A = LoadLife<UTextureRenderTarget2D>(TEXT("RT_TrackTrampleA"));
	UTextureRenderTarget2D* B = LoadLife<UTextureRenderTarget2D>(TEXT("RT_TrackTrampleB"));
	UMaterialInterface* Decay = LoadLife<UMaterialInterface>(TEXT("M_TrampleDecay"));
	UMaterialInterface* Stamp = LoadLife<UMaterialInterface>(TEXT("M_TrampleStamp"));
	if (!Dirt.IsValid() || !GrassMaster || !A || !B || !Decay || !Stamp)
	{
		return;
	}
	const double Ground = (Track.GroundY + Track.SuggestedRebaseY) * 100.0;
	// Where the lawn mesh crowns above the road, stand the grass on it.
	FLawnHeights Lawn;
	const bool bLawn = Lawn.Load(Track.Id);
	auto GroundAt = [&](const FVector2D& P)
	{
		double Cm = 0.0;
		return bLawn && Lawn.Sample(P, Cm) ? Cm : Ground; // the bake is already in world (rebased) cm
	};

	// The trample field: a square over the lawn, in UE space.
	const FVector C0 = Coords::ToUE(Dirt.Min), C1 = Coords::ToUE(Dirt.Min + FVector2D(Dirt.Cols, Dirt.Rows) * Dirt.CellSize);
	const FVector2D Lo(FMath::Min(C0.X, C1.X), FMath::Min(C0.Y, C1.Y)), Hi(FMath::Max(C0.X, C1.X), FMath::Max(C0.Y, C1.Y));
	FieldSize = FMath::Max(Hi.X - Lo.X, Hi.Y - Lo.Y) + 4000.0;
	FieldMin = (Lo + Hi) * 0.5 - FVector2D(FieldSize * 0.5);
	TrampleTargets = { A, B };
	for (UTextureRenderTarget2D* Target : TrampleTargets)
	{
		Target->ResizeTarget(TrampleResolution, TrampleResolution);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Target, FLinearColor::Transparent);
	}
	DecayMaterial = UMaterialInstanceDynamic::Create(Decay, this);
	for (int32 i = 0; i < MaxKarts; ++i)
	{
		StampMaterials.Add(UMaterialInstanceDynamic::Create(Stamp, this));
	}
	bHasField = true;

	UMaterialInstanceDynamic* GrassMaterial = UMaterialInstanceDynamic::Create(GrassMaster, this);
	GrassMaterial->SetTextureParameterValue(TEXT("TrampleMap"), A);
	GrassMaterial->SetScalarParameterValue(TEXT("FadeStart"), GrassFadeStartCm);
	GrassMaterial->SetScalarParameterValue(TEXT("FadeEnd"), GrassCullCm);
	// Violet's lawn (the verge mesh is #5d8a3c under a leafy detail): blades a shade darker at the root and
	// sun-bleached toward the tips, so the tufts melt into the ground they stand on.
	GrassMaterial->SetVectorParameterValue(TEXT("RootColor"), Srgb(TEXT("22401a")));
	GrassMaterial->SetVectorParameterValue(TEXT("TipColor"), Srgb(TEXT("5f9a34")));
	GrassMaterial->SetVectorParameterValue(TEXT("TipColorAlt"), Srgb(TEXT("74a23a")));
	GrassMaterial->SetVectorParameterValue(TEXT("SubsurfaceTint"), FLinearColor(0.45f, 0.62f, 0.22f));
	FieldReaders.Add(GrassMaterial);
	// -KartFieldDebug paints the trample field on the grass (red: flat; blue: inside the field).
	if (FParse::Param(FCommandLine::Get(), TEXT("KartFieldDebug")))
	{
		GrassMaterial->SetScalarParameterValue(TEXT("FieldDebug"), 1.f);
	}
	// The close-up fill layer: short, dense tufts that only draw near each view.
	UMaterialInstanceDynamic* FillMaterial = UMaterialInstanceDynamic::Create(GrassMaster, this);
	FillMaterial->CopyParameterOverrides(GrassMaterial);
	FillMaterial->SetScalarParameterValue(TEXT("FadeStart"), GrassFillFadeCm);
	FillMaterial->SetScalarParameterValue(TEXT("FadeEnd"), GrassFillCullCm);
	FieldReaders.Add(FillMaterial);

	auto MakeGroup = [&](UStaticMesh* Mesh, const TCHAR* Name, int32 CustomFloats, UMaterialInterface* Material, double CullCm)
	{
		UInstancedStaticMeshComponent* H = NewObject<UInstancedStaticMeshComponent>(this, Name);
		H->SetStaticMesh(Mesh);
		H->SetMaterial(0, Material);
		H->SetupAttachment(RootComponent);
		H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		H->SetCastShadow(false);
		H->bAffectDistanceFieldLighting = false;
		H->SetCullDistances(static_cast<int32>(CullCm * 0.9), static_cast<int32>(CullCm));
		H->NumCustomDataFloats = CustomFloats;
		H->RegisterComponent();
		Grass.Add(H);
		return H;
	};
	TArray<UInstancedStaticMeshComponent*> Tufts;
	for (int32 v = 0; v < 4; ++v)
	{
		Tufts.Add(MakeGroup(Life::BuildGrassTuftMesh(this, 101 + v * 37, 11 + v, 22.0, 30.0 + v * 3.0), *FString::Printf(TEXT("GrassTufts%d"), v), 3, GrassMaterial, GrassCullCm));
	}
	TArray<UInstancedStaticMeshComponent*> Fill;
	for (int32 v = 0; v < 2; ++v)
	{
		Fill.Add(MakeGroup(Life::BuildGrassTuftMesh(this, 517 + v * 53, 8, 16.0, 22.0 + v * 4.0), *FString::Printf(TEXT("GrassFill%d"), v), 3, FillMaterial, GrassFillCullCm));
	}
	TArray<UInstancedStaticMeshComponent*> Flowers = {
		MakeGroup(Life::BuildFlowerMesh(this, 8), TEXT("LawnDaisies"), 6, GrassMaterial, GrassCullCm),
		MakeGroup(Life::BuildFlowerMesh(this, 5), TEXT("LawnFlowers"), 6, GrassMaterial, GrassCullCm),
	};

	// Scatter: one draw per 0.5 m cell, density shaped by low-frequency noise into lusher and sparser patches,
	// flowers in loose clumps. Every candidate must stand on a dirt (lawn) cell.
	FRandomStream R(0x9a55);
	TArray<TArray<FTransform>> TuftXforms;
	TuftXforms.SetNum(Tufts.Num());
	TArray<TArray<FTransform>> FillXforms;
	FillXforms.SetNum(Fill.Num());
	TArray<TArray<FTransform>> FlowerXforms;
	FlowerXforms.SetNum(Flowers.Num());
	TArray<TArray<FLinearColor>> FlowerColors;
	FlowerColors.SetNum(Flowers.Num());
	TArray<FVector> FlowerSpots;
	const double Step = 0.5;
	const FVector2D Extent = FVector2D(Dirt.Cols, Dirt.Rows) * Dirt.CellSize;
	for (double Z = 0.0; Z < Extent.Y; Z += Step)
	{
		for (double X = 0.0; X < Extent.X; X += Step)
		{
			const FVector2D Cell = Dirt.Min + FVector2D(X, Z);
			if (!Dirt.IsDirt(Cell + FVector2D(Step * 0.5)))
			{
				continue;
			}
			const double Patch = FMath::PerlinNoise2D(Cell * 0.045) * 0.5 + 0.5;
			const double Lush = FMath::PerlinNoise2D(Cell * 0.21 + FVector2D(31.7, 7.3)) * 0.5 + 0.5;
			const double Expected = 0.4 + 0.6 * FMath::Lerp(Patch, Lush, 0.35); // tufts per cell (0.25 m^2)
			int32 Count = FMath::FloorToInt32(Expected);
			Count += R.FRand() < Expected - Count ? 1 : 0;
			for (int32 k = 0; k < Count; ++k)
			{
				const FVector2D P = Cell + FVector2D(R.FRand(), R.FRand()) * Step;
				if (!Dirt.IsDirt(P))
				{
					continue;
				}
				const double Scale = FMath::Lerp(0.6, 1.25, Patch) * R.FRandRange(0.8, 1.15);
				const FVector Loc = Coords::ToUE(P) + FVector(0.0, 0.0, GroundAt(P));
				TuftXforms[R.RandHelper(Tufts.Num())].Add(FTransform(FRotator(0.0, R.FRandRange(0.0, 360.0), 0.0), Loc, FVector(Scale, Scale, Scale * R.FRandRange(0.85, 1.15))));
			}
			if (R.FRand() < 0.5)
			{
				const FVector2D P = Cell + FVector2D(R.FRand(), R.FRand()) * Step;
				if (Dirt.IsDirt(P))
				{
					const double Scale = R.FRandRange(0.75, 1.15);
					FillXforms[R.RandHelper(Fill.Num())].Add(FTransform(FRotator(0.0, R.FRandRange(0.0, 360.0), 0.0), Coords::ToUE(P) + FVector(0.0, 0.0, GroundAt(P)), FVector(Scale)));
				}
			}
			// Flowers in clumps where a third noise is high.
			const double Bloom = FMath::PerlinNoise2D(Cell * 0.09 + FVector2D(-12.1, 44.4));
			if (Bloom > 0.32 && R.FRand() < (Bloom - 0.32) * 0.9)
			{
				const int32 Kind = R.RandHelper(Flowers.Num());
				const FVector2D P = Cell + FVector2D(R.FRand(), R.FRand()) * Step;
				if (Dirt.IsDirt(P))
				{
					const FVector Loc = Coords::ToUE(P) + FVector(0.0, 0.0, GroundAt(P));
					const double Scale = R.FRandRange(0.8, 1.2);
					FlowerXforms[Kind].Add(FTransform(FRotator(R.FRandRange(-8.0, 8.0), R.FRandRange(0.0, 360.0), R.FRandRange(-8.0, 8.0)), Loc, FVector(Scale)));
					FlowerColors[Kind].Add(Srgb(PetalColors[R.RandHelper(UE_ARRAY_COUNT(PetalColors))]));
					FlowerSpots.Add(Loc);
				}
			}
		}
	}
	// Custom data 0..2: each instance's world origin (the materials cannot get it otherwise on these components).
	auto Fill3 = [](UInstancedStaticMeshComponent* Group, const TArray<FTransform>& Xforms, TFunctionRef<void(int32, TArray<float>&)> Extra)
	{
		Group->AddInstances(Xforms, false, true);
		TArray<float> Data;
		for (int32 k = 0; k < Xforms.Num(); ++k)
		{
			const FVector O = Xforms[k].GetLocation();
			Data.Reset();
			Data.Append({ static_cast<float>(O.X), static_cast<float>(O.Y), static_cast<float>(O.Z) });
			Extra(k, Data);
			Group->SetCustomData(k, Data, k == Xforms.Num() - 1);
		}
		return Xforms.Num();
	};
	for (int32 i = 0; i < Tufts.Num(); ++i)
	{
		GrassTufts += Fill3(Tufts[i], TuftXforms[i], [](int32, TArray<float>&) {});
	}
	for (int32 i = 0; i < Fill.Num(); ++i)
	{
		GrassTufts += Fill3(Fill[i], FillXforms[i], [](int32, TArray<float>&) {});
	}
	for (int32 i = 0; i < Flowers.Num(); ++i)
	{
		Fill3(Flowers[i], FlowerXforms[i], [&](int32 k, TArray<float>& Data)
		{
			const FLinearColor& Col = FlowerColors[i][k];
			Data.Append({ Col.R, Col.G, Col.B });
		});
	}

	// Butterfly homes: flower clumps, spread out.
	const int32 Homes = FMath::Min(42, FlowerSpots.Num());
	for (int32 i = 0; i < Homes; ++i)
	{
		FButterfly& Fly = Butterflies.AddDefaulted_GetRef();
		Fly.Home = FlowerSpots[(static_cast<int64>(i) * FlowerSpots.Num()) / Homes];
		Fly.Position = Fly.Home + FVector(0.0, 0.0, 60.0);
		Fly.Phase = R.FRandRange(0.0, 100.0);
	}
}

// ---- Crowds -----------------------------------------------------------------------------------------------

void ATrackLife::BuildCrowds(ATrackBuilder* Builder)
{
	UMaterialInterface* CrowdMaster = LoadLife<UMaterialInterface>(TEXT("M_TrackCrowd"));
	const TArray<FTransform> Stands = Builder->TakeOverPrefab(TEXT("grandstand"));
	if (!CrowdMaster || Stands.Num() == 0)
	{
		return;
	}
	UInstancedStaticMeshComponent* Shell = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Grandstands"));
	Shell->SetStaticMesh(MovecadeKart::Life::BuildGrandstandMesh(this));
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
	Crowd->SetStaticMesh(MovecadeKart::Life::BuildSpectatorMesh(this));
	Crowd->SetMaterial(0, CrowdMaster);
	Crowd->SetupAttachment(RootComponent);
	Crowd->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Crowd->NumCustomDataFloats = 9;
	// Spectators are ~1.8 m: beyond 160 m they are a few pixels, and their animated vertices still cost.
	Crowd->SetCullDistances(13000, 16000);
	Crowd->RegisterComponent();
	const TArray<MovecadeKart::Life::FSpectatorSpot> Spots = MovecadeKart::Life::GrandstandSpectators();
	FRandomStream R(0xc40d);
	TArray<FTransform> Xforms;
	TArray<float> Data;
	for (const FTransform& Stand : Stands)
	{
		for (const MovecadeKart::Life::FSpectatorSpot& Spot : Spots)
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
		Crowd->SetCustomData(i, TArrayView<const float>(Data.GetData() + i * 9, 9), i == Xforms.Num() - 1);
	}
	Spectators = Xforms.Num();
}

// ---- Butterflies ------------------------------------------------------------------------------------------

void ATrackLife::BuildButterflies(const MovecadeKart::FTrack& Track)
{
	UMaterialInterface* Master = LoadLife<UMaterialInterface>(TEXT("M_TrackButterfly"));
	if (!Master || Butterflies.Num() == 0)
	{
		Butterflies.Reset();
		return;
	}
	ButterflyMesh = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Butterflies"));
	ButterflyMesh->SetStaticMesh(MovecadeKart::Life::BuildButterflyMesh(this));
	ButterflyMesh->SetMaterial(0, Master);
	ButterflyMesh->SetupAttachment(RootComponent);
	ButterflyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ButterflyMesh->SetMobility(EComponentMobility::Movable);
	ButterflyMesh->NumCustomDataFloats = 4;
	ButterflyMesh->SetCastShadow(false);
	ButterflyMesh->RegisterComponent();
	FRandomStream R(0xb077);
	for (int32 i = 0; i < Butterflies.Num(); ++i)
	{
		ButterflyMesh->AddInstance(FTransform(Butterflies[i].Position), true);
		const FLinearColor Wing = Srgb(WingColors[R.RandHelper(UE_ARRAY_COUNT(WingColors))]);
		ButterflyMesh->SetCustomData(i, { Wing.R, Wing.G, Wing.B, Butterflies[i].Rate }, i == Butterflies.Num() - 1);
	}
}

// ---- Per frame --------------------------------------------------------------------------------------------

void ATrackLife::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Collection)
	{
		return;
	}
	if (!Race.IsValid())
	{
		BindRace();
	}
	GatherKarts(DeltaSeconds);
	UpdateShared(DeltaSeconds);
	UpdateTrample(DeltaSeconds);
	UpdateButterflies(DeltaSeconds);
	UpdateFireworks(DeltaSeconds);
}

void ATrackLife::EndPlay(const EEndPlayReason::Type Reason)
{
	if (AKartRaceManager* Manager = Race.Get())
	{
		Manager->OnCue.Remove(CueHandle);
	}
	if (UMaterialParameterCollectionInstance* Shared = Collection ? GetWorld()->GetParameterCollectionInstance(Collection) : nullptr)
	{
		Shared->SetScalarParameterValue(TEXT("Cheer"), 0.f);
	}
	Super::EndPlay(Reason);
}

void ATrackLife::BindRace()
{
	if (AKartRaceManager* Manager = AKartRaceManager::Get(this))
	{
		Race = Manager;
		CueHandle = Manager->OnCue.AddUObject(this, &ATrackLife::OnRaceCue);
	}
}

void ATrackLife::GatherKarts(float DeltaSeconds)
{
	int32 Count = 0;
	for (TActorIterator<AKartPawn> It(GetWorld()); It && Count < MaxKarts; ++It)
	{
		FKartView& View = Karts[Count++];
		const FVector Position = It->GetActorLocation();
		const bool bFresh = View.Position.IsZero();
		View.Velocity = bFresh || DeltaSeconds <= 0.f ? FVector::ZeroVector : FMath::Lerp(View.Velocity, (Position - View.Position) / DeltaSeconds, 0.5);
		View.Position = Position;
		const UKartMovementComponent* Move = It->GetKartMovement();
		View.Speed = Move && Move->HasSim() ? FMath::Abs(Move->SimState().Speed) : View.Velocity.Size() / 100.0;
		View.bOnDirt = Move && Move->HasSim() && Move->SimState().bOnDirt;
	}
	for (int32 i = Count; i < MaxKarts; ++i)
	{
		Karts[i] = FKartView();
		Karts[i].Position = FVector(1.0e7, 1.0e7, -1.0e7);
	}
	// Test hook: a phantom kart lapping a 6 m circle on the lawn where -KartTrackCam looks.
	if (bDemo && Count < MaxKarts)
	{
		const double T = GetWorld()->GetTimeSeconds() - 2.0;
		if (T > 0.0)
		{
			constexpr double Radius = 600.0, Speed = 1200.0;
			const double A = T * Speed / Radius;
			FKartView& View = Karts[Count];
			View.Position = DemoCentre + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * Radius;
			View.Velocity = FVector(-FMath::Sin(A), FMath::Cos(A), 0.0) * Speed;
			View.Speed = Speed / 100.0;
			View.bOnDirt = true;
		}
	}
}

void ATrackLife::UpdateShared(float DeltaSeconds)
{
	UMaterialParameterCollectionInstance* Shared = GetWorld()->GetParameterCollectionInstance(Collection);
	if (!Shared)
	{
		return;
	}
	static const FName Names[MaxKarts] = { TEXT("Kart0"), TEXT("Kart1"), TEXT("Kart2"), TEXT("Kart3"), TEXT("Kart4"), TEXT("Kart5"), TEXT("Kart6"), TEXT("Kart7") };
	for (int32 i = 0; i < MaxKarts; ++i)
	{
		const FKartView& K = Karts[i];
		Shared->SetVectorParameterValue(Names[i], FLinearColor(K.Position.X, K.Position.Y, K.Position.Z, K.Speed));
	}
	// A steady breeze that slowly breathes.
	const double T = GetWorld()->GetTimeSeconds();
	const double Strength = 1.0 + 0.25 * FMath::Sin(T * 0.13) + 0.15 * FMath::Sin(T * 0.37 + 1.1);
	Shared->SetVectorParameterValue(TEXT("Wind"), FLinearColor(0.8f, 0.6f, static_cast<float>(Strength), 0.f));
	if (bHasField)
	{
		Shared->SetVectorParameterValue(TEXT("Field"), FLinearColor(FieldMin.X, FieldMin.Y, FieldSize, FieldSize));
	}
	// Cheers hold for a moment, then settle.
	if (CheerHold > 0.0)
	{
		CheerHold -= DeltaSeconds;
	}
	else
	{
		Cheer = FMath::Max(0.0, Cheer - DeltaSeconds * 0.5);
	}
	Shared->SetScalarParameterValue(TEXT("Cheer"), static_cast<float>(Cheer));
}

void ATrackLife::UpdateTrample(float DeltaSeconds)
{
	if (!bHasField || DeltaSeconds <= 0.f)
	{
		return;
	}
	// 30 updates a second, each applying all the time since the last: the decay is exponential (Exp of the summed
	// time is the product of the frames' factors) and the stamps' press is proportional to time, so this matches
	// a per-frame update while drawing the 1024^2 targets a fraction as often.
	TrampleDt += DeltaSeconds;
	if (TrampleDt < 1.f / 30.f)
	{
		return;
	}
	DeltaSeconds = FMath::Min(TrampleDt, 0.25f);
	TrampleDt = 0.f;
	UTextureRenderTarget2D* Src = TrampleTargets[LiveTarget];
	UTextureRenderTarget2D* Dst = TrampleTargets[1 - LiveTarget];
	DecayMaterial->SetTextureParameterValue(TEXT("Prev"), Src);
	const float KeepPush = static_cast<float>(FMath::Exp(-DeltaSeconds / PushSeconds));
	const float KeepFlat = static_cast<float>(FMath::Exp(-DeltaSeconds / FlatSeconds));
	DecayMaterial->SetVectorParameterValue(TEXT("Keep"), FLinearColor(KeepPush, KeepPush, KeepFlat, 1.f));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Dst, DecayMaterial);

	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	bool bDrawing = false;
	const double PxPerCm = TrampleResolution / FieldSize;
	for (int32 i = 0; i < MaxKarts; ++i)
	{
		const FKartView& K = Karts[i];
		const FVector2D Uv = (FVector2D(K.Position) - FieldMin) / FieldSize;
		if (Uv.X < 0.0 || Uv.Y < 0.0 || Uv.X > 1.0 || Uv.Y > 1.0 || K.Speed < 0.5)
		{
			continue;
		}
		if (!bDrawing)
		{
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Dst, Canvas, Size, Context);
			bDrawing = Canvas != nullptr;
			if (!bDrawing)
			{
				break;
			}
		}
		// Faster karts press harder; a crawling one still flattens what it sits on.
		const double Press = DeltaSeconds * 7.0 * FMath::Clamp(K.Speed / 6.0, 0.25, 1.0);
		const FVector Travel = K.Velocity.GetSafeNormal2D();
		UMaterialInstanceDynamic* Mid = StampMaterials[i];
		Mid->SetScalarParameterValue(TEXT("Flat"), static_cast<float>(Press));
		Mid->SetScalarParameterValue(TEXT("Push"), static_cast<float>(Press * 0.8));
		Mid->SetVectorParameterValue(TEXT("Travel"), FLinearColor(Travel.X, Travel.Y, 0.f, 0.f));
		const double R = StampRadiusCm * PxPerCm;
		Canvas->K2_DrawMaterial(Mid, Uv * TrampleResolution - FVector2D(R), FVector2D(R * 2.0), FVector2D::ZeroVector, FVector2D::UnitVector);
	}
	if (bDrawing)
	{
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}
	LiveTarget = 1 - LiveTarget;
	for (UMaterialInstanceDynamic* Reader : FieldReaders)
	{
		Reader->SetTextureParameterValue(TEXT("TrampleMap"), Dst);
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
			if (K.Speed > 3.0 && Away.SizeSquared2D() < FMath::Square(900.0))
			{
				Fly.Scared = FMath::Max(Fly.Scared, 3.5 + FMath::Frac(Fly.Phase) * 2.0);
				Fly.Velocity = FMath::Lerp(Fly.Velocity, (Away.GetSafeNormal2D() + FVector(0.0, 0.0, 0.9)).GetSafeNormal() * 520.0, 0.25);
			}
		}
		FVector Desired;
		if (Fly.Scared > 0.0)
		{
			Fly.Scared -= DeltaSeconds;
			Desired = Fly.Velocity.GetSafeNormal() * 480.0;
		}
		else
		{
			// Wander round the clump: a slow, lazy figure.
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
		const float Rate = Fly.Scared > 0.0 ? 28.f : 14.f;
		if (Rate != Fly.Rate)
		{
			Fly.Rate = Rate;
			ButterflyMesh->SetCustomDataValue(i, 3, Rate, false);
		}
		const FRotator Heading(FMath::Clamp(Fly.Velocity.Z * 0.05, -25.0, 25.0), Fly.Velocity.Rotation().Yaw, 0.0);
		Xforms.Add(FTransform(Heading, Fly.Position, FVector(1.9)));
	}
	// No render state rebuild: the instance data manager streams the moved instances.
	ButterflyMesh->BatchUpdateInstancesTransforms(0, Xforms, true, false, true);
}

// ---- Cheers and fireworks ---------------------------------------------------------------------------------

void ATrackLife::OnRaceCue(int32 Seat, FName Cue)
{
	auto Raise = [this](double Level, double Hold)
	{
		Cheer = FMath::Max(Cheer, Level);
		CheerHold = FMath::Max(CheerHold, Hold);
	};
	static const FName Go(TEXT("race.go")), Lap(TEXT("kart.lap")), Final(TEXT("kart.final-lap")), Finish(TEXT("kart.finish")), Boom(TEXT("kart.explosion"));
	if (Cue == Go)
	{
		Raise(1.0, 2.5);
		FireworkShow(4, 1.2);
	}
	else if (Cue == Lap)
	{
		Raise(0.7, 1.0);
	}
	else if (Cue == Final)
	{
		Raise(0.9, 1.5);
		FireworkShow(2, 0.6);
	}
	else if (Cue == Finish)
	{
		const bool bFirst = !bFinishShowDone;
		bFinishShowDone = true;
		Raise(1.0, bFirst ? 5.0 : 2.0);
		FireworkShow(bFirst ? 12 : 3, bFirst ? 5.0 : 1.0);
	}
	else if (Cue == Boom)
	{
		Raise(0.8, 0.6);
	}
}

void ATrackLife::FireworkShow(int32 Count, double Over)
{
	if (!bFinishFirework)
	{
		return;
	}
	for (int32 i = 0; i < Count; ++i)
	{
		const double Side = Rng.FRandRange(-1.0, 1.0);
		const FVector Cm = FinishCm + FinishRight * (Side * 2200.0) + FVector(Rng.FRandRange(-600.0, 600.0), Rng.FRandRange(-600.0, 600.0), 0.0);
		LaunchFirework(Cm, Srgb(FireworkColors[Rng.RandHelper(UE_ARRAY_COUNT(FireworkColors))]), Over * i / FMath::Max(Count, 1) + Rng.FRandRange(0.0, 0.25));
	}
}

void ATrackLife::LaunchFirework(const FVector& Cm, const FLinearColor& Color, double Delay)
{
	FShell& Shell = Shells.AddDefaulted_GetRef();
	Shell.Launch = Cm + FVector(0.0, 0.0, 200.0);
	Shell.Burst = Cm + FVector(Rng.FRandRange(-500.0, 500.0), Rng.FRandRange(-500.0, 500.0), Rng.FRandRange(3200.0, 4400.0));
	Shell.Color = Color;
	Shell.Start = GetWorld()->GetTimeSeconds() + Delay;
	Shell.Rise = Rng.FRandRange(1.0, 1.3);
}

void ATrackLife::UpdateFireworks(float DeltaSeconds)
{
	using namespace MovecadeKart::Fx;
	if (Shells.Num() == 0)
	{
		return;
	}
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	const double Now = GetWorld()->GetTimeSeconds();
	TArray<FKartFxEmit> Out;
	for (int32 i = Shells.Num() - 1; i >= 0; --i)
	{
		FShell& S = Shells[i];
		const double Age = Now - S.Start;
		if (Age < 0.0)
		{
			continue;
		}
		if (!S.bBurst)
		{
			// The rising shell: a hot point shedding a sparkling trail, easing out toward the top.
			const double T = FMath::Clamp(Age / S.Rise, 0.0, 1.0);
			const FVector P = FMath::Lerp(S.Launch, S.Burst, 1.0 - FMath::Square(1.0 - T));
			// 70 sparks a second from a stream that rides the shell (world-space particles stay where they fell).
			if (S.Trail == INDEX_NONE && Fx)
			{
				S.Trail = TrailBusy.IndexOfByKey(false);
				if (S.Trail == INDEX_NONE)
				{
					if (UNiagaraComponent* Stream = Fx->MakeStream(EField::Sparks, RootComponent, NAME_None))
					{
						S.Trail = TrailStreams.Add(Stream);
						TrailBusy.Add(false);
					}
				}
				if (S.Trail != INDEX_NONE)
				{
					TrailBusy[S.Trail] = true;
				}
			}
			UNiagaraComponent* Stream = TrailStreams.IsValidIndex(S.Trail) ? TrailStreams[S.Trail].Get() : nullptr;
			static const FKartFxEmit Trail = []
			{
				FKartFxEmit E;
				E.Field = EField::Sparks;
				E.Direction = -FVector::UpVector;
				E.Spread = 0.5;
				E.Speed = { 0.5, 2.5 };
				E.Life = { 0.35, 0.6 };
				E.Size = { 0.08, 0.14 };
				E.SizeEnd = 0.2;
				E.Color = Hex(TEXT("ffe2a8"));
				E.ColorEnd = Hex(TEXT("ff8a3d"));
				E.Intensity = 4.0;
				return E;
			}();
			if (Stream)
			{
				Stream->SetWorldLocation(P);
				UKartFxSubsystem::UpdateStream(Stream, Trail, T < 1.0 ? 70.0 : 0.0);
			}
			if (T >= 1.0)
			{
				S.bBurst = true;
				if (TrailBusy.IsValidIndex(S.Trail))
				{
					TrailBusy[S.Trail] = false;
				}
				S.Trail = INDEX_NONE;
				FKartFxEmit Stars;
				Stars.Field = EField::Sparks;
				Stars.Position = S.Burst;
				Stars.Count = 150;
				Stars.Spread = UE_DOUBLE_PI;
				Stars.Speed = { 12.0, 16.0 };
				Stars.Life = { 1.4, 2.1 };
				Stars.Size = { 0.55, 0.85 };
				Stars.SizeEnd = 0.35;
				Stars.Color = S.Color;
				Stars.ColorEnd = S.Color * 0.5f;
				Stars.Intensity = 14.0;
				Out.Add(Stars);
				// Daylight shows: a puff of coloured smoke where the shell broke, which reads against a bright sky.
				FKartFxEmit Smoke;
				Smoke.Field = EField::Smoke;
				Smoke.Position = S.Burst;
				Smoke.Count = 26;
				Smoke.Spread = UE_DOUBLE_PI;
				Smoke.Speed = { 3.0, 7.0 };
				Smoke.Life = { 2.2, 3.4 };
				Smoke.Size = { 2.4, 3.8 };
				Smoke.SizeEnd = 2.2;
				Smoke.Color = FLinearColor::LerpUsingHSV(S.Color, FLinearColor::White, 0.35f);
				Smoke.ColorEnd = FLinearColor::LerpUsingHSV(S.Color, FLinearColor::White, 0.7f);
				Smoke.Intensity = 1.0;
				Smoke.Jitter = 2.0;
				Out.Add(Smoke);
				FKartFxEmit Glints = Stars;
				Glints.Field = EField::Flare;
				Glints.Count = 22;
				Glints.Speed = { 6.0, 12.0 };
				Glints.Life = { 0.6, 1.1 };
				Glints.Size = { 0.6, 1.0 };
				Glints.SizeEnd = 0.2;
				Glints.Color = FLinearColor::White;
				Glints.ColorEnd = S.Color;
				Glints.Intensity = 4.0;
				Out.Add(Glints);
				FKartFxEmit Core;
				Core.Field = EField::Fire;
				Core.Position = S.Burst;
				Core.Count = 3;
				Core.Spread = UE_DOUBLE_PI;
				Core.Speed = { 0.0, 1.0 };
				Core.Life = { 0.3, 0.45 };
				Core.Size = { 3.0, 4.5 };
				Core.SizeEnd = 1.6;
				Core.Color = S.Color;
				Core.ColorEnd = S.Color;
				Core.Intensity = 3.0;
				Out.Add(Core);
			}
		}
		else if (!S.bCrackled && Age > S.Rise + 0.9)
		{
			// The crackle as the stars burn out.
			S.bCrackled = true;
			FKartFxEmit Crackle;
			Crackle.Field = EField::Flare;
			Crackle.Position = S.Burst - FVector(0.0, 0.0, 600.0);
			Crackle.Count = 40;
			Crackle.Spread = UE_DOUBLE_PI;
			Crackle.Speed = { 0.0, 1.5 };
			Crackle.Life = { 0.12, 0.35 };
			Crackle.Size = { 0.3, 0.55 };
			Crackle.SizeEnd = 0.1;
			Crackle.Color = FLinearColor::White;
			Crackle.ColorEnd = S.Color;
			Crackle.Intensity = 5.0;
			Crackle.Jitter = 10.0;
			Out.Add(Crackle);
			Shells.RemoveAtSwap(i);
		}
	}
	if (Fx && Out.Num() > 0)
	{
		Fx->Emit(Out);
	}
}

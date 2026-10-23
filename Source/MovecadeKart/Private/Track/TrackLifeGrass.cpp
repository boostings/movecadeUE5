// ATrackLife's lawn: geometry tufts and flowers scattered over the sim's dirt cells (Violet's verges), and the
// trample field karts press into them (two ping-ponged render targets: decay, then a disc stamped per kart).
#include "Track/TrackLife.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "KartCoords.h"
#include "KartDebugFlags.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sim/KartTrack.h"
#include "Track/TrackLawnHeights.h"
#include "Track/TrackLifeMeshes.h"
#include "Track/TrackLifeShared.h"

using namespace MovecadeKart;
using namespace MovecadeKart::LifeShared;

namespace
{
	constexpr int32 TrampleResolution = 1024;
	/** The trample field reaches this far (cm) past the lawn's bounds. */
	constexpr double FieldMarginCm = 4000.0;
	/** The field redraws at this rate, applying all the time since the last draw (at most MaxTrampleStep). */
	constexpr float TrampleRate = 30.f;
	constexpr float MaxTrampleStep = 0.25f;
	/** Seconds the flattened grass takes to spring most of the way back, and the push to fade. */
	constexpr double FlatSeconds = 3.5;
	constexpr double PushSeconds = 2.5;
	/** The disc a kart presses into the lawn (its sim radius is 1.15 m). */
	constexpr double StampRadiusCm = 170.0;
	/** A stamp presses this hard per second at full speed (FullPressSpeed SU); a crawling kart still flattens what it
	 *  sits on (a quarter), a stationary one (under MinStampSpeed) nothing. The push is 0.8 of the flattening. */
	constexpr double PressPerSecond = 7.0;
	constexpr double FullPressSpeed = 6.0;
	constexpr double MinPress = 0.25;
	constexpr double MinStampSpeed = 0.5;
	constexpr double PushShare = 0.8;

	/** Grass is drawn this far from each view; the material sinks it into the lawn over the last stretch. */
	constexpr double GrassFadeStartCm = 4500.0;
	constexpr double GrassCullCm = 6500.0;
	/** The dense fill layer under the tufts, close to each view only. */
	constexpr double GrassFillFadeCm = 1700.0;
	constexpr double GrassFillCullCm = 2600.0;
	/** Instances fade from this share of their cull distance. */
	constexpr double GrassCullFadeShare = 0.9;
	constexpr int32 TuftVariants = 4;
	constexpr int32 FillVariants = 2;
	/** Butterflies live over this many flower clumps, spread over the lawn. */
	constexpr int32 MaxButterflies = 42;
	constexpr double ButterflyHoverCm = 60.0;

	/** Scatter: one draw per 0.5 m cell; the scatter's fixed seed keeps every run's lawn the same. */
	constexpr double ScatterStep = 0.5;
	constexpr int32 ScatterSeed = 0x9a55;

	const TCHAR* const PetalColors[] = { TEXT("fdfbf5"), TEXT("fdfbf5"), TEXT("ffe066"), TEXT("ff9ad5"), TEXT("c9a7ff"), TEXT("ffffff") };

	/** Where every tuft, fill tuft and flower goes, per mesh variant, and the flowers' spots for the butterflies. */
	struct FLawnScatter
	{
		TArray<TArray<FTransform>> Tufts;
		TArray<TArray<FTransform>> Fill;
		TArray<TArray<FTransform>> Flowers;
		TArray<TArray<FLinearColor>> FlowerColors;
		TArray<FVector> FlowerSpots;
	};

	/**
	 * Density shaped by low-frequency noise into lusher and sparser patches, flowers in loose clumps where a third noise
	 * is high. Every candidate must stand on a dirt (lawn) cell. R's draws are in a fixed order, so the lawn is the
	 * same every run.
	 */
	void ScatterLawn(const FDirtGrid& Dirt, TFunctionRef<double(const FVector2D&)> GroundAt, FRandomStream& R, FLawnScatter& Out)
	{
		const FVector2D Extent = FVector2D(Dirt.Cols, Dirt.Rows) * Dirt.CellSize;
		for (double Z = 0.0; Z < Extent.Y; Z += ScatterStep)
		{
			for (double X = 0.0; X < Extent.X; X += ScatterStep)
			{
				const FVector2D Cell = Dirt.Min + FVector2D(X, Z);
				if (!Dirt.IsDirt(Cell + FVector2D(ScatterStep * 0.5)))
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
					const FVector2D P = Cell + FVector2D(R.FRand(), R.FRand()) * ScatterStep;
					if (!Dirt.IsDirt(P))
					{
						continue;
					}
					const double Scale = FMath::Lerp(0.6, 1.25, Patch) * R.FRandRange(0.8, 1.15);
					const FVector Loc = Coords::ToUE(P) + FVector(0.0, 0.0, GroundAt(P));
					Out.Tufts[R.RandHelper(Out.Tufts.Num())].Add(FTransform(FRotator(0.0, R.FRandRange(0.0, 360.0), 0.0), Loc, FVector(Scale, Scale, Scale * R.FRandRange(0.85, 1.15))));
				}
				if (R.FRand() < 0.5)
				{
					const FVector2D P = Cell + FVector2D(R.FRand(), R.FRand()) * ScatterStep;
					if (Dirt.IsDirt(P))
					{
						const double Scale = R.FRandRange(0.75, 1.15);
						Out.Fill[R.RandHelper(Out.Fill.Num())].Add(FTransform(FRotator(0.0, R.FRandRange(0.0, 360.0), 0.0), Coords::ToUE(P) + FVector(0.0, 0.0, GroundAt(P)), FVector(Scale)));
					}
				}
				const double Bloom = FMath::PerlinNoise2D(Cell * 0.09 + FVector2D(-12.1, 44.4));
				if (Bloom > 0.32 && R.FRand() < (Bloom - 0.32) * 0.9)
				{
					const int32 Kind = R.RandHelper(Out.Flowers.Num());
					const FVector2D P = Cell + FVector2D(R.FRand(), R.FRand()) * ScatterStep;
					if (Dirt.IsDirt(P))
					{
						const FVector Loc = Coords::ToUE(P) + FVector(0.0, 0.0, GroundAt(P));
						const double Scale = R.FRandRange(0.8, 1.2);
						Out.Flowers[Kind].Add(FTransform(FRotator(R.FRandRange(-8.0, 8.0), R.FRandRange(0.0, 360.0), R.FRandRange(-8.0, 8.0)), Loc, FVector(Scale)));
						Out.FlowerColors[Kind].Add(Srgb(PetalColors[R.RandHelper(UE_ARRAY_COUNT(PetalColors))]));
						Out.FlowerSpots.Add(Loc);
					}
				}
			}
		}
	}

	/** Adds the instances with custom data 0..2 set to each one's world origin (the materials can't get it otherwise
	 *  on these components), plus whatever Extra appends. Returns the count. */
	int32 AddWithOrigins(UInstancedStaticMeshComponent* Group, const TArray<FTransform>& Xforms, TFunctionRef<void(int32, TArray<float>&)> Extra)
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
	}
}

void ATrackLife::BuildGrass(const FTrack& Track)
{
	const FDirtGrid& Dirt = Track.DirtGrid;
	UMaterialInterface* GrassMaster = LoadLife<UMaterialInterface>(TEXT("M_TrackGrass"));
	UTextureRenderTarget2D* TargetA = LoadLife<UTextureRenderTarget2D>(TEXT("RT_TrackTrampleA"));
	UTextureRenderTarget2D* TargetB = LoadLife<UTextureRenderTarget2D>(TEXT("RT_TrackTrampleB"));
	UMaterialInterface* Decay = LoadLife<UMaterialInterface>(TEXT("M_TrampleDecay"));
	UMaterialInterface* Stamp = LoadLife<UMaterialInterface>(TEXT("M_TrampleStamp"));
	if (!Dirt.IsValid() || !GrassMaster || !TargetA || !TargetB || !Decay || !Stamp)
	{
		return;
	}
	BuildTrampleField(Dirt, TargetA, TargetB, Decay, Stamp);

	UMaterialInstanceDynamic* GrassMaterial = UMaterialInstanceDynamic::Create(GrassMaster, this);
	GrassMaterial->SetTextureParameterValue(TEXT("TrampleMap"), TargetA);
	GrassMaterial->SetScalarParameterValue(TEXT("FadeStart"), GrassFadeStartCm);
	GrassMaterial->SetScalarParameterValue(TEXT("FadeEnd"), GrassCullCm);
	// Violet's lawn (the verge mesh is #5d8a3c under a leafy detail): blades a shade darker at the root and
	// sun-bleached toward the tips, so the tufts melt into the ground they stand on.
	GrassMaterial->SetVectorParameterValue(TEXT("RootColor"), Srgb(TEXT("22401a")));
	GrassMaterial->SetVectorParameterValue(TEXT("TipColor"), Srgb(TEXT("5f9a34")));
	GrassMaterial->SetVectorParameterValue(TEXT("TipColorAlt"), Srgb(TEXT("74a23a")));
	GrassMaterial->SetVectorParameterValue(TEXT("SubsurfaceTint"), FLinearColor(0.45f, 0.62f, 0.22f));
	FieldReaders.Add(GrassMaterial);
	if (FKartDebugFlags::Get().bFieldDebug)
	{
		GrassMaterial->SetScalarParameterValue(TEXT("FieldDebug"), 1.f);
	}
	// The close-up fill layer: short, dense tufts that only draw near each view.
	UMaterialInstanceDynamic* FillMaterial = UMaterialInstanceDynamic::Create(GrassMaster, this);
	FillMaterial->CopyParameterOverrides(GrassMaterial);
	FillMaterial->SetScalarParameterValue(TEXT("FadeStart"), GrassFillFadeCm);
	FillMaterial->SetScalarParameterValue(TEXT("FadeEnd"), GrassFillCullCm);
	FieldReaders.Add(FillMaterial);

	TArray<UInstancedStaticMeshComponent*> Tufts;
	for (int32 v = 0; v < TuftVariants; ++v)
	{
		Tufts.Add(MakeGrassGroup(Life::BuildGrassTuftMesh(this, 101 + v * 37, 11 + v, 22.0, 30.0 + v * 3.0), *FString::Printf(TEXT("GrassTufts%d"), v), 3, GrassMaterial, GrassCullCm));
	}
	TArray<UInstancedStaticMeshComponent*> Fill;
	for (int32 v = 0; v < FillVariants; ++v)
	{
		Fill.Add(MakeGrassGroup(Life::BuildGrassTuftMesh(this, 517 + v * 53, 8, 16.0, 22.0 + v * 4.0), *FString::Printf(TEXT("GrassFill%d"), v), 3, FillMaterial, GrassFillCullCm));
	}
	// Daisies and clover heads, each with a petal colour in custom data 3..5.
	TArray<UInstancedStaticMeshComponent*> Flowers = {
		MakeGrassGroup(Life::BuildFlowerMesh(this, 8), TEXT("LawnDaisies"), 6, GrassMaterial, GrassCullCm),
		MakeGrassGroup(Life::BuildFlowerMesh(this, 5), TEXT("LawnFlowers"), 6, GrassMaterial, GrassCullCm),
	};

	// Where the lawn mesh crowns above the road, stand the grass on it.
	const double Ground = (Track.GroundY + Track.SuggestedRebaseY) * 100.0;
	FLawnHeights Lawn;
	const bool bLawn = Lawn.Load(Track.Id);
	auto GroundAt = [&](const FVector2D& P)
	{
		double Cm = 0.0;
		return bLawn && Lawn.Sample(P, Cm) ? Cm : Ground; // the bake is already in world (rebased) cm
	};
	FRandomStream R(ScatterSeed);
	FLawnScatter Scatter;
	Scatter.Tufts.SetNum(Tufts.Num());
	Scatter.Fill.SetNum(Fill.Num());
	Scatter.Flowers.SetNum(Flowers.Num());
	Scatter.FlowerColors.SetNum(Flowers.Num());
	ScatterLawn(Dirt, GroundAt, R, Scatter);
	for (int32 i = 0; i < Tufts.Num(); ++i)
	{
		GrassTufts += AddWithOrigins(Tufts[i], Scatter.Tufts[i], [](int32, TArray<float>&) {});
	}
	for (int32 i = 0; i < Fill.Num(); ++i)
	{
		GrassTufts += AddWithOrigins(Fill[i], Scatter.Fill[i], [](int32, TArray<float>&) {});
	}
	for (int32 i = 0; i < Flowers.Num(); ++i)
	{
		AddWithOrigins(Flowers[i], Scatter.Flowers[i], [&](int32 k, TArray<float>& Data)
		{
			const FLinearColor& Col = Scatter.FlowerColors[i][k];
			Data.Append({ Col.R, Col.G, Col.B });
		});
	}

	// Butterfly homes: flower clumps, spread out.
	const TArray<FVector>& Spots = Scatter.FlowerSpots;
	const int32 Homes = FMath::Min(MaxButterflies, Spots.Num());
	for (int32 i = 0; i < Homes; ++i)
	{
		FButterfly& Fly = Butterflies.AddDefaulted_GetRef();
		Fly.Home = Spots[(static_cast<int64>(i) * Spots.Num()) / Homes];
		Fly.Position = Fly.Home + FVector(0.0, 0.0, ButterflyHoverCm);
		Fly.Phase = R.FRandRange(0.0, 100.0);
	}
}

void ATrackLife::BuildTrampleField(const FDirtGrid& Dirt, UTextureRenderTarget2D* TargetA, UTextureRenderTarget2D* TargetB, UMaterialInterface* Decay, UMaterialInterface* Stamp)
{
	// A square over the lawn, in UE space.
	const FVector C0 = Coords::ToUE(Dirt.Min), C1 = Coords::ToUE(Dirt.Min + FVector2D(Dirt.Cols, Dirt.Rows) * Dirt.CellSize);
	const FVector2D Lo(FMath::Min(C0.X, C1.X), FMath::Min(C0.Y, C1.Y)), Hi(FMath::Max(C0.X, C1.X), FMath::Max(C0.Y, C1.Y));
	FieldSize = FMath::Max(Hi.X - Lo.X, Hi.Y - Lo.Y) + FieldMarginCm;
	FieldMin = (Lo + Hi) * 0.5 - FVector2D(FieldSize * 0.5);
	TrampleTargets = { TargetA, TargetB };
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
}

UInstancedStaticMeshComponent* ATrackLife::MakeGrassGroup(UStaticMesh* Mesh, const TCHAR* Name, int32 CustomFloats, UMaterialInterface* Material, double CullCm)
{
	UInstancedStaticMeshComponent* Group = NewObject<UInstancedStaticMeshComponent>(this, Name);
	Group->SetStaticMesh(Mesh);
	Group->SetMaterial(0, Material);
	Group->SetupAttachment(RootComponent);
	Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Group->SetCastShadow(false);
	Group->bAffectDistanceFieldLighting = false;
	Group->SetCullDistances(static_cast<int32>(CullCm * GrassCullFadeShare), static_cast<int32>(CullCm));
	Group->NumCustomDataFloats = CustomFloats;
	Group->RegisterComponent();
	Grass.Add(Group);
	return Group;
}

void ATrackLife::UpdateTrample(float DeltaSeconds)
{
	if (!bHasField || DeltaSeconds <= 0.f)
	{
		return;
	}
	// TrampleRate updates a second, each applying all the time since the last: the decay is exponential (Exp of the
	// summed time is the product of the frames' factors) and the stamps' press is proportional to time, so this
	// matches a per-frame update while drawing the 1024^2 targets a fraction as often.
	TrampleDt += DeltaSeconds;
	if (TrampleDt < 1.f / TrampleRate)
	{
		return;
	}
	const float Step = FMath::Min(TrampleDt, MaxTrampleStep);
	TrampleDt = 0.f;
	UTextureRenderTarget2D* Src = TrampleTargets[LiveTarget];
	UTextureRenderTarget2D* Dst = TrampleTargets[1 - LiveTarget];
	DecayMaterial->SetTextureParameterValue(TEXT("Prev"), Src);
	const float KeepPush = static_cast<float>(FMath::Exp(-Step / PushSeconds));
	const float KeepFlat = static_cast<float>(FMath::Exp(-Step / FlatSeconds));
	DecayMaterial->SetVectorParameterValue(TEXT("Keep"), FLinearColor(KeepPush, KeepPush, KeepFlat, 1.f));
	UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Dst, DecayMaterial);
	StampKarts(Dst, Step);
	LiveTarget = 1 - LiveTarget;
	for (UMaterialInstanceDynamic* Reader : FieldReaders)
	{
		Reader->SetTextureParameterValue(TEXT("TrampleMap"), Dst);
	}
}

void ATrackLife::StampKarts(UTextureRenderTarget2D* Target, float DeltaSeconds)
{
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	bool bDrawing = false;
	const double PxPerCm = TrampleResolution / FieldSize;
	for (int32 i = 0; i < MaxKarts; ++i)
	{
		const FKartView& K = Karts[i];
		const FVector2D Uv = (FVector2D(K.Position) - FieldMin) / FieldSize;
		if (Uv.X < 0.0 || Uv.Y < 0.0 || Uv.X > 1.0 || Uv.Y > 1.0 || K.Speed < MinStampSpeed)
		{
			continue;
		}
		if (!bDrawing)
		{
			UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, Size, Context);
			bDrawing = Canvas != nullptr;
			if (!bDrawing)
			{
				break;
			}
		}
		const double Press = DeltaSeconds * PressPerSecond * FMath::Clamp(K.Speed / FullPressSpeed, MinPress, 1.0);
		const FVector Travel = K.Velocity.GetSafeNormal2D();
		UMaterialInstanceDynamic* Mid = StampMaterials[i];
		Mid->SetScalarParameterValue(TEXT("Flat"), static_cast<float>(Press));
		Mid->SetScalarParameterValue(TEXT("Push"), static_cast<float>(Press * PushShare));
		Mid->SetVectorParameterValue(TEXT("Travel"), FLinearColor(Travel.X, Travel.Y, 0.f, 0.f));
		const double R = StampRadiusCm * PxPerCm;
		Canvas->K2_DrawMaterial(Mid, Uv * TrampleResolution - FVector2D(R), FVector2D(R * 2.0), FVector2D::ZeroVector, FVector2D::UnitVector);
	}
	if (bDrawing)
	{
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}
}

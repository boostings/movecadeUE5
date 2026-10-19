#include "TrackBuilder.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Dom/JsonObject.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrackBuilder, Log, All);

namespace
{
	FLinearColor Hex(const TCHAR* Value)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Value));
	}

	/** Graybox colours per ribbon material (from HarborTrack.tsx where the web uses a flat colour). */
	FLinearColor RibbonColor(const FString& Material)
	{
		if (Material == TEXT("asphalt")) return Hex(TEXT("4a4f57"));
		if (Material == TEXT("edgeLine") || Material == TEXT("wallCap")) return Hex(TEXT("fff0cc"));
		if (Material == TEXT("dirtShoulder")) return Hex(TEXT("b89a6a"));
		if (Material == TEXT("barrier")) return Hex(TEXT("8fb0ad"));
		if (Material == TEXT("sand")) return Hex(TEXT("e6cc9e"));
		if (Material == TEXT("surf")) return Hex(TEXT("f4f7ec"));
		if (Material == TEXT("shallows")) return Hex(TEXT("7fd0cc"));
		return Hex(TEXT("808080"));
	}

	/** Emits a quad with both windings and one normal, so it shows from its intended side whichever
	 *  winding the renderer treats as front-facing. */
	void AddQuad(TArray<FVector>& V, TArray<int32>& Tri, TArray<FVector>& N, const FVector& A0, const FVector& B0, const FVector& A1, const FVector& B1, const FVector& Normal)
	{
		const int32 Base = V.Add(A0);
		V.Add(B0);
		V.Add(A1);
		V.Add(B1);
		N.Append({ Normal, Normal, Normal, Normal });
		Tri.Append({ Base, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2 });
		Tri.Append({ Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3 });
	}

	double JsonNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default = 0.0)
	{
		double V = Default;
		return O.IsValid() && O->TryGetNumberField(Key, V) ? V : Default;
	}
}

ATrackBuilder::ATrackBuilder()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(RootComponent);
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ground->bUseAsyncCooking = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = ShapeMaterial.Object;

	ItemBoxes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ItemBoxes"));
	ItemBoxes->SetupAttachment(RootComponent);
	ItemBoxes->SetStaticMesh(CubeMesh.Object);
	ItemBoxes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ItemBoxes->SetMobility(EComponentMobility::Movable);

	BoostPads = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoostPads"));
	BoostPads->SetupAttachment(RootComponent);
	BoostPads->SetStaticMesh(PlaneMesh.Object);
	BoostPads->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetMobility(EComponentMobility::Movable);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(RootComponent);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
}

FString ATrackBuilder::TrackPath(const FString& Id)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks"), Id + TEXT(".track.json"));
}

TSharedPtr<const MovecadeKart::FTrack> ATrackBuilder::GetTrack()
{
	if (!Track.IsValid() || LoadedId != TrackId)
	{
		Load();
	}
	return Track;
}

bool ATrackBuilder::Load()
{
	Track.Reset();
	Json.Reset();
	LoadedId = TrackId;

	FString Text;
	const FString Path = TrackPath(TrackId);
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogTrackBuilder, Error, TEXT("Track file not found: %s. Run Tools/track-export (npm run export)."), *Path);
		return false;
	}
	TSharedPtr<MovecadeKart::FTrack> Parsed = MakeShared<MovecadeKart::FTrack>();
	FString Error;
	if (!MovecadeKart::FTrack::LoadFromJsonString(Text, *Parsed, Error))
	{
		UE_LOG(LogTrackBuilder, Error, TEXT("%s: %s"), *Path, *Error);
		return false;
	}
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
	Track = Parsed;
	return true;
}

void ATrackBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The track is authored in world space.
	SetActorTransform(FTransform::Identity);
	if (GetTrack())
	{
		Build();
	}
}

void ATrackBuilder::AddSection(const FString& Name, const FLinearColor& Color, TFunctionRef<void(TArray<FVector>&, TArray<int32>&, TArray<FVector>&)> Emit)
{
	TArray<FVector> Vertices, Normals;
	TArray<int32> Triangles;
	Emit(Vertices, Triangles, Normals);
	if (Triangles.Num() == 0)
	{
		return;
	}
	const int32 Index = SectionCount++;
	Ground->CreateMeshSection_LinearColor(Index, Vertices, Triangles, Normals, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this, *FString::Printf(TEXT("MID_%s"), *Name));
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Ground->SetMaterial(Index, Material);
	}
}

void ATrackBuilder::Build()
{
	using namespace MovecadeKart;
	const FTrack& T = *Track;
	const double Rebase = T.SuggestedRebaseY;
	const int32 Rows = T.Centre.Num();

	Ground->ClearAllMeshSections();
	SectionCount = 0;

	auto Right = [&T](int32 i) { const FVector2D Tg = T.CentreTangent[i % T.Centre.Num()]; return FVector2D(-Tg.Y, Tg.X); };
	auto At = [&](int32 i, double Offset, double Y) { const int32 k = i % Rows; return Coords::ToUE(T.Centre[k] + Right(k) * Offset, Y + Rebase); };

	// Ribbons: one section per ribbon, curbs split into their red and cream stripes (every 4 rows).
	const TArray<TSharedPtr<FJsonValue>>* Ribbons = nullptr;
	if (Json.IsValid() && Json->TryGetArrayField(TEXT("ribbons"), Ribbons))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Ribbons)
		{
			const TSharedPtr<FJsonObject> R = Value->AsObject();
			FString Name, Material;
			R->TryGetStringField(TEXT("name"), Name);
			R->TryGetStringField(TEXT("material"), Material);
			const double Inner = JsonNum(R, TEXT("inner")), Outer = JsonNum(R, TEXT("outer"));
			const double InnerY = JsonNum(R, TEXT("innerY")), OuterY = JsonNum(R, TEXT("outerY"));
			const bool bWall = FMath::IsNearlyEqual(Inner, Outer);
			const int32 StripePeriod = static_cast<int32>(JsonNum(R, TEXT("stripePeriodSamples"), 0));

			auto Emit = [&](int32 Stripe, TArray<FVector>& V, TArray<int32>& Tri, TArray<FVector>& N)
			{
				for (int32 i = 0; i < Rows; ++i)
				{
					if (StripePeriod > 0 && (i / StripePeriod) % 2 != Stripe)
					{
						continue;
					}
					FVector Normal = FVector::UpVector;
					if (bWall)
					{
						// Face the road: a left wall (negative offset) faces right of travel.
						const FVector RightUE = Coords::DirToUE(FVector(Right(i).X, 0.0, Right(i).Y));
						Normal = Inner < 0.0 ? RightUE : -RightUE;
					}
					AddQuad(V, Tri, N, At(i, Inner, InnerY), At(i, Outer, OuterY), At(i + 1, Inner, InnerY), At(i + 1, Outer, OuterY), Normal);
				}
			};

			if (StripePeriod > 0)
			{
				AddSection(Name + TEXT("_red"), Hex(TEXT("d7312a")), [&](auto& V, auto& Tri, auto& N) { Emit(0, V, Tri, N); });
				AddSection(Name + TEXT("_cream"), Hex(TEXT("f7f4ec")), [&](auto& V, auto& Tri, auto& N) { Emit(1, V, Tri, N); });
			}
			else
			{
				AddSection(Name, RibbonColor(Material), [&](auto& V, auto& Tri, auto& N) { Emit(-1, V, Tri, N); });
			}
		}
	}

	// Sea and other flat scenery instances.
	const TSharedPtr<FJsonObject>* Scenery = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
	if (Json.IsValid() && Json->TryGetObjectField(TEXT("scenery"), Scenery) && (*Scenery)->TryGetArrayField(TEXT("instances"), Instances))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Instances)
		{
			const TSharedPtr<FJsonObject> I = Value->AsObject();
			FString Prefab, Tint;
			I->TryGetStringField(TEXT("prefab"), Prefab);
			if (Prefab != TEXT("sea_plane"))
			{
				continue; // Prop prefabs arrive with export phase 1b.
			}
			I->TryGetStringField(TEXT("tint"), Tint);
			const TArray<TSharedPtr<FJsonValue>>* Scale = nullptr;
			I->TryGetArrayField(TEXT("scale"), Scale);
			const double HalfX = Scale ? (*Scale)[0]->AsNumber() / 2.0 : 2500.0;
			const double HalfZ = Scale ? (*Scale)[2]->AsNumber() / 2.0 : 2500.0;
			const FVector2D C(JsonNum(I, TEXT("x")), JsonNum(I, TEXT("z")));
			const double Y = JsonNum(I, TEXT("y")) + Rebase;
			AddSection(TEXT("sea"), Hex(*Tint.Replace(TEXT("#"), TEXT(""))), [&](auto& V, auto& Tri, auto& N)
			{
				AddQuad(V, Tri, N,
					Coords::ToUE(C + FVector2D(-HalfX, -HalfZ), Y), Coords::ToUE(C + FVector2D(HalfX, -HalfZ), Y),
					Coords::ToUE(C + FVector2D(-HalfX, HalfZ), Y), Coords::ToUE(C + FVector2D(HalfX, HalfZ), Y), FVector::UpVector);
			});
		}
	}

	// Start/finish line across the road at the finish checkpoint, 1.5 m deep.
	if (T.Checkpoints.Num() > 0)
	{
		const FVector2D F = T.Checkpoints[0].Position;
		double HalfWidth = 14.0;
		const TSharedPtr<FJsonObject>* StartLine = nullptr;
		if (Json.IsValid() && Json->TryGetObjectField(TEXT("startLine"), StartLine))
		{
			HalfWidth = JsonNum(*StartLine, TEXT("width"), 28.0) / 2.0;
		}
		const double Y = 0.03 + Rebase;
		AddSection(TEXT("startLine"), Hex(TEXT("ffffff")), [&](auto& V, auto& Tri, auto& N)
		{
			AddQuad(V, Tri, N,
				Coords::ToUE(F + FVector2D(-HalfWidth, -0.75), Y), Coords::ToUE(F + FVector2D(HalfWidth, -0.75), Y),
				Coords::ToUE(F + FVector2D(-HalfWidth, 0.75), Y), Coords::ToUE(F + FVector2D(HalfWidth, 0.75), Y), FVector::UpVector);
		});
	}

	// Boost pads: the Plane mesh is 1 m square in UE X/Y; X runs along the pad (length), Y across (width).
	BoostPads->ClearInstances();
	for (const FBoostPad& Pad : T.BoostPads)
	{
		BoostPads->AddInstance(FTransform(FRotator(0.0, Coords::YawToUE(Pad.Yaw), 0.0),
			Coords::ToUE(Pad.Position, 0.04 + Rebase), FVector(Pad.Length, Pad.Width, 1.0)), true);
	}

	// Item boxes: 2 m cubes at y 1 (pickup arrives with the items phase).
	ItemBoxes->ClearInstances();
	ItemBoxBase.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
	if (Json.IsValid() && Json->TryGetArrayField(TEXT("itemBoxes"), Boxes))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Boxes)
		{
			const TSharedPtr<FJsonObject> B = Value->AsObject();
			const FVector P = Coords::ToUE(FVector(JsonNum(B, TEXT("x")), JsonNum(B, TEXT("y")) + Rebase, JsonNum(B, TEXT("z"))));
			ItemBoxBase.Add(P);
			ItemBoxes->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(2.0)), true);
		}
	}
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* PadMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this, TEXT("MID_BoostPad"));
		PadMaterial->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("ff6a1a")));
		BoostPads->SetMaterial(0, PadMaterial);
		UMaterialInstanceDynamic* BoxMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this, TEXT("MID_ItemBox"));
		BoxMaterial->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("ffcc00")));
		ItemBoxes->SetMaterial(0, BoxMaterial);
	}

	// Lighting from the track's look (sun direction is toward the sun, three.js axes).
	Sun->SetVisibility(bAddLighting);
	SkyLight->SetVisibility(bAddLighting);
	Atmosphere->SetVisibility(bAddLighting);
	Fog->SetVisibility(bAddLighting);
	Sun->SetWorldRotation((-Coords::DirToUE(T.SunDirection).GetSafeNormal()).Rotation());
}

void ATrackBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Time = GetWorld()->GetTimeSeconds();

	// MultiplayerPowerups.tsx:159-164: yaw +1.5 rad/s, bob 0.1 * sin(2.5 t).
	for (int32 i = 0; i < ItemBoxBase.Num(); ++i)
	{
		const FVector P = ItemBoxBase[i] + FVector(0.0, 0.0, 10.0 * FMath::Sin(2.5 * Time));
		ItemBoxes->UpdateInstanceTransform(i, FTransform(FRotator(0.0, -FMath::RadiansToDegrees(1.5 * Time), 0.0), P, FVector(2.0)), true, i == ItemBoxBase.Num() - 1);
	}

	if (bShowCheckpoints && Track.IsValid())
	{
		for (const MovecadeKart::FCheckpoint& Cp : Track->Checkpoints)
		{
			DrawDebugCircle(GetWorld(), MovecadeKart::Coords::ToUE(Cp.Position, 0.3 + Track->SuggestedRebaseY), Cp.Radius * 100.0, 48,
				Cp.bFinish ? FColor::Green : FColor::Cyan, false, -1.f, 0, 12.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
	}
}

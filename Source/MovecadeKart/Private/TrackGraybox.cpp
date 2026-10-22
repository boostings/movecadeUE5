// ATrackBuilder's procedural track: the ribbons (road, edges, kerbs, walls) as procedural mesh sections when no
// imported mesh replaces them, the graybox sea and start line, the boost pads and the preview item boxes.
#include "TrackBuilder.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Track/TrackBuildHelpers.h"

using namespace MovecadeKart;
using namespace MovecadeKart::TrackBuild;
using Fx::Hex;

namespace
{
	/** Kerb stripes alternate red and cream every stripePeriodSamples rows. */
	const TCHAR* const KerbRed = TEXT("d7312a");
	const TCHAR* const KerbCream = TEXT("f7f4ec");
	/** B14: a ribbon row folds back inside a bend tighter than its offset when its nearest centre point is this much
	 *  closer than the offset (m); ribbons within 1 m of the centre never fold. */
	constexpr double FoldTolerance = 0.25;
	constexpr double FoldMinOffset = 1.0;
	/** The graybox start line: 1.5 m deep, the JSON's width (28 m by default), 3 cm over the road. */
	constexpr double StartLineHalfDepth = 0.75;
	constexpr double StartLineDefaultWidth = 28.0;
	constexpr double StartLineHeight = 0.03;
	/** A 5 km sea when the instance has no scale. */
	constexpr double DefaultSeaHalfSize = 2500.0;
	/** Boost pads sit just over the road: the prefab 4.5 cm up, the graybox plane 4 cm. */
	constexpr double PadHeight = 0.045;
	constexpr double GrayboxPadHeight = 0.04;
	/** Preview item boxes: 2 m cubes. */
	constexpr double PreviewBoxScale = 2.0;

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

	/** A flat rectangle at height Y (m) around C, half sizes in three.js x and z. */
	void AddFlatRect(TArray<FVector>& V, TArray<int32>& Tri, TArray<FVector>& N, const FVector2D& C, double HalfX, double HalfZ, double Y)
	{
		AddQuad(V, Tri, N,
			Coords::ToUE(C + FVector2D(-HalfX, -HalfZ), Y), Coords::ToUE(C + FVector2D(HalfX, -HalfZ), Y),
			Coords::ToUE(C + FVector2D(-HalfX, HalfZ), Y), Coords::ToUE(C + FVector2D(HalfX, HalfZ), Y), FVector::UpVector);
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

void ATrackBuilder::BuildRibbons()
{
	// One section per ribbon, kerbs split into their red and cream stripes.
	const FTrack& T = *Track;
	const double Rebase = T.SuggestedRebaseY;
	const int32 Rows = T.Centre.Num();
	auto Right = [&T](int32 i) { const FVector2D Tg = T.CentreTangent[i % T.Centre.Num()]; return FVector2D(-Tg.Y, Tg.X); };
	auto At = [&](int32 i, double Offset, double Y) { const int32 k = i % Rows; return Coords::ToUE(T.Centre[k] + Right(k) * Offset, Y + Rebase); };

	for (const TSharedPtr<FJsonValue>& Value : Arr(Json, TEXT("ribbons")))
	{
		const TSharedPtr<FJsonObject> R = Value->AsObject();
		const FString Name = Str(R, TEXT("name"));
		const FString Material = Str(R, TEXT("material"));
		const double Inner = Num(R, TEXT("inner")), Outer = Num(R, TEXT("outer"));
		const double InnerY = Num(R, TEXT("innerY")), OuterY = Num(R, TEXT("outerY"));
		const bool bWall = FMath::IsNearlyEqual(Inner, Outer);
		const int32 StripePeriod = Json::IntField(R, TEXT("stripePeriodSamples"), 0, 0, MAX_int32);

		// Skip quads where the offset curve folds back inside a bend tighter than the offset.
		TArray<bool> Folded;
		Folded.Init(false, Rows);
		for (int32 i = 0; i < Rows; ++i)
		{
			for (const double Offset : { Inner, Outer })
			{
				FVector2D Nearest;
				double Lateral = 0.0;
				const FVector2D P = T.Centre[i] + Right(i) * Offset;
				if (FMath::Abs(Offset) > FoldMinOffset && T.NearestCentre(P, Nearest, Lateral) < FMath::Abs(Offset) - FoldTolerance)
				{
					Folded[i] = true;
				}
			}
		}

		auto Emit = [&](int32 Stripe, TArray<FVector>& V, TArray<int32>& Tri, TArray<FVector>& N)
		{
			for (int32 i = 0; i < Rows; ++i)
			{
				if ((StripePeriod > 0 && (i / StripePeriod) % 2 != Stripe) || Folded[i] || Folded[(i + 1) % Rows])
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
			AddSection(Name + TEXT("_red"), Hex(KerbRed), [&](auto& V, auto& Tri, auto& N) { Emit(0, V, Tri, N); });
			AddSection(Name + TEXT("_cream"), Hex(KerbCream), [&](auto& V, auto& Tri, auto& N) { Emit(1, V, Tri, N); });
		}
		else
		{
			AddSection(Name, RibbonColor(Material), [&](auto& V, auto& Tri, auto& N) { Emit(-1, V, Tri, N); });
		}
	}
}

void ATrackBuilder::BuildGrayboxSea()
{
	// Prefabs are instanced by BuildPrefabInstances; only the sea plane gets a graybox here.
	for (const TSharedPtr<FJsonValue>& Value : Arr(Obj(Json, TEXT("scenery")), TEXT("instances")))
	{
		const TSharedPtr<FJsonObject> I = Value->AsObject();
		if (Str(I, TEXT("prefab")) != TEXT("sea_plane"))
		{
			continue;
		}
		const FString Tint = Str(I, TEXT("tint"));
		const TArray<TSharedPtr<FJsonValue>>* Scale = nullptr;
		I->TryGetArrayField(TEXT("scale"), Scale);
		const double HalfX = Scale ? (*Scale)[0]->AsNumber() / 2.0 : DefaultSeaHalfSize;
		const double HalfZ = Scale ? (*Scale)[2]->AsNumber() / 2.0 : DefaultSeaHalfSize;
		const FVector2D C(Num(I, TEXT("x")), Num(I, TEXT("z")));
		const double Y = Num(I, TEXT("y")) + Track->SuggestedRebaseY;
		AddSection(TEXT("sea"), Hex(*Tint.Replace(TEXT("#"), TEXT(""))), [&](auto& V, auto& Tri, auto& N) { AddFlatRect(V, Tri, N, C, HalfX, HalfZ, Y); });
	}
}

void ATrackBuilder::BuildStartLine()
{
	// Across the road at the finish checkpoint.
	if (Track->Checkpoints.Num() == 0)
	{
		return;
	}
	const FVector2D F = Track->Checkpoints[0].Position;
	double HalfWidth = 14.0;
	if (const TSharedPtr<FJsonObject> StartLine = Obj(Json, TEXT("startLine")))
	{
		HalfWidth = Num(StartLine, TEXT("width"), StartLineDefaultWidth) / 2.0;
	}
	const double Y = StartLineHeight + Track->SuggestedRebaseY;
	AddSection(TEXT("startLine"), Hex(TEXT("ffffff")), [&](auto& V, auto& Tri, auto& N) { AddFlatRect(V, Tri, N, F, HalfWidth, StartLineHalfDepth, Y); });
}

void ATrackBuilder::BuildBoostPads()
{
	const FTrack& T = *Track;
	BoostPads->ClearInstances();
	BoostPads->SetCastShadow(false);
	// The props prefab: a 1 m plane on the ground whose texture's up is the pad's forward (local -Z).
	const TSharedPtr<FJsonObject> Def = Obj(Obj(Obj(Json, TEXT("scenery")), TEXT("prefabs")), TEXT("boost_pad"));
	UStaticMesh* Mesh = Def.IsValid() ? LoadAsset<UStaticMesh>(Str(Def, TEXT("ueAsset"))) : nullptr;
	if (Mesh)
	{
		BoostPads->SetStaticMesh(Mesh);
		// The track's boostPadLook (scroll, pulse) on top of the prefab's own material hint.
		TSharedPtr<FJsonObject> Hint = MakeShared<FJsonObject>(*Obj(Def, TEXT("material")));
		if (const TSharedPtr<FJsonObject> Look = Obj(Json, TEXT("boostPadLook")))
		{
			for (const auto& Field : Look->Values)
			{
				Hint->SetField(Field.Key, Field.Value);
			}
		}
		BoostPads->SetMaterial(0, MaterialFor(Hint));
		for (const FBoostPad& Pad : T.BoostPads)
		{
			const FTransform Local = Three(FVector(Pad.Position.X, T.GroundY + PadHeight, Pad.Position.Y), Euler(0.0, Pad.Yaw, 0.0), FVector(Pad.Width, 1.0, Pad.Length));
			BoostPads->AddInstance(ToUETransform(Local, T.SuggestedRebaseY), true);
		}
		return;
	}
	// Graybox: the engine Plane (1 m square in UE X/Y); X runs along the pad (length), Y across (width).
	for (const FBoostPad& Pad : T.BoostPads)
	{
		BoostPads->AddInstance(FTransform(FRotator(0.0, Coords::YawToUE(Pad.Yaw), 0.0),
			Coords::ToUE(Pad.Position, GrayboxPadHeight + T.SuggestedRebaseY), FVector(Pad.Length, Pad.Width, 1.0)), true);
	}
}

void ATrackBuilder::BuildItemBoxes()
{
	// The race's item presenter draws the live boxes; these preview where they are in the editor.
	ItemBoxes->ClearInstances();
	ItemBoxBase.Reset();
	const double Rebase = Track->SuggestedRebaseY;
	for (const TSharedPtr<FJsonValue>& Value : Arr(Json, TEXT("itemBoxes")))
	{
		const TSharedPtr<FJsonObject> B = Value->AsObject();
		const FVector P = Coords::ToUE(FVector(Num(B, TEXT("x")), Num(B, TEXT("y")) + Rebase, Num(B, TEXT("z"))));
		ItemBoxBase.Add(P);
		ItemBoxes->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(PreviewBoxScale)), true);
	}
}

void ATrackBuilder::ApplyGrayboxMaterials()
{
	if (!BaseMaterial)
	{
		return;
	}
	if (BoostPads->GetStaticMesh() && BoostPads->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/")))
	{
		UMaterialInstanceDynamic* PadMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this, TEXT("MID_BoostPad"));
		PadMaterial->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("ff6a1a")));
		BoostPads->SetMaterial(0, PadMaterial);
	}
	UMaterialInstanceDynamic* BoxMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this, TEXT("MID_ItemBox"));
	BoxMaterial->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("ffcc00")));
	ItemBoxes->SetMaterial(0, BoxMaterial);
}

#include "Sim/KartTrack.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MovecadeKart
{
	// ---- FWallSet -------------------------------------------------------------------------------

	FIntPoint FWallSet::CellOf(const FVector2D& P) const
	{
		return FIntPoint(FMath::FloorToInt32(P.X / CellSize), FMath::FloorToInt32(P.Y / CellSize));
	}

	void FWallSet::Reset(TArray<FWallSegment>&& InSegments, double InCellSize)
	{
		Segments_ = MoveTemp(InSegments);
		CellSize = InCellSize;
		Grid.Reset();
		for (int32 i = 0; i < Segments_.Num(); ++i)
		{
			const FWallSegment& S = Segments_[i];
			const FVector2D B = S.A + S.D;
			const FIntPoint C0 = CellOf(FVector2D(FMath::Min(S.A.X, B.X), FMath::Min(S.A.Y, B.Y)));
			const FIntPoint C1 = CellOf(FVector2D(FMath::Max(S.A.X, B.X), FMath::Max(S.A.Y, B.Y)));
			for (int32 X = C0.X; X <= C1.X; ++X)
			{
				for (int32 Y = C0.Y; Y <= C1.Y; ++Y)
				{
					Grid.FindOrAdd(FIntPoint(X, Y)).Add(i);
				}
			}
		}
	}

	void FWallSet::Query(const FVector2D& Min, const FVector2D& Max, TArray<int32>& Out) const
	{
		Out.Reset();
		const FIntPoint C0 = CellOf(Min);
		const FIntPoint C1 = CellOf(Max);
		for (int32 X = C0.X; X <= C1.X; ++X)
		{
			for (int32 Y = C0.Y; Y <= C1.Y; ++Y)
			{
				if (const TArray<int32>* Cell = Grid.Find(FIntPoint(X, Y)))
				{
					Out.Append(*Cell);
				}
			}
		}
		Out.Sort();
		// Drop duplicates from segments spanning several cells.
		int32 W = 0;
		for (int32 R = 0; R < Out.Num(); ++R)
		{
			if (W == 0 || Out[W - 1] != Out[R])
			{
				Out[W++] = Out[R];
			}
		}
		Out.SetNum(W, EAllowShrinking::No);
	}

	bool FWallSet::Raycast(const FVector2D& Origin, const FVector2D& Dir, double Far, double& OutDistance, FVector2D& OutNormal) const
	{
		const FVector2D End = Origin + Dir * Far;
		TArray<int32> Candidates;
		Query(FVector2D(FMath::Min(Origin.X, End.X), FMath::Min(Origin.Y, End.Y)),
			FVector2D(FMath::Max(Origin.X, End.X), FMath::Max(Origin.Y, End.Y)), Candidates);

		double Best = TNumericLimits<double>::Max();
		for (const int32 Index : Candidates)
		{
			const FWallSegment& S = Segments_[Index];
			// Origin + t*Dir = A + u*D
			const double Denom = Dir.X * S.D.Y - Dir.Y * S.D.X;
			if (FMath::Abs(Denom) < 1e-12)
			{
				continue;
			}
			const FVector2D AO = S.A - Origin;
			const double T = (AO.X * S.D.Y - AO.Y * S.D.X) / Denom;
			const double U = (AO.X * Dir.Y - AO.Y * Dir.X) / Denom;
			if (T >= 0.0 && T <= Far && U >= 0.0 && U <= 1.0 && T < Best)
			{
				Best = T;
				OutNormal = S.FaceNormal;
			}
		}
		if (Best <= Far)
		{
			OutDistance = Best;
			return true;
		}
		return false;
	}

	// ---- FDirtGrid ------------------------------------------------------------------------------

	bool FDirtGrid::IsDirt(const FVector2D& P) const
	{
		if (!IsValid())
		{
			return false;
		}
		const int32 Col = FMath::FloorToInt32((P.X - Min.X) / CellSize);
		const int32 Row = FMath::FloorToInt32((P.Y - Min.Y) / CellSize);
		if (Col < 0 || Row < 0 || Col >= Cols || Row >= Rows)
		{
			return false;
		}
		return Cells[Row * Cols + Col];
	}

	// ---- FTrack ---------------------------------------------------------------------------------

	double FTrack::NearestCentre(const FVector2D& P, FVector2D& OutPoint, double& OutOffset) const
	{
		double BestSq = TNumericLimits<double>::Max();
		OutPoint = P;
		OutOffset = 0.0;
		const int32 N = Centre.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D A = Centre[i];
			const FVector2D D = Centre[(i + 1) % N] - A;
			const double LenSq = D.SizeSquared();
			const double T = LenSq > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, D) / LenSq, 0.0, 1.0) : 0.0;
			const FVector2D Q = A + D * T;
			const double DistSq = FVector2D::DistSquared(P, Q);
			if (DistSq < BestSq)
			{
				BestSq = DistSq;
				OutPoint = Q;
				// Right of travel is (-dz, dx) on the three.js ground plane.
				const FVector2D Right = LenSq > 0.0 ? FVector2D(-D.Y, D.X) / FMath::Sqrt(LenSq) : FVector2D::ZeroVector;
				OutOffset = FVector2D::DotProduct(P - Q, Right);
			}
		}
		return FMath::Sqrt(BestSq);
	}

	int32 FTrack::NearestWaypoint(const FVector2D& P) const
	{
		int32 Best = 0;
		double BestSq = TNumericLimits<double>::Max();
		for (int32 i = 0; i < Waypoints.Num(); ++i)
		{
			const double DistSq = FVector2D::DistSquared(P, Waypoints[i]);
			if (DistSq < BestSq)
			{
				BestSq = DistSq;
				Best = i;
			}
		}
		return Best;
	}

	namespace
	{
		double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default = 0.0)
		{
			double V = Default;
			return O.IsValid() && O->TryGetNumberField(Key, V) ? V : Default;
		}

		FVector2D XZ(const TSharedPtr<FJsonObject>& O)
		{
			return FVector2D(Num(O, TEXT("x")), Num(O, TEXT("z")));
		}

		const TArray<TSharedPtr<FJsonValue>>& Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
		{
			static const TArray<TSharedPtr<FJsonValue>> Empty;
			const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
			return O.IsValid() && O->TryGetArrayField(Key, Out) ? *Out : Empty;
		}

		TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
		{
			const TSharedPtr<FJsonObject>* Out = nullptr;
			return O.IsValid() && O->TryGetObjectField(Key, Out) ? *Out : nullptr;
		}
	}

	bool FTrack::LoadFromJsonFile(const FString& Path, FTrack& Out, FString& OutError)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			OutError = FString::Printf(TEXT("Could not read %s"), *Path);
			return false;
		}
		return LoadFromJsonString(Json, Out, OutError);
	}

	bool FTrack::LoadFromJsonString(const FString& Json, FTrack& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
		{
			OutError = TEXT("Track JSON did not parse");
			return false;
		}
		if (Num(Root, TEXT("schemaVersion")) != 1.0)
		{
			OutError = TEXT("Unsupported track schemaVersion (expected 1)");
			return false;
		}

		Out = FTrack();
		const TSharedPtr<FJsonObject> Track = Obj(Root, TEXT("track"));
		Track->TryGetStringField(TEXT("id"), Out.Id);
		Track->TryGetStringField(TEXT("name"), Out.Name);
		Out.Laps = static_cast<int32>(Num(Track, TEXT("laps"), 3));
		Out.GroundY = Num(Track, TEXT("groundY"));
		Out.SuggestedRebaseY = Num(Track, TEXT("suggestedRebaseY"));
		FString TrackType;
		Track->TryGetStringField(TEXT("trackType"), TrackType);
		Out.bAnalyticWalls = TrackType == TEXT("analytic-spline");

		const TArray<TSharedPtr<FJsonValue>>& Points = Arr(Obj(Root, TEXT("samples")), TEXT("points"));
		for (const TSharedPtr<FJsonValue>& V : Points)
		{
			const TSharedPtr<FJsonObject> P = V->AsObject();
			Out.Centre.Add(XZ(P));
			Out.CentreTangent.Add(FVector2D(Num(P, TEXT("tx")), Num(P, TEXT("tz"))));
		}
		if (Out.Centre.Num() < 3)
		{
			OutError = TEXT("Track has no centreline samples");
			return false;
		}
		if (Out.bAnalyticWalls)
		{
			const TSharedPtr<FJsonObject> Right = Obj(Points[0]->AsObject(), TEXT("right"));
			Out.WallOffset = Num(Right, TEXT("wall"), 16.3);
		}

		for (const TSharedPtr<FJsonValue>& V : Arr(Obj(Root, TEXT("navigation")), TEXT("waypoints")))
		{
			Out.Waypoints.Add(XZ(V->AsObject()));
		}

		for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("checkpoints")))
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			FCheckpoint& Cp = Out.Checkpoints.AddDefaulted_GetRef();
			C->TryGetStringField(TEXT("id"), Cp.Id);
			Cp.Position = XZ(C);
			Cp.Radius = Num(C, TEXT("radius"), 24);
			C->TryGetBoolField(TEXT("isFinish"), Cp.bFinish);
		}
		Out.StartCheckpoint = static_cast<int32>(Num(Root, TEXT("startCheckpointIndex"), 1));

		// Respawn poses: the waypoint before the checkpoint's nearest waypoint, facing the next one.
		if (Out.Waypoints.Num() > 1)
		{
			const int32 W = Out.Waypoints.Num();
			for (FCheckpoint& Cp : Out.Checkpoints)
			{
				const int32 Near = Out.NearestWaypoint(Cp.Position);
				const int32 Recovery = (Near - 1 + W) % W;
				const FVector2D P = Out.Waypoints[Recovery];
				const FVector2D Next = Out.Waypoints[(Recovery + 1) % W];
				Cp.ApproachPosition = P;
				Cp.ApproachYaw = FMath::Atan2(P.X - Next.X, P.Y - Next.Y);
			}
		}

		for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("spawnGrid")))
		{
			const TSharedPtr<FJsonObject> S = V->AsObject();
			Out.Grid.Add({ XZ(S), Num(S, TEXT("yaw")) });
		}

		for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("boostPads")))
		{
			const TSharedPtr<FJsonObject> B = V->AsObject();
			Out.BoostPads.Add({ XZ(B), Num(B, TEXT("yaw")), Num(B, TEXT("width")), Num(B, TEXT("length")) });
		}
		if (const TSharedPtr<FJsonObject> Boost = Obj(Root, TEXT("boostConfig")))
		{
			Out.BoostMultiplier = Num(Boost, TEXT("multiplier"), 1.8);
			Out.BoostDuration = Num(Boost, TEXT("durationS"), 1.5);
			Out.BoostCooldown = Num(Boost, TEXT("cooldownS"), 2.0);
			Out.BoostPadding = Num(Boost, TEXT("triggerPaddingM"), 1.5);
		}

		for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("ribbons")))
		{
			const TSharedPtr<FJsonObject> R = V->AsObject();
			FString Collision, Surface;
			R->TryGetStringField(TEXT("collision"), Collision);
			R->TryGetStringField(TEXT("surface"), Surface);
			if (Collision == TEXT("ground"))
			{
				const double Inner = Num(R, TEXT("inner")), Outer = Num(R, TEXT("outer"));
				Out.GroundBands.Add({ FMath::Min(Inner, Outer), FMath::Max(Inner, Outer),
					FMath::Max(Num(R, TEXT("innerY")), Num(R, TEXT("outerY"))), Surface == TEXT("dirt") });
			}
		}

		const TSharedPtr<FJsonObject> Collision = Obj(Root, TEXT("collision"));
		Out.KartRadius = Num(Collision, TEXT("kartRadiusM"), 1.15);
		Out.SweepSubstep = Num(Collision, TEXT("substepM"), 0.65);
		{
			// The web build tests the left wall's segments before the right wall's.
			TArray<TSharedPtr<FJsonObject>> Walls;
			for (const TSharedPtr<FJsonValue>& V : Arr(Collision, TEXT("walls")))
			{
				Walls.Add(V->AsObject());
			}
			Walls.StableSort([](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
			{
				return Num(A, TEXT("side")) < Num(B, TEXT("side"));
			});
			TArray<FWallSegment> Segments;
			// B14: where a bend is tighter than the wall offset, the inner offset curve folds back on itself
			// and leaves wall segments across the road (Harbor's final hairpin). A real wall point is a full
			// offset away from the centreline; drop segments that come closer. The analytic sweep still pulls
			// back any kart more than offset + radius out, so the trimmed apex can't leak.
			auto Folded = [&Out](const FVector2D& A, const FVector2D& B)
			{
				if (!Out.bAnalyticWalls)
				{
					return false;
				}
				FVector2D Nearest;
				double Offset = 0.0;
				const double Limit = Out.WallOffset - 0.25;
				return Out.NearestCentre(A, Nearest, Offset) < Limit || Out.NearestCentre(B, Nearest, Offset) < Limit;
			};
			TArray<FWallSegment> ProbeSegments;
			for (const TSharedPtr<FJsonObject>& W : Walls)
			{
				// Mesh walls (Violet): explicit [ax, az, bx, bz, nx, nz] segments with the face normal the web's
				// raycast reports, for the sweep ("sweep"), the probe rays ("probe") or both (no role).
				const TArray<TSharedPtr<FJsonValue>>& Explicit = Arr(W, TEXT("segments"));
				if (Explicit.Num() > 0)
				{
					FString Role;
					W->TryGetStringField(TEXT("role"), Role);
					for (const TSharedPtr<FJsonValue>& SV : Explicit)
					{
						const TArray<TSharedPtr<FJsonValue>>& E = SV->AsArray();
						if (E.Num() < 6)
						{
							continue;
						}
						FWallSegment S;
						S.A = FVector2D(E[0]->AsNumber(), E[1]->AsNumber());
						S.D = FVector2D(E[2]->AsNumber(), E[3]->AsNumber()) - S.A;
						S.LengthSq = S.D.SizeSquared();
						S.FaceNormal = FVector2D(E[4]->AsNumber(), E[5]->AsNumber());
						if (S.LengthSq <= 0.0)
						{
							continue;
						}
						if (Role != TEXT("probe"))
						{
							Segments.Add(S);
						}
						if (Role != TEXT("sweep"))
						{
							ProbeSegments.Add(S);
						}
					}
					Out.bRadialWallPush = true;
					continue;
				}

				const double Side = Num(W, TEXT("side"), 1);
				bool bClosed = false;
				W->TryGetBoolField(TEXT("closed"), bClosed);
				TArray<FVector2D> Line;
				for (const TSharedPtr<FJsonValue>& PV : Arr(W, TEXT("polyline")))
				{
					const TArray<TSharedPtr<FJsonValue>>& XZPair = PV->AsArray();
					Line.Add(FVector2D(XZPair[0]->AsNumber(), XZPair[1]->AsNumber()));
				}
				const int32 Count = bClosed ? Line.Num() : Line.Num() - 1;
				for (int32 i = 0; i < Count; ++i)
				{
					FWallSegment S;
					S.A = Line[i];
					S.D = Line[(i + 1) % Line.Num()] - S.A;
					S.LengthSq = S.D.SizeSquared();
					if (S.LengthSq <= 0.0 || Folded(S.A, S.A + S.D))
					{
						continue;
					}
					const FVector2D Right = FVector2D(-S.D.Y, S.D.X) / FMath::Sqrt(S.LengthSq);
					S.FaceNormal = Right * Side;
					Segments.Add(S);
				}
			}
			Out.Walls.Reset(MoveTemp(Segments));
			if (Out.bRadialWallPush)
			{
				Out.ProbeWalls.Reset(MoveTemp(ProbeSegments));
			}
		}

		if (const TSharedPtr<FJsonObject> Dirt = Obj(Collision, TEXT("dirtGrid")))
		{
			FDirtGrid& G = Out.DirtGrid;
			G.CellSize = Num(Dirt, TEXT("cellM"));
			G.Min = FVector2D(Num(Dirt, TEXT("minX")), Num(Dirt, TEXT("minZ")));
			G.Cols = static_cast<int32>(Num(Dirt, TEXT("cols")));
			G.Rows = static_cast<int32>(Num(Dirt, TEXT("rows")));
			if (G.IsValid())
			{
				G.Cells.Init(false, G.Cols * G.Rows);
				for (const TSharedPtr<FJsonValue>& RV : Arr(Dirt, TEXT("runs")))
				{
					const TArray<TSharedPtr<FJsonValue>>& Run = RV->AsArray();
					if (Run.Num() < 3)
					{
						continue;
					}
					const int32 Row = static_cast<int32>(Run[0]->AsNumber());
					const int32 First = FMath::Max(0, static_cast<int32>(Run[1]->AsNumber()));
					const int32 Last = FMath::Min(G.Cols - 1, static_cast<int32>(Run[2]->AsNumber()));
					if (Row < 0 || Row >= G.Rows)
					{
						continue;
					}
					for (int32 Col = First; Col <= Last; ++Col)
					{
						G.Cells[Row * G.Cols + Col] = true;
					}
				}
			}
		}

		if (const TSharedPtr<FJsonObject> Recovery = Obj(Root, TEXT("recovery")))
		{
			Out.RecoveryDistance = Num(Recovery, TEXT("maxDistanceM"), 64);
			Out.RecoverySkipWaypoints = static_cast<int32>(Num(Recovery, TEXT("skipWaypoints"), 3));
		}

		for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("itemBoxes")))
		{
			const TSharedPtr<FJsonObject> B = V->AsObject();
			Out.ItemBoxes.Add(FVector(Num(B, TEXT("x")), Num(B, TEXT("y"), 1.0), Num(B, TEXT("z"))));
		}

		for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("staticCameras")))
		{
			const TSharedPtr<FJsonObject> C = V->AsObject();
			auto Vec = [&C](const TCHAR* Key)
			{
				const TArray<TSharedPtr<FJsonValue>>& P = Arr(C, Key);
				return P.Num() == 3 ? FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()) : FVector::ZeroVector;
			};
			FStaticCamera& Camera = Out.StaticCameras.AddDefaulted_GetRef();
			C->TryGetStringField(TEXT("name"), Camera.Name);
			Camera.Position = Vec(TEXT("position"));
			Camera.LookAt = Vec(TEXT("lookAt"));
		}

		if (const TSharedPtr<FJsonObject> Minimap = Obj(Root, TEXT("minimap")))
		{
			if (const TSharedPtr<FJsonObject> Bounds = Obj(Minimap, TEXT("bounds")))
			{
				Out.MinimapBounds = FBox2D(FVector2D(Num(Bounds, TEXT("minX")), Num(Bounds, TEXT("minZ"))),
					FVector2D(Num(Bounds, TEXT("maxX")), Num(Bounds, TEXT("maxZ"))));
			}
		}

		if (const TSharedPtr<FJsonObject> Look = Obj(Root, TEXT("look")))
		{
			const TArray<TSharedPtr<FJsonValue>>& Sun = Arr(Look, TEXT("sunDirection"));
			if (Sun.Num() == 3)
			{
				Out.SunDirection = FVector(Sun[0]->AsNumber(), Sun[1]->AsNumber(), Sun[2]->AsNumber());
			}
		}

		if (Out.Grid.Num() == 0 || Out.Checkpoints.Num() == 0)
		{
			OutError = TEXT("Track has no spawn grid or checkpoints");
			return false;
		}
		return true;
	}
}

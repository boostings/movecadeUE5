#include "Sim/KartTrack.h"

#include "Algo/AllOf.h"
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

		/** The object in an array entry, or null when the entry is something else (bad entries are skipped). */
		TSharedPtr<FJsonObject> ObjOf(const TSharedPtr<FJsonValue>& V)
		{
			return V.IsValid() && V->Type == EJson::Object ? V->AsObject() : nullptr;
		}

		using FNumbers = TArray<double, TInlineAllocator<8>>;

		/** The numbers in an array entry; false unless it is an array of at least Min numbers. */
		bool NumbersOf(const TSharedPtr<FJsonValue>& V, int32 Min, FNumbers& Out)
		{
			Out.Reset();
			if (!V.IsValid() || V->Type != EJson::Array)
			{
				return false;
			}
			for (const TSharedPtr<FJsonValue>& E : V->AsArray())
			{
				if (!E.IsValid() || E->Type != EJson::Number)
				{
					return false;
				}
				Out.Add(E->AsNumber());
			}
			return Out.Num() >= Min;
		}

		FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
		{
			FString V;
			if (O.IsValid())
			{
				O->TryGetStringField(Key, V);
			}
			return V;
		}

		/** Centreline samples (and, for spline tracks, the wall offset from the first sample). */
		void ParseCentre(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			for (const TSharedPtr<FJsonValue>& V : Arr(Obj(Root, TEXT("samples")), TEXT("points")))
			{
				if (const TSharedPtr<FJsonObject> P = ObjOf(V))
				{
					if (Out.bAnalyticWalls && Out.Centre.Num() == 0)
					{
						Out.WallOffset = Num(Obj(P, TEXT("right")), TEXT("wall"), 16.3);
					}
					Out.Centre.Add(XZ(P));
					Out.CentreTangent.Add(FVector2D(Num(P, TEXT("tx")), Num(P, TEXT("tz"))));
				}
			}
		}

		/** Waypoints, checkpoints and each checkpoint's respawn pose. */
		void ParseRoute(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			for (const TSharedPtr<FJsonValue>& V : Arr(Obj(Root, TEXT("navigation")), TEXT("waypoints")))
			{
				if (const TSharedPtr<FJsonObject> W = ObjOf(V))
				{
					Out.Waypoints.Add(XZ(W));
				}
			}
			for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("checkpoints")))
			{
				if (const TSharedPtr<FJsonObject> C = ObjOf(V))
				{
					FCheckpoint& Cp = Out.Checkpoints.AddDefaulted_GetRef();
					Cp.Id = Str(C, TEXT("id"));
					Cp.Position = XZ(C);
					Cp.Radius = Num(C, TEXT("radius"), 24);
					C->TryGetBoolField(TEXT("isFinish"), Cp.bFinish);
				}
			}
			Out.StartCheckpoint = static_cast<int32>(Num(Root, TEXT("startCheckpointIndex"), 1));

			// Respawn poses: the waypoint before the checkpoint's nearest waypoint, facing the next one.
			const int32 W = Out.Waypoints.Num();
			if (W > 1)
			{
				for (FCheckpoint& Cp : Out.Checkpoints)
				{
					const int32 Recovery = (Out.NearestWaypoint(Cp.Position) - 1 + W) % W;
					const FVector2D P = Out.Waypoints[Recovery];
					const FVector2D Next = Out.Waypoints[(Recovery + 1) % W];
					Cp.ApproachPosition = P;
					Cp.ApproachYaw = FMath::Atan2(P.X - Next.X, P.Y - Next.Y);
				}
			}
		}

		/** Spawn grid, boost pads and their config, and the ground bands. */
		void ParseSurfaces(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("spawnGrid")))
			{
				if (const TSharedPtr<FJsonObject> S = ObjOf(V))
				{
					Out.Grid.Add({ XZ(S), Num(S, TEXT("yaw")) });
				}
			}
			for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("boostPads")))
			{
				if (const TSharedPtr<FJsonObject> B = ObjOf(V))
				{
					Out.BoostPads.Add({ XZ(B), Num(B, TEXT("yaw")), Num(B, TEXT("width")), Num(B, TEXT("length")) });
				}
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
				const TSharedPtr<FJsonObject> R = ObjOf(V);
				if (R && Str(R, TEXT("collision")) == TEXT("ground"))
				{
					const double Inner = Num(R, TEXT("inner")), Outer = Num(R, TEXT("outer"));
					Out.GroundBands.Add({ FMath::Min(Inner, Outer), FMath::Max(Inner, Outer),
						FMath::Max(Num(R, TEXT("innerY")), Num(R, TEXT("outerY"))), Str(R, TEXT("surface")) == TEXT("dirt") });
				}
			}
		}

		/** Walls: explicit mesh-wall segments (Violet) or offset polylines (Harbor); then the dirt grid. */
		void ParseCollision(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			const TSharedPtr<FJsonObject> Collision = Obj(Root, TEXT("collision"));
			Out.KartRadius = Num(Collision, TEXT("kartRadiusM"), 1.15);
			Out.SweepSubstep = Num(Collision, TEXT("substepM"), 0.65);

			// The web build tests the left wall's segments before the right wall's.
			TArray<TSharedPtr<FJsonObject>> Walls;
			for (const TSharedPtr<FJsonValue>& V : Arr(Collision, TEXT("walls")))
			{
				if (const TSharedPtr<FJsonObject> W = ObjOf(V))
				{
					Walls.Add(W);
				}
			}
			Walls.StableSort([](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
			{
				return Num(A, TEXT("side")) < Num(B, TEXT("side"));
			});

			// B14: where a bend is tighter than the wall offset, the inner offset curve folds back on itself and
			// leaves wall segments across the road (Harbor's final hairpin). A real wall point is a full offset
			// away from the centreline; drop segments that come closer. The analytic sweep still pulls back any
			// kart more than offset + radius out, so the trimmed apex can't leak.
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

			TArray<FWallSegment> Segments;
			TArray<FWallSegment> ProbeSegments;
			FNumbers E;
			for (const TSharedPtr<FJsonObject>& W : Walls)
			{
				// Mesh walls (Violet): explicit [ax, az, bx, bz, nx, nz] segments with the face normal the web's
				// raycast reports, for the sweep ("sweep"), the probe rays ("probe") or both (no role).
				const TArray<TSharedPtr<FJsonValue>>& Explicit = Arr(W, TEXT("segments"));
				if (Explicit.Num() > 0)
				{
					const FString Role = Str(W, TEXT("role"));
					for (const TSharedPtr<FJsonValue>& SV : Explicit)
					{
						if (!NumbersOf(SV, 6, E))
						{
							continue;
						}
						FWallSegment S;
						S.A = FVector2D(E[0], E[1]);
						S.D = FVector2D(E[2], E[3]) - S.A;
						S.LengthSq = S.D.SizeSquared();
						S.FaceNormal = FVector2D(E[4], E[5]);
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
					if (NumbersOf(PV, 2, E))
					{
						Line.Add(FVector2D(E[0], E[1]));
					}
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
						if (!NumbersOf(RV, 3, E))
						{
							continue;
						}
						const int32 Row = static_cast<int32>(E[0]);
						const int32 First = FMath::Max(0, static_cast<int32>(E[1]));
						const int32 Last = FMath::Min(G.Cols - 1, static_cast<int32>(E[2]));
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
		}

		/** Recovery tuning, item boxes, intro cameras, minimap bounds and the sun. */
		void ParsePresentation(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			if (const TSharedPtr<FJsonObject> Recovery = Obj(Root, TEXT("recovery")))
			{
				Out.RecoveryDistance = Num(Recovery, TEXT("maxDistanceM"), 64);
				Out.RecoverySkipWaypoints = static_cast<int32>(Num(Recovery, TEXT("skipWaypoints"), 3));
			}
			for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("itemBoxes")))
			{
				if (const TSharedPtr<FJsonObject> B = ObjOf(V))
				{
					Out.ItemBoxes.Add(FVector(Num(B, TEXT("x")), Num(B, TEXT("y"), 1.0), Num(B, TEXT("z"))));
				}
			}
			auto Vec3 = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
			{
				const TArray<TSharedPtr<FJsonValue>>& P = Arr(O, Key);
				const bool bNumbers = P.Num() == 3 && Algo::AllOf(P, [](const TSharedPtr<FJsonValue>& V) { return V.IsValid() && V->Type == EJson::Number; });
				return bNumbers ? FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()) : FVector::ZeroVector;
			};
			for (const TSharedPtr<FJsonValue>& V : Arr(Root, TEXT("staticCameras")))
			{
				if (const TSharedPtr<FJsonObject> C = ObjOf(V))
				{
					FStaticCamera& Camera = Out.StaticCameras.AddDefaulted_GetRef();
					Camera.Name = Str(C, TEXT("name"));
					Camera.Position = Vec3(C, TEXT("position"));
					Camera.LookAt = Vec3(C, TEXT("lookAt"));
				}
			}
			if (const TSharedPtr<FJsonObject> Bounds = Obj(Obj(Root, TEXT("minimap")), TEXT("bounds")))
			{
				Out.MinimapBounds = FBox2D(FVector2D(Num(Bounds, TEXT("minX")), Num(Bounds, TEXT("minZ"))),
					FVector2D(Num(Bounds, TEXT("maxX")), Num(Bounds, TEXT("maxZ"))));
			}
			const TSharedPtr<FJsonObject> Look = Obj(Root, TEXT("look"));
			if (Look && Arr(Look, TEXT("sunDirection")).Num() == 3)
			{
				Out.SunDirection = Vec3(Look, TEXT("sunDirection"));
			}
		}

		/** Everything the sim relies on without checking again. */
		bool Validate(const FTrack& T, FString& OutError)
		{
			if (T.Centre.Num() < 3)
			{
				OutError = TEXT("Track has no centreline samples");
			}
			else if (T.Grid.Num() == 0 || T.Checkpoints.Num() == 0)
			{
				OutError = TEXT("Track has no spawn grid or checkpoints");
			}
			else if (!T.Checkpoints.IsValidIndex(T.StartCheckpoint))
			{
				OutError = FString::Printf(TEXT("startCheckpointIndex %d is out of range (%d checkpoints)"), T.StartCheckpoint, T.Checkpoints.Num());
			}
			else if (T.Waypoints.Num() < 2)
			{
				OutError = TEXT("Track needs at least two waypoints");
			}
			else if (T.Laps < 1)
			{
				OutError = TEXT("Track needs at least one lap");
			}
			return OutError.IsEmpty();
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
		const TSharedPtr<FJsonObject> Header = Obj(Root, TEXT("track"));
		if (!Header)
		{
			OutError = TEXT("Track JSON has no \"track\" header");
			return false;
		}

		Out = FTrack();
		Out.Id = Str(Header, TEXT("id"));
		Out.Name = Str(Header, TEXT("name"));
		Out.Laps = static_cast<int32>(Num(Header, TEXT("laps"), 3));
		Out.GroundY = Num(Header, TEXT("groundY"));
		Out.SuggestedRebaseY = Num(Header, TEXT("suggestedRebaseY"));
		Out.bAnalyticWalls = Str(Header, TEXT("trackType")) == TEXT("analytic-spline");

		// Order matters: the walls are trimmed against the centreline, and the respawn poses use the waypoints.
		ParseCentre(Root, Out);
		ParseRoute(Root, Out);
		ParseSurfaces(Root, Out);
		ParseCollision(Root, Out);
		ParsePresentation(Root, Out);
		return Validate(Out, OutError);
	}
}

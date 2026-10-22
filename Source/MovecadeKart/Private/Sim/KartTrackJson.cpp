#include "Sim/KartTrack.h"

#include "Algo/AllOf.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Loading Data/Tracks/<id>.track.json (schemaVersion 1, written by Tools/track-export) into an FTrack.
// A missing field keeps FTrack's default; a malformed entry is skipped; Validate rejects a track the sim can't run.

namespace MovecadeKart
{
	namespace
	{
		// Field readers that tolerate a null object or a missing or mistyped field, returning the default.

		double NumberField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default = 0.0)
		{
			double V = Default;
			return O.IsValid() && O->TryGetNumberField(Key, V) ? V : Default;
		}

		/** The object's { x, z } as a point on the three.js ground plane. */
		FVector2D GroundPoint(const TSharedPtr<FJsonObject>& O)
		{
			return FVector2D(NumberField(O, TEXT("x")), NumberField(O, TEXT("z")));
		}

		const TArray<TSharedPtr<FJsonValue>>& ArrayField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
		{
			static const TArray<TSharedPtr<FJsonValue>> Empty;
			const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
			return O.IsValid() && O->TryGetArrayField(Key, Out) ? *Out : Empty;
		}

		TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
		{
			const TSharedPtr<FJsonObject>* Out = nullptr;
			return O.IsValid() && O->TryGetObjectField(Key, Out) ? *Out : nullptr;
		}

		/** The object in an array entry, or null when the entry is something else (bad entries are skipped). */
		TSharedPtr<FJsonObject> AsObject(const TSharedPtr<FJsonValue>& V)
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

		FString StringField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
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
			for (const TSharedPtr<FJsonValue>& V : ArrayField(ObjectField(Root, TEXT("samples")), TEXT("points")))
			{
				if (const TSharedPtr<FJsonObject> P = AsObject(V))
				{
					if (Out.bAnalyticWalls && Out.Centre.Num() == 0)
					{
						Out.WallOffset = NumberField(ObjectField(P, TEXT("right")), TEXT("wall"), Out.WallOffset);
					}
					Out.Centre.Add(GroundPoint(P));
					Out.CentreTangent.Add(FVector2D(NumberField(P, TEXT("tx")), NumberField(P, TEXT("tz"))));
				}
			}
			Out.RebuildCentreSegments();
		}

		/** Waypoints, checkpoints and each checkpoint's respawn pose. */
		void ParseRoute(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			for (const TSharedPtr<FJsonValue>& V : ArrayField(ObjectField(Root, TEXT("navigation")), TEXT("waypoints")))
			{
				if (const TSharedPtr<FJsonObject> W = AsObject(V))
				{
					Out.Waypoints.Add(GroundPoint(W));
				}
			}
			for (const TSharedPtr<FJsonValue>& V : ArrayField(Root, TEXT("checkpoints")))
			{
				if (const TSharedPtr<FJsonObject> C = AsObject(V))
				{
					FCheckpoint& Cp = Out.Checkpoints.AddDefaulted_GetRef();
					Cp.Id = StringField(C, TEXT("id"));
					Cp.Position = GroundPoint(C);
					Cp.Radius = NumberField(C, TEXT("radius"), Cp.Radius);
					C->TryGetBoolField(TEXT("isFinish"), Cp.bFinish);
				}
			}
			Out.StartCheckpoint = static_cast<int32>(NumberField(Root, TEXT("startCheckpointIndex"), Out.StartCheckpoint));

			// Respawn poses: the waypoint before the checkpoint's nearest waypoint, facing the next one.
			const int32 W = Out.Waypoints.Num();
			if (W > 1)
			{
				for (FCheckpoint& Cp : Out.Checkpoints)
				{
					Cp.Waypoint = Out.NearestWaypoint(Cp.Position);
					const int32 Recovery = (Cp.Waypoint - 1 + W) % W;
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
			for (const TSharedPtr<FJsonValue>& V : ArrayField(Root, TEXT("spawnGrid")))
			{
				if (const TSharedPtr<FJsonObject> S = AsObject(V))
				{
					Out.Grid.Add({ GroundPoint(S), NumberField(S, TEXT("yaw")) });
				}
			}
			for (const TSharedPtr<FJsonValue>& V : ArrayField(Root, TEXT("boostPads")))
			{
				if (const TSharedPtr<FJsonObject> B = AsObject(V))
				{
					FBoostPad& Pad = Out.BoostPads.AddDefaulted_GetRef();
					Pad.Position = GroundPoint(B);
					Pad.Yaw = NumberField(B, TEXT("yaw"));
					Pad.Width = NumberField(B, TEXT("width"));
					Pad.Length = NumberField(B, TEXT("length"));
					Pad.CosYaw = FMath::Cos(Pad.Yaw);
					Pad.SinYaw = FMath::Sin(Pad.Yaw);
				}
			}
			if (const TSharedPtr<FJsonObject> Boost = ObjectField(Root, TEXT("boostConfig")))
			{
				Out.BoostMultiplier = NumberField(Boost, TEXT("multiplier"), Out.BoostMultiplier);
				Out.BoostDuration = NumberField(Boost, TEXT("durationS"), Out.BoostDuration);
				Out.BoostCooldown = NumberField(Boost, TEXT("cooldownS"), Out.BoostCooldown);
				Out.BoostPadding = NumberField(Boost, TEXT("triggerPaddingM"), Out.BoostPadding);
			}
			for (const TSharedPtr<FJsonValue>& V : ArrayField(Root, TEXT("ribbons")))
			{
				const TSharedPtr<FJsonObject> R = AsObject(V);
				if (R && StringField(R, TEXT("collision")) == TEXT("ground"))
				{
					const double Inner = NumberField(R, TEXT("inner")), Outer = NumberField(R, TEXT("outer"));
					Out.GroundBands.Add({ FMath::Min(Inner, Outer), FMath::Max(Inner, Outer),
						FMath::Max(NumberField(R, TEXT("innerY")), NumberField(R, TEXT("outerY"))), StringField(R, TEXT("surface")) == TEXT("dirt") });
				}
			}
		}

		/** Walls: explicit mesh-wall segments (Violet) or offset polylines (Harbor); then the dirt grid. */
		void ParseCollision(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			const TSharedPtr<FJsonObject> Collision = ObjectField(Root, TEXT("collision"));
			Out.KartRadius = NumberField(Collision, TEXT("kartRadiusM"), Out.KartRadius);
			Out.SweepSubstep = NumberField(Collision, TEXT("substepM"), Out.SweepSubstep);

			// The web build tests the left wall's segments before the right wall's.
			TArray<TSharedPtr<FJsonObject>> Walls;
			for (const TSharedPtr<FJsonValue>& V : ArrayField(Collision, TEXT("walls")))
			{
				if (const TSharedPtr<FJsonObject> W = AsObject(V))
				{
					Walls.Add(W);
				}
			}
			Walls.StableSort([](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
			{
				return NumberField(A, TEXT("side")) < NumberField(B, TEXT("side"));
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
				const TArray<TSharedPtr<FJsonValue>>& Explicit = ArrayField(W, TEXT("segments"));
				if (Explicit.Num() > 0)
				{
					const FString Role = StringField(W, TEXT("role"));
					for (const TSharedPtr<FJsonValue>& SV : Explicit)
					{
						if (!NumbersOf(SV, 6, E))
						{
							continue;
						}
						FWallSegment S;
						S.Start = FVector2D(E[0], E[1]);
						S.Delta = FVector2D(E[2], E[3]) - S.Start;
						S.LengthSq = S.Delta.SizeSquared();
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

				const double Side = NumberField(W, TEXT("side"), 1);
				bool bClosed = false;
				W->TryGetBoolField(TEXT("closed"), bClosed);
				TArray<FVector2D> Line;
				for (const TSharedPtr<FJsonValue>& PV : ArrayField(W, TEXT("polyline")))
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
					S.Start = Line[i];
					S.Delta = Line[(i + 1) % Line.Num()] - S.Start;
					S.LengthSq = S.Delta.SizeSquared();
					if (S.LengthSq <= 0.0 || Folded(S.Start, S.Start + S.Delta))
					{
						continue;
					}
					const FVector2D Right = FVector2D(-S.Delta.Y, S.Delta.X) / FMath::Sqrt(S.LengthSq);
					S.FaceNormal = Right * Side;
					Segments.Add(S);
				}
			}
			Out.Walls.Reset(MoveTemp(Segments));
			if (Out.bRadialWallPush)
			{
				Out.ProbeWalls.Reset(MoveTemp(ProbeSegments));
			}

			if (const TSharedPtr<FJsonObject> Dirt = ObjectField(Collision, TEXT("dirtGrid")))
			{
				FDirtGrid& G = Out.DirtGrid;
				G.CellSize = NumberField(Dirt, TEXT("cellM"));
				G.Min = FVector2D(NumberField(Dirt, TEXT("minX")), NumberField(Dirt, TEXT("minZ")));
				G.Cols = static_cast<int32>(NumberField(Dirt, TEXT("cols")));
				G.Rows = static_cast<int32>(NumberField(Dirt, TEXT("rows")));
				if (G.IsValid())
				{
					G.Cells.Init(false, G.Cols * G.Rows);
					for (const TSharedPtr<FJsonValue>& RV : ArrayField(Dirt, TEXT("runs")))
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
			if (const TSharedPtr<FJsonObject> Recovery = ObjectField(Root, TEXT("recovery")))
			{
				Out.RecoveryDistance = NumberField(Recovery, TEXT("maxDistanceM"), Out.RecoveryDistance);
				Out.RecoverySkipWaypoints = static_cast<int32>(NumberField(Recovery, TEXT("skipWaypoints"), Out.RecoverySkipWaypoints));
			}
			for (const TSharedPtr<FJsonValue>& V : ArrayField(Root, TEXT("itemBoxes")))
			{
				if (const TSharedPtr<FJsonObject> B = AsObject(V))
				{
					Out.ItemBoxes.Add(FVector(NumberField(B, TEXT("x")), NumberField(B, TEXT("y"), 1.0), NumberField(B, TEXT("z"))));
				}
			}
			auto Vec3 = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
			{
				const TArray<TSharedPtr<FJsonValue>>& P = ArrayField(O, Key);
				const bool bNumbers = P.Num() == 3 && Algo::AllOf(P, [](const TSharedPtr<FJsonValue>& V) { return V.IsValid() && V->Type == EJson::Number; });
				return bNumbers ? FVector(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()) : FVector::ZeroVector;
			};
			for (const TSharedPtr<FJsonValue>& V : ArrayField(Root, TEXT("staticCameras")))
			{
				if (const TSharedPtr<FJsonObject> C = AsObject(V))
				{
					FStaticCamera& Camera = Out.StaticCameras.AddDefaulted_GetRef();
					Camera.Name = StringField(C, TEXT("name"));
					Camera.Position = Vec3(C, TEXT("position"));
					Camera.LookAt = Vec3(C, TEXT("lookAt"));
				}
			}
			if (const TSharedPtr<FJsonObject> Bounds = ObjectField(ObjectField(Root, TEXT("minimap")), TEXT("bounds")))
			{
				Out.MinimapBounds = FBox2D(FVector2D(NumberField(Bounds, TEXT("minX")), NumberField(Bounds, TEXT("minZ"))),
					FVector2D(NumberField(Bounds, TEXT("maxX")), NumberField(Bounds, TEXT("maxZ"))));
			}
			const TSharedPtr<FJsonObject> Look = ObjectField(Root, TEXT("look"));
			if (Look && ArrayField(Look, TEXT("sunDirection")).Num() == 3)
			{
				Out.SunDirection = Vec3(Look, TEXT("sunDirection"));
			}
		}

		/** Everything the sim relies on without checking again. */
		bool Validate(const FTrack& Track, FString& OutError)
		{
			if (Track.Centre.Num() < 3)
			{
				OutError = TEXT("Track has no centreline samples");
			}
			else if (Track.Grid.Num() == 0 || Track.Checkpoints.Num() == 0)
			{
				OutError = TEXT("Track has no spawn grid or checkpoints");
			}
			else if (!Track.Checkpoints.IsValidIndex(Track.StartCheckpoint))
			{
				OutError = FString::Printf(TEXT("startCheckpointIndex %d is out of range (%d checkpoints)"), Track.StartCheckpoint, Track.Checkpoints.Num());
			}
			else if (Track.Waypoints.Num() < 2)
			{
				OutError = TEXT("Track needs at least two waypoints");
			}
			else if (Track.Laps < 1)
			{
				OutError = TEXT("Track needs at least one lap");
			}
			else if (!(Track.SweepSubstep > 0.0) || !(Track.KartRadius > 0.0))
			{
				// The wall sweep divides by the substep; a zero or NaN here never terminates.
				OutError = FString::Printf(TEXT("sweepSubstep (%f) and kartRadius (%f) must be positive"), Track.SweepSubstep, Track.KartRadius);
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
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root))
		{
			OutError = TEXT("Track JSON did not parse");
			return false;
		}
		return LoadFromJsonObject(Root, Out, OutError);
	}

	bool FTrack::LoadFromJsonObject(const TSharedPtr<FJsonObject>& Root, FTrack& Out, FString& OutError)
	{
		if (!Root.IsValid())
		{
			OutError = TEXT("Track JSON did not parse");
			return false;
		}
		if (NumberField(Root, TEXT("schemaVersion")) != 1.0)
		{
			OutError = TEXT("Unsupported track schemaVersion (expected 1)");
			return false;
		}
		const TSharedPtr<FJsonObject> Header = ObjectField(Root, TEXT("track"));
		if (!Header)
		{
			OutError = TEXT("Track JSON has no \"track\" header");
			return false;
		}

		Out = FTrack();
		Out.Id = StringField(Header, TEXT("id"));
		Out.Name = StringField(Header, TEXT("name"));
		Out.Laps = static_cast<int32>(NumberField(Header, TEXT("laps"), Out.Laps));
		Out.GroundY = NumberField(Header, TEXT("groundY"));
		Out.SuggestedRebaseY = NumberField(Header, TEXT("suggestedRebaseY"));
		Out.bAnalyticWalls = StringField(Header, TEXT("trackType")) == TEXT("analytic-spline");

		// Order matters: the walls are trimmed against the centreline, and the respawn poses use the waypoints.
		ParseCentre(Root, Out);
		ParseRoute(Root, Out);
		ParseSurfaces(Root, Out);
		ParseCollision(Root, Out);
		ParsePresentation(Root, Out);
		return Validate(Out, OutError);
	}
}

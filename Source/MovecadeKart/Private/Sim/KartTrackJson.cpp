#include "Sim/KartTrack.h"

#include "Algo/AllOf.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Sim/KartJsonFields.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartTrack, Log, All);

// Loading Data/Tracks/<id>.track.json (schemaVersion 1, written by Tools/track-export) into an FTrack.
// A missing field keeps FTrack's default; a malformed entry is skipped; Validate rejects a track the sim can't run.

namespace MovecadeKart
{
	namespace
	{
		/** How much closer than the wall offset a Harbor wall point may come to the centreline before it counts as folded. */
		constexpr double FoldedWallTolerance = 0.25;
		/** No real dirt raster comes near this (Violet's is 1885 x 1744); larger means a corrupt file. */
		constexpr double MaxDirtGridCells = 1 << 26;
		/** Integer fields' ranges: far past any real track, and small enough that nothing downstream overflows. */
		constexpr int32 MaxLaps = 99;
		constexpr int32 MaxRecoverySkipWaypoints = 1 << 16;
		/** No track coordinate comes near this (both are within 350 m of the origin): wall points beyond it are
		 *  dropped, as the wall grid's cell walk must stay bounded. */
		constexpr double MaxTrackCoordinate = 1e5;
		/** The kart disc and sweep step the sim can run: the sweep query box is 8 radii wide, and a substep below
		 *  the floor would multiply the sweep's per-step work without changing the result. */
		constexpr double MaxKartRadius = 10.0;
		constexpr double MinSweepSubstep = 0.01;
		/** A spawn heading past this many radians (every real one is within a turn) is corrupt. */
		constexpr double MaxSpawnYaw = 100.0;

		bool IsTrackPoint(const FVector2D& P)
		{
			return FMath::Abs(P.X) <= MaxTrackCoordinate && FMath::Abs(P.Y) <= MaxTrackCoordinate; // false for NaN
		}

		// Field readers that tolerate a null object or a missing or mistyped field, returning the default.

		/** O[Key], or Default when it is missing, not a number, or not finite (a JSON 1e999 parses as infinity). */
		double NumberField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default = 0.0)
		{
			double V = Default;
			if (!O.IsValid() || !O->TryGetNumberField(Key, V))
			{
				return Default;
			}
			if (!FMath::IsFinite(V))
			{
				UE_LOG(LogKartTrack, Warning, TEXT("\"%s\" is not a finite number; using %g"), Key, Default);
				return Default;
			}
			return V;
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

		/** The numbers in an array entry; false unless it is an array of at least Min finite numbers. */
		bool NumbersOf(const TSharedPtr<FJsonValue>& V, int32 Min, FNumbers& Out)
		{
			Out.Reset();
			if (!V.IsValid() || V->Type != EJson::Array)
			{
				return false;
			}
			for (const TSharedPtr<FJsonValue>& E : V->AsArray())
			{
				if (!E.IsValid() || E->Type != EJson::Number || !FMath::IsFinite(E->AsNumber()))
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
				if (const TSharedPtr<FJsonObject> Waypoint = AsObject(V))
				{
					Out.Waypoints.Add(GroundPoint(Waypoint));
				}
			}
			for (const TSharedPtr<FJsonValue>& V : ArrayField(Root, TEXT("checkpoints")))
			{
				if (const TSharedPtr<FJsonObject> Json = AsObject(V))
				{
					FCheckpoint& Checkpoint = Out.Checkpoints.AddDefaulted_GetRef();
					Checkpoint.Id = StringField(Json, TEXT("id"));
					Checkpoint.Position = GroundPoint(Json);
					Checkpoint.Radius = NumberField(Json, TEXT("radius"), Checkpoint.Radius);
					Json->TryGetBoolField(TEXT("isFinish"), Checkpoint.bFinish);
				}
			}
			// Below zero reads as INDEX_NONE, which Validate rejects.
			Out.StartCheckpoint = Json::IntField(Root, TEXT("startCheckpointIndex"), Out.StartCheckpoint, INDEX_NONE, MAX_int32);

			// Respawn poses: the waypoint before the checkpoint's nearest waypoint, facing the next one.
			const int32 WaypointCount = Out.Waypoints.Num();
			if (WaypointCount > 1)
			{
				for (FCheckpoint& Checkpoint : Out.Checkpoints)
				{
					Checkpoint.Waypoint = Out.NearestWaypoint(Checkpoint.Position);
					const int32 Recovery = (Checkpoint.Waypoint - 1 + WaypointCount) % WaypointCount;
					const FVector2D Approach = Out.Waypoints[Recovery];
					const FVector2D Next = Out.Waypoints[(Recovery + 1) % WaypointCount];
					Checkpoint.ApproachPosition = Approach;
					Checkpoint.ApproachYaw = FMath::Atan2(Approach.X - Next.X, Approach.Y - Next.Y);
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
					const FSpawnSlot Slot{ GroundPoint(S), NumberField(S, TEXT("yaw")) };
					// The karts start and the bots steer from this pose: it must be on the map, with a sane heading.
					if (!IsTrackPoint(Slot.Position) || !(FMath::Abs(Slot.Yaw) <= MaxSpawnYaw))
					{
						UE_LOG(LogKartTrack, Warning, TEXT("Skipping spawn slot at (%g, %g) yaw %g"), Slot.Position.X, Slot.Position.Y, Slot.Yaw);
						continue;
					}
					Out.Grid.Add(Slot);
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

		void WarnOutOfRange(int32 Dropped)
		{
			UE_CLOG(Dropped > 0, LogKartTrack, Warning, TEXT("Dropped %d wall segments with a point beyond %.0f m"), Dropped, MaxTrackCoordinate);
		}

		/** Mesh walls (Violet): explicit [ax, az, bx, bz, nx, nz] segments with the face normal the web's raycast
		 *  reports, for the sweep ("sweep"), the probe rays ("probe") or both (no role). */
		void AddMeshWall(const TSharedPtr<FJsonObject>& Wall, const TArray<TSharedPtr<FJsonValue>>& Entries,
			TArray<FWallSegment>& OutSweep, TArray<FWallSegment>& OutProbe)
		{
			const FString Role = StringField(Wall, TEXT("role"));
			FNumbers Numbers;
			int32 OutOfRange = 0;
			for (const TSharedPtr<FJsonValue>& Entry : Entries)
			{
				if (!NumbersOf(Entry, 6, Numbers))
				{
					continue;
				}
				const FVector2D End(Numbers[2], Numbers[3]);
				FWallSegment Segment;
				Segment.Start = FVector2D(Numbers[0], Numbers[1]);
				if (!IsTrackPoint(Segment.Start) || !IsTrackPoint(End))
				{
					++OutOfRange;
					continue;
				}
				Segment.Delta = End - Segment.Start;
				Segment.LengthSq = Segment.Delta.SizeSquared();
				Segment.FaceNormal = FVector2D(Numbers[4], Numbers[5]);
				if (Segment.LengthSq <= 0.0)
				{
					continue;
				}
				if (Role != TEXT("probe"))
				{
					OutSweep.Add(Segment);
				}
				if (Role != TEXT("sweep"))
				{
					OutProbe.Add(Segment);
				}
			}
			WarnOutOfRange(OutOfRange);
		}

		/** B14: where a bend is tighter than the wall offset, the inner offset curve folds back on itself and leaves
		 *  wall segments across the road (Harbor's final hairpin). A real wall point is a full offset away from the
		 *  centreline, so a segment that comes closer is dropped. The analytic sweep still pulls back any kart more
		 *  than offset + radius out, so the trimmed apex can't leak. */
		bool IsFoldedBack(const FTrack& Track, const FVector2D& A, const FVector2D& B)
		{
			if (!Track.bAnalyticWalls)
			{
				return false;
			}
			FVector2D Nearest;
			double Offset = 0.0;
			const double Limit = Track.WallOffset - FoldedWallTolerance;
			return Track.NearestCentre(A, Nearest, Offset) < Limit || Track.NearestCentre(B, Nearest, Offset) < Limit;
		}

		/** Offset walls (Harbor): a polyline whose face normal is its right-hand normal times "side". */
		void AddOffsetWall(const FTrack& Track, const TSharedPtr<FJsonObject>& Wall, TArray<FWallSegment>& OutSweep)
		{
			const double Side = NumberField(Wall, TEXT("side"), 1);
			bool bClosed = false;
			Wall->TryGetBoolField(TEXT("closed"), bClosed);
			TArray<FVector2D> Line;
			FNumbers Numbers;
			for (const TSharedPtr<FJsonValue>& Entry : ArrayField(Wall, TEXT("polyline")))
			{
				if (NumbersOf(Entry, 2, Numbers))
				{
					Line.Add(FVector2D(Numbers[0], Numbers[1]));
				}
			}
			const int32 Count = bClosed ? Line.Num() : Line.Num() - 1;
			int32 OutOfRange = 0;
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector2D& End = Line[(i + 1) % Line.Num()];
				if (!IsTrackPoint(Line[i]) || !IsTrackPoint(End))
				{
					++OutOfRange;
					continue;
				}
				FWallSegment Segment;
				Segment.Start = Line[i];
				Segment.Delta = End - Segment.Start;
				Segment.LengthSq = Segment.Delta.SizeSquared();
				if (Segment.LengthSq <= 0.0 || IsFoldedBack(Track, Segment.Start, Segment.Start + Segment.Delta))
				{
					continue;
				}
				const FVector2D Right = FVector2D(-Segment.Delta.Y, Segment.Delta.X) / FMath::Sqrt(Segment.LengthSq);
				Segment.FaceNormal = Right * Side;
				OutSweep.Add(Segment);
			}
			WarnOutOfRange(OutOfRange);
		}

		/** The wall sets, in the web build's order: the left wall's segments before the right wall's. */
		void ParseWalls(const TSharedPtr<FJsonObject>& Collision, FTrack& Out)
		{
			TArray<TSharedPtr<FJsonObject>> Walls;
			for (const TSharedPtr<FJsonValue>& Value : ArrayField(Collision, TEXT("walls")))
			{
				if (const TSharedPtr<FJsonObject> Wall = AsObject(Value))
				{
					Walls.Add(Wall);
				}
			}
			Walls.StableSort([](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
			{
				return NumberField(A, TEXT("side")) < NumberField(B, TEXT("side"));
			});

			TArray<FWallSegment> SweepSegments;
			TArray<FWallSegment> ProbeSegments;
			for (const TSharedPtr<FJsonObject>& Wall : Walls)
			{
				const TArray<TSharedPtr<FJsonValue>>& MeshSegments = ArrayField(Wall, TEXT("segments"));
				if (MeshSegments.Num() > 0)
				{
					AddMeshWall(Wall, MeshSegments, SweepSegments, ProbeSegments);
					Out.bRadialWallPush = true;
				}
				else
				{
					AddOffsetWall(Out, Wall, SweepSegments);
				}
			}
			Out.Walls.Reset(MoveTemp(SweepSegments));
			if (Out.bRadialWallPush)
			{
				Out.ProbeWalls.Reset(MoveTemp(ProbeSegments));
			}
		}

		/** The dirt raster: run-length rows of [row, firstCol, lastCol]. A grid too large to be real is ignored. */
		void ParseDirtGrid(const TSharedPtr<FJsonObject>& Dirt, FDirtGrid& Grid)
		{
			const double Cols = NumberField(Dirt, TEXT("cols"));
			const double Rows = NumberField(Dirt, TEXT("rows"));
			if (!(Cols * Rows <= MaxDirtGridCells))
			{
				UE_LOG(LogKartTrack, Warning, TEXT("Ignoring a %.0f x %.0f dirt grid (limit %.0f cells)"), Cols, Rows, MaxDirtGridCells);
				return;
			}
			Grid.CellSize = NumberField(Dirt, TEXT("cellM"));
			Grid.Min = FVector2D(NumberField(Dirt, TEXT("minX")), NumberField(Dirt, TEXT("minZ")));
			Grid.Cols = Json::ToInt32(Cols, 0, 0, MAX_int32);
			Grid.Rows = Json::ToInt32(Rows, 0, 0, MAX_int32);
			if (!Grid.IsValid())
			{
				return;
			}
			Grid.Cells.Init(false, Grid.Cols * Grid.Rows);
			FNumbers Run;
			for (const TSharedPtr<FJsonValue>& Entry : ArrayField(Dirt, TEXT("runs")))
			{
				if (!NumbersOf(Entry, 3, Run))
				{
					continue;
				}
				const int32 Row = Json::ToInt32(Run[0], INDEX_NONE, INDEX_NONE, Grid.Rows);
				const int32 First = Json::ToInt32(Run[1], 0, 0, Grid.Cols);
				const int32 Last = Json::ToInt32(Run[2], INDEX_NONE, INDEX_NONE, Grid.Cols - 1);
				if (Row < 0 || Row >= Grid.Rows)
				{
					continue;
				}
				for (int32 Col = First; Col <= Last; ++Col)
				{
					Grid.Cells[Row * Grid.Cols + Col] = true;
				}
			}
		}

		/** Kart disc size, walls and the dirt grid. */
		void ParseCollision(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			const TSharedPtr<FJsonObject> Collision = ObjectField(Root, TEXT("collision"));
			Out.KartRadius = NumberField(Collision, TEXT("kartRadiusM"), Out.KartRadius);
			Out.SweepSubstep = NumberField(Collision, TEXT("substepM"), Out.SweepSubstep);
			ParseWalls(Collision, Out);
			if (const TSharedPtr<FJsonObject> Dirt = ObjectField(Collision, TEXT("dirtGrid")))
			{
				ParseDirtGrid(Dirt, Out.DirtGrid);
			}
		}

		/** Recovery tuning, item boxes, intro cameras, minimap bounds and the sun. */
		void ParsePresentation(const TSharedPtr<FJsonObject>& Root, FTrack& Out)
		{
			if (const TSharedPtr<FJsonObject> Recovery = ObjectField(Root, TEXT("recovery")))
			{
				Out.RecoveryDistance = NumberField(Recovery, TEXT("maxDistanceM"), Out.RecoveryDistance);
				Out.RecoverySkipWaypoints = Json::IntField(Recovery, TEXT("skipWaypoints"), Out.RecoverySkipWaypoints, 0, MaxRecoverySkipWaypoints);
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
			else if (!(FMath::IsFinite(Track.SweepSubstep) && Track.SweepSubstep >= MinSweepSubstep)
				|| !(Track.KartRadius > 0.0 && Track.KartRadius <= MaxKartRadius))
			{
				// The wall sweep divides by the substep (a zero or NaN never terminates), and its query box and the
				// cells it walks grow with the radius.
				OutError = FString::Printf(TEXT("substepM (%f) must be at least %g and kartRadiusM (%f) in (0, %g]"),
					Track.SweepSubstep, MinSweepSubstep, Track.KartRadius, MaxKartRadius);
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
		// Zero (or anything below, clamped to zero) is rejected by Validate.
		Out.Laps = Json::IntField(Header, TEXT("laps"), Out.Laps, 0, MaxLaps);
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

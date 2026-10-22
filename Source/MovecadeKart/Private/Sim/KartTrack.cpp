#include "Sim/KartTrack.h"

// The track's runtime queries: the wall grid, the dirt grid and the centreline. Loading is in KartTrackJson.cpp.

DEFINE_LOG_CATEGORY_STATIC(LogKartWalls, Log, All);

namespace MovecadeKart
{
	namespace
	{
		/** A ray this close to parallel with a segment never hits it. */
		constexpr double ParallelEpsilon = 1e-12;

		/** The wall grid's bounds. A real wall segment spans a few cells (Harbor's longest is 5 m, Violet's 3 m, at
		 *  8 m cells) and a track a few hundred thousand entries; past these the file is corrupt or hostile, and the
		 *  segment is left out of the grid rather than stalling the load or exhausting memory. */
		constexpr int32 MaxCellsPerSegment = 512;
		constexpr int32 MaxGridEntries = 1 << 20;
		/** A query box covering more cells than this (a 2 km square at 8 m cells) finds no walls, with a warning.
		 *  The sim's sweep box is 8 kart radii wide and its longest probe ray 999 m. */
		constexpr int64 MaxCellsPerQuery = 1 << 16;
		/** Beyond this the cell walk would overflow int32 (or take forever). The loader rejects past 1e5 m. */
		constexpr double MaxCoordinate = 1e9;
		/** Slack (in cells) on each column's row span, so a segment exactly on a cell edge is in both cells. */
		constexpr double CellEdgeSlack = 1e-6;

		bool IsInRange(const FVector2D& P)
		{
			return FMath::Abs(P.X) < MaxCoordinate && FMath::Abs(P.Y) < MaxCoordinate; // false for NaN
		}

		/** Where a ray (unit Dir) crosses a segment, as the distance along the ray; false if it misses within Far. */
		FORCEINLINE bool RayHitsSegment(const FVector2D& Origin, const FVector2D& Dir, double Far, const FWallSegment& Segment, double& OutRayT)
		{
			// Solve Origin + RayT * Dir = Start + SegmentU * Delta.
			const double Denom = Dir.X * Segment.Delta.Y - Dir.Y * Segment.Delta.X;
			if (FMath::Abs(Denom) < ParallelEpsilon)
			{
				return false;
			}
			const FVector2D AO = Segment.Start - Origin;
			OutRayT = (AO.X * Segment.Delta.Y - AO.Y * Segment.Delta.X) / Denom;
			const double SegmentU = (AO.X * Dir.Y - AO.Y * Dir.X) / Denom;
			return OutRayT >= 0.0 && OutRayT <= Far && SegmentU >= 0.0 && SegmentU <= 1.0;
		}
	}

	// ---- FWallSet -------------------------------------------------------------------------------

	FIntPoint FWallSet::CellOf(const FVector2D& P) const
	{
		return FIntPoint(FMath::FloorToInt32(P.X / CellSize), FMath::FloorToInt32(P.Y / CellSize));
	}

	bool FWallSet::CrossedCells(const FWallSegment& Segment, FCellList& Out) const
	{
		Out.Reset();
		const FVector2D A = Segment.Start;
		const FVector2D B = Segment.Start + Segment.Delta;
		if (!IsInRange(A) || !IsInRange(B))
		{
			return false;
		}
		// The bounding box's cells, as the grid used to take whole; each column keeps only the rows the segment
		// spans inside it. That is a superset of the cells the segment touches (the slack only adds edge cells), so
		// every query still sees every segment that reaches its box.
		const FIntPoint C0 = CellOf(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)));
		const FIntPoint C1 = CellOf(FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
		const FVector2D CellA(A.X / CellSize, A.Y / CellSize); // as CellOf divides
		const FVector2D CellB(B.X / CellSize, B.Y / CellSize);
		const double SpanX = CellB.X - CellA.X;
		for (int32 X = C0.X; X <= C1.X; ++X)
		{
			double Lo = FMath::Min(CellA.Y, CellB.Y);
			double Hi = FMath::Max(CellA.Y, CellB.Y);
			if (C0.X != C1.X && SpanX != 0.0)
			{
				const double T0 = FMath::Clamp((X - CellEdgeSlack - CellA.X) / SpanX, 0.0, 1.0);
				const double T1 = FMath::Clamp((X + 1 + CellEdgeSlack - CellA.X) / SpanX, 0.0, 1.0);
				const double Y0 = CellA.Y + (CellB.Y - CellA.Y) * T0;
				const double Y1 = CellA.Y + (CellB.Y - CellA.Y) * T1;
				Lo = FMath::Min(Y0, Y1);
				Hi = FMath::Max(Y0, Y1);
			}
			const int32 Row0 = FMath::Max(C0.Y, FMath::FloorToInt32(Lo - CellEdgeSlack));
			const int32 Row1 = FMath::Min(C1.Y, FMath::FloorToInt32(Hi + CellEdgeSlack));
			for (int32 Y = Row0; Y <= Row1; ++Y)
			{
				if (Out.Num() >= MaxCellsPerSegment)
				{
					Out.Reset();
					return false;
				}
				Out.Add(FIntPoint(X, Y));
			}
		}
		return true;
	}

	void FWallSet::Reset(TArray<FWallSegment>&& InSegments, double InCellSize)
	{
		WallSegments = MoveTemp(InSegments);
		CellSize = FMath::IsFinite(InCellSize) && InCellSize > 0.0 ? InCellSize : DefaultCellSize;
		Grid.Reset();
		FCellList Cells;
		int32 Entries = 0;
		int32 Skipped = 0;
		bool bFull = false;
		for (int32 i = 0; i < WallSegments.Num(); ++i)
		{
			// Once the budget is spent every later segment is left out, so what is filed doesn't depend on sizes.
			bFull = bFull || (CrossedCells(WallSegments[i], Cells) && Entries + Cells.Num() > MaxGridEntries);
			if (bFull || Cells.IsEmpty())
			{
				++Skipped;
				continue;
			}
			Entries += Cells.Num();
			for (const FIntPoint& Cell : Cells)
			{
				Grid.FindOrAdd(Cell).Add(i);
			}
		}
		if (Skipped > 0)
		{
			UE_LOG(LogKartWalls, Warning, TEXT("Left %d of %d wall segments out of the grid (out of range, longer than %d cells, or past %d entries)"),
				Skipped, WallSegments.Num(), MaxCellsPerSegment, MaxGridEntries);
		}
	}

	bool FWallSet::CellRange(const FVector2D& Min, const FVector2D& Max, FIntPoint& OutC0, FIntPoint& OutC1) const
	{
		// NaN or a position a million km away would overflow the int32 cell walk (or take forever): no walls.
		if (!ensureMsgf(IsInRange(Min) && IsInRange(Max), TEXT("Wall query out of range: (%f, %f)-(%f, %f)"), Min.X, Min.Y, Max.X, Max.Y))
		{
			return false;
		}
		OutC0 = CellOf(Min);
		OutC1 = CellOf(Max);
		const int64 Cells = static_cast<int64>(OutC1.X - OutC0.X + 1) * (OutC1.Y - OutC0.Y + 1);
		if (Cells > MaxCellsPerQuery)
		{
			static bool bWarned = false; // once per run: a caller asking this every step would flood the log
			UE_CLOG(!bWarned, LogKartWalls, Warning, TEXT("Wall query spans %lld cells (limit %lld); no walls returned"), Cells, MaxCellsPerQuery);
			bWarned = true;
			return false;
		}
		return true;
	}

	void FWallSet::Query(const FVector2D& Min, const FVector2D& Max, FCandidates& Out) const
	{
		Out.Reset();
		FIntPoint C0, C1;
		if (!CellRange(Min, Max, C0, C1))
		{
			return;
		}
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
		// Walks the cells in place instead of gathering, sorting and deduplicating candidates. The winner is the
		// smallest RayT with the lowest index among exact ties, which is what an ascending scan with a strict < picks;
		// a segment seen again from a neighbouring cell cannot change it.
		const FVector2D End = Origin + Dir * Far;
		double Best = TNumericLimits<double>::Max();
		int32 BestIndex = INDEX_NONE;
		FIntPoint C0, C1;
		if (CellRange(FVector2D(FMath::Min(Origin.X, End.X), FMath::Min(Origin.Y, End.Y)),
			FVector2D(FMath::Max(Origin.X, End.X), FMath::Max(Origin.Y, End.Y)), C0, C1))
		{
			for (int32 X = C0.X; X <= C1.X; ++X)
			{
				for (int32 Y = C0.Y; Y <= C1.Y; ++Y)
				{
					const TArray<int32>* Cell = Grid.Find(FIntPoint(X, Y));
					if (!Cell)
					{
						continue;
					}
					for (const int32 Index : *Cell)
					{
						double RayT = 0.0;
						if (RayHitsSegment(Origin, Dir, Far, WallSegments[Index], RayT)
							&& (RayT < Best || (RayT == Best && BestIndex != INDEX_NONE && Index < BestIndex)))
						{
							Best = RayT;
							BestIndex = Index;
						}
					}
				}
			}
		}
		if (BestIndex != INDEX_NONE)
		{
			OutNormal = WallSegments[BestIndex].FaceNormal;
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

	namespace
	{
		FCentreSegment MakeCentreSegment(const FVector2D& A, const FVector2D& B)
		{
			FCentreSegment Segment;
			Segment.Start = A;
			Segment.Delta = B - A;
			Segment.LengthSq = Segment.Delta.SizeSquared();
			return Segment;
		}

		/** The closest point Q of a centre segment to P and its squared distance: NearestCentre's per-segment maths. */
		FORCEINLINE double DistSqToSegment(const FVector2D& P, const FCentreSegment& Segment, FVector2D& OutQ)
		{
			const double Along = Segment.LengthSq > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - Segment.Start, Segment.Delta) / Segment.LengthSq, 0.0, 1.0) : 0.0;
			OutQ = Segment.Start + Segment.Delta * Along;
			return FVector2D::DistSquared(P, OutQ);
		}

		/** The track's precomputed segments, or (for a track built by hand, never loaded) ones built into Scratch. */
		TConstArrayView<FCentreSegment> CentreSegmentsOf(const FTrack& Track, TArray<FCentreSegment>& Scratch)
		{
			const int32 N = Track.Centre.Num();
			if (Track.CentreSegments.Num() == N)
			{
				return Track.CentreSegments;
			}
			Scratch.Reset(N);
			for (int32 i = 0; i < N; ++i)
			{
				Scratch.Add(MakeCentreSegment(Track.Centre[i], Track.Centre[(i + 1) % N]));
			}
			return Scratch;
		}
	}

	void FTrack::RebuildCentreSegments()
	{
		CentreSegments.Reset(); // so CentreSegmentsOf builds afresh
		TArray<FCentreSegment> Built;
		(void)CentreSegmentsOf(*this, Built);
		CentreSegments = MoveTemp(Built);
	}

	double FTrack::NearestCentre(const FVector2D& P, FVector2D& OutPoint, double& OutOffset) const
	{
		double BestSq = TNumericLimits<double>::Max();
		OutPoint = P;
		OutOffset = 0.0;
		TArray<FCentreSegment> Scratch;
		for (const FCentreSegment& Segment : CentreSegmentsOf(*this, Scratch))
		{
			FVector2D Q;
			const double DistSq = DistSqToSegment(P, Segment, Q);
			if (DistSq < BestSq)
			{
				BestSq = DistSq;
				OutPoint = Q;
				// Right of travel is (-dz, dx) on the three.js ground plane.
				const FVector2D Right = Segment.LengthSq > 0.0 ? FVector2D(-Segment.Delta.Y, Segment.Delta.X) / FMath::Sqrt(Segment.LengthSq) : FVector2D::ZeroVector;
				OutOffset = FVector2D::DotProduct(P - Q, Right);
			}
		}
		return FMath::Sqrt(BestSq);
	}

	bool FTrack::IsNearCentre(const FVector2D& P, double Limit, int32& InOutHint) const
	{
		// NearestCentre returns sqrt(min DistSq). sqrt is correctly rounded, hence monotonic, so one segment with
		// sqrt(DistSq) <= Limit proves the minimum is within Limit too, and the minimum's own segment is found
		// otherwise: the answer is exact, not a tolerance.
		TArray<FCentreSegment> Scratch;
		const TConstArrayView<FCentreSegment> Segments = CentreSegmentsOf(*this, Scratch);
		const int32 N = Segments.Num();
		const int32 From = InOutHint >= 0 && InOutHint < N ? InOutHint : 0;
		for (int32 k = 0; k < N; ++k)
		{
			// From, From + 1, From - 1, From + 2, ...: N consecutive offsets, so every segment once.
			const int32 Offset = (k & 1) ? (k + 1) / 2 : -(k / 2);
			const int32 i = ((From + Offset) % N + N) % N;
			FVector2D Q;
			if (FMath::Sqrt(DistSqToSegment(P, Segments[i], Q)) <= Limit)
			{
				InOutHint = i;
				return true;
			}
		}
		return false;
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
}

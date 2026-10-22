#include "Sim/KartTrack.h"

// The track's runtime queries: the wall grid, the dirt grid and the centreline. Loading is in KartTrackJson.cpp.

namespace MovecadeKart
{
	namespace
	{
		/** A ray this close to parallel with a segment never hits it. */
		constexpr double ParallelEpsilon = 1e-12;
	}

	// ---- FWallSet -------------------------------------------------------------------------------

	FIntPoint FWallSet::CellOf(const FVector2D& P) const
	{
		return FIntPoint(FMath::FloorToInt32(P.X / CellSize), FMath::FloorToInt32(P.Y / CellSize));
	}

	void FWallSet::Reset(TArray<FWallSegment>&& InSegments, double InCellSize)
	{
		WallSegments = MoveTemp(InSegments);
		CellSize = InCellSize;
		Grid.Reset();
		for (int32 i = 0; i < WallSegments.Num(); ++i)
		{
			const FWallSegment& Segment = WallSegments[i];
			const FVector2D B = Segment.A + Segment.D;
			const FIntPoint C0 = CellOf(FVector2D(FMath::Min(Segment.A.X, B.X), FMath::Min(Segment.A.Y, B.Y)));
			const FIntPoint C1 = CellOf(FVector2D(FMath::Max(Segment.A.X, B.X), FMath::Max(Segment.A.Y, B.Y)));
			for (int32 X = C0.X; X <= C1.X; ++X)
			{
				for (int32 Y = C0.Y; Y <= C1.Y; ++Y)
				{
					Grid.FindOrAdd(FIntPoint(X, Y)).Add(i);
				}
			}
		}
	}

	bool FWallSet::CellRange(const FVector2D& Min, const FVector2D& Max, FIntPoint& OutC0, FIntPoint& OutC1) const
	{
		// NaN or a position a million km away would overflow the int32 cell walk (or take forever): no walls.
		constexpr double MaxCoordinate = 1e9;
		if (!ensureMsgf(FMath::Abs(Min.X) < MaxCoordinate && FMath::Abs(Min.Y) < MaxCoordinate
			&& FMath::Abs(Max.X) < MaxCoordinate && FMath::Abs(Max.Y) < MaxCoordinate,
			TEXT("Wall query out of range: (%f, %f)-(%f, %f)"), Min.X, Min.Y, Max.X, Max.Y))
		{
			return false;
		}
		OutC0 = CellOf(Min);
		OutC1 = CellOf(Max);
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
		// smallest T with the lowest index among exact ties, which is what an ascending scan with a strict < picks;
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
						const FWallSegment& Segment = WallSegments[Index];
						// Solve Origin + RayT * Dir = A + SegmentU * D.
						const double Denom = Dir.X * Segment.D.Y - Dir.Y * Segment.D.X;
						if (FMath::Abs(Denom) < ParallelEpsilon)
						{
							continue;
						}
						const FVector2D AO = Segment.A - Origin;
						const double RayT = (AO.X * Segment.D.Y - AO.Y * Segment.D.X) / Denom;
						const double SegmentU = (AO.X * Dir.Y - AO.Y * Dir.X) / Denom;
						if (RayT >= 0.0 && RayT <= Far && SegmentU >= 0.0 && SegmentU <= 1.0
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
			FCentreSegment S;
			S.A = A;
			S.D = B - A;
			S.LengthSq = S.D.SizeSquared();
			return S;
		}

		/** The closest point Q of a centre segment to P and its squared distance: NearestCentre's per-segment maths. */
		FORCEINLINE double DistSqToSegment(const FVector2D& P, const FCentreSegment& S, FVector2D& OutQ)
		{
			const double T = S.LengthSq > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - S.A, S.D) / S.LengthSq, 0.0, 1.0) : 0.0;
			OutQ = S.A + S.D * T;
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
		for (const FCentreSegment& S : CentreSegmentsOf(*this, Scratch))
		{
			FVector2D Q;
			const double DistSq = DistSqToSegment(P, S, Q);
			if (DistSq < BestSq)
			{
				BestSq = DistSq;
				OutPoint = Q;
				// Right of travel is (-dz, dx) on the three.js ground plane.
				const FVector2D Right = S.LengthSq > 0.0 ? FVector2D(-S.D.Y, S.D.X) / FMath::Sqrt(S.LengthSq) : FVector2D::ZeroVector;
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

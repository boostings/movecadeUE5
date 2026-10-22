#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Race/KartBotDriver.h"
#include "Sim/KartTrack.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	bool LoadTrack(FAutomationTestBase& Test, const TCHAR* File, FTrack& Out)
	{
		FString Error;
		if (!FTrack::LoadFromJsonFile(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks"), File), Out, Error))
		{
			Test.AddError(FString::Printf(TEXT("%s failed to load: %s"), File, *Error));
			return false;
		}
		return true;
	}

	template <typename T>
	bool SameBits(const T& A, const T& B)
	{
		return FMemory::Memcmp(&A, &B, sizeof(T)) == 0;
	}

	// The queries as they were written before they were optimised, kept verbatim as the reference.

	double ReferenceNearestCentre(const FTrack& Track, const FVector2D& P, FVector2D& OutPoint, double& OutOffset)
	{
		double BestSq = TNumericLimits<double>::Max();
		OutPoint = P;
		OutOffset = 0.0;
		const int32 N = Track.Centre.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D A = Track.Centre[i];
			const FVector2D D = Track.Centre[(i + 1) % N] - A;
			const double LenSq = D.SizeSquared();
			const double T = LenSq > 0.0 ? FMath::Clamp(FVector2D::DotProduct(P - A, D) / LenSq, 0.0, 1.0) : 0.0;
			const FVector2D Q = A + D * T;
			const double DistSq = FVector2D::DistSquared(P, Q);
			if (DistSq < BestSq)
			{
				BestSq = DistSq;
				OutPoint = Q;
				const FVector2D Right = LenSq > 0.0 ? FVector2D(-D.Y, D.X) / FMath::Sqrt(LenSq) : FVector2D::ZeroVector;
				OutOffset = FVector2D::DotProduct(P - Q, Right);
			}
		}
		return FMath::Sqrt(BestSq);
	}

	bool ReferenceRaycast(const FWallSet& Walls, const FVector2D& Origin, const FVector2D& Dir, double Far, double& OutDistance, FVector2D& OutNormal)
	{
		const FVector2D End = Origin + Dir * Far;
		FWallSet::FCandidates Candidates;
		Walls.Query(FVector2D(FMath::Min(Origin.X, End.X), FMath::Min(Origin.Y, End.Y)),
			FVector2D(FMath::Max(Origin.X, End.X), FMath::Max(Origin.Y, End.Y)), Candidates);
		double Best = TNumericLimits<double>::Max();
		for (const int32 Index : Candidates)
		{
			const FWallSegment& S = Walls.Segments()[Index];
			const double Denom = Dir.X * S.Delta.Y - Dir.Y * S.Delta.X;
			if (FMath::Abs(Denom) < 1e-12)
			{
				continue;
			}
			const FVector2D AO = S.Start - Origin;
			const double T = (AO.X * S.Delta.Y - AO.Y * S.Delta.X) / Denom;
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

	FVector2D ReferenceLookAhead(const FVector2D& Position, double Yaw, const TArray<FVector2D>& Path, double Distance)
	{
		const FVector2D Facing(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
		const int32 N = Path.Num();
		double Best = TNumericLimits<double>::Max();
		int32 Segment = 0;
		double Along = 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D A = Path[i];
			const FVector2D D = Path[(i + 1) % N] - A;
			double Length = D.Size();
			if (Length == 0.0)
			{
				Length = 1.0;
			}
			const double T = FMath::Clamp(FVector2D::DotProduct(Position - A, D) / (Length * Length), 0.0, 1.0);
			const double Cost = FVector2D::Distance(A + D * T, Position) + FKartBotDriver::AlignmentCost * (1.0 - FVector2D::DotProduct(Facing, D) / Length);
			if (Cost < Best)
			{
				Best = Cost;
				Segment = i;
				Along = T;
			}
		}
		FVector2D P = Path[Segment] + (Path[(Segment + 1) % N] - Path[Segment]) * Along;
		double Remaining = Distance;
		for (int32 Step = 0; Step <= N; ++Step)
		{
			const FVector2D Next = Path[(Segment + 1 + Step) % N];
			const double Gap = FVector2D::Distance(Next, P);
			if (Gap >= Remaining)
			{
				return P + (Next - P) * (Remaining / Gap);
			}
			Remaining -= Gap;
			P = Next;
		}
		return P;
	}
}

// The sim's track queries were rewritten for speed (cached segments, an early-out, an in-place cell walk); each must
// give bit-for-bit what the original code gave, including on exact boundaries and ties.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartTrackQueryTest, "MovecadeKart.Sim.TrackQueriesExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartTrackQueryTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* File : { TEXT("sunset-harbor.track.json"), TEXT("violet-loop.track.json") })
	{
		FTrack Track;
		if (!LoadTrack(*this, File, Track))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s: one cached segment per centre sample"), File), Track.CentreSegments.Num(), Track.Centre.Num());

		FRandomStream Random(2024);
		int32 CentreMismatch = 0, NearMismatch = 0, RayMismatch = 0, BotMismatch = 0, Hits = 0, Outside = 0;
		int32 Hint = 0;
		for (int32 i = 0; i < 3000; ++i)
		{
			const FVector2D Base = Track.Centre[Random.RandRange(0, Track.Centre.Num() - 1)];
			const FVector2D P = Base + FVector2D(Random.FRandRange(-30.0, 30.0), Random.FRandRange(-30.0, 30.0));

			FVector2D RefPoint, Point;
			double RefOffset = 0.0, Offset = 0.0;
			const double RefDistance = ReferenceNearestCentre(Track, P, RefPoint, RefOffset);
			const double Distance = Track.NearestCentre(P, Point, Offset);
			CentreMismatch += !(SameBits(RefDistance, Distance) && SameBits(RefPoint, Point) && SameBits(RefOffset, Offset));

			// The exact boundary, one ulp either side, and the sweep's own limit, from a random starting hint.
			const double WallLimit = Track.WallOffset + Track.KartRadius;
			Outside += RefDistance > WallLimit;
			for (const double Limit : { RefDistance, std::nextafter(RefDistance, 0.0), std::nextafter(RefDistance, 1e300), WallLimit })
			{
				Hint = Random.RandRange(-5, Track.Centre.Num() + 5);
				NearMismatch += Track.IsNearCentre(P, Limit, Hint) != (RefDistance <= Limit);
			}

			const double Angle = Random.FRandRange(0.0, UE_DOUBLE_TWO_PI);
			const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
			for (const FWallSet* Walls : { static_cast<const FWallSet*>(&Track.Walls), &Track.RayWalls() })
			{
				for (const double Far : { 0.6, 2.5, 8.0, 80.0 })
				{
					double RefHit = -1.0, Hit = -1.0;
					FVector2D RefNormal(7.0, 7.0), Normal(7.0, 7.0);
					const bool bRef = ReferenceRaycast(*Walls, P, Dir, Far, RefHit, RefNormal);
					const bool bHit = Walls->Raycast(P, Dir, Far, Hit, Normal);
					Hits += bHit;
					RayMismatch += !(bRef == bHit && SameBits(RefHit, Hit) && SameBits(RefNormal, Normal));
				}
			}

			const double Yaw = Random.FRandRange(-UE_DOUBLE_PI, UE_DOUBLE_PI);
			const double Ahead = Random.FRandRange(0.0, 40.0);
			BotMismatch += !SameBits(ReferenceLookAhead(P, Yaw, Track.Waypoints, Ahead), FKartBotDriver::LookAhead(P, Yaw, Track.Waypoints, Ahead));
		}
		TestEqual(FString::Printf(TEXT("%s: NearestCentre matches the original bit for bit"), File), CentreMismatch, 0);
		TestEqual(FString::Printf(TEXT("%s: IsNearCentre is exactly NearestCentre <= Limit"), File), NearMismatch, 0);
		TestEqual(FString::Printf(TEXT("%s: Raycast matches the original bit for bit"), File), RayMismatch, 0);
		TestEqual(FString::Printf(TEXT("%s: LookAhead matches the original bit for bit"), File), BotMismatch, 0);
		TestTrue(FString::Printf(TEXT("%s: the samples include ray hits (%d)"), File, Hits), Hits > 1000);
		AddInfo(FString::Printf(TEXT("%s: %d of 3000 samples outside the sweep limit"), File, Outside));
	}
	return true;
}

#endif

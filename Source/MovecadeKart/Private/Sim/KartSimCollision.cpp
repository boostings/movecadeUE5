#include "Sim/KartSim.h"

#include "Sim/KartSimMath.h"
#include "Sim/KartSimTuning.h"

// Walls and kart contact, in Step's order: the cached radial push, the forward and side probes and their
// response, the advance, kart-to-kart contact, the swept disc constraint, and the collision feedback events
// (MultiplayerKart.tsx:611-675, :1280-1569; harborBoundary.ts; meshBoundary.ts).

namespace MovecadeKart
{
	using namespace KartTuning;
	using SimMath::NormalizeOrZero;

	namespace
	{
		/** The travel direction with its component into the wall removed, renormalised. */
		FVector2D SlideAlongWall(const FVector2D& MoveDir, const FVector2D& WallNormal)
		{
			return NormalizeOrZero(MoveDir - WallNormal * FVector2D::DotProduct(MoveDir, WallNormal));
		}

		/** Pushes the kart's disc at P out of one wall segment; false if they don't overlap. Old is where this substep
		 *  started, which picks the side for a segment-normal push. */
		bool PushDiscOutOfWall(const FWallSegment& Wall, bool bRadialPush, double Radius, const FVector2D& Old, FVector2D& P)
		{
			const double U = FMath::Clamp(FVector2D::DotProduct(P - Wall.Start, Wall.Delta) / Wall.LengthSq, 0.0, 1.0);
			const FVector2D Proj = Wall.Start + Wall.Delta * U;
			const FVector2D V = P - Proj;
			const double DistSq = V.SizeSquared();
			if (DistSq >= Radius * Radius)
			{
				return false;
			}
			if (bRadialPush)
			{
				// meshBoundary.ts pushOutOfMesh: away from the closest point, or along the face normal dead on it.
				const double Dist = FMath::Sqrt(DistSq);
				P += Dist > RadialPushEpsilon ? V * ((Radius - Dist) / Dist) : NormalizeOrZero(Wall.FaceNormal) * Radius;
				return true;
			}
			// harborBoundary.ts: out along the segment normal, on the side the substep came from.
			FVector2D Normal = FVector2D(-Wall.Delta.Y, Wall.Delta.X) / FMath::Sqrt(Wall.LengthSq);
			if (FVector2D::DotProduct(Old - Proj, Normal) < 0.0)
			{
				Normal = -Normal;
			}
			P += Normal * (Radius - FVector2D::DotProduct(V, Normal));
			return true;
		}
	}

	void FKartSim::ApplyRadialPush(const FStepMotion& Motion)
	{
		const uint64 Interval = Kart.NearestWall < CollisionSoftDistance ? FastCheckInterval : ContinuousCheckInterval;
		if (!Motion.bHopping && Kart.Frame - Kart.LastRadialFrame >= Interval)
		{
			ProbeRadial();
			Kart.LastRadialFrame = Kart.Frame;
		}
		// The cached radial push is re-applied every step until the next probe, as on the web.
		if (!Motion.bHopping && Kart.bRadialCritical)
		{
			Kart.Pos += Kart.RadialPush;
			Kart.Speed *= CriticalWallSpeedKeep;
		}
		else if (!Motion.bHopping && Kart.RadialStrength > 0.0)
		{
			Kart.Pos += Kart.RadialPush;
		}
	}

	void FKartSim::ProbeRadial()
	{
		FVector2D Push = FVector2D::ZeroVector;
		double Total = 0.0;
		double Nearest = NoHitDistance;
		int32 Critical = 0;
		for (int32 ProbeIndex = 0; ProbeIndex < NumRadialProbes; ++ProbeIndex)
		{
			// World-axis probes, not kart-relative (:611-652).
			const double Angle = (static_cast<double>(ProbeIndex) / NumRadialProbes) * UE_DOUBLE_TWO_PI;
			const FVector2D Dir(FMath::Sin(Angle), FMath::Cos(Angle));
			double Distance = 0.0;
			FVector2D Normal;
			if (!TrackData->RayWalls().Raycast(Kart.Pos, Dir, ContinuousProbeDistance, Distance, Normal))
			{
				continue;
			}
			Nearest = FMath::Min(Nearest, Distance);
			if (Distance < CollisionCriticalDistance)
			{
				++Critical;
				const double Magnitude = (CollisionCriticalDistance - Distance) * EmergencyPushStrength;
				Push -= Dir * Magnitude;
				Total += Magnitude;
			}
			else if (Distance < CollisionHardDistance)
			{
				const double Magnitude = (CollisionHardDistance - Distance) * CollisionPushStrength;
				Push -= Dir * Magnitude;
				Total += Magnitude * HardPushStrengthWeight;
			}
		}
		Kart.RadialPush = Push;
		Kart.RadialStrength = Total;
		Kart.bRadialCritical = Critical >= 2;
		Kart.NearestWall = Nearest;
	}

	FKartSim::FBarrier FKartSim::ProbeBarrier(const FVector2D& MoveDir)
	{
		FBarrier Barrier;
		auto Ray = [this](const FVector2D& Dir, double Far, double& OutDistance, FVector2D& OutNormal)
		{
			return TrackData->RayWalls().Raycast(Kart.Pos, NormalizeOrZero(Dir), Far, OutDistance, OutNormal);
		};

		double DForward = NoHitDistance, DLeft = NoHitDistance, DRight = NoHitDistance;
		double DForwardLeft = NoHitDistance, DForwardRight = NoHitDistance;
		FVector2D NForward, NUnused;
		const bool bForward = Ray(MoveDir, BarrierCollisionDistance, DForward, NForward);
		// Named as in the web build; on the three.js ground plane (-z, x) is actually the driver's right.
		const FVector2D LeftProbe = NormalizeOrZero(FVector2D(-MoveDir.Y, MoveDir.X));
		const FVector2D RightProbe = NormalizeOrZero(FVector2D(MoveDir.Y, -MoveDir.X));
		if (!Ray(LeftProbe, BarrierCollisionDistance * SideProbeReach, DLeft, NUnused)) { DLeft = NoHitDistance; }
		if (!Ray(RightProbe, BarrierCollisionDistance * SideProbeReach, DRight, NUnused)) { DRight = NoHitDistance; }
		if (!Ray(MoveDir + LeftProbe, BarrierCollisionDistance * DiagonalProbeReach, DForwardLeft, NUnused)) { DForwardLeft = NoHitDistance; }
		if (!Ray(MoveDir + RightProbe, BarrierCollisionDistance * DiagonalProbeReach, DForwardRight, NUnused)) { DForwardRight = NoHitDistance; }

		const double LeftMin = FMath::Min(DLeft, DForwardLeft);
		const double RightMin = FMath::Min(DRight, DForwardRight);
		if (LeftMin < CollisionSoftDistance || RightMin < CollisionSoftDistance)
		{
			const double LeftInfluence = LeftMin < CollisionSoftDistance ? (CollisionSoftDistance - LeftMin) / CollisionSoftDistance : 0.0;
			const double RightInfluence = RightMin < CollisionSoftDistance ? (CollisionSoftDistance - RightMin) / CollisionSoftDistance : 0.0;
			Barrier.Assist = (LeftInfluence - RightInfluence) * SteeringAssistStrength;
		}

		if (bForward && DForward < CollisionHardDistance)
		{
			Barrier.Penetration = FMath::Max(Barrier.Penetration, CollisionHardDistance - DForward);
		}
		if (LeftMin < CollisionHardDistance)
		{
			Barrier.Penetration = FMath::Max(Barrier.Penetration, (CollisionHardDistance - LeftMin) * SidePenetrationWeight);
		}
		if (RightMin < CollisionHardDistance)
		{
			Barrier.Penetration = FMath::Max(Barrier.Penetration, (CollisionHardDistance - RightMin) * SidePenetrationWeight);
		}

		Barrier.bBlocked = bForward;
		if (bForward)
		{
			Kart.LastCollisionNormal = NForward;
			Barrier.Distance = DForward;
			Barrier.Normal = Kart.LastCollisionNormal;
			Barrier.bHasNormal = true;
		}
		return Barrier;
	}

	void FKartSim::ApplyBarrierResponse(const FBarrier& Barrier, const FStepMotion& Motion, const FStepControls& Controls)
	{
		if (Motion.bHopping)
		{
			return;
		}
		if (Barrier.Assist != 0.0)
		{
			// Steer away from a wall the kart is closing on, less so if the player is steering into it.
			const double AssistMultiplier = FMath::Min(FMath::Abs(Kart.Speed) / MaxSpeed, 1.0);
			const double AssistSign = Motion.bReversing ? -1.0 : 1.0;
			const double Proximity = Kart.NearestWall < CollisionSoftDistance ? AssistNearWallBoost : 1.0;
			const bool bOpposing = (Controls.SteeringX > SteerDeadZone && Barrier.Assist > 0.0) || (Controls.SteeringX < -SteerDeadZone && Barrier.Assist < 0.0);
			Kart.Yaw += Barrier.Assist * AssistMultiplier * AssistSign * Proximity * (bOpposing ? AssistOpposedScale : 1.0);
		}
		if (Barrier.Penetration > 0.0 && Barrier.bHasNormal)
		{
			// Not scaled by dt, as on the web (spec 01, shipped bug 8). It runs once per fixed step, so it is
			// stable here, and the swept constraint still keeps the kart out of the wall.
			Kart.Pos += NormalizeOrZero(Barrier.Normal) * (Barrier.Penetration * CollisionPushStrength * WebFramesPerSecond);
		}
	}

	void FKartSim::Advance(const FBarrier& Barrier, const FStepMotion& Motion, double Dt)
	{
		const double AbsSpeed = FMath::Abs(Kart.Speed);
		if (Motion.bHopping)
		{
			AdvanceAirborne(Motion, Dt);
		}
		else if (Barrier.bBlocked && AbsSpeed > 0.0)
		{
			ApplyBlockedResponse(Barrier, Motion, AbsSpeed, Dt);
		}
		// The second clause can't be true when the first is false (Hard < Soft); kept as the web wrote it (spec 01 bug 10).
		else if (!Motion.bNearWall || Kart.NearestWall > CollisionSoftDistance)
		{
			Kart.Pos += Kart.Dir * Kart.Speed * Dt;
		}
		else
		{
			Kart.Pos += Kart.Dir * Kart.Speed * Dt * FMath::Min(1.0, Kart.NearestWall / CollisionSoftDistance);
		}
	}

	void FKartSim::AdvanceAirborne(const FStepMotion& Motion, double Dt)
	{
		// In the air: no wall response, but a hop straight into a wall loses its speed.
		double Distance = 0.0;
		FVector2D Normal;
		if (TrackData->RayWalls().Raycast(Kart.Pos, NormalizeOrZero(Motion.MoveDir), HopWallProbeDistance, Distance, Normal) && Distance < HopWallStopDistance)
		{
			Kart.Speed *= HopWallSpeedKeep;
		}
		else
		{
			Kart.Pos += Kart.Dir * Kart.Speed * Dt;
		}
	}

	void FKartSim::ApplyBlockedResponse(const FBarrier& Barrier, const FStepMotion& Motion, double AbsSpeed, double Dt)
	{
		const double Closeness = Barrier.Distance != 0.0 ? 1.0 - FMath::Min(Barrier.Distance / BarrierCollisionDistance, 1.0) : UnknownCloseness;
		const double LegacyMult = Motion.bNearWall
			? FMath::Max(NearWallMinSpeedMult, MinCollisionSpeedMult - Closeness * NearWallClosenessPenalty)
			: FMath::Max(MinCollisionSpeedMult, 1.0 - Closeness * ClosenessPenalty);
		const double Reverse = Motion.bReversing ? -1.0 : 1.0;
		if (bLegacyStickyWalls)
		{
			if (Barrier.bHasNormal && AbsSpeed > LegacyBounceMinSpeed)
			{
				Kart.Pos += Barrier.Normal * ((AbsSpeed / MaxSpeed) * BounceStrength);
			}
			Kart.Speed *= LegacyMult;
			if (Barrier.bHasNormal)
			{
				Kart.Pos += SlideAlongWall(Motion.MoveDir, Barrier.Normal) * (AbsSpeed * (Motion.bNearWall ? NearWallSlideRate : SlideRate) * Dt) * Reverse;
			}
			return;
		}
		// B11: scale the web's wall response by how square-on the kart meets the wall. Head-on still bites as hard;
		// a glancing hit keeps its speed and slides along the wall.
		const double HeadOn = Barrier.bHasNormal ? FMath::Abs(FVector2D::DotProduct(NormalizeOrZero(Motion.MoveDir), Barrier.Normal)) : 1.0;
		Kart.Speed *= 1.0 - (1.0 - LegacyMult) * HeadOn;
		if (Barrier.bHasNormal)
		{
			Kart.Pos += SlideAlongWall(Motion.MoveDir, Barrier.Normal) * (AbsSpeed * (1.0 - HeadOnSlideLoss * HeadOn) * Dt) * Reverse;
		}
	}

	void FKartSim::ApplyKartContact(const FKartRaceContext& Race, double Dt)
	{
		// Kart vs kart (:658-675, :1428-1434, kartContact.ts): the first overlapping kart pushes this one away.
		if (!Race.bKartContact)
		{
			return;
		}
		for (const FKartRaceContext::FOther& Other : Race.Others)
		{
			const FVector2D Delta = Kart.Pos - Other.Position;
			const double Distance = Delta.Size();
			if (Distance >= KartContactDistance)
			{
				continue;
			}
			const FVector2D Away = Distance > KartContactCoincident ? Delta / Distance : FVector2D(Race.SelfSeat < Other.Seat ? -1.0 : 1.0, 0.0);
			Kart.Pos += Away * FMath::Min(KartContactDistance - Distance, KartBumpForce * Dt * WebFramesPerSecond);
			Kart.Speed *= FMath::Pow(KartContactSpeedKeepPerFrame, Dt * WebFramesPerSecond);
			Kart.BumpEvent = FMath::Min(1.0, (FMath::Abs(Kart.Speed) / MaxSpeed) * BumpEventSpeedWeight + BumpEventFloor);
			Kart.ImpactNormal = Away;
			++Kart.BumpEventCount;
			break;
		}
	}

	bool FKartSim::ConstrainMovement(const FVector2D& Start, FVector2D& InOutDesired) const
	{
		const FVector2D Delta = InOutDesired - Start;
		const double SubstepCount = Delta.Size() / TrackData->SweepSubstep;
		// A kart moves a few metres a step; anything near this limit (or NaN) is a bug upstream, and sweeping it would
		// stall the game thread. Hold the kart where it was instead.
		if (!ensureMsgf(SubstepCount <= MaxSweepSubsteps, TEXT("Kart asked to move %f m in one step; holding it in place"), Delta.Size()))
		{
			InOutDesired = Start;
			return false;
		}
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(SubstepCount));
		const double Radius = TrackData->KartRadius;
		FVector2D P = Start;
		bool bCollided = PullInsideAnalyticWalls(Start, P);

		const double Margin = Radius * SweepQueryMarginRadii;
		FWallSet::FCandidates Candidates;
		for (int32 SubStep = 0; SubStep < Steps; ++SubStep)
		{
			const FVector2D Old = P;
			P += Delta / Steps;
			TrackData->Walls.Query(P - FVector2D(Margin), P + FVector2D(Margin), Candidates);
			for (const int32 Index : Candidates)
			{
				bCollided |= PushDiscOutOfWall(TrackData->Walls.Segments()[Index], TrackData->bRadialWallPush, Radius, Old, P);
			}
		}
		InOutDesired = P;
		return bCollided;
	}

	bool FKartSim::PullInsideAnalyticWalls(const FVector2D& Start, FVector2D& InOutP) const
	{
		const double Radius = TrackData->KartRadius;
		// Almost every step starts inside the walls, which IsNearCentre settles in a few segments; only a start
		// outside them needs the full nearest-point scan.
		if (!TrackData->bAnalyticWalls || TrackData->IsNearCentre(Start, TrackData->WallOffset + Radius, CentreHint))
		{
			return false;
		}
		FVector2D Nearest;
		double Offset = 0.0;
		const double Distance = TrackData->NearestCentre(Start, Nearest, Offset);
		if (!(Distance > TrackData->WallOffset + Radius))
		{
			return false;
		}
		const double Limit = TrackData->WallOffset - Radius;
		InOutP = Nearest + (InOutP - Nearest) * (Limit / Distance);
		return true;
	}

	void FKartSim::UpdateCollisionFeedback(const FBarrier& Barrier, const FStepMotion& Motion)
	{
		// Collision feedback (:1457-1569), for haptics, audio and FX: first contact fires at once, sustained
		// contact every 150 ms.
		const bool bWasColliding = Kart.bCollidingBarrier;
		Kart.bCollidingBarrier = !Motion.bHopping
			&& (Barrier.bBlocked || Barrier.Penetration > 0.0 || Kart.bRadialCritical || Kart.RadialStrength > 0.0 || Motion.bNearWall);
		Kart.CollisionIntensity = Kart.bCollidingBarrier
			? FMath::Min(1.0, Barrier.Penetration * IntensityPenetrationWeight + (FMath::Abs(Kart.Speed) / MaxSpeed) * IntensitySpeedWeight
				+ (Kart.bRadialCritical ? IntensityCriticalBonus : 0.0) + (Motion.bNearWall ? IntensityNearWallBonus : 0.0))
			: 0.0;
		if (Kart.bCollidingBarrier && !bWasColliding)
		{
			Kart.BarrierEvent = FMath::Max(FirstContactEventFloor, Kart.CollisionIntensity);
			Kart.LastBarrierEventTime = Kart.Time;
		}
		else if (Kart.bCollidingBarrier && Kart.Time - Kart.LastBarrierEventTime >= SustainedEventInterval)
		{
			Kart.BarrierEvent = FMath::Max(SustainedEventFloor, Kart.CollisionIntensity * SustainedEventScale);
			Kart.bBarrierEventSustained = true;
			Kart.LastBarrierEventTime = Kart.Time;
		}
		if (Kart.BarrierEvent > 0.0)
		{
			++Kart.BarrierEventCount;
			// Away from the wall, toward the kart: the probe's face normal turned against the travel (a face normal
			// may point either way), else the radial push, which already points off the wall.
			FVector2D Away = Barrier.bHasNormal ? Barrier.Normal : NormalizeOrZero(Kart.RadialPush);
			if (Barrier.bHasNormal && FVector2D::DotProduct(Away, Kart.Dir) > 0.0)
			{
				Away = -Away;
			}
			Kart.ImpactNormal = Away;
		}
	}
}

#include "Sim/KartSim.h"

#include "Sim/KartSimMath.h"
#include "Sim/KartSimTuning.h"

// The step order and the driving phases: speed, steering, hop and drift, boost pads, wrong way, terrain,
// checkpoints and the visual layers. Walls are in KartSimCollision.cpp, respawns in KartSimRecovery.cpp.

namespace MovecadeKart
{
	using namespace KartTuning;
	using SimMath::Damp;
	using SimMath::Forward;
	using SimMath::PowerTwoOut;

	void FKartSim::Init(const FTrack* InTrack, int32 InGridSlot)
	{
		// FTrack::LoadFromJsonString validates these; a hand-built track must too.
		check(InTrack && InTrack->Grid.Num() > 0 && InTrack->Checkpoints.IsValidIndex(InTrack->StartCheckpoint));
		TrackData = InTrack;
		GridIndex = FMath::Clamp(InGridSlot, 0, TrackData->Grid.Num() - 1);
		Kart = FKartState();
		Kart.Pos = TrackData->Grid[GridIndex].Position;
		Kart.Yaw = TrackData->Grid[GridIndex].Yaw;
		Kart.Dir = Forward(Kart.Yaw);
		Kart.NextCheckpoint = TrackData->StartCheckpoint;
		Kart.PadLastBoostTime.Init(-1000.0, TrackData->BoostPads.Num());
		Kart.AnchorPos = Kart.Pos;
	}

	void FKartSim::Teleport(const FVector2D& Position, double Yaw)
	{
		Respawn(Position, Yaw, TEXT("teleport"));
	}

	void FKartSim::ApplyBoost(double Multiplier, double Seconds, const FString& Label)
	{
		Kart.BoostMultiplier = Multiplier;
		Kart.BoostTimeRemaining = Seconds;
		Kart.ActiveBoost = Label;
	}

	void FKartSim::Step(const FKartInput& Input)
	{
		Step(Input, FKartRaceContext());
	}

	void FKartSim::Step(const FKartInput& Input, const FKartRaceContext& Race)
	{
		check(TrackData);
		const double Dt = StepSeconds;
		BeginStep(Dt);
		UpdateOffTrack();

		const FStepControls Controls = DecodeControls(Input);
		UpdateWatchdogs(Controls.bForward && !Controls.bBackward);
		UpdateSpeed(Controls, Race, Dt);
		UpdateYaw(Controls);
		UpdateHopAndDrift(Controls, Dt);

		const FVector2D Start = Kart.Pos;
		UpdateHeading(Dt);

		FStepMotion Motion;
		Motion.bReversing = Kart.Speed < 0.0;
		Motion.MoveDir = Motion.bReversing ? -Kart.Dir : Kart.Dir;
		Motion.bHopping = Kart.HopTime >= 0.0 || Kart.HopOffset > HopAirborneOffset;

		// Collision steps 1-5 (:1280-1425), then kart contact and the swept constraint.
		++Kart.Frame;
		ApplyRadialPush(Motion);
		const FBarrier Barrier = Motion.bHopping ? FBarrier() : ProbeBarrier(Motion.MoveDir);
		ApplyBarrierResponse(Barrier, Motion, Controls);
		Motion.bNearWall = !Motion.bHopping && Kart.NearestWall < CollisionHardDistance;
		Advance(Barrier, Motion, Dt);
		ApplyKartContact(Race, Dt);
		if (ConstrainMovement(Start, Kart.Pos))
		{
			// Swept wall constraint over the whole step (:1440-1445).
			Kart.Speed *= FMath::Exp(-WallSpeedDecay * Dt);
			Kart.bHitWallThisStep = true;
		}
		UpdateCollisionFeedback(Barrier, Motion);

		UpdateBoostPads();
		UpdateWrongWayAndTerrain();
		UpdateCheckpoints();
		UpdatePresentation(Dt);
	}

	FKartSim::FStepControls FKartSim::DecodeControls(const FKartInput& Input)
	{
		FStepControls Controls;
		Controls.SteeringX = FMath::Clamp(Input.SteeringX, -1.0, 1.0);
		Controls.Keys = (Input.bLeft ? 1.0 : 0.0) - (Input.bRight ? 1.0 : 0.0);
		Controls.bLeft = Input.bLeft;
		Controls.bRight = Input.bRight;
		Controls.bForward = Input.bForward;
		Controls.bBackward = Input.bBackward;
		Controls.bJump = Input.bJump;
		return Controls;
	}

	void FKartSim::BeginStep(double Dt)
	{
		Kart.Time += Dt;
		// One-step pulses.
		Kart.bHitWallThisStep = false;
		Kart.BarrierEvent = 0.0;
		Kart.bBarrierEventSustained = false;
		Kart.BumpEvent = 0.0;
		if (Kart.HopTime >= 0.0)
		{
			Kart.HopTime += Dt;
			if (Kart.HopTime >= HopDuration)
			{
				Kart.HopTime = -1.0;
			}
		}
	}

	void FKartSim::UpdateSpeed(const FStepControls& Controls, const FKartRaceContext& Race, double Dt)
	{
		// Boost timer (:1092-1106). This step still uses the multiplier it started with.
		const double Multiplier = Kart.BoostMultiplier;
		if (Kart.BoostTimeRemaining > 0.0)
		{
			Kart.BoostTimeRemaining -= Dt;
			if (Kart.BoostTimeRemaining <= 0.0)
			{
				Kart.BoostTimeRemaining = 0.0;
				Kart.BoostMultiplier = 1.0;
				Kart.ActiveBoost.Reset();
			}
		}

		// Rubber band (:1112-1129): at most +2 SU for the last kart, scaled down in small lobbies.
		Kart.RubberBandBoost = 0.0;
		if (Race.PlayerCount > 1 && Race.bRacing)
		{
			const double PositionRatio = static_cast<double>(Race.RacePosition - 1) / (Race.PlayerCount - 1);
			const double LobbyScale = Race.PlayerCount <= 2 ? RubberBandTwoPlayerScale
				: Race.PlayerCount == 3 ? RubberBandThreePlayerScale : RubberBandFullScale;
			Kart.RubberBandBoost = MaxSpeed * PositionRatio * Race.LeaderGapRatio * RubberBandScale * LobbyScale;
		}
		Kart.SlipstreamBoost = UpdateSlipstream(Race, Dt);

		// Speed (:1108-1181). B1: a multiplier below 1 slows the kart instead of being ignored.
		const double PowerupBoost = Multiplier > 1.0 ? MaxSpeed * (Multiplier - 1.0) : 0.0;
		const double Slowdown = Multiplier < 1.0 ? Multiplier : 1.0;
		const double Terrain = Kart.bOnDirt ? DirtMultiplier : 1.0;
		const double Max = (MaxSpeed + (Kart.Turbo > 0.0 ? TurboBonus : 0.0) + PowerupBoost + Kart.RubberBandBoost + Kart.SlipstreamBoost) * Terrain * Slowdown;
		const double Target = Controls.bBackward ? ReverseSpeed : Controls.bForward ? Max : 0.0;
		const double Lambda = Controls.bBackward ? LambdaBrake : Controls.bForward ? LambdaThrottle : LambdaCoast;
		Kart.Speed = Damp(Kart.Speed, Target, Lambda, Dt);
		if (Kart.Speed < DriftMinSpeed)
		{
			Kart.DriftDirection = 0.0;
			Kart.DriftPower = 0.0;
		}
		Kart.Turbo -= Dt;
	}

	void FKartSim::UpdateYaw(const FStepControls& Controls)
	{
		// A direct per-step turn (:1188-1199).
		Kart.InputTurn = (Controls.SteeringX + Controls.Keys + Kart.DriftDirection) * TurnPerStep;
		// B11: a kart pressed against a wall on the gas can still turn away (the web left it wedged).
		const bool bWedged = !bLegacyStickyWalls && Kart.bCollidingBarrier && Controls.bForward && !Controls.bBackward;
		const double TurnSpeed = bWedged ? FMath::Max(Kart.Speed, WedgedMinTurnSpeed) : Kart.Speed;
		Kart.Yaw += Kart.InputTurn * FMath::Min(TurnSpeed, TurnSpeedReference) / TurnSpeedReference;
	}

	void FKartSim::UpdateHopAndDrift(const FStepControls& Controls, double Dt)
	{
		// Hop and drift (:1202-1244): pressing jump hops and picks the drift side; releasing it cashes the drift
		// in for a mini-turbo.
		if (Controls.bJump && !Kart.bJumpHeld && Kart.HopTime < 0.0)
		{
			Kart.HopTime = 0.0;
			Kart.bJumpHeld = true;
			Kart.DriftDirection = (Controls.bLeft || Controls.SteeringX > SteerDeadZone) ? DriftTurn
				: (Controls.bRight || Controls.SteeringX < -SteerDeadZone) ? -DriftTurn : 0.0;
		}
		if (!Controls.bJump)
		{
			Kart.bJumpHeld = false;
			if (Kart.Turbo <= 0.0)
			{
				FDriftBoost Boost;
				// The web grades the drift by its held time in milliseconds.
				if (DriftExitBoost(Kart.DriftPower, Kart.DriftPower * 1000.0, Controls.SteeringX + Controls.Keys, Kart.DriftDirection, Boost))
				{
					Kart.Turbo = Boost.Time;
					Kart.BoostMultiplier = Boost.Multiplier;
					Kart.BoostTimeRemaining = Boost.Time;
					Kart.ActiveBoost = Boost.Label;
					Kart.LastDriftBoost = Boost.Label;
					++Kart.DriftBoostCount;
				}
				// The web falls back to a boostPower value shared by every kart here (B3). Not ported.
			}
			Kart.DriftDirection = 0.0;
			Kart.DriftPower = 0.0;
		}
		if (Kart.DriftDirection != 0.0)
		{
			Kart.DriftPower = FMath::Min(Kart.DriftPower + Dt, DriftPowerCap);
		}
	}

	void FKartSim::UpdateHeading(double Dt)
	{
		// Heading lag (:1249-1263): an unclamped, unnormalised lerp toward the nose. The angle between the two
		// drives the visual slip layer.
		const FVector2D Desired = Forward(Kart.Yaw);
		Kart.Dir = Kart.Dir + (Desired - Kart.Dir) * (DirLerp * Dt);
		const double SlipAngle = FMath::Atan2(Desired.X * Kart.Dir.Y - Desired.Y * Kart.Dir.X, Desired.X * Kart.Dir.X + Desired.Y * Kart.Dir.Y);
		Kart.SlipYaw = Damp(Kart.SlipYaw, SlipAngle * SlipYawFromSlipAngle + Kart.DriftDirection * SlipYawFromDrift, SlipYawLambda, Dt);
	}

	void FKartSim::UpdateBoostPads()
	{
		// BoostPads.jsx:208-230, games/kart/boostPad.ts isInsideBoostPad.
		for (int32 PadIndex = 0; PadIndex < TrackData->BoostPads.Num(); ++PadIndex)
		{
			if (Kart.Time - Kart.PadLastBoostTime[PadIndex] <= TrackData->BoostCooldown)
			{
				continue;
			}
			const FBoostPad& Pad = TrackData->BoostPads[PadIndex];
			const double Dx = Kart.Pos.X - Pad.Position.X;
			const double Dz = Kart.Pos.Y - Pad.Position.Y;
			const double Across = Dx * Pad.CosYaw - Dz * Pad.SinYaw;
			const double Along = Dx * Pad.SinYaw + Dz * Pad.CosYaw;
			if (FMath::Abs(Across) <= Pad.Width / 2.0 + TrackData->BoostPadding && FMath::Abs(Along) <= Pad.Length / 2.0 + TrackData->BoostPadding)
			{
				Kart.BoostMultiplier = TrackData->BoostMultiplier;
				Kart.BoostTimeRemaining = TrackData->BoostDuration;
				Kart.ActiveBoost = TEXT("BOOST_PAD");
				Kart.PadLastBoostTime[PadIndex] = Kart.Time;
			}
		}
	}

	void FKartSim::UpdateWrongWayAndTerrain()
	{
		// Wrong way, every 15 steps above 5 SU (:1571-1586).
		if (Kart.Frame - Kart.LastWrongWayFrame >= WrongWayInterval && FMath::Abs(Kart.Speed) > WrongWayMinSpeed && TrackData->Waypoints.Num() > 1)
		{
			Kart.LastWrongWayFrame = Kart.Frame;
			const int32 Nearest = TrackData->NearestWaypoint(Kart.Pos);
			const FVector2D ToNext = TrackData->Waypoints[(Nearest + 1) % TrackData->Waypoints.Num()] - Kart.Pos;
			const double Distance = ToNext.Size();
			if (Distance > WrongWayMinLookahead)
			{
				Kart.bWrongWay = FVector2D::DotProduct(Forward(Kart.Yaw), ToNext) / Distance < WrongWayDot;
			}
		}

		// Terrain, every 10 steps (:1588-1610).
		if (Kart.Frame - Kart.LastTerrainFrame >= TerrainInterval)
		{
			Kart.LastTerrainFrame = Kart.Frame;
			Kart.bOnDirt = IsOnDirt(Kart.Pos);
		}
	}

	bool FKartSim::IsOnDirt(const FVector2D& Position) const
	{
		if (TrackData->DirtGrid.IsValid())
		{
			return TrackData->DirtGrid.IsDirt(Position);
		}
		if (TrackData->GroundBands.Num() == 0)
		{
			return false;
		}
		FVector2D Nearest;
		double Offset = 0.0;
		TrackData->NearestCentre(Position, Nearest, Offset);
		// The down ray hits the highest ribbon under the kart first.
		double BestY = -TNumericLimits<double>::Max();
		bool bDirt = false;
		for (const FGroundBand& Band : TrackData->GroundBands)
		{
			if (Offset >= Band.Inner && Offset <= Band.Outer && Band.Y > BestY)
			{
				BestY = Band.Y;
				bDirt = Band.bDirt;
			}
		}
		return bDirt;
	}

	void FKartSim::UpdateCheckpoints()
	{
		// useCheckpointDetection.ts advanceSequentialCheckpointState.
		const int32 CheckpointCount = TrackData->Checkpoints.Num();
		const FCheckpoint& Next = TrackData->Checkpoints[Kart.NextCheckpoint];
		const bool bInside = FVector2D::Distance(Kart.Pos, Next.Position) < Next.Radius;
		const bool bCrossed = bInside && !Kart.bWasInsideNext;
		if (bCrossed)
		{
			if (Next.bFinish)
			{
				++Kart.LapsCompleted;
				Kart.NextCheckpoint = TrackData->StartCheckpoint;
				if (!Kart.bFinished && Kart.LapsCompleted >= TrackData->Laps)
				{
					Kart.bFinished = true;
					Kart.FinishTime = Kart.Time;
				}
			}
			else
			{
				Kart.NextCheckpoint = (Kart.NextCheckpoint + 1) % CheckpointCount;
			}
		}
		const FCheckpoint& After = TrackData->Checkpoints[Kart.NextCheckpoint];
		Kart.bWasInsideNext = bCrossed ? FVector2D::Distance(Kart.Pos, After.Position) < After.Radius : bInside;

		const int32 Previous = Kart.NextCheckpoint == 0 ? CheckpointCount - 1 : Kart.NextCheckpoint - 1;
		const double SegmentLength = FMath::Max(1.0, FVector2D::Distance(TrackData->Checkpoints[Previous].Position, After.Position));
		const double SegmentProgress = 1.0 - FMath::Min(FVector2D::Distance(Kart.Pos, After.Position) / SegmentLength, 1.0);
		Kart.ProgressPercent = FMath::Min(MaxProgressPercent, ((Previous + FMath::Max(0.0, SegmentProgress)) / CheckpointCount) * 100.0);
	}

	void FKartSim::UpdatePresentation(double Dt)
	{
		// Kart.jsx:194-242 steering and body layers.
		Kart.SteerVisual = Damp(Kart.SteerVisual, Kart.InputTurn, SteerVisualLambda, Dt);
		const double SpeedFactor = FMath::Min(1.0, Kart.Speed / BodyFullLeanSpeed);
		Kart.BodyYaw = Damp(Kart.BodyYaw, Kart.SteerVisual * BodyYawGain * SpeedFactor, BodyYawLambda, Dt);
		Kart.GroupYaw = Damp(Kart.GroupYaw, Kart.DriftDirection * GroupYawDriftGain + Kart.BodyYaw, GroupYawLambda, Dt);
		Kart.Lean = Damp(Kart.Lean, -Kart.SteerVisual * LeanGain * SpeedFactor, LeanLambda, Dt);

		// Hop: 0 -> 0.3 m in 0.125 s (power2.out), then back (gsap yoyo).
		const double Half = HopDuration / 2.0;
		if (Kart.HopTime < 0.0)
		{
			Kart.HopOffset = 0.0;
		}
		else if (Kart.HopTime < Half)
		{
			Kart.HopOffset = HopHeight * PowerTwoOut(Kart.HopTime / Half);
		}
		else
		{
			Kart.HopOffset = HopHeight * PowerTwoOut((HopDuration - Kart.HopTime) / Half);
		}
	}
}

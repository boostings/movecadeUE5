#include "Sim/KartSim.h"

namespace MovecadeKart
{
	namespace
	{
		// legacy/Experience/kart/engine/constants.ts:2-20
		constexpr double MaxSpeed = 40.0;
		constexpr double ReverseSpeed = -15.0;
		constexpr double LambdaThrottle = 1.8;
		constexpr double LambdaBrake = 6.0;
		constexpr double LambdaCoast = 1.1;
		constexpr double TurboBonus = 40.0;
		constexpr double DirtMultiplier = 0.75;
		constexpr double DriftMinSpeed = 20.0;
		constexpr double DriftTurn = 1.4;
		constexpr double DriftPowerCap = 10.0;
		// MultiplayerKart.tsx
		constexpr double TurnPerStep = 0.018;
		constexpr double TurnSpeedReference = 40.0;
		constexpr double DirLerp = 12.0;
		constexpr double HopDuration = 0.25;
		constexpr double HopHeight = 0.3;
		constexpr double WallSpeedDecay = 8.0;
		// MultiplayerKart.tsx:88-109
		constexpr double BarrierCollisionDistance = 2.5;
		constexpr double CollisionPushStrength = 0.8;
		constexpr double CollisionSoftDistance = 0.85;
		constexpr double CollisionHardDistance = 0.7;
		constexpr double CollisionCriticalDistance = 0.4;
		constexpr double SteeringAssistStrength = 0.01;
		constexpr double MinCollisionSpeedMult = 0.3;
		constexpr double BounceStrength = 1.2;
		constexpr double ContinuousProbeDistance = 1.5;
		constexpr int32 NumRadialProbes = 4;
		constexpr double EmergencyPushStrength = 1.5;
		constexpr double KartContactDistance = 1.8;
		constexpr double KartBumpForce = 0.5;
		constexpr uint64 ContinuousCheckInterval = 6;
		constexpr uint64 FastCheckInterval = 3;
		constexpr uint64 WrongWayInterval = 15;
		constexpr uint64 TerrainInterval = 10;
		// trackRecovery.ts, recoveryWatchdogs.ts
		constexpr double OffTrackRespawnSeconds = 2.0;
		constexpr double StuckAnchorDistance = 4.0;
		constexpr double StuckAnchorSeconds = 2.5;
		constexpr double CheckpointStallSeconds = 8.0;
		constexpr double WatchGapSeconds = 1.0;

		/** three.js MathUtils.damp. */
		double Damp(double X, double Y, double Lambda, double Dt)
		{
			return X + (Y - X) * (1.0 - FMath::Exp(-Lambda * Dt));
		}

		FVector2D Forward(double Yaw)
		{
			return FVector2D(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
		}

		/** three.js Vector3.normalize(): a zero vector stays zero. */
		FVector2D Normalize(const FVector2D& V)
		{
			const double Len = V.Size();
			return Len > 0.0 ? V / Len : FVector2D::ZeroVector;
		}

		double PowerTwoOut(double X)
		{
			X = FMath::Clamp(X, 0.0, 1.0);
			return 1.0 - (1.0 - X) * (1.0 - X);
		}
	}

	bool DriftExitBoost(double DriftPower, double DriftDurationMs, double SteeringInput, double DriftDirection, FDriftBoost& Out)
	{
		if (!FMath::IsFinite(DriftPower) || !FMath::IsFinite(DriftDurationMs) || DriftPower < 0.35 || DriftDurationMs < 650.0)
		{
			return false;
		}

		enum class EGrade { Mini, Super, Ultra };
		EGrade Grade = EGrade::Mini;
		Out = { 1.18, 0.85, TEXT("DRIFT_MINI") };
		if (DriftPower >= 0.9 && DriftDurationMs >= 1800.0)
		{
			Grade = EGrade::Ultra;
			Out = { 1.55, 1.6, TEXT("DRIFT_ULTRA") };
		}
		else if (DriftPower >= 0.65 && DriftDurationMs >= 1150.0)
		{
			Grade = EGrade::Super;
			Out = { 1.35, 1.25, TEXT("DRIFT_SUPER") };
		}

		if (!FMath::IsFinite(SteeringInput) || !FMath::IsFinite(DriftDirection) || FMath::Abs(DriftDirection) < 0.01)
		{
			return true;
		}

		const double Alignment = FMath::Sign(SteeringInput) * FMath::Sign(DriftDirection);
		if (Alignment < 0.0)
		{
			// Counter-steered at release: drop a grade.
			if (Grade == EGrade::Ultra)
			{
				Out = { 1.35, 1.25, TEXT("DRIFT_SLOPPY_SUPER") };
			}
			else if (Grade == EGrade::Super)
			{
				Out = { 1.18, 0.85, TEXT("DRIFT_SLOPPY_MINI") };
			}
			return true;
		}

		const double Magnitude = FMath::Abs(SteeringInput);
		if (Alignment > 0.0 && Grade == EGrade::Ultra && DriftPower >= 0.96 && DriftDurationMs >= 2200.0 && Magnitude >= 0.62 && Magnitude <= 0.9)
		{
			Out = { 1.7, 1.88, TEXT("DRIFT_APEX_ULTRA") };
			return true;
		}
		if (Alignment > 0.0 && Grade != EGrade::Mini)
		{
			Out.Multiplier = FMath::RoundToDouble((Out.Multiplier + 0.07) * 100.0) / 100.0;
			Out.Time = FMath::RoundToDouble((Out.Time + 0.13) * 100.0) / 100.0;
			Out.Label = Grade == EGrade::Ultra ? TEXT("DRIFT_CLEAN_ULTRA") : TEXT("DRIFT_CLEAN_SUPER");
		}
		return true;
	}

	bool RocketStartBoost(double HeldMs, FDriftBoost& Out)
	{
		if (!FMath::IsFinite(HeldMs) || HeldMs < 500.0)
		{
			return false;
		}
		if (HeldMs > 2000.0)
		{
			Out = { 0.82, 0.75, TEXT("START_BOGGED") };
		}
		else if (HeldMs >= 800.0 && HeldMs <= 1200.0)
		{
			Out = { 1.6, 1.75, TEXT("START_PERFECT") };
		}
		else if (HeldMs < 800.0)
		{
			Out = { 1.35, 1.25, TEXT("START_CLEAN") };
		}
		else
		{
			Out = { 1.25, 1.1, TEXT("START_LATE") };
		}
		return true;
	}

	void FKartSim::ApplyBoost(double Multiplier, double Seconds, const FString& Label)
	{
		S.BoostMultiplier = Multiplier;
		S.BoostTimeRemaining = Seconds;
		S.ActiveBoost = Label;
	}

	double FKartSim::UpdateSlipstream(const FKartRaceContext& Race, double Dt)
	{
		// performanceQuality.ts:867-983, with speed thresholds and boosts scaled to the 40 SU range (B4).
		constexpr double K = SlipstreamScale;
		double Boost = 0.0;
		double Rate = 0.0;
		bool bActive = false;
		if (Race.bRacing && Race.bHasAhead)
		{
			const FVector2D ToRival = Race.AheadPosition - S.Pos;
			const double Distance = ToRival.Size();
			const double AlignDot = Distance > 0.0 ? FVector2D::DotProduct(ToRival / Distance, Forward(S.Yaw)) : 0.0;
			const double Speed = S.Speed;
			const bool bBump = Distance < 6.0 && AlignDot >= 0.96 && Speed >= 105.0 * K;
			bActive = !(Distance < 4.5 || Distance > 28.0 || AlignDot < 0.72 || Speed < 70.0 * K) && !(Distance < 6.0 && !bBump);
			if (bActive)
			{
				const double DScore = FMath::Clamp((28.0 - Distance) / 22.0, 0.0, 1.0);
				const double AScore = FMath::Clamp((AlignDot - 0.72) / 0.28, 0.0, 1.0);
				const double SScore = FMath::Clamp((Speed - 70.0 * K) / (70.0 * K), 0.0, 1.0);
				const bool bPocket = !bBump && Distance <= 8.5 && AlignDot >= 0.97 && Speed >= 130.0 * K;
				const bool bStrong = Distance <= 14.0 && AlignDot >= 0.9 && Speed >= 110.0 * K;
				const bool bComeback = !bBump && !bPocket && !bStrong && Race.LeaderGapRatio >= 0.65;
				if (bPocket) { Boost = 11.5 + DScore * 3.5 + AScore * 1.3 + SScore * 0.8; Rate = 1.15; }
				else if (bBump) { Boost = 9.1 + DScore * 2.2 + AScore * 1.1 + SScore * 0.45; Rate = 1.35; }
				else if (bStrong) { Boost = 9.2 + DScore * 3.5 + AScore * 1.3 + SScore * 0.5; Rate = 0.92; }
				else if (bComeback) { Boost = 4.7 + DScore * 4.8 + AScore * 2.4 + SScore * 0.8 + Race.LeaderGapRatio * 1.45; Rate = 1.2; }
				else { Boost = 2.9 + DScore * 5.0 + AScore * 3.0 + SScore * 0.5; Rate = 0.55; }
				Boost *= K;
			}
		}
		S.SlipstreamCharge = bActive
			? FMath::Min(1.0, S.SlipstreamCharge + Dt * Rate)
			: FMath::Max(0.0, S.SlipstreamCharge - Dt * 1.15);
		if (!bActive)
		{
			return 0.0;
		}
		return S.SlipstreamCharge < 0.65 ? Boost : Boost * (1.0 + ((S.SlipstreamCharge - 0.65) / 0.35) * 0.35);
	}

	void FKartSim::Init(const FTrack* InTrack, int32 GridSlot)
	{
		check(InTrack && InTrack->Grid.Num() > 0);
		T = InTrack;
		Slot = FMath::Clamp(GridSlot, 0, T->Grid.Num() - 1);
		S = FKartState();
		S.Pos = T->Grid[Slot].Position;
		S.Yaw = T->Grid[Slot].Yaw;
		S.Dir = Forward(S.Yaw);
		S.NextCheckpoint = T->StartCheckpoint;
		S.PadLastBoostTime.Init(-1000.0, T->BoostPads.Num());
		S.AnchorPos = S.Pos;
	}

	void FKartSim::Teleport(const FVector2D& Position, double Yaw)
	{
		Respawn(Position, Yaw, TEXT("teleport"));
	}

	void FKartSim::Respawn(const FVector2D& Position, double Yaw, const TCHAR* Reason)
	{
		S.Pos = Position;
		S.Yaw = Yaw;
		S.Speed = 0.0;
		S.DriftDirection = 0.0;
		S.DriftPower = 0.0;
		++S.TeleportCount;
		S.LastRespawnReason = Reason;
	}

	void FKartSim::Step(const FKartInput& Input)
	{
		Step(Input, FKartRaceContext());
	}

	void FKartSim::Step(const FKartInput& Input, const FKartRaceContext& Race)
	{
		check(T);
		const double Dt = StepSeconds;
		S.Time += Dt;
		S.bHitWallThisStep = false;
		S.BarrierEvent = 0.0;
		S.bBarrierEventSustained = false;
		S.BumpEvent = 0.0;
		if (S.HopTime >= 0.0)
		{
			S.HopTime += Dt;
			if (S.HopTime >= HopDuration)
			{
				S.HopTime = -1.0;
			}
		}

		const FCheckpoint* Next = T->Checkpoints.IsValidIndex(S.NextCheckpoint) ? &T->Checkpoints[S.NextCheckpoint] : nullptr;

		// Off-track: two seconds to steer back, then return to the next checkpoint's approach (:862-880).
		{
			FVector2D RecoverPos;
			double RecoverYaw = 0.0;
			if (!RecoveryPose(S.Pos, Next, RecoverPos, RecoverYaw))
			{
				S.OffTrackSince = -1.0;
			}
			else if (S.OffTrackSince < 0.0)
			{
				S.OffTrackSince = S.Time;
			}
			else if (S.Time - S.OffTrackSince >= OffTrackRespawnSeconds)
			{
				Respawn(RecoverPos, RecoverYaw, TEXT("offtrack"));
				S.OffTrackSince = -1.0;
			}
		}

		const double SteeringX = FMath::Clamp(Input.SteeringX, -1.0, 1.0);
		const double Keys = (Input.bLeft ? 1.0 : 0.0) - (Input.bRight ? 1.0 : 0.0);
		const bool bForward = Input.bForward;
		const bool bBackward = Input.bBackward;
		const bool bJump = Input.bJump;

		UpdateWatchdogs(bForward && !bBackward);

		// Boost timer (:1092-1106). This step still uses the multiplier it started with.
		const double Multiplier = S.BoostMultiplier;
		if (S.BoostTimeRemaining > 0.0)
		{
			S.BoostTimeRemaining -= Dt;
			if (S.BoostTimeRemaining <= 0.0)
			{
				S.BoostTimeRemaining = 0.0;
				S.BoostMultiplier = 1.0;
				S.ActiveBoost.Reset();
			}
		}

		// Rubber band (:1112-1129): at most +2 SU for the last kart, scaled down in small lobbies.
		S.RubberBandBoost = 0.0;
		if (Race.PlayerCount > 1 && Race.bRacing)
		{
			const double PositionRatio = static_cast<double>(Race.RacePosition - 1) / (Race.PlayerCount - 1);
			const double LobbyScale = Race.PlayerCount <= 2 ? 0.4 : Race.PlayerCount == 3 ? 0.7 : 1.0;
			S.RubberBandBoost = MaxSpeed * PositionRatio * Race.LeaderGapRatio * 0.05 * LobbyScale;
		}
		S.SlipstreamBoost = UpdateSlipstream(Race, Dt);

		// Speed (:1108-1181). B1: a multiplier below 1 slows the kart instead of being ignored.
		const double PowerupBoost = Multiplier > 1.0 ? MaxSpeed * (Multiplier - 1.0) : 0.0;
		const double Slowdown = Multiplier < 1.0 ? Multiplier : 1.0;
		const double Terrain = S.bOnDirt ? DirtMultiplier : 1.0;
		const double Max = (MaxSpeed + (S.Turbo > 0.0 ? TurboBonus : 0.0) + PowerupBoost + S.RubberBandBoost + S.SlipstreamBoost) * Terrain * Slowdown;
		const double Target = bBackward ? ReverseSpeed : bForward ? Max : 0.0;
		const double Lambda = bBackward ? LambdaBrake : bForward ? LambdaThrottle : LambdaCoast;
		S.Speed = Damp(S.Speed, Target, Lambda, Dt);
		if (S.Speed < DriftMinSpeed)
		{
			S.DriftDirection = 0.0;
			S.DriftPower = 0.0;
		}
		S.Turbo -= Dt;

		// Yaw: a direct per-step turn (:1188-1199).
		S.InputTurn = (SteeringX + Keys + S.DriftDirection) * TurnPerStep;
		S.Yaw += S.InputTurn * FMath::Min(S.Speed, TurnSpeedReference) / TurnSpeedReference;

		// Hop and drift (:1202-1244).
		if (bJump && !S.bJumpHeld && S.HopTime < 0.0)
		{
			S.HopTime = 0.0;
			S.bJumpHeld = true;
			S.DriftDirection = (Input.bLeft || SteeringX > 0.1) ? DriftTurn : (Input.bRight || SteeringX < -0.1) ? -DriftTurn : 0.0;
		}
		if (!bJump)
		{
			S.bJumpHeld = false;
			if (S.Turbo <= 0.0)
			{
				FDriftBoost Boost;
				if (DriftExitBoost(S.DriftPower, S.DriftPower * 1000.0, SteeringX + Keys, S.DriftDirection, Boost))
				{
					S.Turbo = Boost.Time;
					S.BoostMultiplier = Boost.Multiplier;
					S.BoostTimeRemaining = Boost.Time;
					S.ActiveBoost = Boost.Label;
					S.LastDriftBoost = Boost.Label;
					++S.DriftBoostCount;
				}
				// The web falls back to a boostPower value shared by every kart here (B3). Not ported.
			}
			S.DriftDirection = 0.0;
			S.DriftPower = 0.0;
		}
		if (S.DriftDirection != 0.0)
		{
			S.DriftPower = FMath::Min(S.DriftPower + Dt, DriftPowerCap);
		}

		// Heading lag (:1249-1263): an unclamped, unnormalised lerp toward the nose.
		const FVector2D Start = S.Pos;
		const FVector2D Desired = Forward(S.Yaw);
		S.Dir = S.Dir + (Desired - S.Dir) * (DirLerp * Dt);
		const double SlipAngle = FMath::Atan2(Desired.X * S.Dir.Y - Desired.Y * S.Dir.X, Desired.X * S.Dir.X + Desired.Y * S.Dir.Y);
		S.SlipYaw = Damp(S.SlipYaw, SlipAngle * 1.3 + S.DriftDirection * 0.1, 6.0, Dt);
		const bool bReversing = S.Speed < 0.0;
		const FVector2D MoveDir = bReversing ? -S.Dir : S.Dir;
		const bool bHopping = S.HopTime >= 0.0 || S.HopOffset > 0.05;

		// Collision steps 1-5 (:1280-1425).
		++S.Frame;
		const uint64 Interval = S.NearestWall < CollisionSoftDistance ? FastCheckInterval : ContinuousCheckInterval;
		if (!bHopping && S.Frame - S.LastRadialFrame >= Interval)
		{
			ProbeRadial();
			S.LastRadialFrame = S.Frame;
		}
		// The cached radial push is re-applied every step until the next probe, as on the web.
		if (!bHopping && S.bRadialCritical)
		{
			S.Pos += S.RadialPush;
			S.Speed *= 0.3;
		}
		else if (!bHopping && S.RadialStrength > 0.0)
		{
			S.Pos += S.RadialPush;
		}

		const FBarrier Barrier = bHopping ? FBarrier() : ProbeBarrier(MoveDir);
		if (!bHopping && Barrier.Assist != 0.0)
		{
			const double AssistMultiplier = FMath::Min(FMath::Abs(S.Speed) / MaxSpeed, 1.0);
			const double AssistSign = bReversing ? -1.0 : 1.0;
			const double Proximity = S.NearestWall < CollisionSoftDistance ? 1.2 : 1.0;
			const bool bOpposing = (SteeringX > 0.1 && Barrier.Assist > 0.0) || (SteeringX < -0.1 && Barrier.Assist < 0.0);
			S.Yaw += Barrier.Assist * AssistMultiplier * AssistSign * Proximity * (bOpposing ? 0.15 : 1.0);
		}
		if (!bHopping && Barrier.Penetration > 0.0 && Barrier.bHasNormal)
		{
			S.Pos += Normalize(Barrier.Normal) * (Barrier.Penetration * CollisionPushStrength * 60.0);
		}

		const double AbsSpeed = FMath::Abs(S.Speed);
		const bool bNearWall = !bHopping && S.NearestWall < CollisionHardDistance;
		if (bHopping)
		{
			double Distance = 0.0;
			FVector2D Normal;
			if (T->Walls.Raycast(S.Pos, Normalize(MoveDir), 1.0, Distance, Normal) && Distance < 0.6)
			{
				S.Speed *= 0.3;
			}
			else
			{
				S.Pos += S.Dir * S.Speed * Dt;
			}
		}
		else if (Barrier.bBlocked && AbsSpeed > 0.0)
		{
			const double Closeness = Barrier.Distance != 0.0 ? 1.0 - FMath::Min(Barrier.Distance / BarrierCollisionDistance, 1.0) : 0.5;
			if (Barrier.bHasNormal && AbsSpeed > 30.0)
			{
				S.Pos += Barrier.Normal * ((AbsSpeed / MaxSpeed) * BounceStrength);
			}
			S.Speed *= bNearWall
				? FMath::Max(0.1, MinCollisionSpeedMult - Closeness * 0.5)
				: FMath::Max(MinCollisionSpeedMult, 1.0 - Closeness * 0.6);
			if (Barrier.bHasNormal)
			{
				const FVector2D Slide = Normalize(MoveDir - Barrier.Normal * FVector2D::DotProduct(MoveDir, Barrier.Normal));
				const double SlideSpeed = AbsSpeed * (bNearWall ? 0.15 : 0.4) * Dt;
				S.Pos += Slide * SlideSpeed * (bReversing ? -1.0 : 1.0);
			}
		}
		else if (!bNearWall || S.NearestWall > CollisionSoftDistance)
		{
			S.Pos += S.Dir * S.Speed * Dt;
		}
		else
		{
			S.Pos += S.Dir * S.Speed * Dt * FMath::Min(1.0, S.NearestWall / CollisionSoftDistance);
		}

		// Kart vs kart (:658-675, :1428-1434, kartContact.ts): the first overlapping kart pushes this one away.
		if (Race.bKartContact)
		{
			for (const FKartRaceContext::FOther& Other : Race.Others)
			{
				const FVector2D Delta = S.Pos - Other.Position;
				const double Distance = Delta.Size();
				if (Distance >= KartContactDistance)
				{
					continue;
				}
				const FVector2D N = Distance > 0.0001 ? Delta / Distance : FVector2D(Race.SelfSeat < Other.Seat ? -1.0 : 1.0, 0.0);
				S.Pos += N * FMath::Min(KartContactDistance - Distance, KartBumpForce * Dt * 60.0);
				S.Speed *= FMath::Pow(0.8, Dt * 60.0);
				S.BumpEvent = FMath::Min(1.0, (FMath::Abs(S.Speed) / MaxSpeed) * 0.8 + 0.2);
				break;
			}
		}

		// Swept wall constraint over the whole step (:1440-1445).
		if (ConstrainMovement(Start, S.Pos))
		{
			S.Speed *= FMath::Exp(-WallSpeedDecay * Dt);
			S.bHitWallThisStep = true;
		}

		// Collision feedback (:1457-1569): first contact fires at once, sustained contact every 150 ms.
		{
			const bool bWasColliding = S.bCollidingBarrier;
			S.bCollidingBarrier = !bHopping && (Barrier.bBlocked || Barrier.Penetration > 0.0 || S.bRadialCritical || S.RadialStrength > 0.0 || bNearWall);
			S.CollisionIntensity = S.bCollidingBarrier
				? FMath::Min(1.0, Barrier.Penetration * 2.0 + (FMath::Abs(S.Speed) / MaxSpeed) * 0.5 + (S.bRadialCritical ? 1.0 : 0.0) + (bNearWall ? 0.3 : 0.0))
				: 0.0;
			if (S.bCollidingBarrier && !bWasColliding)
			{
				S.BarrierEvent = FMath::Max(0.3, S.CollisionIntensity);
				S.LastBarrierEventTime = S.Time;
			}
			else if (S.bCollidingBarrier && S.Time - S.LastBarrierEventTime >= 0.15)
			{
				S.BarrierEvent = FMath::Max(0.2, S.CollisionIntensity * 0.6);
				S.bBarrierEventSustained = true;
				S.LastBarrierEventTime = S.Time;
			}
		}

		UpdateBoostPads();

		// Wrong way, every 15 steps above 5 SU (:1571-1586).
		if (S.Frame - S.LastWrongWayFrame >= WrongWayInterval && FMath::Abs(S.Speed) > 5.0 && T->Waypoints.Num() > 1)
		{
			S.LastWrongWayFrame = S.Frame;
			const int32 Nearest = T->NearestWaypoint(S.Pos);
			const FVector2D To = T->Waypoints[(Nearest + 1) % T->Waypoints.Num()] - S.Pos;
			const double Len = To.Size();
			if (Len > 0.1)
			{
				S.bWrongWay = FVector2D::DotProduct(Forward(S.Yaw), To) / Len < -0.5;
			}
		}

		// Terrain, every 10 steps (:1588-1610).
		if (S.Frame - S.LastTerrainFrame >= TerrainInterval)
		{
			S.LastTerrainFrame = S.Frame;
			S.bOnDirt = IsOnDirt(S.Pos);
		}

		UpdateCheckpoints();
		UpdatePresentation(Dt);
	}

	void FKartSim::ProbeRadial()
	{
		FVector2D Push = FVector2D::ZeroVector;
		double Total = 0.0;
		double Nearest = 999.0;
		int32 Critical = 0;
		for (int32 i = 0; i < NumRadialProbes; ++i)
		{
			// World-axis probes, not kart-relative (:611-652).
			const double Angle = (static_cast<double>(i) / NumRadialProbes) * UE_DOUBLE_TWO_PI;
			const FVector2D Dir(FMath::Sin(Angle), FMath::Cos(Angle));
			double Distance = 0.0;
			FVector2D Normal;
			if (!T->Walls.Raycast(S.Pos, Dir, ContinuousProbeDistance, Distance, Normal))
			{
				continue;
			}
			Nearest = FMath::Min(Nearest, Distance);
			if (Distance < CollisionCriticalDistance)
			{
				++Critical;
				const double M = (CollisionCriticalDistance - Distance) * EmergencyPushStrength;
				Push -= Dir * M;
				Total += M;
			}
			else if (Distance < CollisionHardDistance)
			{
				const double M = (CollisionHardDistance - Distance) * CollisionPushStrength;
				Push -= Dir * M;
				Total += M * 0.5;
			}
		}
		S.RadialPush = Push;
		S.RadialStrength = Total;
		S.bRadialCritical = Critical >= 2;
		S.NearestWall = Nearest;
	}

	FKartSim::FBarrier FKartSim::ProbeBarrier(const FVector2D& MoveDir)
	{
		FBarrier B;
		auto Ray = [this](const FVector2D& Dir, double Far, double& OutDistance, FVector2D& OutNormal)
		{
			return T->Walls.Raycast(S.Pos, Normalize(Dir), Far, OutDistance, OutNormal);
		};

		double DForward = 999.0, DLeft = 999.0, DRight = 999.0, DForwardLeft = 999.0, DForwardRight = 999.0;
		FVector2D NForward, NUnused;
		const bool bForward = Ray(MoveDir, BarrierCollisionDistance, DForward, NForward);
		// Named as in the web build; on the three.js ground plane (-z, x) is actually the driver's right.
		const FVector2D LeftProbe = Normalize(FVector2D(-MoveDir.Y, MoveDir.X));
		const FVector2D RightProbe = Normalize(FVector2D(MoveDir.Y, -MoveDir.X));
		if (!Ray(LeftProbe, BarrierCollisionDistance * 0.8, DLeft, NUnused)) { DLeft = 999.0; }
		if (!Ray(RightProbe, BarrierCollisionDistance * 0.8, DRight, NUnused)) { DRight = 999.0; }
		if (!Ray(MoveDir + LeftProbe, BarrierCollisionDistance * 0.9, DForwardLeft, NUnused)) { DForwardLeft = 999.0; }
		if (!Ray(MoveDir + RightProbe, BarrierCollisionDistance * 0.9, DForwardRight, NUnused)) { DForwardRight = 999.0; }

		const double LeftMin = FMath::Min(DLeft, DForwardLeft);
		const double RightMin = FMath::Min(DRight, DForwardRight);
		if (LeftMin < CollisionSoftDistance || RightMin < CollisionSoftDistance)
		{
			const double LeftInfluence = LeftMin < CollisionSoftDistance ? (CollisionSoftDistance - LeftMin) / CollisionSoftDistance : 0.0;
			const double RightInfluence = RightMin < CollisionSoftDistance ? (CollisionSoftDistance - RightMin) / CollisionSoftDistance : 0.0;
			B.Assist = (LeftInfluence - RightInfluence) * SteeringAssistStrength;
		}

		if (bForward && DForward < CollisionHardDistance)
		{
			B.Penetration = FMath::Max(B.Penetration, CollisionHardDistance - DForward);
		}
		if (LeftMin < CollisionHardDistance)
		{
			B.Penetration = FMath::Max(B.Penetration, (CollisionHardDistance - LeftMin) * 0.5);
		}
		if (RightMin < CollisionHardDistance)
		{
			B.Penetration = FMath::Max(B.Penetration, (CollisionHardDistance - RightMin) * 0.5);
		}

		B.bBlocked = bForward;
		if (bForward)
		{
			S.LastCollisionNormal = NForward;
			B.Distance = DForward;
			B.Normal = S.LastCollisionNormal;
			B.bHasNormal = true;
		}
		return B;
	}

	bool FKartSim::ConstrainMovement(const FVector2D& Start, FVector2D& InOutDesired) const
	{
		const FVector2D Delta = InOutDesired - Start;
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Delta.Size() / T->SweepSubstep));
		const double Radius = T->KartRadius;
		FVector2D P = Start;
		bool bCollided = false;

		if (T->bAnalyticWalls)
		{
			FVector2D Nearest;
			double Offset = 0.0;
			const double Distance = T->NearestCentre(Start, Nearest, Offset);
			if (Distance > T->WallOffset + Radius)
			{
				const double Limit = T->WallOffset - Radius;
				P = Nearest + (P - Nearest) * (Limit / Distance);
				bCollided = true;
			}
		}

		// Sequential push-outs can move the kart up to a radius per segment, so query a wider box.
		const double Margin = Radius * 4.0;
		TArray<int32> Candidates;
		for (int32 i = 0; i < Steps; ++i)
		{
			const FVector2D Old = P;
			P += Delta / Steps;
			T->Walls.Query(P - FVector2D(Margin), P + FVector2D(Margin), Candidates);
			for (const int32 Index : Candidates)
			{
				const FWallSegment& W = T->Walls.Segments()[Index];
				const double U = FMath::Clamp(FVector2D::DotProduct(P - W.A, W.D) / W.LengthSq, 0.0, 1.0);
				const FVector2D Proj = W.A + W.D * U;
				const FVector2D V = P - Proj;
				if (V.SizeSquared() >= Radius * Radius)
				{
					continue;
				}
				FVector2D N = FVector2D(-W.D.Y, W.D.X) / FMath::Sqrt(W.LengthSq);
				if (FVector2D::DotProduct(Old - Proj, N) < 0.0)
				{
					N = -N;
				}
				P += N * (Radius - FVector2D::DotProduct(V, N));
				bCollided = true;
			}
		}
		InOutDesired = P;
		return bCollided;
	}

	bool FKartSim::IsOnDirt(const FVector2D& Position) const
	{
		if (T->GroundBands.Num() == 0)
		{
			return false;
		}
		FVector2D Nearest;
		double Offset = 0.0;
		T->NearestCentre(Position, Nearest, Offset);
		// The down ray hits the highest ribbon under the kart first.
		double BestY = -TNumericLimits<double>::Max();
		bool bDirt = false;
		for (const FGroundBand& Band : T->GroundBands)
		{
			if (Offset >= Band.Inner && Offset <= Band.Outer && Band.Y > BestY)
			{
				BestY = Band.Y;
				bDirt = Band.bDirt;
			}
		}
		return bDirt;
	}

	bool FKartSim::RecoveryPose(const FVector2D& Position, const FCheckpoint* Next, FVector2D& OutPos, double& OutYaw) const
	{
		const int32 W = T->Waypoints.Num();
		if (W < 2)
		{
			return false;
		}
		const int32 NearestIndex = T->NearestWaypoint(Position);
		const double Distance = FVector2D::Distance(Position, T->Waypoints[NearestIndex]);

		// On a figure-eight a kart can leave one branch and land on a later one (trackRecovery.ts:68-76).
		bool bSkipped = false;
		if (Next)
		{
			const int32 CheckpointWaypoint = T->NearestWaypoint(Next->Position);
			const int32 Past = (NearestIndex - CheckpointWaypoint + W) % W;
			bSkipped = Past >= T->RecoverySkipWaypoints && Past < W / 2.0;
		}
		if (Distance <= T->RecoveryDistance && !bSkipped)
		{
			return false;
		}
		if (Next)
		{
			OutPos = Next->ApproachPosition;
			OutYaw = Next->ApproachYaw;
		}
		else
		{
			const FVector2D P = T->Waypoints[NearestIndex];
			const FVector2D N = T->Waypoints[(NearestIndex + 1) % W];
			OutPos = P;
			OutYaw = FMath::Atan2(P.X - N.X, P.Y - N.Y);
		}
		return true;
	}

	void FKartSim::UpdateWatchdogs(bool bPowered)
	{
		const FCheckpoint* Next = T->Checkpoints.IsValidIndex(S.NextCheckpoint) ? &T->Checkpoints[S.NextCheckpoint] : nullptr;

		// Stuck anchor (recoveryWatchdogs.ts stepStuckAnchor).
		const bool bAnchorResumed = S.Time - S.AnchorSeen > WatchGapSeconds;
		if (bAnchorResumed || FVector2D::Distance(S.Pos, S.AnchorPos) >= StuckAnchorDistance || !bPowered)
		{
			S.AnchorPos = S.Pos;
			S.AnchorSince = S.AnchorSeen = S.Time;
		}
		else
		{
			S.AnchorSeen = S.Time;
			if (S.Time - S.AnchorSince >= StuckAnchorSeconds)
			{
				const FSpawnSlot& Spawn = T->Grid[Slot];
				Respawn(Next ? Next->ApproachPosition : Spawn.Position, Next ? Next->ApproachYaw : Spawn.Yaw, TEXT("stuck"));
				S.AnchorPos = S.Pos;
				S.AnchorSince = S.AnchorSeen = S.Time;
			}
		}

		// Checkpoint stall (recoveryWatchdogs.ts stepCheckpointWatch).
		const bool bWatchResumed = S.Time - S.WatchSeen > WatchGapSeconds;
		if (bWatchResumed || S.WatchIndex != S.NextCheckpoint || !bPowered)
		{
			S.WatchIndex = S.NextCheckpoint;
			S.WatchSince = S.WatchSeen = S.Time;
		}
		else if (S.Time - S.WatchSince >= CheckpointStallSeconds && Next)
		{
			S.WatchSince = S.WatchSeen = S.Time;
			Respawn(Next->ApproachPosition, Next->ApproachYaw, TEXT("checkpoint-stall"));
			S.AnchorPos = S.Pos;
			S.AnchorSince = S.AnchorSeen = S.Time;
		}
		else
		{
			S.WatchSeen = S.Time;
		}
	}

	void FKartSim::UpdateBoostPads()
	{
		// BoostPads.jsx:208-230, games/kart/boostPad.ts isInsideBoostPad.
		for (int32 i = 0; i < T->BoostPads.Num(); ++i)
		{
			if (S.Time - S.PadLastBoostTime[i] <= T->BoostCooldown)
			{
				continue;
			}
			const FBoostPad& Pad = T->BoostPads[i];
			const double Dx = S.Pos.X - Pad.Position.X;
			const double Dz = S.Pos.Y - Pad.Position.Y;
			const double C = FMath::Cos(Pad.Yaw), Sn = FMath::Sin(Pad.Yaw);
			const double Across = Dx * C - Dz * Sn;
			const double Along = Dx * Sn + Dz * C;
			if (FMath::Abs(Across) <= Pad.Width / 2.0 + T->BoostPadding && FMath::Abs(Along) <= Pad.Length / 2.0 + T->BoostPadding)
			{
				S.BoostMultiplier = T->BoostMultiplier;
				S.BoostTimeRemaining = T->BoostDuration;
				S.ActiveBoost = TEXT("BOOST_PAD");
				S.PadLastBoostTime[i] = S.Time;
			}
		}
	}

	void FKartSim::UpdateCheckpoints()
	{
		// useCheckpointDetection.ts advanceSequentialCheckpointState.
		const int32 N = T->Checkpoints.Num();
		const FCheckpoint& Next = T->Checkpoints[S.NextCheckpoint];
		const bool bInside = FVector2D::Distance(S.Pos, Next.Position) < Next.Radius;
		const bool bCrossed = bInside && !S.bWasInsideNext;
		if (bCrossed)
		{
			if (Next.bFinish)
			{
				++S.LapsCompleted;
				S.NextCheckpoint = T->StartCheckpoint;
				if (!S.bFinished && S.LapsCompleted >= T->Laps)
				{
					S.bFinished = true;
					S.FinishTime = S.Time;
				}
			}
			else
			{
				S.NextCheckpoint = (S.NextCheckpoint + 1) % N;
			}
		}
		const FCheckpoint& After = T->Checkpoints[S.NextCheckpoint];
		S.bWasInsideNext = bCrossed ? FVector2D::Distance(S.Pos, After.Position) < After.Radius : bInside;

		const int32 Previous = S.NextCheckpoint == 0 ? N - 1 : S.NextCheckpoint - 1;
		const double SegmentLength = FMath::Max(1.0, FVector2D::Distance(T->Checkpoints[Previous].Position, After.Position));
		const double SegmentProgress = 1.0 - FMath::Min(FVector2D::Distance(S.Pos, After.Position) / SegmentLength, 1.0);
		S.ProgressPercent = FMath::Min(99.9, ((Previous + FMath::Max(0.0, SegmentProgress)) / N) * 100.0);
	}

	void FKartSim::UpdatePresentation(double Dt)
	{
		// Kart.jsx:194-242 steering and body layers.
		S.SteerVisual = Damp(S.SteerVisual, S.InputTurn, 14.0, Dt);
		const double SpeedFactor = FMath::Min(1.0, S.Speed / 50.0);
		S.BodyYaw = Damp(S.BodyYaw, S.SteerVisual * 6.0 * SpeedFactor, 6.0, Dt);
		S.GroupYaw = Damp(S.GroupYaw, S.DriftDirection * 0.4 + S.BodyYaw, 4.0, Dt);
		S.Lean = Damp(S.Lean, -S.SteerVisual * 3.0 * SpeedFactor, 5.0, Dt);

		// Hop: 0 -> 0.3 m in 0.125 s (power2.out), then back (gsap yoyo).
		const double Half = HopDuration / 2.0;
		if (S.HopTime < 0.0)
		{
			S.HopOffset = 0.0;
		}
		else if (S.HopTime < Half)
		{
			S.HopOffset = HopHeight * PowerTwoOut(S.HopTime / Half);
		}
		else
		{
			S.HopOffset = HopHeight * PowerTwoOut((HopDuration - S.HopTime) / Half);
		}
	}
}

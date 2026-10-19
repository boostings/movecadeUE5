#include "Audio/KartSoundObserver.h"

#include "Sim/KartTrack.h"

namespace MovecadeKart::Audio
{
	namespace
	{
		// MultiplayerKart.tsx / KartSim.cpp constants the derivations need.
		constexpr double RoadTopSpeed = 40.0;
		constexpr double TurboBonus = 40.0;
		constexpr double DirtMultiplier = 0.75;
		constexpr double BarrierCollisionDistance = 2.5;
		constexpr double CollisionHardDistance = 0.7;
		constexpr double SimStep = 1.0 / 60.0;
		/** At most this many sim steps are replayed into the drivetrain per rendered frame (the sim's cap is 4). */
		constexpr int64 MaxReplaySteps = 6;

		const FName CueShift(TEXT("kart.shift"));
		const FName CueBackfireA(TEXT("kart.backfire-a"));
		const FName CueBackfireB(TEXT("kart.backfire-b"));
		const FName CueSkid(TEXT("kart.tire-skid"));
		const FName CueWind(TEXT("kart.wind"));
		const FName CueOffroad(TEXT("kart.offroad"));
		const FName CueBoostLoop(TEXT("kart.boost-loop"));
		const FName CueRoulette(TEXT("kart.item-roulette"));
		const FName CueInvincibleLoop(TEXT("kart.invincible-loop"));
		const FName CueRocketLoop(TEXT("kart.rocket-loop"));
		const FName CueBoost(TEXT("kart.boost"));
		const FName CueDriftCharge(TEXT("kart.drift-charge"));
		const FName CueItemPickup(TEXT("kart.item-pickup"));
		const FName CueItemReady(TEXT("kart.item-ready"));
		const FName CueInvincibleStart(TEXT("kart.invincible-start"));
		const FName CueLightning(TEXT("kart.lightning"));
		const FName CueInk(TEXT("kart.ink"));
		const FName CueWarning(TEXT("kart.warning"));
		const FName CueLap(TEXT("kart.lap"));
		const FName CueFinalLap(TEXT("kart.final-lap"));
		const FName CueFinish(TEXT("kart.finish"));
		const FName CueHop(TEXT("kart.hop"));
		const FName CueLand(TEXT("kart.land"));
		const FName CueFall(TEXT("kart.fall"));
		const FName CueRespawn(TEXT("kart.respawn"));
		const FName ItemLightning(TEXT("LIGHTNING"));
		const FName ItemBlooper(TEXT("BLOOPER"));

		FVector2D Normalize(const FVector2D& V)
		{
			const double Len = V.Size();
			return Len > 0.0 ? V / Len : FVector2D::ZeroVector;
		}
	}

	void FKartSoundObserver::Reset()
	{
		Drivetrain.Reset();
		LastEngine = FDrivetrainFrame();
		bHasPrevious = false;
		Previous = FSnapshot();
		Backfires = 0;
	}

	bool FKartSoundObserver::IsBoosting(const FKartState& S)
	{
		const double PowerupBoost = S.BoostMultiplier > 1.0 ? RoadTopSpeed * (S.BoostMultiplier - 1.0) : 0.0;
		const double Max = (RoadTopSpeed + (S.Turbo > 0.0 ? TurboBonus : 0.0) + PowerupBoost) * (S.bOnDirt ? DirtMultiplier : 1.0);
		return Max > RoadTopSpeed;
	}

	bool FKartSoundObserver::BarrierContact(const FKartState& S, const FTrack& Track, double& OutPenetration, bool& bOutCritical)
	{
		// isCollidingBarrier = blocked || penetrationDepth > 0 || isCritical || pushStrength > 0 || isNearWall.
		OutPenetration = 0.0;
		bOutCritical = S.bRadialCritical;
		const bool bHopping = S.HopTime >= 0.0 || S.HopOffset > 0.05;
		bool bBlocked = false;
		if (!bHopping)
		{
			// The sim's barrier probe (KartSim.cpp ProbeBarrier), repeated here because its result is step-local.
			const FVector2D MoveDir = S.Speed < 0.0 ? -S.Dir : S.Dir;
			auto Ray = [&](const FVector2D& Dir, double Far)
			{
				double Distance = 0.0;
				FVector2D Normal;
				return Track.Walls.Raycast(S.Pos, Normalize(Dir), Far, Distance, Normal) ? Distance : 999.0;
			};
			const double DForward = Ray(MoveDir, BarrierCollisionDistance);
			const FVector2D LeftProbe = Normalize(FVector2D(-MoveDir.Y, MoveDir.X));
			const FVector2D RightProbe = Normalize(FVector2D(MoveDir.Y, -MoveDir.X));
			const double LeftMin = FMath::Min(Ray(LeftProbe, BarrierCollisionDistance * 0.8), Ray(MoveDir + LeftProbe, BarrierCollisionDistance * 0.9));
			const double RightMin = FMath::Min(Ray(RightProbe, BarrierCollisionDistance * 0.8), Ray(MoveDir + RightProbe, BarrierCollisionDistance * 0.9));
			bBlocked = DForward < 999.0;
			if (DForward < CollisionHardDistance)
			{
				OutPenetration = FMath::Max(OutPenetration, CollisionHardDistance - DForward);
			}
			if (LeftMin < CollisionHardDistance)
			{
				OutPenetration = FMath::Max(OutPenetration, (CollisionHardDistance - LeftMin) * 0.5);
			}
			if (RightMin < CollisionHardDistance)
			{
				OutPenetration = FMath::Max(OutPenetration, (CollisionHardDistance - RightMin) * 0.5);
			}
		}
		const bool bNearWall = !bHopping && S.NearestWall < CollisionHardDistance;
		return bBlocked || OutPenetration > 0.0 || bOutCritical || S.RadialStrength > 0.0 || bNearWall;
	}

	FKartAudioCommands FKartSoundObserver::Observe(const FKartAudioContext& Context, const FKartState& S, const FKartInput& Input,
		const FKartAudioStatus& Status, const FTrack* Track)
	{
		FKartAudioCommands Out;
		const FSnapshot Old = Previous;
		const bool bHadPrevious = bHasPrevious;
		bool bBarrier = false;

		auto Snapshot = [&]()
		{
			Previous.Speed = S.Speed;
			Previous.DriftPower = S.DriftPower;
			Previous.bBoosting = IsBoosting(S);
			Previous.ActiveBoost = S.ActiveBoost;
			Previous.bWrongWay = S.bWrongWay;
			Previous.bFinished = S.bFinished;
			Previous.Laps = S.LapsCompleted;
			Previous.HopTime = S.HopTime;
			Previous.OffTrackSince = S.OffTrackSince;
			Previous.TeleportCount = S.TeleportCount;
			Previous.WatchSince = S.WatchSince;
			Previous.bBarrier = bBarrier;
			Previous.Frame = S.Frame;
			Previous.Status = Status;
			Previous.Phase = Context.Phase;
			Previous.bPaused = Context.bPaused;
			bHasPrevious = true;
		};

		// sound.ts: paused (or muted / hidden, which the mixer handles) stops the kart and forgets the drivetrain.
		if (Context.bPaused)
		{
			Out.bStopAll = true;
			Drivetrain.Reset();
			Snapshot();
			return Out;
		}

		// A new race: the sim was re-initialised (its counters went back), or the room went back to waiting.
		const bool bReset = !bHadPrevious || S.Frame < Old.Frame || S.TeleportCount < Old.TeleportCount ||
			(Context.Phase == ERacePhase::Waiting && Old.Phase != ERacePhase::Waiting);
		if (bReset)
		{
			Out.bStopAll = true;
			Drivetrain.Reset();
		}

		// Each new finisher: kart.finish at the cue's own gain, centred (no per-kart options).
		if (bHadPrevious && !Old.bPaused && !bReset && S.bFinished && !Old.bFinished)
		{
			Out.Plays.Add({ CueFinish, 1.0, 1.0, 0.0 });
		}

		if (Context.Phase != ERacePhase::Racing && Context.Phase != ERacePhase::Countdown)
		{
			Out.bStopAll = true;
			Snapshot();
			return Out;
		}

		const int32 NumKarts = FMath::Max(1, Context.NumKarts);
		const double Mix = 1.0 / FMath::Sqrt(static_cast<double>(NumKarts));
		const double Pan = LaneToPan(Context.Lane, NumKarts);
		Out.Pan = Pan;
		auto Play = [&](FName Cue, double Gain = -1.0, double Rate = 1.0)
		{
			Out.Plays.Add({ Cue, Gain < 0.0 ? Mix : Gain, Rate, Pan });
		};

		if (S.bFinished)
		{
			Out.bStopAll = true;
			Snapshot();
			return Out;
		}

		const bool bRacing = Context.Phase == ERacePhase::Racing;
		Out.bRacing = bRacing;
		const double Speed = FMath::Abs(S.Speed);

		// Drivetrain. While racing it advances once per 60 Hz sim step, which is the web's 60 fps frame; on the
		// grid (clutch in) it runs on frame time, so gas before GO revs it even if the sim is held.
		bool bFresh = false;
		if (!Drivetrain.IsSet())
		{
			Drivetrain.Emplace(static_cast<uint32>(Context.SpawnIndex + 1));
			bFresh = true;
		}
		FDrivetrainInput DriveInput;
		DriveInput.Accelerating = Input.bForward ? 1 : 0;
		DriveInput.bNeutral = !bRacing;
		TArray<FDrivetrainFrame, TInlineAllocator<6>> Frames;
		if (!bRacing)
		{
			DriveInput.Speed = 0.0;
			DriveInput.Dt = bFresh ? SimStep : FMath::Clamp(Context.FrameDt, 0.0, 0.1);
			Frames.Add(Drivetrain->Step(DriveInput));
		}
		else if (bFresh || !bHadPrevious)
		{
			DriveInput.Speed = S.Speed;
			DriveInput.Dt = SimStep;
			Frames.Add(Drivetrain->Step(DriveInput));
		}
		else
		{
			const int64 Steps = FMath::Clamp(static_cast<int64>(S.Frame) - static_cast<int64>(Old.Frame), int64(0), MaxReplaySteps);
			for (int64 i = 1; i <= Steps; ++i)
			{
				DriveInput.Speed = FMath::Lerp(Old.Speed, S.Speed, static_cast<double>(i) / Steps);
				DriveInput.Dt = SimStep;
				Frames.Add(Drivetrain->Step(DriveInput));
			}
		}
		if (Frames.Num() > 0)
		{
			LastEngine = Frames.Last();
		}
		Out.Drivetrain = LastEngine;

		if (!Status.bBulletBill)
		{
			Out.bEngine = true;
			Out.EngineRpm = LastEngine.Rpm;
			Out.EngineLoad = LastEngine.Load;
			Out.EngineCharacter = EngineCharacter(Context.SpawnIndex);
			Out.EngineMix = Mix;
			for (const FDrivetrainFrame& Frame : Frames)
			{
				if (Frame.Events & DrivetrainUpshift)
				{
					Play(CueShift);
				}
				if (Frame.Events & DrivetrainBackfire)
				{
					++Backfires;
					Play(Backfires % 2 ? CueBackfireA : CueBackfireB, Mix * (0.6 + Frame.Rpm / 12000.0), 0.92 + (Backfires % 5) * 0.04);
				}
			}
		}

		// Continuous layers.
		auto Loop = [&](ELoopLayer Layer, FName Cue, bool bActive, double Rate = 1.0, double Gain = 1.0, double Lowpass = -1.0)
		{
			FLoopCommand& Command = Out.Loops[static_cast<int32>(Layer)];
			Command.bActive = bActive;
			Command.Cue = Cue;
			Command.Gain = Mix * Gain;
			Command.Rate = Rate;
			Command.Lowpass = Lowpass;
		};
		// A drift is the physics' drift direction; the button alone is only a hop.
		const bool bSliding = S.DriftDirection != 0.0;
		const double Keys = (Input.bLeft ? 1.0 : 0.0) - (Input.bRight ? 1.0 : 0.0);
		const double Steer = FMath::Clamp(Input.SteeringX + Keys, -1.0, 1.0);
		const FLoopMix Squeal = SquealMix(Speed, bSliding, S.DriftPower, Steer);
		Loop(ELoopLayer::Skid, CueSkid, bRacing && Squeal.Gain > 0.0, Squeal.Rate, Squeal.Gain);
		const FLoopMix Wind = WindMix(Speed);
		Loop(ELoopLayer::Wind, CueWind, bRacing && Wind.Gain > 0.0, Wind.Rate, Wind.Gain, Wind.Lowpass);
		const bool bBoosting = IsBoosting(S);
		Loop(ELoopLayer::Dirt, CueOffroad, bRacing && S.bOnDirt && Speed > 3.0);
		Loop(ELoopLayer::Boost, CueBoostLoop, bRacing && bBoosting && !Status.bBulletBill);
		Loop(ELoopLayer::Roulette, CueRoulette, bRacing && Status.bRollingItem);
		Loop(ELoopLayer::Aura, CueInvincibleLoop, bRacing && Status.bInvincible && !Status.bBulletBill);
		Loop(ELoopLayer::Rocket, CueRocketLoop, bRacing && Status.bBulletBill);

		if (bRacing && Track)
		{
			double Penetration = 0.0;
			bool bCritical = false;
			bBarrier = BarrierContact(S, *Track, Penetration, bCritical);
			if (bBarrier && bHadPrevious && !Old.bBarrier && !bReset)
			{
				// playKartImpact(impactStrength({ speedMph, penetration, critical })) on the first contact frame.
				const double Strength = ImpactStrength(S.Speed, Penetration, bCritical);
				double TierGain = 1.0;
				const EImpactTier Tier = ImpactTier(Strength, TierGain);
				Play(ImpactCueId(Tier), Mix * TierGain, ImpactRate(Strength));
			}
		}

		if (!bHadPrevious || !bRacing || bReset || Old.bPaused)
		{
			Snapshot();
			return Out;
		}

		// State transitions (sound.ts).
		if (bBoosting && (!Old.bBoosting || S.ActiveBoost != Old.ActiveBoost))
		{
			double Gain = 0.0;
			const int32 Level = DriftBoostLevel(S.ActiveBoost, Old.DriftPower, Gain);
			if (Level > 0)
			{
				Play(DriftBoostCueId(Level), Mix * Gain);
			}
			else
			{
				Play(CueBoost);
			}
		}
		if (Status.bBulletBill && !Old.Status.bBulletBill)
		{
			Play(CueBoost);
		}
		if (FMath::FloorToDouble(S.DriftPower) > FMath::FloorToDouble(Old.DriftPower) && Input.bJump)
		{
			Play(CueDriftCharge);
		}
		if (Status.bRollingItem && !Old.Status.bRollingItem)
		{
			Play(CueItemPickup);
		}
		if (!Status.bRollingItem && Old.Status.bRollingItem && Status.bHasItem)
		{
			Play(CueItemReady);
		}
		if (Status.bInvincible && !Old.Status.bInvincible && !Status.bBulletBill)
		{
			Play(CueInvincibleStart);
		}
		if (Status.bShrunk && !Old.Status.bShrunk)
		{
			Play(CueLightning);
		}
		if (Status.bInk && !Old.Status.bInk)
		{
			Play(CueInk);
		}
		if (Old.Status.bHasItem && !Status.bHasItem && !Old.Status.bRollingItem)
		{
			if (Old.Status.HeldItem == ItemLightning)
			{
				Play(CueLightning);
			}
			if (Old.Status.HeldItem == ItemBlooper)
			{
				Play(CueInk);
			}
		}
		if (S.bWrongWay && !Old.bWrongWay)
		{
			Play(CueWarning);
		}
		if (S.LapsCompleted > Old.Laps)
		{
			const int32 TotalLaps = Track ? Track->Laps : 3;
			Play(S.LapsCompleted == TotalLaps - 1 ? CueFinalLap : CueLap);
		}

		// Physics one-shots (playKartEvent), from the state they leave behind.
		if (S.HopTime >= 0.0 && (Old.HopTime < 0.0 || S.HopTime < Old.HopTime))
		{
			Play(CueHop);
		}
		if (Old.HopTime >= 0.0 && S.HopTime < 0.0)
		{
			Play(CueLand);
		}
		if (S.OffTrackSince >= 0.0 && Old.OffTrackSince < 0.0)
		{
			Play(CueFall);
		}
		if (S.TeleportCount > Old.TeleportCount)
		{
			// Off-track recovery and the stuck-anchor rescue are voiced; the checkpoint-stall rescue is silent on the
			// web. A stall rescue restarts the checkpoint watch and the anchor on the same step.
			const bool bOffTrackRescue = Old.OffTrackSince >= 0.0 && S.OffTrackSince < 0.0;
			const bool bStallRescue = !bOffTrackRescue && S.WatchSince != Old.WatchSince && S.WatchSince == S.AnchorSince;
			if (!bStallRescue)
			{
				Play(CueRespawn);
			}
		}

		Snapshot();
		return Out;
	}
}

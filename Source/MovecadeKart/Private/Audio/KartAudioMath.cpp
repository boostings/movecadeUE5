#include "Audio/KartAudioMath.h"

namespace MovecadeKart::Audio
{
	namespace
	{
		double Clamp01(double Value)
		{
			return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0, 1.0) : 0.0;
		}

		/** JS Math.round for the non-negative values used here. */
		double RoundHalfUp(double Value)
		{
			return FMath::FloorToDouble(Value + 0.5);
		}
	}

	double FMulberry32::Next()
	{
		A = A + 0x6d2b79f5u;
		uint32 T = (A ^ (A >> 15)) * (1u | A);
		T = (T + ((T ^ (T >> 7)) * (61u | T))) ^ T;
		return static_cast<double>(T ^ (T >> 14)) / 4294967296.0;
	}

	double Approach(double Value, double Target, double Tau, double Dt)
	{
		return Target + (Value - Target) * FMath::Exp(-Dt / FMath::Max(1e-4, Tau));
	}

	double WheelRpm(double SpeedMph, int32 Gear)
	{
		if (Gear == 0)
		{
			return 0.0;
		}
		const int32 Index = Gear < 0 ? 0 : Gear - 1;
		return FMath::Abs(SpeedMph) * (LimiterRpm / GearTopMph[Index]);
	}

	FDrivetrainFrame FDrivetrain::Step(const FDrivetrainInput& Input)
	{
		const double Dt = FMath::Min(0.1, FMath::Max(0.0, Input.Dt));
		uint8 Events = 0;
		const double Speed = FMath::IsFinite(Input.Speed) ? Input.Speed : 0.0;
		const double Abs = FMath::Abs(Speed);

		// Keyboard drivers never report the pedal, so the speed trend decides, with hysteresis.
		if (bHasLastSpeed && Dt > 0.0)
		{
			AccelEma = Approach(AccelEma, (Abs - FMath::Abs(LastSpeed)) / Dt, 0.08, Dt);
		}
		LastSpeed = Speed;
		bHasLastSpeed = true;
		const bool bWasThrottle = bThrottle;
		if (Input.Accelerating == 1)
		{
			bThrottle = true;
		}
		else if (AccelEma > 1.5)
		{
			bThrottle = true;
		}
		else if (AccelEma < -2.5 || (Input.Accelerating == 0 && Abs < 0.5))
		{
			bThrottle = false;
		}

		ShiftGap = FMath::Max(0.0, ShiftGap - Dt);
		Lockout = FMath::Max(0.0, Lockout - Dt);
		LimiterCut = FMath::Max(0.0, LimiterCut - Dt);

		const bool bClutchIn = Input.bNeutral || Abs < 0.6;
		if (bClutchIn)
		{
			Gear = 0;
		}
		else if (Speed < 0.0)
		{
			Gear = -1;
		}
		else if (Gear <= 0)
		{
			Gear = 1;
		}

		if (Gear >= 1 && Lockout == 0.0)
		{
			if (bThrottle && Rpm >= UpshiftRpm && Gear < NumGears)
			{
				Gear += 1;
				ShiftGap = ShiftGapSeconds;
				Lockout = ShiftLockoutSeconds;
				Events |= DrivetrainUpshift;
				if (Rng.Next() < 0.35)
				{
					Pops.Add(0.02);
				}
			}
			else if (Gear > 1 && WheelRpm(Speed, Gear) < (bThrottle ? KickdownRpm : DownshiftRpm) &&
				WheelRpm(Speed, Gear - 1) < UpshiftRpm - 600.0)
			{
				Gear -= 1;
				Lockout = ShiftLockoutSeconds;
				Events |= DrivetrainDownshift;
			}
		}

		const bool bPedal = bThrottle && ShiftGap == 0.0;
		double Target;
		double Tau;
		if (Gear == 0)
		{
			// Free revving: light flywheel, climbs fast, falls slower.
			Target = bPedal ? LimiterRpm + 400.0 : IdleRpm;
			Tau = bPedal ? 0.22 : 0.3;
		}
		else
		{
			const double Wheel = WheelRpm(Speed, Gear);
			// Clutch slip off the line holds revs up until the wheels catch them.
			const double Slip = Gear == 1 && bPedal ? LaunchRpm * FMath::Max(0.0, 1.0 - Wheel / LaunchRpm) : 0.0;
			Target = FMath::Max(IdleRpm, Wheel + Slip);
			Tau = ShiftGap > 0.0 ? 0.05 : 0.045;
		}
		if (LimiterCut > 0.0)
		{
			Target = FMath::Min(Target, LimiterRpm - 700.0);
			Tau = 0.03;
		}
		Rpm = Approach(Rpm, Target, Tau, Dt);
		bool bLimiter = LimiterCut > 0.0;
		if (Rpm >= LimiterRpm)
		{
			Rpm = LimiterRpm;
			if (bPedal)
			{
				LimiterCut = LimiterCutSeconds;
				bLimiter = true;
			}
		}

		// Lift-off above mid revs: unburnt fuel lights in a hot exhaust.
		if (bWasThrottle && !bThrottle && Rpm > 4500.0)
		{
			Pops.Add(0.0);
			const int32 Extra = 1 + static_cast<int32>(FMath::FloorToDouble(Rng.Next() * 3.0));
			for (int32 i = 0; i < Extra; ++i)
			{
				Pops.Add(0.07 + Rng.Next() * 0.32);
			}
		}
		if (bThrottle)
		{
			Pops.RemoveAll([](double Delay) { return !(Delay <= 0.03); });
		}
		int32 Due = 0;
		for (int32 i = Pops.Num() - 1; i >= 0; --i)
		{
			if (Pops[i] - Dt <= 0.0)
			{
				++Due;
				Pops.RemoveAt(i, EAllowShrinking::No);
			}
			else
			{
				Pops[i] -= Dt;
			}
		}
		if (Due > 0 && Rpm > 2400.0)
		{
			Events |= DrivetrainBackfire;
		}

		const double TargetLoad = ShiftGap > 0.0 || bLimiter ? -0.35
			: bPedal ? 1.0
			: Rpm > IdleRpm + 350.0 ? -1.0 : 0.1;
		Load = Approach(Load, TargetLoad, TargetLoad > Load ? 0.05 : 0.09, Dt);

		FDrivetrainFrame Frame;
		Frame.Rpm = Rpm;
		Frame.Gear = Gear;
		Frame.Load = Load;
		Frame.bThrottle = bThrottle;
		Frame.bLimiter = bLimiter;
		Frame.Events = Events;
		return Frame;
	}

	void LayerWeights(const double* Anchors, int32 Num, double Rpm, double* OutWeights)
	{
		for (int32 i = 0; i < Num; ++i)
		{
			OutWeights[i] = 0.0;
		}
		if (Num == 0)
		{
			return;
		}
		if (Rpm <= Anchors[0])
		{
			OutWeights[0] = 1.0;
			return;
		}
		const int32 Last = Num - 1;
		if (Rpm >= Anchors[Last])
		{
			OutWeights[Last] = 1.0;
			return;
		}
		int32 Upper = 0;
		while (Anchors[Upper] < Rpm)
		{
			++Upper;
		}
		const double T = (Rpm - Anchors[Upper - 1]) / (Anchors[Upper] - Anchors[Upper - 1]);
		OutWeights[Upper - 1] = FMath::Cos(T * UE_DOUBLE_PI / 2.0);
		OutWeights[Upper] = FMath::Sin(T * UE_DOUBLE_PI / 2.0);
	}

	FEngineTargets EngineTargets(double Rpm, double Load, double MixGain)
	{
		FEngineTargets Out;
		Out.Rpm = FMath::Clamp(FMath::IsFinite(Rpm) ? Rpm : 950.0, 500.0, 9500.0);
		Out.Load = FMath::Clamp(FMath::IsFinite(Load) ? Load : 0.0, -1.0, 1.0);
		// Idle belongs to the on bank's idle recording whatever the pedal says.
		const double Idle = FMath::Clamp((1800.0 - Out.Rpm) / 800.0, 0.0, 1.0);
		const double Drive = FMath::Max(Idle, FMath::Clamp((Out.Load + 0.3) / 0.9, 0.0, 1.0));
		Out.OnBus = FMath::Sin(Drive * UE_DOUBLE_PI / 2.0);
		Out.OffBus = FMath::Cos(Drive * UE_DOUBLE_PI / 2.0) * 0.7;
		LayerWeights(OnLayerRpm, NumOnLayers, Out.Rpm, Out.OnGain);
		LayerWeights(OffLayerRpm, NumOffLayers, Out.Rpm, Out.OffGain);
		for (int32 i = 0; i < NumOnLayers; ++i)
		{
			Out.OnRate[i] = FMath::Clamp(Out.Rpm / OnLayerRpm[i], 0.5, 2.0);
		}
		for (int32 i = 0; i < NumOffLayers; ++i)
		{
			Out.OffRate[i] = FMath::Clamp(Out.Rpm / OffLayerRpm[i], 0.5, 2.0);
		}
		const double Level = 0.8 + 0.2 * FMath::Clamp(Out.Rpm / 7000.0, 0.0, 1.0);
		Out.Out = Level * FMath::Clamp(MixGain, 0.0, 2.0);
		return Out;
	}

	FLoopMix WindMix(double SpeedMph)
	{
		// Road top speed is 40; a turbo adds 40, so the air rush peaks at 80.
		const double T = Clamp01((FMath::Abs(SpeedMph) - 4.0) / (80.0 - 4.0));
		FLoopMix Mix;
		Mix.Gain = FMath::Pow(T, 1.6);
		Mix.Rate = 0.85 + 0.3 * T;
		Mix.Lowpass = RoundHalfUp(700.0 + 13300.0 * FMath::Pow(T, 1.2));
		return Mix;
	}

	FLoopMix SquealMix(double SpeedMph, bool bDrifting, double DriftPower, double Steer)
	{
		const double Speed = FMath::Abs(SpeedMph);
		const double SteerAbs = Clamp01(FMath::Abs(Steer));
		double Intensity;
		if (bDrifting)
		{
			const double SpeedFactor = Clamp01((Speed - 6.0) / 24.0);
			Intensity = SpeedFactor * (0.5 + 0.3 * Clamp01(DriftPower / 6.0) + 0.2 * SteerAbs);
		}
		else
		{
			Intensity = 0.35 * Clamp01((SteerAbs - 0.55) / 0.45) * Clamp01((Speed - 28.0) / 20.0);
		}
		FLoopMix Mix;
		Mix.Gain = Intensity < 0.02 ? 0.0 : Intensity;
		Mix.Rate = 0.9 + FMath::Min(Speed / 160.0, 0.5) + 0.08 * Mix.Gain;
		return Mix;
	}

	double ImpactStrength(double SpeedMph, double Penetration, bool bCritical)
	{
		return Clamp01(FMath::Abs(SpeedMph) / 60.0 + Penetration * 1.5 + (bCritical ? 0.15 : 0.0));
	}

	EImpactTier ImpactTier(double Strength, double& OutGain)
	{
		const double S = Clamp01(Strength);
		double Lo = 0.7, Hi = 1.0;
		EImpactTier Tier = EImpactTier::Heavy;
		if (S < 0.35)
		{
			Tier = EImpactTier::Light;
			Lo = 0.0;
			Hi = 0.35;
		}
		else if (S < 0.7)
		{
			Tier = EImpactTier::Medium;
			Lo = 0.35;
			Hi = 0.7;
		}
		OutGain = 0.6 + 0.4 * Clamp01((S - Lo) / (Hi - Lo));
		return Tier;
	}

	FName ImpactCueId(EImpactTier Tier)
	{
		static const FName Ids[] = { TEXT("kart.crash-light"), TEXT("kart.crash-medium"), TEXT("kart.crash-heavy") };
		return Ids[static_cast<int32>(Tier)];
	}

	double ImpactRate(double Strength)
	{
		return 0.96 + FMath::Fmod(Strength * 7919.0, 1.0) * 0.08;
	}

	int32 DriftLevel(double DriftPower)
	{
		return DriftPower >= 6.0 ? 3 : DriftPower >= 3.0 ? 2 : DriftPower >= 1.0 ? 1 : 0;
	}

	int32 DriftBoostLevel(const FString& ActiveBoost, double ChargeBeforeRelease, double& OutGain)
	{
		OutGain = 0.0;
		if (!ActiveBoost.StartsWith(TEXT("DRIFT_"), ESearchCase::CaseSensitive))
		{
			return 0;
		}
		int32 Underscore = INDEX_NONE;
		ActiveBoost.FindLastChar(TEXT('_'), Underscore);
		const FString Grade = ActiveBoost.Mid(Underscore + 1);
		int32 Level = Grade == TEXT("MINI") ? 1 : Grade == TEXT("SUPER") ? 2 : Grade == TEXT("ULTRA") ? 3 : 0;
		if (Level == 0)
		{
			Level = FMath::Max(1, DriftLevel(ChargeBeforeRelease));
		}
		static constexpr double Gains[] = { 0.8, 0.9, 1.0 };
		OutGain = Gains[Level - 1];
		return Level;
	}

	FName DriftBoostCueId(int32 Level)
	{
		static const FName Ids[] = { TEXT("kart.drift-boost-1"), TEXT("kart.drift-boost-2"), TEXT("kart.drift-boost-3") };
		return Ids[FMath::Clamp(Level, 1, 3) - 1];
	}

	double LaneToPan(int32 LaneIndex, int32 LaneCount)
	{
		if (LaneCount <= 1)
		{
			return 0.0;
		}
		const double Half = (LaneCount - 1) / 2.0;
		return ((LaneIndex - Half) / Half) * PanSpread;
	}

	double CountdownRate(int32 Value)
	{
		return Value <= 3 ? 1.0 + (3 - Value) * 0.1 : 1.0;
	}

	double EngineCharacter(int32 SpawnIndex)
	{
		return FMath::Fmod(SpawnIndex * 0.29 + 0.13, 1.0);
	}

	TConstArrayView<FCueDef> AllCues()
	{
		// kartCues.ts KART_CUES and cues.ts lobby.tick / lobby.go: gain, throttleMs, loop.
		static const FCueDef Cues[] = {
			{ TEXT("kart.engine-on-1000"), TEXT("kart_engine_on_1000"), 1.f, 0, true },
			{ TEXT("kart.engine-on-2500"), TEXT("kart_engine_on_2500"), 1.f, 0, true },
			{ TEXT("kart.engine-on-4000"), TEXT("kart_engine_on_4000"), 1.f, 0, true },
			{ TEXT("kart.engine-on-5500"), TEXT("kart_engine_on_5500"), 1.f, 0, true },
			{ TEXT("kart.engine-on-7000"), TEXT("kart_engine_on_7000"), 1.f, 0, true },
			{ TEXT("kart.engine-off-2500"), TEXT("kart_engine_off_2500"), 1.f, 0, true },
			{ TEXT("kart.engine-off-6500"), TEXT("kart_engine_off_6500"), 1.f, 0, true },
			{ TEXT("kart.shift"), TEXT("kart_shift"), 0.32f, 120, false },
			{ TEXT("kart.backfire-a"), TEXT("kart_backfire_a"), 0.3f, 60, false },
			{ TEXT("kart.backfire-b"), TEXT("kart_backfire_b"), 0.3f, 60, false },
			{ TEXT("kart.tire-skid"), TEXT("kart_tire_skid"), 0.16f, 0, true },
			{ TEXT("kart.offroad"), TEXT("kart_offroad"), 0.14f, 0, true },
			{ TEXT("kart.drift-charge"), TEXT("kart_drift_charge"), 0.45f, 65, false },
			{ TEXT("kart.wind"), TEXT("kart_wind"), 0.2f, 0, true },
			{ TEXT("kart.crash-light"), TEXT("kart_crash_light"), 0.4f, 90, false },
			{ TEXT("kart.crash-medium"), TEXT("kart_crash_medium"), 0.5f, 90, false },
			{ TEXT("kart.crash-heavy"), TEXT("kart_crash_heavy"), 0.62f, 90, false },
			{ TEXT("kart.drift-boost-1"), TEXT("kart_drift_boost_1"), 0.4f, 65, false },
			{ TEXT("kart.drift-boost-2"), TEXT("kart_drift_boost_2"), 0.5f, 65, false },
			{ TEXT("kart.drift-boost-3"), TEXT("kart_drift_boost_3"), 0.62f, 65, false },
			{ TEXT("kart.boost"), TEXT("kart_boost"), 0.45f, 65, false },
			{ TEXT("kart.boost-loop"), TEXT("kart_boost_loop"), 0.17f, 0, true },
			{ TEXT("kart.collision"), TEXT("kart_collision"), 0.48f, 65, false },
			{ TEXT("kart.hop"), TEXT("kart_hop"), 0.45f, 65, false },
			{ TEXT("kart.land"), TEXT("kart_land"), 0.45f, 65, false },
			{ TEXT("kart.fall"), TEXT("kart_fall"), 0.45f, 65, false },
			{ TEXT("kart.respawn"), TEXT("kart_respawn"), 0.45f, 65, false },
			{ TEXT("kart.item-pickup"), TEXT("kart_item_pickup"), 0.45f, 65, false },
			{ TEXT("kart.item-roulette"), TEXT("kart_item_roulette"), 0.16f, 0, true },
			{ TEXT("kart.item-ready"), TEXT("kart_item_ready"), 0.45f, 65, false },
			{ TEXT("kart.projectile-launch"), TEXT("kart_projectile_launch"), 0.45f, 65, false },
			{ TEXT("kart.projectile-bounce"), TEXT("kart_projectile_bounce"), 0.45f, 65, false },
			{ TEXT("kart.projectile-hit"), TEXT("kart_projectile_hit"), 0.45f, 65, false },
			{ TEXT("kart.hazard-drop"), TEXT("kart_hazard_drop"), 0.45f, 65, false },
			{ TEXT("kart.spinout"), TEXT("kart_spinout"), 0.45f, 65, false },
			{ TEXT("kart.explosion"), TEXT("kart_explosion"), 0.65f, 65, false },
			{ TEXT("kart.lightning"), TEXT("kart_lightning"), 0.45f, 65, false },
			{ TEXT("kart.ink"), TEXT("kart_ink"), 0.45f, 65, false },
			{ TEXT("kart.invincible-start"), TEXT("kart_invincible_start"), 0.45f, 65, false },
			{ TEXT("kart.invincible-loop"), TEXT("kart_invincible_loop"), 0.12f, 0, true },
			{ TEXT("kart.rocket-loop"), TEXT("kart_rocket_loop"), 0.2f, 0, true },
			{ TEXT("kart.warning"), TEXT("kart_warning"), 0.34f, 850, false },
			{ TEXT("kart.lap"), TEXT("kart_lap"), 0.45f, 65, false },
			{ TEXT("kart.final-lap"), TEXT("kart_final_lap"), 0.45f, 65, false },
			{ TEXT("kart.finish"), TEXT("kart_finish"), 0.58f, 65, false },
			{ TEXT("lobby.tick"), TEXT("lobby_tick"), 0.5f, 100, false },
			{ TEXT("lobby.go"), TEXT("lobby_go"), 0.5f, 100, false },
		};
		return Cues;
	}

	const FCueDef* FindCue(FName Id)
	{
		static const TMap<FName, const FCueDef*> ById = []()
		{
			TMap<FName, const FCueDef*> Map;
			for (const FCueDef& Cue : AllCues())
			{
				Map.Add(FName(Cue.Id), &Cue);
			}
			return Map;
		}();
		const FCueDef* const* Found = ById.Find(Id);
		return Found ? *Found : nullptr;
	}

	bool FCueThrottle::TryPlay(const FCueDef& Cue, double Now)
	{
		if (const double* Last = LastPlayed.Find(&Cue))
		{
			if ((Now - *Last) * 1000.0 < Cue.ThrottleMs)
			{
				return false;
			}
		}
		LastPlayed.Add(&Cue, Now);
		return true;
	}
}

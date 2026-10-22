#include "Sim/KartSim.h"

#include "Sim/KartSimMath.h"

// Earned boosts: the drift-release grades, the rocket start and the slipstream draft
// (legacy performanceQuality.ts). The values are the web build's, unchanged.

namespace MovecadeKart
{
	namespace
	{
		struct FBoostGrade
		{
			double Multiplier;
			double Seconds;
		};

		// ---- Drift release (getRaceDriftExitBoostFeedback). Thresholds are drift power (s) and held time (ms). ----
		constexpr double DriftMinPower = 0.35;
		constexpr double DriftMinMs = 650.0;
		constexpr double DriftSuperPower = 0.65;
		constexpr double DriftSuperMs = 1150.0;
		constexpr double DriftUltraPower = 0.9;
		constexpr double DriftUltraMs = 1800.0;
		constexpr double DriftApexPower = 0.96;
		constexpr double DriftApexMs = 2200.0;
		/** The apex grade wants a firm but not maxed-out steer into the drift at release. */
		constexpr double DriftApexMinSteer = 0.62;
		constexpr double DriftApexMaxSteer = 0.9;
		/** Below this the drift has no side, so steering can't grade it. */
		constexpr double DriftMinDirection = 0.01;
		constexpr FBoostGrade DriftMini = { 1.18, 0.85 };
		constexpr FBoostGrade DriftSuper = { 1.35, 1.25 };
		constexpr FBoostGrade DriftUltra = { 1.55, 1.6 };
		constexpr FBoostGrade DriftApex = { 1.7, 1.88 };
		/** A clean (steered-into) Super or Ultra release adds this, rounded to hundredths. */
		constexpr FBoostGrade DriftCleanBonus = { 0.07, 0.13 };

		// ---- Rocket start: how long the gas was held before GO (ms) ----
		constexpr double RocketMinHeldMs = 500.0;
		constexpr double RocketPerfectFromMs = 800.0;
		constexpr double RocketPerfectToMs = 1200.0;
		constexpr double RocketBoggedAfterMs = 2000.0;
		constexpr FBoostGrade RocketBogged = { 0.82, 0.75 };
		constexpr FBoostGrade RocketPerfect = { 1.6, 1.75 };
		constexpr FBoostGrade RocketClean = { 1.35, 1.25 };
		constexpr FBoostGrade RocketLate = { 1.25, 1.1 };

		// ---- Slipstream (performanceQuality.ts:867-983). Speeds are web SU, scaled by SlipstreamScale (B4). ----
		constexpr double SlipMinDistance = 4.5;
		constexpr double SlipMaxDistance = 28.0;
		/** The distance score falls from 1 at 6 m to 0 at SlipMaxDistance. */
		constexpr double SlipDistanceScoreRange = 22.0;
		constexpr double SlipMinAlign = 0.72;
		constexpr double SlipAlignScoreRange = 0.28;
		constexpr double SlipMinSpeed = 70.0;
		/** Closer than this the draft only works as a "bump" draft, dead behind and fast. */
		constexpr double SlipBumpDistance = 6.0;
		constexpr double SlipBumpAlign = 0.96;
		constexpr double SlipBumpSpeed = 105.0;
		constexpr double SlipPocketDistance = 8.5;
		constexpr double SlipPocketAlign = 0.97;
		constexpr double SlipPocketSpeed = 130.0;
		constexpr double SlipStrongDistance = 14.0;
		constexpr double SlipStrongAlign = 0.9;
		constexpr double SlipStrongSpeed = 110.0;
		/** A kart this far behind the leader (LeaderGapRatio) gets the comeback draft. */
		constexpr double SlipComebackGap = 0.65;
		constexpr double SlipChargeDecayRate = 1.15;
		/** Past this charge the boost grows by up to SlipFullChargeBonus at full charge. */
		constexpr double SlipChargeThreshold = 0.65;
		constexpr double SlipChargeRange = 0.35;
		constexpr double SlipFullChargeBonus = 0.35;

		/** One draft tier: Base + DistanceScore * Distance + AlignScore * Align + SpeedScore * Speed, charging at Rate per second. */
		struct FSlipstreamTier
		{
			double Base;
			double Distance;
			double Align;
			double Speed;
			double Rate;
		};
		constexpr FSlipstreamTier SlipPocket = { 11.5, 3.5, 1.3, 0.8, 1.15 };
		constexpr FSlipstreamTier SlipBump = { 9.1, 2.2, 1.1, 0.45, 1.35 };
		constexpr FSlipstreamTier SlipStrong = { 9.2, 3.5, 1.3, 0.5, 0.92 };
		constexpr FSlipstreamTier SlipComeback = { 4.7, 4.8, 2.4, 0.8, 1.2 };
		/** The comeback tier also scales with the gap to the leader. */
		constexpr double SlipComebackGapWeight = 1.45;
		constexpr FSlipstreamTier SlipBasic = { 2.9, 5.0, 3.0, 0.5, 0.55 };

		void SetBoost(FDriftBoost& Out, const FBoostGrade& Grade, const TCHAR* Label)
		{
			Out = { Grade.Multiplier, Grade.Seconds, Label };
		}

		/** Hundredths, as the web's Math.round(x * 100) / 100. */
		double RoundToHundredths(double Value)
		{
			return FMath::RoundToDouble(Value * 100.0) / 100.0;
		}
	}

	bool DriftExitBoost(double DriftPower, double DriftDurationMs, double SteeringInput, double DriftDirection, FDriftBoost& Out)
	{
		if (!FMath::IsFinite(DriftPower) || !FMath::IsFinite(DriftDurationMs) || DriftPower < DriftMinPower || DriftDurationMs < DriftMinMs)
		{
			return false;
		}

		enum class EGrade { Mini, Super, Ultra };
		EGrade Grade = EGrade::Mini;
		SetBoost(Out, DriftMini, TEXT("DRIFT_MINI"));
		if (DriftPower >= DriftUltraPower && DriftDurationMs >= DriftUltraMs)
		{
			Grade = EGrade::Ultra;
			SetBoost(Out, DriftUltra, TEXT("DRIFT_ULTRA"));
		}
		else if (DriftPower >= DriftSuperPower && DriftDurationMs >= DriftSuperMs)
		{
			Grade = EGrade::Super;
			SetBoost(Out, DriftSuper, TEXT("DRIFT_SUPER"));
		}

		if (!FMath::IsFinite(SteeringInput) || !FMath::IsFinite(DriftDirection) || FMath::Abs(DriftDirection) < DriftMinDirection)
		{
			return true;
		}

		const double Alignment = FMath::Sign(SteeringInput) * FMath::Sign(DriftDirection);
		if (Alignment < 0.0)
		{
			// Counter-steered at release: drop a grade.
			if (Grade == EGrade::Ultra)
			{
				SetBoost(Out, DriftSuper, TEXT("DRIFT_SLOPPY_SUPER"));
			}
			else if (Grade == EGrade::Super)
			{
				SetBoost(Out, DriftMini, TEXT("DRIFT_SLOPPY_MINI"));
			}
			return true;
		}

		const double Magnitude = FMath::Abs(SteeringInput);
		if (Alignment > 0.0 && Grade == EGrade::Ultra && DriftPower >= DriftApexPower && DriftDurationMs >= DriftApexMs
			&& Magnitude >= DriftApexMinSteer && Magnitude <= DriftApexMaxSteer)
		{
			SetBoost(Out, DriftApex, TEXT("DRIFT_APEX_ULTRA"));
			return true;
		}
		if (Alignment > 0.0 && Grade != EGrade::Mini)
		{
			Out.Multiplier = RoundToHundredths(Out.Multiplier + DriftCleanBonus.Multiplier);
			Out.Time = RoundToHundredths(Out.Time + DriftCleanBonus.Seconds);
			Out.Label = Grade == EGrade::Ultra ? TEXT("DRIFT_CLEAN_ULTRA") : TEXT("DRIFT_CLEAN_SUPER");
		}
		return true;
	}

	bool RocketStartBoost(double HeldMs, FDriftBoost& Out)
	{
		if (!FMath::IsFinite(HeldMs) || HeldMs < RocketMinHeldMs)
		{
			return false;
		}
		if (HeldMs > RocketBoggedAfterMs)
		{
			SetBoost(Out, RocketBogged, TEXT("START_BOGGED"));
		}
		else if (HeldMs >= RocketPerfectFromMs && HeldMs <= RocketPerfectToMs)
		{
			SetBoost(Out, RocketPerfect, TEXT("START_PERFECT"));
		}
		else if (HeldMs < RocketPerfectFromMs)
		{
			SetBoost(Out, RocketClean, TEXT("START_CLEAN"));
		}
		else
		{
			SetBoost(Out, RocketLate, TEXT("START_LATE"));
		}
		return true;
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
			const FVector2D ToRival = Race.AheadPosition - Kart.Pos;
			const double Distance = ToRival.Size();
			const double AlignDot = Distance > 0.0 ? FVector2D::DotProduct(ToRival / Distance, SimMath::Forward(Kart.Yaw)) : 0.0;
			const double Speed = Kart.Speed;
			const bool bBump = Distance < SlipBumpDistance && AlignDot >= SlipBumpAlign && Speed >= SlipBumpSpeed * K;
			bActive = !(Distance < SlipMinDistance || Distance > SlipMaxDistance || AlignDot < SlipMinAlign || Speed < SlipMinSpeed * K)
				&& !(Distance < SlipBumpDistance && !bBump);
			if (bActive)
			{
				const double DistanceScore = FMath::Clamp((SlipMaxDistance - Distance) / SlipDistanceScoreRange, 0.0, 1.0);
				const double AlignScore = FMath::Clamp((AlignDot - SlipMinAlign) / SlipAlignScoreRange, 0.0, 1.0);
				const double SpeedScore = FMath::Clamp((Speed - SlipMinSpeed * K) / (SlipMinSpeed * K), 0.0, 1.0);
				const bool bPocket = !bBump && Distance <= SlipPocketDistance && AlignDot >= SlipPocketAlign && Speed >= SlipPocketSpeed * K;
				const bool bStrong = Distance <= SlipStrongDistance && AlignDot >= SlipStrongAlign && Speed >= SlipStrongSpeed * K;
				const bool bComeback = !bBump && !bPocket && !bStrong && Race.LeaderGapRatio >= SlipComebackGap;
				const FSlipstreamTier& Tier = bPocket ? SlipPocket : bBump ? SlipBump : bStrong ? SlipStrong : bComeback ? SlipComeback : SlipBasic;
				Boost = Tier.Base + DistanceScore * Tier.Distance + AlignScore * Tier.Align + SpeedScore * Tier.Speed;
				if (bComeback)
				{
					Boost += Race.LeaderGapRatio * SlipComebackGapWeight;
				}
				Rate = Tier.Rate;
				Boost *= K;
			}
		}
		Kart.SlipstreamCharge = bActive
			? FMath::Min(1.0, Kart.SlipstreamCharge + Dt * Rate)
			: FMath::Max(0.0, Kart.SlipstreamCharge - Dt * SlipChargeDecayRate);
		if (!bActive)
		{
			return 0.0;
		}
		return Kart.SlipstreamCharge < SlipChargeThreshold
			? Boost
			: Boost * (1.0 + ((Kart.SlipstreamCharge - SlipChargeThreshold) / SlipChargeRange) * SlipFullChargeBonus);
	}
}

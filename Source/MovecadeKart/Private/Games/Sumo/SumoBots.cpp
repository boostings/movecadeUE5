// Sumo Bumpers' house players: MakeBot and ThinkBot (declared in SumoRules.h), pure and deterministic like the rules.

#include "Games/Sumo/SumoRules.h"

#include "Games/Sumo/SumoRulesMath.h"

namespace MovecadeKart::Sumo
{
	namespace
	{
		/**
		 * The house bots' tuning. S is the bot's skill (0 sloppy .. 1 sharp); most terms are "Base + PerSkill * S",
		 * or scaled by (1 - S) for the sloppiness. Times are ms, distances m.
		 */
		namespace Bot
		{
			constexpr uint32 DefaultSeed = 0x1234567u;
			/** The first dash decision comes this long into the fight (plus up to ThinkJitter more). */
			constexpr double FirstThinkMs = 200.0;
			constexpr double FirstThinkJitterMs = 400.0;
			/** A sharp bot already treats a warned band as gone, less this margin. */
			constexpr double WarnedBandMargin = 0.3;
			// Picking a mark: the nearest, discounted toward those near the edge, blurred for a sloppy bot.
			constexpr double TargetEdgeWeight = 4.0;
			constexpr double TargetNoise = 2.0;
			constexpr double RetargetMs = 900.0;
			constexpr double RetargetJitterMs = 900.0;
			/** With nothing better to do, drift toward the middle. */
			constexpr double CentrePull = 0.2;
			// Lining up: lead the mark, then get between it and the centre (Behind: the cosine needed to push).
			constexpr double LeadSeconds = 0.15;
			constexpr double LeadSecondsPerSkill = 0.2;
			constexpr double CentredMark = 1.2;
			constexpr double LinedUpCos = 0.55;
			constexpr double LinedUpSloppyCos = 0.25;
			constexpr double BehindSpotRadii = 2.4;
			constexpr double ToSpotWeight = 0.7;
			constexpr double AroundWeight = 0.5;
			// Power-ups worth a detour: near enough, nearer than the mark, inside the ring that will be left.
			constexpr double PowerGreedMinSkill = 0.25;
			constexpr double PowerReachPerSkill = 4.5;
			constexpr double PowerReach = 1.0;
			constexpr double PowerNearerThanMark = 1.1;
			constexpr double NoMarkDistance = 99.0;
			constexpr double PowerSafeInset = 1.0;
			constexpr double PowerPull = 1.2;
			// Edge sense: look ahead along the momentum, keep a margin (narrower when sharp), steer back in.
			constexpr double LookAheadSeconds = 0.3;
			constexpr double LookAheadPerSkill = 0.3;
			constexpr double EdgeMargin = 2.2;
			constexpr double EdgeMarginPerSkill = 0.6;
			constexpr double EdgeRamp = 1.2;
			constexpr double EdgeWeight = 1.5;
			constexpr double EdgeWeightPerSkill = 1.5;
			constexpr double OutwardWeight = 0.12;
			constexpr double EdgeOverrideMax = 0.8;
			constexpr double EdgeOverrideShare = 0.4;
			constexpr double DangerShare = 0.7;
			// Dodging a dash coming our way (only a sharp bot sees it in time).
			constexpr double DodgeRange = 4.0;
			constexpr double DodgeHeadingCos = 0.8;
			constexpr double DodgeChancePerSkill = 0.25;
			constexpr double DodgeWeight = 1.5;
			// A sloppy hand wanders; even a sharp one never quite holds full tilt.
			constexpr double WanderStep = 0.25;
			constexpr double WanderWeight = 0.75;
			constexpr double TiltBase = 0.8;
			constexpr double TiltPerSkill = 0.2;
			// Dash decisions, faster when sharp.
			constexpr double ThinkMs = 200.0;
			constexpr double ThinkMsPerSkill = 140.0;
			constexpr double ThinkJitterMs = 100.0;
			constexpr double MovingSpeedSq = 1.0;
			// The save: near the edge and heading out, dash straight back.
			constexpr double SaveEdgeInset = 1.2;
			constexpr double SaveOutwardSpeed = 1.5;
			constexpr double SaveChance = 0.25;
			constexpr double SaveChancePerSkill = 0.65;
			// The attack: in range, moving the right way, not dashing off the ring.
			constexpr double AttackAlignedCos = 0.75;
			constexpr double AttackSloppyCos = 0.3;
			constexpr double AttackLanding = 3.2;
			constexpr double AttackSafeInset = 0.4;
			constexpr double AttackRange = 4.4;
			/** Sloppy bots dash without lining up. */
			constexpr double Reckless = 0.45;
			constexpr double AttackChance = 0.5;
			constexpr double AttackChancePerSkill = 0.5;
		}
	}

	FBotBrain MakeBot(double Skill, uint32 Seed)
	{
		FBotBrain B;
		B.Skill = FMath::Clamp(Skill, 0.0, 1.0);
		B.Rng = Seed ? Seed : Bot::DefaultSeed;
		B.ThinkMs = Bot::FirstThinkMs + Bot::FirstThinkJitterMs * Random01(B.Rng);
		return B;
	}

	namespace
	{
		/** What a bot has worked out so far this step; each stage below refines it, in order. */
		struct FBotPlan
		{
			const FPuck* Target = nullptr;
			FVector2D Want = FVector2D::ZeroVector;
			FVector2D DashDir = FVector2D::ZeroVector;
			bool bLinedUp = false;
			bool bDanger = false;
		};

		/** The mark: kept until it falls or it's time to look again, then whoever is nearest, weighted toward the edge. */
		const FPuck* PickTarget(const FMatch& M, int32 Self, const FPuck& P, FBotBrain& B)
		{
			const double S = B.Skill;
			const FPuck* Target = M.Pucks.IsValidIndex(B.Target) && M.Pucks[B.Target].bStanding && B.Target != Self ? &M.Pucks[B.Target] : nullptr;
			if (Target && B.RetargetMs > 0.0)
			{
				return Target;
			}
			double Best = TNumericLimits<double>::Max();
			B.Target = INDEX_NONE;
			for (const FPuck& Q : M.Pucks)
			{
				if (Q.Slot == Self || !Q.bStanding)
				{
					continue;
				}
				const double Edge = Q.Pos.Size() / FMath::Max(1.0, M.Radius);
				const double Cost = FVector2D::Distance(P.Pos, Q.Pos) - Bot::TargetEdgeWeight * Edge * S + Bot::TargetNoise * Random01(B.Rng) * (1.0 - S);
				if (Cost < Best)
				{
					Best = Cost;
					B.Target = Q.Slot;
				}
			}
			B.RetargetMs = Bot::RetargetMs + Bot::RetargetJitterMs * Random01(B.Rng);
			return B.Target != INDEX_NONE ? &M.Pucks[B.Target] : nullptr;
		}

		/** Lead the mark a little, then get between it and the centre: a push from there goes out of the ring. */
		void LineUp(const FPuck& P, double S, FBotPlan& Plan)
		{
			const FPuck& Target = *Plan.Target;
			const FVector2D T = Target.Pos + Target.Vel * (Bot::LeadSeconds + Bot::LeadSecondsPerSkill * S);
			const FVector2D Out = T.Size() > Bot::CentredMark ? SafeNormal(T) : SafeNormal(T - P.Pos);
			const FVector2D ToT = T - P.Pos;
			const FVector2D Dir = SafeNormal(ToT);
			const double Behind = FVector2D::DotProduct(Dir, Out);
			if (Behind > Bot::LinedUpCos - Bot::LinedUpSloppyCos * (1.0 - S))
			{
				Plan.Want = Dir;
				Plan.bLinedUp = true;
			}
			else
			{
				const FVector2D Spot = T - Out * (PuckRadius * Bot::BehindSpotRadii);
				// Round the target rather than through it: slide sideways toward the spot behind.
				const FVector2D ToSpot = Spot - P.Pos;
				const FVector2D Side = Perp(Dir) * (FVector2D::DotProduct(Perp(Dir), ToSpot) >= 0.0 ? 1.0 : -1.0);
				Plan.Want = SafeNormal(ToSpot) * Bot::ToSpotWeight + Side * Bot::AroundWeight * S;
			}
			Plan.DashDir = Dir;
		}

		/** A power-up within reach and on the way is worth a detour. */
		void SeekPowerUp(const FMatch& M, const FPuck& P, double S, double Safe, FBotPlan& Plan)
		{
			if (S <= Bot::PowerGreedMinSkill)
			{
				return;
			}
			for (const FPowerUp& Up : M.PowerUps)
			{
				const double D = FVector2D::Distance(Up.Pos, P.Pos);
				const double ToTarget = Plan.Target ? FVector2D::Distance(Plan.Target->Pos, P.Pos) : Bot::NoMarkDistance;
				if (D < Bot::PowerReachPerSkill * S + Bot::PowerReach && D < ToTarget * Bot::PowerNearerThanMark && Up.Pos.Size() < Safe - Bot::PowerSafeInset)
				{
					Plan.Want = SafeNormal(Up.Pos - P.Pos) * Bot::PowerPull;
					Plan.bLinedUp = false;
					return;
				}
			}
		}

		/** Keep off the edge: where the momentum is taking us, against the ring that will be left. */
		void KeepOffEdge(const FPuck& P, double S, double Safe, FBotPlan& Plan)
		{
			const FVector2D Ahead = P.Pos + P.Vel * (Bot::LookAheadSeconds + Bot::LookAheadPerSkill * S);
			const double Margin = Bot::EdgeMargin - Bot::EdgeMarginPerSkill * S;
			const double Over = Ahead.Size() - (Safe - Margin);
			if (Over <= 0.0)
			{
				return;
			}
			const FVector2D Inward = SafeNormal(-Ahead);
			const double Outward = FMath::Max(0.0, FVector2D::DotProduct(P.Vel, -Inward));
			const double W = FMath::Clamp(Over / Bot::EdgeRamp, 0.0, 1.0) * (Bot::EdgeWeight + Bot::EdgeWeightPerSkill * S) + Outward * Bot::OutwardWeight * S;
			Plan.Want = Plan.Want * (1.0 - FMath::Min(Bot::EdgeOverrideMax, W * Bot::EdgeOverrideShare)) + Inward * W;
			Plan.bDanger = Over > Margin * Bot::DangerShare;
		}

		/** A dash coming our way: step aside (toward the middle), if we're sharp enough to see it. */
		void DodgeDashes(const FMatch& M, int32 Self, const FPuck& P, FBotBrain& B, FBotPlan& Plan)
		{
			const double S = B.Skill;
			for (const FPuck& Q : M.Pucks)
			{
				if (Q.Slot == Self || !Q.bStanding || Q.DashMs <= 0.0)
				{
					continue;
				}
				const FVector2D Rel = P.Pos - Q.Pos;
				const double Dist = Rel.Size();
				if (Dist < Bot::DodgeRange && FVector2D::DotProduct(SafeNormal(Q.Vel), SafeNormal(Rel)) > Bot::DodgeHeadingCos && Random01(B.Rng) < S * Bot::DodgeChancePerSkill)
				{
					FVector2D Side = Perp(SafeNormal(Rel));
					if (FVector2D::DotProduct(Side, -P.Pos) < 0.0)
					{
						Side = -Side;
					}
					Plan.Want += Side * (Bot::DodgeWeight * S);
				}
			}
		}

		/** The dash, decided every so often (faster when sharp): a save off the edge first, else an attack. */
		void DecideDash(const FPuck& P, FBotBrain& B, double Safe, const FBotPlan& Plan, FInput& In)
		{
			const double S = B.Skill;
			if (B.ThinkMs > 0.0 || !P.CanDash())
			{
				return;
			}
			B.ThinkMs = Bot::ThinkMs - Bot::ThinkMsPerSkill * S + Bot::ThinkJitterMs * Random01(B.Rng);
			const FVector2D Moving = P.Vel.SizeSquared() > Bot::MovingSpeedSq ? SafeNormal(P.Vel) : In.Tilt;
			if (Plan.bDanger && P.Pos.Size() > Safe - Bot::SaveEdgeInset && FVector2D::DotProduct(P.Vel, SafeNormal(P.Pos)) > Bot::SaveOutwardSpeed
				&& Random01(B.Rng) < Bot::SaveChance + Bot::SaveChancePerSkill * S)
			{
				// Saved by the dash: straight back to the middle.
				In.Tilt = SafeNormal(-P.Pos);
				In.bDash = true;
				return;
			}
			if (!Plan.Target || Plan.DashDir.IsZero())
			{
				return;
			}
			const double Dist = FVector2D::Distance(Plan.Target->Pos, P.Pos);
			const bool bAligned = FVector2D::DotProduct(Moving, Plan.DashDir) > Bot::AttackAlignedCos - Bot::AttackSloppyCos * (1.0 - S);
			const bool bSafe = (P.Pos + Plan.DashDir * Bot::AttackLanding).Size() < Safe - Bot::AttackSafeInset || Plan.bLinedUp;
			if (Dist < Bot::AttackRange && Dist > PuckRadius * 2.0 && bAligned && bSafe && (Plan.bLinedUp || S < Bot::Reckless)
				&& Random01(B.Rng) < Bot::AttackChance + Bot::AttackChancePerSkill * S)
			{
				In.Tilt = Plan.DashDir;
				In.bDash = true;
			}
		}
	}

	FInput ThinkBot(const FMatch& M, int32 Self, FBotBrain& B)
	{
		FInput In;
		if (M.Phase != EPhase::Fight || !M.Pucks.IsValidIndex(Self) || !M.Pucks[Self].bStanding)
		{
			return In;
		}
		const FPuck& P = M.Pucks[Self];
		const double S = B.Skill;
		B.RetargetMs -= StepMs;
		B.ThinkMs -= StepMs;
		// The ring that will still be there: a warned band is already lava to a sharp bot.
		const double Safe = FMath::Lerp(M.Radius, FMath::Min(M.Radius, M.PendingRadius + Bot::WarnedBandMargin), S);

		FBotPlan Plan;
		Plan.Target = PickTarget(M, Self, P, B);
		Plan.Want = -P.Pos * Bot::CentrePull;
		if (Plan.Target)
		{
			LineUp(P, S, Plan);
		}
		SeekPowerUp(M, P, S, Safe, Plan);
		KeepOffEdge(P, S, Safe, Plan);
		DodgeDashes(M, Self, P, B, Plan);
		// A sloppy hand wanders.
		B.Wander = ClampDisc(B.Wander + FVector2D(Random01(B.Rng) - 0.5, Random01(B.Rng) - 0.5) * Bot::WanderStep);
		Plan.Want += B.Wander * (Bot::WanderWeight * (1.0 - S));
		In.Tilt = ClampDisc(SafeNormal(Plan.Want, FVector2D::ZeroVector) * (Bot::TiltBase + Bot::TiltPerSkill * S));
		DecideDash(P, B, Safe, Plan, In);
		return In;
	}
}

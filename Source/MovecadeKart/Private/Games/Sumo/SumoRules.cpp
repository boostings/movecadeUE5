#include "Games/Sumo/SumoRules.h"

#include <limits>

namespace MovecadeKart::Sumo
{
	namespace
	{
		/** Whole-step times accumulate 1000/60 with rounding: compare against schedules with this slack. */
		constexpr double TimeEpsilon = 1e-6;

		// Dashes: the direction comes from the tilt past a small lean, else the roll past a crawl, else the facing.
		constexpr double DashTiltMinSq = 0.04;
		constexpr double DashRollMinSq = 0.25;
		/** The share of the sideways drift a dash keeps (the rest is traded into the dash's line). */
		constexpr double DashAcrossKeep = 0.35;
		/** Pucks rolling faster than this (m/s, squared) turn to face their roll. */
		constexpr double FacingSpeedSq = 0.16;

		// Contacts.
		/** A shield makes its holder four times as hard to move for the contact. */
		constexpr double ShieldInverseMassScale = 0.25;
		/** A dash only shoves when it drives into the contact at least this fast (m/s along the normal). */
		constexpr double DashHitMinApproach = 2.0;
		/** The shove scales with the mass ratio, within these bounds. */
		constexpr double DashShoveRatioMin = 0.5;
		constexpr double DashShoveRatioMax = 2.0;
		/** The attacker keeps this share of the shove as recoil. */
		constexpr double DashRecoilShare = 0.25;
		/** A bump closing this fast (m/s) pops a shield even without a dash. */
		constexpr double ShieldPopSpeed = 5.0;

		// Power-ups.
		/** A power-up within this of the ring's edge (m) goes with its band. */
		constexpr double PowerEdgeMargin = 0.3;
		/** Spawns keep this far inside the ring that will be left (m), and skip a ring smaller than PowerSpawnMinReach. */
		constexpr double PowerSpawnInset = 1.6;
		constexpr double PowerSpawnMinReach = 0.8;
		/** Tries for a spot clear of every standing puck by PowerSpawnClearance (m) before taking the last one. */
		constexpr int32 PowerSpawnTries = 6;
		constexpr double PowerSpawnClearance = 2.0;

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

		FVector2D SafeNormal(const FVector2D& V, const FVector2D& Fallback = FVector2D(1.0, 0.0))
		{
			const double L = V.Size();
			return L > 1e-9 ? V / L : Fallback;
		}

		FVector2D ClampDisc(const FVector2D& V)
		{
			const double L = V.Size();
			return L > 1.0 ? V / L : V;
		}

		FVector2D Perp(const FVector2D& V)
		{
			return FVector2D(-V.Y, V.X);
		}

		FRingEvent& Emit(FMatch& M, EEventKind Kind, int32 A = INDEX_NONE, int32 B = INDEX_NONE, const FVector2D& Pos = FVector2D::ZeroVector, double Strength = 0.0)
		{
			if (M.Events.Num() >= MaxEvents)
			{
				M.Events.RemoveAt(0, M.Events.Num() - MaxEvents + 1, EAllowShrinking::No);
			}
			FRingEvent& E = M.Events.AddDefaulted_GetRef();
			E.Id = M.NextId++;
			E.Kind = Kind;
			E.A = A;
			E.B = B;
			E.Pos = Pos;
			E.Strength = Strength;
			E.Round = M.Round;
			E.AtMs = M.ElapsedMs;
			return E;
		}

		/** Spawn spots evenly round the ring, the first on the left so two players face off across the screen. */
		FVector2D SpawnAt(int32 Slot, int32 Count)
		{
			const double A = -UE_DOUBLE_HALF_PI + 2.0 * UE_DOUBLE_PI * Slot / FMath::Max(1, Count);
			return FVector2D(FMath::Cos(A), FMath::Sin(A)) * SpawnRadius;
		}

		void StartRound(FMatch& M)
		{
			M.Phase = EPhase::Countdown;
			M.PhaseMs = 0.0;
			M.RoundMs = 0.0;
			M.Radius = ArenaRadius;
			M.PendingRadius = ArenaRadius;
			M.ShrinksDone = 0;
			M.PowerUps.Reset();
			M.NextPowerMs = PowerFirstMs;
			M.RoundWinner = INDEX_NONE;
			const int32 N = M.Pucks.Num();
			for (FPuck& P : M.Pucks)
			{
				P.Pos = SpawnAt(P.Slot, N);
				P.Vel = FVector2D::ZeroVector;
				P.Facing = SafeNormal(-P.Pos);
				P.Input = FInput();
				P.bStanding = true;
				P.bFalling = false;
				P.FallMs = 0.0;
				P.bOut = false;
				P.DashMs = 0.0;
				P.DashCooldownMs = 0.0;
				P.DashQueuedMs = 0.0;
				P.Power = EPower::None;
				P.PowerMs = 0.0;
				P.LastHitBy = INDEX_NONE;
				P.LastHitAtMs = -1e9;
			}
			Emit(M, EEventKind::RoundStart);
		}

		void DoDash(FMatch& M, FPuck& P)
		{
			const FVector2D Dir = P.Input.Tilt.SizeSquared() > DashTiltMinSq ? SafeNormal(P.Input.Tilt) : (P.Vel.SizeSquared() > DashRollMinSq ? SafeNormal(P.Vel) : P.Facing);
			// The dash replaces the speed across its line and adds DashSpeed along it, so a dash away from your
			// own drift is a real escape rather than a stall.
			const double Along = FVector2D::DotProduct(P.Vel, Dir);
			const FVector2D Across = P.Vel - Dir * Along;
			P.Vel = Across * DashAcrossKeep + Dir * (FMath::Max(0.0, Along) + DashSpeed);
			if (P.Vel.Size() > MaxSpeed)
			{
				P.Vel = SafeNormal(P.Vel) * MaxSpeed;
			}
			P.Facing = Dir;
			P.DashMs = DashMs;
			P.DashCooldownMs = P.Power == EPower::Turbo ? TurboCooldownMs : DashCooldownMs;
			P.DashQueuedMs = 0.0;
			// A dash event's Strength is its heading (radians): the presentation throws its dust back along it.
			Emit(M, EEventKind::Dash, P.Slot, INDEX_NONE, P.Pos, Dir.X).Strength = FMath::Atan2(Dir.Y, Dir.X);
		}

		void ApplyPower(FMatch& M, FPuck& P, const FPowerUp& Up)
		{
			P.Power = Up.Kind;
			switch (Up.Kind)
			{
			case EPower::Heavy: P.PowerMs = HeavyMs; break;
			case EPower::Shield: P.PowerMs = ShieldMs; break;
			case EPower::Turbo:
				P.PowerMs = TurboMs;
				P.DashCooldownMs = 0.0;
				break;
			default: break;
			}
			FRingEvent& E = Emit(M, EEventKind::PowerPickup, P.Slot, INDEX_NONE, Up.Pos);
			E.Power = Up.Kind;
		}

		void StepPucks(FMatch& M, bool bControl)
		{
			const double Dt = StepSeconds;
			const double Decay = FMath::Exp(-Drag * Dt);
			for (FPuck& P : M.Pucks)
			{
				if (P.bOut)
				{
					continue;
				}
				if (P.bFalling)
				{
					P.Vel *= FMath::Exp(-FallingDrag * Dt);
					P.Pos += P.Vel * Dt;
					P.FallMs += StepMs;
					if (P.FallMs >= FallMs - TimeEpsilon)
					{
						P.bFalling = false;
						P.bOut = true;
						Emit(M, EEventKind::Splash, P.Slot, INDEX_NONE, P.Pos);
					}
					continue;
				}
				P.DashMs = FMath::Max(0.0, P.DashMs - StepMs);
				P.DashCooldownMs = FMath::Max(0.0, P.DashCooldownMs - StepMs);
				P.DashQueuedMs = FMath::Max(0.0, P.DashQueuedMs - StepMs);
				if (P.Power != EPower::None)
				{
					P.PowerMs -= StepMs;
					if (P.PowerMs <= 0.0)
					{
						FRingEvent& E = Emit(M, EEventKind::PowerExpire, P.Slot, INDEX_NONE, P.Pos);
						E.Power = P.Power;
						P.Power = EPower::None;
						P.PowerMs = 0.0;
					}
				}
				if (bControl)
				{
					const FVector2D Tilt = ClampDisc(P.Input.Tilt);
					P.Vel += Tilt * (P.Power == EPower::Heavy ? HeavyAccel : Accel) * Dt;
					if (P.Input.bDash)
					{
						P.DashQueuedMs = DashBufferMs;
					}
					if (P.DashQueuedMs > 0.0 && P.DashCooldownMs <= 0.0)
					{
						DoDash(M, P);
					}
				}
				P.Input.bDash = false;
				P.Vel *= Decay;
				if (P.Vel.SizeSquared() > MaxSpeed * MaxSpeed)
				{
					P.Vel = SafeNormal(P.Vel) * MaxSpeed;
				}
				P.Pos += P.Vel * Dt;
				if (P.Vel.SizeSquared() > FacingSpeedSq)
				{
					P.Facing = SafeNormal(P.Vel);
				}
			}
			for (int32 i = 0; i < M.Pucks.Num(); ++i)
			{
				for (int32 j = i + 1; j < M.Pucks.Num(); ++j)
				{
					ResolveContact(M, i, j);
				}
			}
		}

		void RingOuts(FMatch& M)
		{
			for (FPuck& P : M.Pucks)
			{
				if (!P.bStanding || P.Pos.Size() <= M.Radius)
				{
					continue;
				}
				P.bStanding = false;
				P.bFalling = true;
				P.FallMs = 0.0;
				P.DashMs = 0.0;
				P.Falls += 1;
				int32 Killer = INDEX_NONE;
				if (M.Pucks.IsValidIndex(P.LastHitBy) && P.LastHitBy != P.Slot && M.RoundMs - P.LastHitAtMs <= KoCreditMs)
				{
					Killer = P.LastHitBy;
					M.Pucks[Killer].Knockouts += 1;
					M.Pucks[Killer].Score += KnockoutPoints;
				}
				Emit(M, EEventKind::RingOut, P.Slot, Killer, P.Pos, P.Vel.Size());
			}
		}

		void StepRing(FMatch& M)
		{
			const double At = ShrinkAtMs(M.ShrinksDone);
			if (!M.IsShrinkPending() && M.RoundMs >= At - ShrinkWarningMs - TimeEpsilon)
			{
				M.PendingRadius = RadiusAfter(M.ShrinksDone + 1);
				Emit(M, EEventKind::ShrinkWarning, INDEX_NONE, INDEX_NONE, FVector2D::ZeroVector, M.PendingRadius);
			}
			if (M.RoundMs >= At - TimeEpsilon)
			{
				M.ShrinksDone += 1;
				M.Radius = RadiusAfter(M.ShrinksDone);
				M.PendingRadius = M.Radius;
				Emit(M, EEventKind::Shrink, INDEX_NONE, INDEX_NONE, FVector2D::ZeroVector, M.Radius);
			}
		}

		void StepPowerUps(FMatch& M)
		{
			for (int32 k = M.PowerUps.Num() - 1; k >= 0; --k)
			{
				FPowerUp& Up = M.PowerUps[k];
				Up.AgeMs += StepMs;
				// A power-up goes with its band, or when it has waited too long.
				if (Up.AgeMs >= PowerLifeMs || Up.Pos.Size() > M.Radius - PowerEdgeMargin)
				{
					FRingEvent& E = Emit(M, EEventKind::PowerExpire, INDEX_NONE, INDEX_NONE, Up.Pos);
					E.Power = Up.Kind;
					M.PowerUps.RemoveAt(k);
					continue;
				}
				for (FPuck& P : M.Pucks)
				{
					if (P.bStanding && FVector2D::Distance(P.Pos, Up.Pos) <= PowerPickupReach)
					{
						const FPowerUp Taken = Up;
						M.PowerUps.RemoveAt(k);
						ApplyPower(M, P, Taken);
						break;
					}
				}
			}
			if (M.RoundMs < M.NextPowerMs - TimeEpsilon)
			{
				return;
			}
			M.NextPowerMs = M.RoundMs + FMath::Lerp(PowerEveryMinMs, PowerEveryMaxMs, Random01(M.Rng));
			const double Reach = FMath::Min(M.PendingRadius, M.Radius) - PowerSpawnInset;
			if (M.PowerUps.Num() >= MaxPowerUps || Reach < PowerSpawnMinReach)
			{
				return;
			}
			// A spot inside the ring that will stay, clear of the pucks (a few tries, then anywhere).
			FVector2D Spot = FVector2D::ZeroVector;
			for (int32 Try = 0; Try < PowerSpawnTries; ++Try)
			{
				const double R = FMath::Sqrt(Random01(M.Rng)) * Reach;
				const double A = Random01(M.Rng) * 2.0 * UE_DOUBLE_PI;
				Spot = FVector2D(FMath::Cos(A), FMath::Sin(A)) * R;
				bool bClear = true;
				for (const FPuck& P : M.Pucks)
				{
					bClear &= !P.bStanding || FVector2D::Distance(P.Pos, Spot) > PowerSpawnClearance;
				}
				if (bClear)
				{
					break;
				}
			}
			FPowerUp& Up = M.PowerUps.AddDefaulted_GetRef();
			Up.Id = M.NextId++;
			Up.Kind = static_cast<EPower>(1 + NextRandom(M.Rng) % 3);
			Up.Pos = Spot;
			FRingEvent& E = Emit(M, EEventKind::PowerSpawn, INDEX_NONE, INDEX_NONE, Spot);
			E.Power = Up.Kind;
		}

		void CheckRoundEnd(FMatch& M)
		{
			const int32 Standing = M.StandingCount();
			const bool bOver = M.Pucks.Num() > 1 ? Standing <= 1 : Standing == 0;
			if (!bOver)
			{
				return;
			}
			int32 Winner = INDEX_NONE;
			if (M.Pucks.Num() > 1 && Standing == 1)
			{
				Winner = M.Pucks.IndexOfByPredicate([](const FPuck& P) { return P.bStanding; });
			}
			M.Phase = EPhase::RoundOver;
			M.PhaseMs = 0.0;
			M.RoundsPlayed += 1;
			M.RoundWinner = Winner;
			M.RoundWinners.Add(Winner);
			if (Winner != INDEX_NONE)
			{
				FPuck& W = M.Pucks[Winner];
				W.RoundWins += 1;
				W.Score += RoundWinPoints;
				W.DashMs = 0.0;
				Emit(M, EEventKind::RoundWin, Winner, INDEX_NONE, W.Pos, W.RoundWins);
			}
			else
			{
				Emit(M, EEventKind::RoundDraw);
			}
		}

		void FinishRound(FMatch& M)
		{
			bool bDecided = M.RoundsPlayed >= MaxRounds;
			for (const FPuck& P : M.Pucks)
			{
				bDecided |= P.RoundWins >= WinsForMatch;
			}
			if (!bDecided)
			{
				M.Round += 1;
				StartRound(M);
				return;
			}
			M.Phase = EPhase::MatchOver;
			M.PhaseMs = 0.0;
			const TArray<int32> Order = Standings(M);
			M.MatchWinner = Order.Num() > 0 ? Order[0] : INDEX_NONE;
			Emit(M, EEventKind::MatchOver, M.MatchWinner);
		}

		void Mix(uint64& H, uint64 V)
		{
			H ^= V;
			H *= 1099511628211ull;
		}

		void MixD(uint64& H, double V)
		{
			uint64 Bits = 0;
			FMemory::Memcpy(&Bits, &V, sizeof(Bits));
			Mix(H, Bits);
		}
	}

	uint32 NextRandom(uint32& State)
	{
		uint32 X = State ? State : 0x9e3779b9u;
		X ^= X << 13;
		X ^= X >> 17;
		X ^= X << 5;
		State = X;
		return X;
	}

	double Random01(uint32& State)
	{
		return NextRandom(State) / 4294967296.0;
	}

	int32 FMatch::StandingCount() const
	{
		int32 N = 0;
		for (const FPuck& P : Pucks)
		{
			N += P.bStanding ? 1 : 0;
		}
		return N;
	}

	const FPuck* FMatch::Find(const FString& Id) const
	{
		return Pucks.FindByPredicate([&Id](const FPuck& P) { return P.Id == Id; });
	}

	int32 FMatch::IndexOf(const FString& Id) const
	{
		return Pucks.IndexOfByPredicate([&Id](const FPuck& P) { return P.Id == Id; });
	}

	double ShrinkAtMs(int32 Done)
	{
		if (Done < RingBands)
		{
			return FirstShrinkMs + Done * ShrinkEveryMs;
		}
		return Done == RingBands ? CollapseMs : std::numeric_limits<double>::infinity();
	}

	double RadiusAfter(int32 Done)
	{
		return Done <= RingBands ? ArenaRadius - Done * BandWidth : 0.0;
	}

	FMatch CreateMatch(const TArray<FSeed>& Players, uint32 Seed)
	{
		FMatch M;
		M.Rng = Seed ? Seed : 0x5badf00du;
		for (int32 i = 0; i < FMath::Min(Players.Num(), MaxPlayers); ++i)
		{
			FPuck& P = M.Pucks.AddDefaulted_GetRef();
			P.Id = Players[i].Id;
			P.Name = Players[i].Name;
			P.Slot = i;
		}
		StartRound(M);
		return M;
	}

	void SetInput(FMatch& Match, int32 Index, const FInput& Input)
	{
		if (!Match.Pucks.IsValidIndex(Index))
		{
			return;
		}
		FPuck& P = Match.Pucks[Index];
		const bool bDash = P.Input.bDash || Input.bDash;
		P.Input.Tilt = ClampDisc(Input.Tilt);
		P.Input.bDash = bDash;
	}

	void ResolveContact(FMatch& M, int32 Ai, int32 Bi)
	{
		FPuck& A = M.Pucks[Ai];
		FPuck& B = M.Pucks[Bi];
		if (!A.bStanding || !B.bStanding)
		{
			return;
		}
		const FVector2D D = B.Pos - A.Pos;
		const double Dist = D.Size();
		const double MinDist = PuckRadius * 2.0;
		if (Dist >= MinDist)
		{
			return;
		}
		const FVector2D N = SafeNormal(D, SafeNormal(B.Facing - A.Facing, FVector2D(0.0, 1.0)));
		// A shield makes its holder hard to move for this contact.
		const double InvA = (1.0 / A.Mass()) * (A.HasShield() ? ShieldInverseMassScale : 1.0);
		const double InvB = (1.0 / B.Mass()) * (B.HasShield() ? ShieldInverseMassScale : 1.0);
		const double Pen = MinDist - Dist;
		A.Pos -= N * (Pen * InvA / (InvA + InvB));
		B.Pos += N * (Pen * InvB / (InvA + InvB));
		const double Closing = -FVector2D::DotProduct(B.Vel - A.Vel, N);
		if (Closing <= 0.0)
		{
			return;
		}
		const bool bShield = A.HasShield() || B.HasShield();
		const double E = bShield ? ShieldRestitution : Restitution;
		const double J = (1.0 + E) * Closing / (InvA + InvB);
		const double AlongA = FVector2D::DotProduct(A.Vel, N);
		const double AlongB = -FVector2D::DotProduct(B.Vel, N);
		A.Vel -= N * (J * InvA);
		B.Vel += N * (J * InvB);
		// A dash that drives into someone shoves them on (the attacker keeps a little of its own push).
		bool bDashHit = false;
		const double Ratio = FMath::Clamp(A.Mass() / B.Mass(), DashShoveRatioMin, DashShoveRatioMax);
		if (A.DashMs > 0.0 && AlongA > DashHitMinApproach && !B.HasShield())
		{
			B.Vel += N * (DashShove * Ratio);
			A.Vel -= N * (DashShove * DashRecoilShare);
			bDashHit = true;
		}
		if (B.DashMs > 0.0 && AlongB > DashHitMinApproach && !A.HasShield())
		{
			A.Vel -= N * (DashShove / Ratio);
			B.Vel += N * (DashShove * DashRecoilShare);
			bDashHit = true;
		}
		const double Now = M.RoundMs;
		A.LastHitBy = Bi;
		A.LastHitAtMs = Now;
		B.LastHitBy = Ai;
		B.LastHitAtMs = Now;
		const FVector2D At = A.Pos + N * PuckRadius;
		// A shield pops on a real hit (a dash, or a hard bump) and the attacker bounces off it.
		const bool bHard = bDashHit || Closing >= ShieldPopSpeed || A.DashMs > 0.0 || B.DashMs > 0.0;
		if (bShield && bHard)
		{
			FPuck& Holder = A.HasShield() ? A : B;
			FPuck& Other = A.HasShield() ? B : A;
			Holder.Power = EPower::None;
			Holder.PowerMs = 0.0;
			Other.DashMs = 0.0;
			FRingEvent& Ev = Emit(M, EEventKind::ShieldBlock, Holder.Slot, Other.Slot, At, Closing);
			Ev.Power = EPower::Shield;
			return;
		}
		if (bDashHit || Closing >= BumpEventSpeed)
		{
			Emit(M, bDashHit ? EEventKind::DashHit : EEventKind::Bump, Ai, Bi, At, Closing);
		}
	}

	void StepMatch(FMatch& M)
	{
		M.Steps += 1;
		M.ElapsedMs += StepMs;
		M.PhaseMs += StepMs;
		switch (M.Phase)
		{
		case EPhase::Countdown:
			for (FPuck& P : M.Pucks)
			{
				P.Input.bDash = false;
			}
			if (M.PhaseMs >= CountdownMs - TimeEpsilon)
			{
				M.Phase = EPhase::Fight;
				M.PhaseMs = 0.0;
				Emit(M, EEventKind::Fight);
			}
			break;
		case EPhase::Fight:
			M.RoundMs += StepMs;
			StepPucks(M, true);
			RingOuts(M);
			StepRing(M);
			StepPowerUps(M);
			CheckRoundEnd(M);
			break;
		case EPhase::RoundOver:
			// The winner coasts to a stop and can't fall now; the losers finish their fall.
			StepPucks(M, false);
			if (M.PhaseMs >= RoundOverMs - TimeEpsilon)
			{
				FinishRound(M);
			}
			break;
		case EPhase::MatchOver:
			StepPucks(M, false);
			break;
		}
	}

	TArray<int32> Standings(const FMatch& M)
	{
		TArray<int32> Order;
		for (int32 i = 0; i < M.Pucks.Num(); ++i)
		{
			Order.Add(i);
		}
		Order.StableSort([&M](int32 A, int32 B)
		{
			const FPuck& Pa = M.Pucks[A];
			const FPuck& Pb = M.Pucks[B];
			if (Pa.RoundWins != Pb.RoundWins) return Pa.RoundWins > Pb.RoundWins;
			if (Pa.Score != Pb.Score) return Pa.Score > Pb.Score;
			if (Pa.Knockouts != Pb.Knockouts) return Pa.Knockouts > Pb.Knockouts;
			return Pa.Falls < Pb.Falls;
		});
		return Order;
	}

	int64 StandingKey(const FPuck& P)
	{
		auto Field = [](int32 Value, int32 Max) { return static_cast<int64>(FMath::Clamp(Value, 0, Max)); };
		// Round wins on top (15 bits keep the key positive), then score, knockouts, and falls inverted.
		return (Field(P.RoundWins, 0x7fff) << 48) | (Field(P.Score, 0xffff) << 32) | (Field(P.Knockouts, 0xffff) << 16) | (0xffff - Field(P.Falls, 0xffff));
	}

	uint64 Fingerprint(const FMatch& M)
	{
		uint64 H = 14695981039346656037ull;
		Mix(H, static_cast<uint64>(M.Phase));
		Mix(H, M.Round);
		Mix(H, static_cast<uint64>(M.Steps));
		Mix(H, M.Rng);
		Mix(H, M.NextId);
		MixD(H, M.Radius);
		MixD(H, M.PendingRadius);
		MixD(H, M.RoundMs);
		for (const FPuck& P : M.Pucks)
		{
			MixD(H, P.Pos.X);
			MixD(H, P.Pos.Y);
			MixD(H, P.Vel.X);
			MixD(H, P.Vel.Y);
			MixD(H, P.DashCooldownMs);
			MixD(H, P.PowerMs);
			Mix(H, static_cast<uint64>(P.Power));
			Mix(H, (P.bStanding ? 1 : 0) | (P.bFalling ? 2 : 0) | (P.bOut ? 4 : 0));
			Mix(H, P.RoundWins * 1000 + P.Knockouts * 10 + P.Score);
		}
		for (const FPowerUp& Up : M.PowerUps)
		{
			Mix(H, Up.Id);
			MixD(H, Up.Pos.X);
			MixD(H, Up.Pos.Y);
		}
		return H;
	}

	// ----------------------------------------------------------------------------------------------------- Bots

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

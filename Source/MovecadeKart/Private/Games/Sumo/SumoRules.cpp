// Sumo Bumpers' rules (SumoRules.h): the match, the pucks' physics and contacts, the ring, power-ups and scoring.
// The house bots live in SumoBots.cpp.

#include "Games/Sumo/SumoRules.h"

#include "Games/Sumo/SumoRulesMath.h"
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
		// ClampDisc keeps NaN (its length test is false) and turns an infinite or overflowing tilt into NaN: either
		// would spread through the velocity to every puck it touches. Such a tilt reads as none.
		const FVector2D Tilt = ClampDisc(Input.Tilt);
		P.Input.Tilt = FMath::IsFinite(Tilt.X) && FMath::IsFinite(Tilt.Y) ? Tilt : FVector2D::ZeroVector;
		P.Input.bDash = bDash;
	}

	void ResolveContact(FMatch& M, int32 Ai, int32 Bi)
	{
		// Public, so the indices are checked (a puck against itself would divide its own push by zero).
		if (!M.Pucks.IsValidIndex(Ai) || !M.Pucks.IsValidIndex(Bi) || Ai == Bi)
		{
			return;
		}
		FPuck& A = M.Pucks[Ai];
		FPuck& B = M.Pucks[Bi];
		if (!A.bStanding || !B.bStanding)
		{
			return;
		}
		const FVector2D D = B.Pos - A.Pos;
		const double Dist = D.Size();
		const double MinDist = PuckRadius * 2.0;
		// Negated so a NaN distance (a NaN position) is no contact, rather than a NaN push into the other puck.
		if (!(Dist < MinDist))
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
}

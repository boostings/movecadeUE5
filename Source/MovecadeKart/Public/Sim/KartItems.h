#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Sim/KartSim.h"

/**
 * Items and powerups (spec Docs/Port/02), in web units on the sim's 60 Hz step.
 *
 * Faithful to the shipped game: the 11 items, position-weighted odds, per-player item boxes (2.5 m pickup,
 * 15 s respawn), the ~3 s roulette lockout, projectile speeds, homing rates, bounces, lifetimes, the 1.5 m
 * hit test, owner grace, explosion radii and the single boost slot.
 *
 * Deliberate changes (00-PLAN.md B1 and "better powerups"):
 * - Hits are real: a spin cuts speed to 10 % with a 720° spin and a short input lock; an explosion stops
 *   the kart and pops it into the air; both grant 1 s of protection afterwards. (On the web, hits only
 *   cancelled boosts.)
 * - The Prism (star) and the Rocket spin out karts they touch.
 * - Driving through a box while holding an item no longer uses up that box for 15 s.
 * - Bananas last 20 s (5 s on the web, which made them near useless).
 * Display names avoid the Nintendo marks (spec 02 IP note).
 */
namespace MovecadeKart
{
	enum class EKartItem : uint8
	{
		None,
		Shot,         // GREEN_SHELL: straight, bouncing
		Seeker,       // RED_SHELL: homes on the nearest kart ahead
		SkySeeker,    // BLUE_SHELL: flies high, homes on the leader, explodes
		Banana,       // BANANA: dropped hazard
		Nitro,        // MUSHROOM
		TripleNitro,  // TRIPLE_MUSHROOM: one 5 s boost
		Prism,        // STAR: invincible + boost
		Lightning,    // LIGHTNING: slows and shrinks everyone else
		Ink,          // BLOOPER: inks everyone else's view
		Bomb,         // BOB_OMB: lobbed, explodes
		Rocket,       // BULLET_BILL: autopilot + invincible + boost
		Count
	};

	MOVECADEKART_API const TCHAR* ItemName(EKartItem Item);

	enum class EKartItemEventType : uint8
	{
		Pickup,        // roulette starts (Seat)
		Ready,         // roulette ends (Seat, Item)
		Used,          // (Seat, Item)
		Launch,        // projectile thrown (Seat, Item, Position)
		Drop,          // banana dropped
		Bounce,        // projectile bounced (Position)
		Hit,           // projectile hit a kart (Seat = victim, Other = owner, Item)
		SpinOut,       // victim spins (Seat)
		Explosion,     // (Position, Radius, Other = owner)
		Lightning,     // victim shrunk (Seat)
		Inked,         // victim inked (Seat)
		Invincible,    // prism start (Seat)
		RocketStart,   // (Seat)
		Bump,          // invincible kart rammed another (Seat = victim, Other = rammer)
	};

	struct FKartItemEvent
	{
		EKartItemEventType Type = EKartItemEventType::Pickup;
		int32 Seat = -1;
		int32 Other = -1;
		EKartItem Item = EKartItem::None;
		FVector2D Position = FVector2D::ZeroVector;
		double Radius = 0.0;
	};

	struct FKartItemProjectile
	{
		int32 Id = 0;
		EKartItem Type = EKartItem::None;
		int32 Owner = 0;
		FVector2D Pos = FVector2D::ZeroVector;
		double Y = 0.0;
		FVector2D Vel = FVector2D::ZeroVector;
		double VY = 0.0;
		double Age = 0.0;
		int32 Bounces = 0;
		bool bAlive = true;
	};

	struct FKartItemBox
	{
		FVector2D Pos = FVector2D::ZeroVector;
		double Y = 1.0;
		/** Per seat: sim time it was collected, or < 0 if available. */
		TArray<double> CollectedAt;
	};

	/** One kart's item and status state. Times are race seconds (the item world's clock). */
	struct FKartItemSlot
	{
		EKartItem Held = EKartItem::None;
		bool bRolling = false;
		double RollEnds = 0.0;
		double InvincibleUntil = -1.0;
		double PrismUntil = -1.0;
		double RocketUntil = -1.0;
		double SpinStart = -1.0;
		double SpinUntil = -1.0;
		double ExplodeStart = -1.0;
		double LockUntil = -1.0;
		double ProtectedUntil = -1.0;
		double ShrinkUntil = -1.0;
		double InkUntil = -1.0;
		double FlashUntil = -1.0;
		double BotUseAt = -1.0;
		EKartItem LastUsed = EKartItem::None;
		double LastUsedAt = -1000.0;
	};

	/** What the item world needs to know about one kart each step. */
	struct FKartItemRacer
	{
		FKartSim* Sim = nullptr;
		int32 Laps = 0;
		double Progress = 0.0;
		bool bFinished = false;
		bool bUseItem = false;
		bool bBot = false;
	};

	class MOVECADEKART_API FKartItemWorld
	{
	public:
		static constexpr double RouletteSeconds = 3.0;   // 1.8 s strip + 1.2 s hold (RaceUI.tsx)
		static constexpr double BoxRadius = 2.5;
		static constexpr double BoxRespawnSeconds = 15.0;
		static constexpr double HitRadius = 1.5;
		static constexpr double OwnerGraceSeconds = 0.5;
		static constexpr double ThrownLifetime = 5.0;
		static constexpr double BananaLifetime = 20.0;

		void Init(const FTrack* InTrack, const TArray<FVector>& BoxPositions, int32 KartCount, int32 Seed);
		void Reset(int32 Seed);
		/** One 60 Hz step after the karts have moved. Appends to Events (cleared by the caller). */
		void Step(TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);

		/** Give an item directly (tests, debug). */
		void Give(int32 Seat, EKartItem Item, double Now);
		/** The odds table (spec 02 §2.3): uniform pick from the bracket's pool. */
		EKartItem RollItem(int32 Position, int32 KartCount, double TrailingFactor);

		bool IsInvincible(int32 Seat, double Now) const;
		bool IsInputLocked(int32 Seat, double Now) const;
		bool IsRocket(int32 Seat, double Now) const;

		const TArray<FKartItemSlot>& Slots() const { return SlotsArr; }
		const TArray<FKartItemProjectile>& Projectiles() const { return ProjectilesArr; }
		const TArray<FKartItemBox>& Boxes() const { return BoxesArr; }
		/** The roulette RNG's position, so tests can prove two races drew the same numbers. */
		int32 RandomSeed() const { return Random.GetCurrentSeed(); }
		/** True if any seat can still collect this box (it is hidden when everyone has it). */
		bool IsBoxVisible(int32 Box, double Now) const;

	private:
		void Use(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		void StepProjectile(FKartItemProjectile& P, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		void Explode(const FVector2D& Center, double Radius, int32 Owner, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		void SpinOut(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		bool Bounce(FKartItemProjectile& P, double Travel, double Retention);
		int32 Position(int32 Seat, const TArray<FKartItemRacer>& Racers) const;
		int32 NearestAhead(int32 Seat, const TArray<FKartItemRacer>& Racers) const;
		int32 Leader(const TArray<FKartItemRacer>& Racers) const;

		const FTrack* T = nullptr;
		TArray<FKartItemSlot> SlotsArr;
		TArray<FKartItemProjectile> ProjectilesArr;
		TArray<FKartItemBox> BoxesArr;
		FRandomStream Random;
		int32 NextId = 1;
	};
}

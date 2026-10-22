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

	/** Something the presentation reacts to (FX, audio, HUD, phones). Which fields are set depends on Type. */
	struct FKartItemEvent
	{
		EKartItemEventType Type = EKartItemEventType::Pickup;
		int32 Seat = -1;
		int32 Other = -1;
		EKartItem Item = EKartItem::None;
		FVector2D Position = FVector2D::ZeroVector;
		double Radius = 0.0;
	};

	/** A thrown or dropped item in the world. */
	struct FKartItemProjectile
	{
		int32 Id = 0;
		EKartItem Type = EKartItem::None;
		/** The seat that used it. */
		int32 Owner = 0;
		FVector2D Pos = FVector2D::ZeroVector;
		/** Height above the ground plane (three.js y). */
		double Height = 0.0;
		FVector2D Vel = FVector2D::ZeroVector;
		/** Vertical speed (the bomb's lob). */
		double VerticalSpeed = 0.0;
		/** Seconds since launch. */
		double Age = 0.0;
		int32 Bounces = 0;
		bool bAlive = true;
	};

	/** An item box. Each seat collects it separately. */
	struct FKartItemBox
	{
		FVector2D Pos = FVector2D::ZeroVector;
		/** Centre height above the ground plane (three.js y). */
		double Height = 1.0;
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

	/**
	 * Every kart's items, the item boxes and the projectiles in flight, stepped on the sim's 60 Hz clock after the
	 * karts move. Owned by AKartRaceManager (and by tests). It points at the track, and through the racers passed
	 * to Step it changes the karts' sims directly (boosts, spin-outs, explosions). Deterministic for a seed: the
	 * roulette and the bots' timing draw from one FRandomStream in a fixed order.
	 */
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

		const TArray<FKartItemSlot>& Slots() const { return ItemSlots; }
		const TArray<FKartItemProjectile>& Projectiles() const { return LiveProjectiles; }
		const TArray<FKartItemBox>& Boxes() const { return ItemBoxes; }
		/** The roulette RNG's position, so tests can prove two races drew the same numbers. */
		int32 RandomSeed() const { return Random.GetCurrentSeed(); }
		/** True if any seat can still collect this box (it is hidden when everyone has it). */
		bool IsBoxVisible(int32 Box, double Now) const;

	private:
		// The phases of Step, in order.
		void FinishRoulettes(double Now, TArray<FKartItemEvent>& Events);
		void CollectBoxes(const TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		void UseItems(TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		void StepProjectile(FKartItemProjectile& Projectile, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		void RamWithInvincibles(TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);

		// Using an item (KartItems.cpp).
		void Use(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		/** A new projectile Offset metres ahead of the kart, flying straight on at Speed. */
		FKartItemProjectile& SpawnProjectile(EKartItem Item, int32 Owner, const FKartState& Kart, double Offset, double Speed);
		void LaunchSkySeeker(int32 Seat, const TArray<FKartItemRacer>& Racers, TArray<FKartItemEvent>& Events);
		void StrikeOthersWithLightning(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		void InkOthers(int32 Seat, int32 KartCount, double Now, TArray<FKartItemEvent>& Events);

		// Projectiles in flight (KartItemProjectiles.cpp).
		void FlySeeker(FKartItemProjectile& Projectile, const TArray<FKartItemRacer>& Racers, double Travel, TArray<FKartItemEvent>& Events) const;
		void FlySkySeeker(FKartItemProjectile& Projectile, const TArray<FKartItemRacer>& Racers, double Travel, TArray<FKartItemEvent>& Events) const;
		void FlyBomb(FKartItemProjectile& Projectile, TArray<FKartItemRacer>& Racers, double Travel, double Now, TArray<FKartItemEvent>& Events);
		/** Reflects the projectile off a wall within this step's travel; false if there is none. */
		bool Bounce(FKartItemProjectile& Projectile, double Travel, double Retention) const;
		/** Bounce (raising a Bounce event), or else fly straight on. */
		void BounceOrMove(FKartItemProjectile& Projectile, double Travel, double Retention, TArray<FKartItemEvent>& Events) const;
		void HitKarts(FKartItemProjectile& Projectile, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);

		// Hits (KartItemHits.cpp).
		void Explode(const FVector2D& Center, double Radius, int32 Owner, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);
		void SpinOut(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events);

		const FTrack* TrackData = nullptr;
		TArray<FKartItemSlot> ItemSlots;
		TArray<FKartItemProjectile> LiveProjectiles;
		TArray<FKartItemBox> ItemBoxes;
		FRandomStream Random;
		/** The next projectile's Id, unique within a race so the presentation can track each one. */
		int32 NextId = 1;
	};
}

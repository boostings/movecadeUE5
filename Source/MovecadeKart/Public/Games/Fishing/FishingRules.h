#pragma once

#include "CoreMinimal.h"

/**
 * Fishing Frenzy's rules: 2-4 anglers fish side by side off one dock, all at once, for a three-minute round.
 *
 *   Idle --cast(power)--> Casting --lands--> Waiting --bite--> Bite --hook in time--> Fighting --reeled in--> Landed
 *                                              ^  \--hook early: "too early", the fish spook, the wait restarts
 *                                              \--reel while waiting: the lure comes back (Idle at the dock)
 *   Bite --window passes--> Lost (the bait is stolen); Fighting --tension pinned in the red--> Lost (snap)
 *   Fighting --line slack too long--> Lost (escape); Landed / Lost --a beat later--> Idle
 *
 * Power sets the cast's distance; the distance band (shallows, reeds, deep, far) picks what bites, so the far
 * water holds the big, rare fish (and the trophy). A fight is a tension model: the fish pulls (harder while it has
 * stamina, much harder in its bursts), reeling adds tension, and only a line in the green both wears the fish down
 * and reels it in at full speed. Too much tension for too long snaps the line; too little lets the fish shake the
 * hook. Score is the total weight landed (grams), scaled by a combo for consecutive catches; junk (a boot, a tyre,
 * a rubber duck) scores nothing but keeps the combo.
 *
 * Deterministic: every angler draws from its own seeded stream (the match seed and its lane), so one angler's luck
 * never depends on another's timing. Pure: no UObjects, no clock; the game steps it on a fixed 10 ms clock.
 */
namespace MovecadeKart::Fishing
{
	constexpr double RoundDurationMs = 180000.0;
	constexpr double CountdownMs = 3000.0;
	constexpr double MinCastM = 4.0;
	constexpr double MaxCastM = 30.0;
	/** The bands' far edges, metres from the dock. */
	constexpr double ShallowsEndM = 11.0;
	constexpr double ReedsEndM = 19.0;
	constexpr double DeepEndM = 25.0;
	/** The fish is in the landing net inside this. */
	constexpr double LandM = 1.2;
	/** Tension zones (0 slack .. 1 the red line). */
	constexpr double GreenLow = 0.28;
	constexpr double GreenHigh = 0.78;
	constexpr double RedLine = 0.92;
	constexpr double SnapStrainMs = 520.0;
	constexpr double EscapeSlackMs = 2000.0;
	constexpr double LandedHoldMs = 2600.0;
	constexpr double LostHoldMs = 1500.0;
	constexpr double MaxReel = 1.25;
	constexpr int32 ComboCap = 5;
	constexpr double ComboStep = 0.15;
	constexpr int32 MaxEffects = 32;

	enum class EBand : uint8 { Shallows, Reeds, Deep, Far };
	enum class EKind : uint8 { Fish, Trophy, Junk };
	enum class EAnglerState : uint8 { Idle, Casting, Waiting, Bite, Fighting, Landed, Lost };
	enum class ELoss : uint8 { None, StolenBait, Snap, Escape };
	enum class EMatchPhase : uint8 { Countdown, Playing, Ended };

	enum class EEffectKind : uint8
	{
		Cast,
		/** The lure hits the water. */
		Splash,
		/** A fish noses the bait: the bobber twitches (a hook now is too early). */
		Nibble,
		/** The bobber dips: hook now. */
		Bite,
		Hooked,
		TooEarly,
		/** The fish starts a run (a warning before it pulls). */
		Burst,
		Snap,
		Escape,
		StolenBait,
		Catch,
		Junk,
		/** The biggest catch of the round so far. */
		Record,
		/** The lure was reeled back to the dock without a bite. */
		Retrieved,
	};

	struct FSpecies
	{
		const TCHAR* Id;
		const TCHAR* Name;
		EKind Kind;
		double MinKg;
		double MaxKg;
		/** Relative odds of biting in each band. */
		double Odds[4];
		/** How hard it pulls (tension per unit at full stamina). */
		double Strength;
		/** Seconds of fighting in the green to tire it out. */
		double StaminaS;
		/** Mean gap between runs (ms; 0: never runs), and the run's extra pull. */
		double BurstGapMs;
		double BurstPower;
		/** How long the bobber stays down for the hook. */
		double HookWindowMs;
	};

	MOVECADEKART_API const TArray<FSpecies>& Species();
	MOVECADEKART_API int32 SpeciesIndex(const TCHAR* Id);
	MOVECADEKART_API EBand BandAt(double DistanceM);
	MOVECADEKART_API const TCHAR* BandName(EBand Band);
	/** Cast power 0..1 to metres. */
	MOVECADEKART_API double CastDistance(double Power);
	MOVECADEKART_API double ComboMultiplier(int32 Combo);

	/** A small, explicit generator (mulberry32), so the sequences are the same on every compiler and engine. */
	struct FRng
	{
		uint32 State = 1;
		uint32 Next();
		/** [0, 1) */
		double Unit();
		double Range(double A, double B) { return A + (B - A) * Unit(); }
	};

	struct FFight
	{
		int32 Species = 0;
		int32 WeightG = 0;
		/** Metres from the dock. */
		double DistanceM = 0.0;
		/** 1 fresh .. 0 spent. */
		double Stamina = 1.0;
		double Tension = 0.0;
		/** Time over the red line (ms, weighted by how far over), and time slack. */
		double StrainMs = 0.0;
		double SlackMs = 0.0;
		/** The run in progress (ms left) and the warning before the next one; NextBurstMs counts down to it. */
		double BurstMs = 0.0;
		double WarnMs = 0.0;
		double NextBurstMs = 0.0;
		int32 Bursts = 0;
		/** The fish's pull this step (after stamina and any run), for the HUD and the bots. */
		double Pull = 0.0;
		/** Side to side, -1..1, as it swims (presentation; deterministic). */
		double Lateral = 0.0;
		double FightMs = 0.0;
	};

	struct FCatch
	{
		int32 Species = 0;
		int32 WeightG = 0;
		int32 ScoreG = 0;
		int32 Combo = 0;
		bool bRecord = false;
		double DistanceM = 0.0;
	};

	struct FAngler
	{
		FString PlayerId;
		FString Name;
		int32 LaneIndex = 0;
		EAnglerState State = EAnglerState::Idle;
		/** Time in the current state. */
		double StateMs = 0.0;
		/** The lure: where it is now (metres out) and where the cast is headed. */
		double LureM = 0.0;
		double CastM = 0.0;
		double CastPower = 0.0;
		double FlightMs = 0.0;
		/** Waiting: ms to the bite, and to the next nibble (a negative nibble time: none left). */
		double BiteInMs = 0.0;
		double NibbleInMs = -1.0;
		int32 NibblesLeft = 0;
		/** Bite: the fish already chosen, and the window left. */
		int32 BiteSpecies = 0;
		double WindowMs = 0.0;
		/** The reel input this step, 0..MaxReel (revolutions against the full-speed crank). */
		double Reel = 0.0;
		FFight Fight;
		ELoss LastLoss = ELoss::None;
		FCatch LastCatch;
		int32 Score = 0;
		int32 TotalG = 0;
		int32 Catches = 0;
		int32 JunkCount = 0;
		int32 Losses = 0;
		int32 Casts = 0;
		int32 Combo = 0;
		int32 BestCombo = 0;
		int32 BiggestG = 0;
		int32 BiggestSpecies = -1;
		FRng Rng;

		bool IsFighting() const { return State == EAnglerState::Fighting; }
		/** The lure or fish is out in the water. */
		bool IsLineOut() const { return State == EAnglerState::Casting || State == EAnglerState::Waiting || State == EAnglerState::Bite || State == EAnglerState::Fighting; }
	};

	struct FEffect
	{
		int32 Id = 0;
		FString PlayerId;
		int32 LaneIndex = 0;
		EEffectKind Kind = EEffectKind::Cast;
		double TtlMs = 0.0;
		double DurationMs = 0.0;
		/** Where it happened, metres out. */
		double DistanceM = 0.0;
		int32 Species = -1;
		int32 WeightG = 0;
		int32 ScoreG = 0;
		FString Label;
	};

	struct FMatch
	{
		EMatchPhase Phase = EMatchPhase::Countdown;
		uint32 Seed = 0;
		double ElapsedMs = 0.0;
		double CountdownRemainingMs = CountdownMs;
		double TimeRemainingMs = RoundDurationMs;
		TArray<FAngler> Anglers;
		TArray<FEffect> Effects;
		TArray<FString> WinnerIds;
		/** The round's biggest fish so far (grams) and who has it. */
		int32 RecordG = 0;
		FString RecordHolder;
		int32 NextId = 1;

		FAngler* Find(const FString& PlayerId);
		const FAngler* Find(const FString& PlayerId) const;
	};

	struct FSeed
	{
		FString Id;
		FString Name;
	};

	MOVECADEKART_API FMatch CreateMatch(const TArray<FSeed>& Players, uint32 Seed);
	MOVECADEKART_API void StepMatch(FMatch& Match, double DtMs);
	/** Casts at Power (0..1); false unless the angler is idle and the round is on. */
	MOVECADEKART_API bool Cast(FMatch& Match, const FString& PlayerId, double Power);
	/** The hook: true when it set the hook (a bite in its window); early, it spooks the fish. */
	MOVECADEKART_API bool Hook(FMatch& Match, const FString& PlayerId);
	/** The reel this step, 0..MaxReel (it holds until set again). */
	MOVECADEKART_API void SetReel(FMatch& Match, const FString& PlayerId, double Reel);
	MOVECADEKART_API TArray<const FAngler*> Leaderboard(const FMatch& Match);
	/** grams to "2.34 kg" (or "850 g"). */
	MOVECADEKART_API FString FormatWeight(int32 Grams);
	/** The tension the fight settles at for a reel (the bots' and the HUD's target). */
	MOVECADEKART_API double SettledTension(double Pull, double Reel);
	/** The reel that settles the tension at Target against Pull (clamped 0..MaxReel). */
	MOVECADEKART_API double ReelForTension(double Pull, double Target);
}

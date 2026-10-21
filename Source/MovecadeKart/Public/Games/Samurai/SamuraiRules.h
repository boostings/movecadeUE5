#pragma once

#include "CoreMinimal.h"

/**
 * Samurai Slice's rules (movecade-v2 samurai/engine.ts, Docs/Port/08-samurai.md §1): every player has their own
 * lane of fruit, bombs and bonus scrolls tossed up and falling back; a cut scores by timing, direction and combo;
 * three precise cuts start Focus (six seconds of double points and slowed fruit); a bomb costs a life and voids the
 * swing. Seeded (mulberry32, bit-exact with the web) and pure: no UObjects, no clock.
 */
namespace MovecadeKart::Samurai
{
	constexpr double RoundDurationMs = 60000.0;
	constexpr double CountdownMs = 3000.0;
	constexpr int32 StartingLives = 3;
	constexpr int32 FocusCuts = 3;
	constexpr double FocusDurationMs = 6000.0;
	constexpr double FocusFlightScale = 0.65;
	constexpr int32 MaxTargetsPerLane = 4;
	constexpr int32 WaveCount = 4;
	/** The timing window cuts without a contact set use (sims, bots): start, end, apex. */
	constexpr double CutStart = 0.22;
	constexpr double CutEnd = 0.86;
	constexpr double CutApex = 0.54;
	constexpr uint32 DefaultSeed = 0x5a3c;

	enum class EVariant : uint8 { Apple, Melon, Lantern, Bamboo, Bomb, Scroll };
	enum class EKind : uint8 { Fruit, Bomb, Bonus };
	enum class EDirection : uint8 { None, Up, Down, Left, Right };
	enum class EEffectKind : uint8 { Slash, Hit, Combo, Bomb, Miss };

	struct FTargetDef
	{
		EKind Kind = EKind::Fruit;
		int32 Score = 0;
		double Radius = 0.4;
		EDirection Preferred = EDirection::None;
		/** The renderer's hue, kept with the rules so they can't disagree. */
		const TCHAR* Color = TEXT("#ffffff");
	};

	MOVECADEKART_API const FTargetDef& Def(EVariant Variant);
	MOVECADEKART_API const TCHAR* VariantName(EVariant Variant);

	/** mulberry32, with the web's exact uint32 maths: the same seed gives the same round everywhere. */
	struct FMulberry32
	{
		uint32 A = 0;
		explicit FMulberry32(uint32 Seed = 0) : A(Seed) {}
		double Next();
	};

	struct FTarget
	{
		int32 Id = 0;
		FString PlayerId;
		int32 LaneIndex = 0;
		EVariant Variant = EVariant::Apple;
		EKind Kind = EKind::Fruit;
		/** 0 just launched, 1 fallen out of the lane. */
		double Progress = 0.0;
		/** Progress per millisecond. */
		double Speed = 0.0;
		/** -1..1 across the lane. */
		double Lateral = 0.0;
		double Spin = 0.0;
		double Radius = 0.4;
		int32 Score = 0;
		EDirection Preferred = EDirection::None;
	};

	struct FEffect
	{
		int32 Id = 0;
		FString PlayerId;
		int32 LaneIndex = 0;
		EEffectKind Kind = EEffectKind::Slash;
		double TtlMs = 0.0;
		double DurationMs = 0.0;
		EDirection Direction = EDirection::Right;
		double Intensity = 1.0;
		FString Color = TEXT("#ffffff");
		int32 ScoreDelta = 0;
		FString Label;
		/** The first cut target's variant, for the interior faces. */
		TOptional<EVariant> Variant;
		int32 CutCount = 0;
		/** What the swing removed, captured before removal (the halves and debris). */
		TArray<FTarget> Struck;
	};

	struct FPlayer
	{
		FString Id;
		FString Name;
		int32 LaneIndex = 0;
		int32 Score = 0;
		int32 Combo = 0;
		int32 BestCombo = 0;
		int32 FocusCharge = 0;
		double FocusRemainingMs = 0.0;
		int32 Launches = 0;
		int32 Hits = 0;
		int32 Misses = 0;
		int32 Bombs = 0;
		int32 Lives = StartingLives;
		bool bEliminated = false;
	};

	enum class EMatchPhase : uint8 { Countdown, Playing, Ended };

	struct FMatch
	{
		EMatchPhase Phase = EMatchPhase::Countdown;
		double ElapsedMs = 0.0;
		double CountdownRemainingMs = CountdownMs;
		double TimeRemainingMs = RoundDurationMs;
		TArray<FPlayer> Players;
		TArray<FTarget> Targets;
		TArray<FEffect> Effects;
		TArray<double> SpawnCooldownMs;
		TArray<FString> WinnerIds;
		int32 NextId = 1;
		FMulberry32 Rng;

		FPlayer* FindPlayer(const FString& Id);
		const FPlayer* FindPlayer(const FString& Id) const;
	};

	struct FSeed
	{
		FString Id;
		FString Name;
	};

	struct FSwing
	{
		EDirection Direction = EDirection::Down;
		double Power = 0.25;
	};

	enum class EOutcome : uint8 { Hit, Bomb, Miss };

	struct FSwingOutcome
	{
		EOutcome Kind = EOutcome::Miss;
		int32 ScoreDelta = 0;
		int32 Combo = 0;
		FString Label;
		int32 CutTargets = 0;
	};

	/** The live gesture runtime.ts synthesises for every contact (M3): a downward cut at power 0.25. */
	inline FSwing ContactSwing() { return FSwing{ EDirection::Down, 0.25 }; }

	MOVECADEKART_API FMatch CreateMatch(const TArray<FSeed>& Players, uint32 Seed = DefaultSeed);
	/** Advances (dt clamped to [0, 100] ms): countdown, clock, effects, Focus, flights, spawns, the end. */
	MOVECADEKART_API void StepMatch(FMatch& Match, double DtMs);
	/** 1 at the apex, 0 at the window's edges. */
	MOVECADEKART_API double CutQuality(double Progress);
	/**
	 * One swing for one player: the only place the score moves. Contacts are the targets the blade touched; without
	 * them the timing window decides (sims and bots). Unset when the player can't swing.
	 */
	MOVECADEKART_API TOptional<FSwingOutcome> ResolveSwing(FMatch& Match, const FString& PlayerId, const FSwing& Swing, const TSet<int32>* Contacts = nullptr);
	/** The HUD's 1..4 wave band. */
	MOVECADEKART_API int32 WaveNumber(double ElapsedMs);

	/** Presentation geometry (SamuraiScene.tsx): a target's height on its arc, metres (apex about 2.17 at .538). */
	MOVECADEKART_API double TargetHeight(double Progress);
	/** Lane centres are 3.6 m apart around 0. */
	MOVECADEKART_API double LaneX(int32 Lane, int32 LaneCount);
	constexpr double TargetDepth = 0.55;
	/** A target's centre (web metres) and contact radius. */
	MOVECADEKART_API FVector TargetCentre(const FTarget& Target, int32 LaneCount);
	inline double ContactRadius(const FTarget& Target) { return Target.Radius * 0.75; }

	/** JS Math.round: halves round up. */
	inline double JsRound(double X) { return FMath::FloorToDouble(X + 0.5); }
}

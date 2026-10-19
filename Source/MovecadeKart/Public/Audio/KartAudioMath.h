#pragma once

#include "CoreMinimal.h"

/**
 * Pure kart-audio maths, ported from the shipped web build with its constants (spec Docs/Port/04 §9,
 * Docs/Port/01 §2.6). No UObjects here, so every number is covered by the MovecadeKart.Audio.* tests
 * against golden values that the web code itself produced (Tools/audio/golden.mjs).
 *
 * Speeds are the sim's SU, which is the value the web passes as "mph" (soundMix.ts, drivetrain.ts).
 */
namespace MovecadeKart::Audio
{
	// ── drivetrain.ts: a pretend 6-speed gearbox for the engine note ──
	inline constexpr double IdleRpm = 950.0;
	inline constexpr double UpshiftRpm = 7600.0;
	inline constexpr double LimiterRpm = 8600.0;
	inline constexpr double DownshiftRpm = 3500.0;
	inline constexpr double KickdownRpm = 5200.0;
	inline constexpr double LaunchRpm = 3300.0;
	inline constexpr int32 NumGears = 6;
	inline constexpr double GearTopMph[NumGears] = { 19.2, 31.7, 42.5, 54.4, 70.0, 90.0 };
	inline constexpr double ShiftGapSeconds = 0.13;
	inline constexpr double ShiftLockoutSeconds = 0.25;
	inline constexpr double LimiterCutSeconds = 0.055;

	/** drivetrain.ts mulberry32, bit for bit (uint32 wrap = Math.imul / >>> 0). */
	struct MOVECADEKART_API FMulberry32
	{
		explicit FMulberry32(uint32 Seed) : A(Seed) {}
		double Next();
		uint32 A;
	};

	/** Frame-rate independent approach toward Target with time constant Tau (also Web Audio setTargetAtTime). */
	MOVECADEKART_API double Approach(double Value, double Target, double Tau, double Dt);
	MOVECADEKART_API double WheelRpm(double SpeedMph, int32 Gear);

	enum EDrivetrainEvent : uint8
	{
		DrivetrainUpshift = 1,
		DrivetrainDownshift = 2,
		DrivetrainBackfire = 4,
	};

	struct FDrivetrainInput
	{
		/** Road speed (SU / "mph"); negative is reverse. */
		double Speed = 0.0;
		double Dt = 0.0;
		/** -1 not reported (keyboard: inferred from the speed trend), 0 off, 1 held. */
		int8 Accelerating = -1;
		/** Clutch in regardless of speed: the grid before GO. */
		bool bNeutral = false;
	};

	struct FDrivetrainFrame
	{
		double Rpm = IdleRpm;
		/** 0 neutral, -1 reverse, 1..6 forward. */
		int32 Gear = 0;
		/** -1 engine braking .. 0 idle .. 1 full throttle, smoothed. */
		double Load = 0.0;
		bool bThrottle = false;
		bool bLimiter = false;
		/** EDrivetrainEvent bits. */
		uint8 Events = 0;
	};

	/** drivetrain.ts createDrivetrain(seed). Deterministic; the web seeds it with spawnIndex + 1. */
	class MOVECADEKART_API FDrivetrain
	{
	public:
		explicit FDrivetrain(uint32 Seed = 1) : Rng(Seed) {}
		FDrivetrainFrame Step(const FDrivetrainInput& Input);

	private:
		FMulberry32 Rng;
		double Rpm = IdleRpm;
		int32 Gear = 0;
		double Load = 0.0;
		bool bThrottle = false;
		bool bHasLastSpeed = false;
		double LastSpeed = 0.0;
		double AccelEma = 0.0;
		double ShiftGap = 0.0;
		double Lockout = 0.0;
		double LimiterCut = 0.0;
		/** Seconds until each queued lift-off pop. */
		TArray<double, TInlineAllocator<8>> Pops;
	};

	// ── engineVoice.ts: layered engine recordings ──
	inline constexpr int32 NumOnLayers = 5;
	inline constexpr int32 NumOffLayers = 2;
	/** kartCues.ts KART_ENGINE_LAYERS: the RPM each recording was made at. */
	inline constexpr double OnLayerRpm[NumOnLayers] = { 1000.0, 2500.0, 4000.0, 5500.0, 7000.0 };
	inline constexpr double OffLayerRpm[NumOffLayers] = { 2500.0, 6500.0 };
	/** setTargetAtTime time constants in EngineVoice.update. */
	inline constexpr double EngineLayerGainTau = 0.03;
	inline constexpr double EngineLayerRateTau = 0.02;
	inline constexpr double EngineBusTau = 0.04;
	inline constexpr double EngineOutTau = 0.03;

	/** Equal-power weights over anchors sorted by RPM: only the two layers bracketing Rpm sound. */
	MOVECADEKART_API void LayerWeights(const double* Anchors, int32 Num, double Rpm, double* OutWeights);

	/** The targets one EngineVoice.update({rpm, load}, {gain: MixGain}) sets on its Web Audio graph. */
	struct FEngineTargets
	{
		double Rpm = 0.0;
		double Load = 0.0;
		double OnBus = 0.0;
		double OffBus = 0.0;
		/** Output gain: (0.8 + 0.2 * clamp(rpm / 7000)) * clamp(MixGain, 0, 2). */
		double Out = 0.0;
		double OnGain[NumOnLayers] = {};
		double OffGain[NumOffLayers] = {};
		double OnRate[NumOnLayers] = {};
		double OffRate[NumOffLayers] = {};
	};
	MOVECADEKART_API FEngineTargets EngineTargets(double Rpm, double Load, double MixGain);

	// ── soundMix.ts ──
	struct FLoopMix
	{
		double Gain = 0.0;
		double Rate = 1.0;
		/** Lowpass cutoff in Hz, or < 0 when the layer has no filter. */
		double Lowpass = -1.0;
	};
	MOVECADEKART_API FLoopMix WindMix(double SpeedMph);
	MOVECADEKART_API FLoopMix SquealMix(double SpeedMph, bool bDrifting, double DriftPower, double Steer);

	enum class EImpactTier : uint8 { Light, Medium, Heavy };
	MOVECADEKART_API double ImpactStrength(double SpeedMph, double Penetration, bool bCritical);
	/** Tier and gain (0.6 .. 1 within the tier). */
	MOVECADEKART_API EImpactTier ImpactTier(double Strength, double& OutGain);
	MOVECADEKART_API FName ImpactCueId(EImpactTier Tier);
	/** kartAudioEvents.ts playKartImpact: 0.96 + ((s * 7919) % 1) * 0.08. */
	MOVECADEKART_API double ImpactRate(double Strength);

	/** Drift charge level (0 none, 1..3) from drift seconds, thresholds 1/3/6 s. */
	MOVECADEKART_API int32 DriftLevel(double DriftPower);
	/**
	 * The whoosh for a boost start. Returns 0 when ActiveBoost is not a DRIFT_ boost, else the level 1..3
	 * (its grade wins; the charge before release is the fallback) and OutGain 0.8/0.9/1.0.
	 */
	MOVECADEKART_API int32 DriftBoostLevel(const FString& ActiveBoost, double ChargeBeforeRelease, double& OutGain);
	MOVECADEKART_API FName DriftBoostCueId(int32 Level);

	// ── audioService.ts mixer ──
	inline constexpr int32 VoiceCount = 24;
	inline constexpr double EngineGain = 0.3;
	inline constexpr int32 MaxEngines = 8;
	inline constexpr int32 MaxLoops = 32;
	inline constexpr double PanSpread = 0.55;
	inline constexpr double DefaultSfxVolume = 0.8;
	inline constexpr double LoopGainTau = 0.035;
	inline constexpr double LoopRateTau = 0.06;
	inline constexpr double LoopLowpassTau = 0.08;

	MOVECADEKART_API double LaneToPan(int32 LaneIndex, int32 LaneCount);
	/** sound.ts race countdown tick rate: 1.0 for values >= 3, 1.1 at 2, 1.2 at 1. */
	MOVECADEKART_API double CountdownRate(int32 Value);
	/** sound.ts engine start offset: (spawnIndex * 0.29 + 0.13) % 1. */
	MOVECADEKART_API double EngineCharacter(int32 SpawnIndex);

	// ── kartCues.ts / cues.ts ──
	struct FCueDef
	{
		/** The web cue name, e.g. "kart.hop". */
		const TCHAR* Id;
		/** SoundWave under /Game/Audio/Kart. */
		const TCHAR* Asset;
		float Gain;
		int32 ThrottleMs;
		bool bLoop;
	};
	MOVECADEKART_API TConstArrayView<FCueDef> AllCues();
	MOVECADEKART_API const FCueDef* FindCue(FName Id);

	/** audioService.ts play(): the per-cue minimum gap between plays, keyed by cue, shared by every kart. */
	class MOVECADEKART_API FCueThrottle
	{
	public:
		/** True (and records the play) when the cue may sound at time Now (seconds). */
		bool TryPlay(const FCueDef& Cue, double Now);
		void Reset() { LastPlayed.Reset(); }

	private:
		TMap<const FCueDef*, double> LastPlayed;
	};
}

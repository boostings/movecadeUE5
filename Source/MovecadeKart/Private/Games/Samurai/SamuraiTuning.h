#pragma once

#include "CoreMinimal.h"
#include "Art/KartMaterials.h"

/**
 * Every presentation and house-AI number of Samurai Slice's round (ASamuraiGame), grouped by the part of the game
 * that uses it. The rules' numbers live in SamuraiRules.h and the rig's in SamuraiBlade.h; nothing here changes a
 * score. Lengths are web metres unless a name says Cm (UE centimetres); times are seconds unless a name says Ms.
 * Where a value comes from the web original, its group names the source file.
 *
 * Private to the Samurai files: the game actor is spawned in code (never placed or subclassed), so there is no
 * editor surface for these; they are constexpr so the round stays bit-for-bit the same build to build.
 */
namespace MovecadeKart::Samurai::Tuning
{
	constexpr double CmPerMetre = 100.0;
	/** Shrinking props stop at this scale rather than zero (a degenerate transform). */
	constexpr double MinVisibleScale = 0.01;

	// ---- Round flow ----
	/** The rules' fixed step (60 Hz), whatever the frame rate (M2). */
	constexpr double StepMs = 1000.0 / 60.0;
	/** A hitch longer than this is played as this long, so a stall never fast-forwards the round. */
	constexpr float MaxFrameSeconds = 0.1f;
	/** The practice apple appears this long before the title card would give way. */
	constexpr double PracticeLeadSeconds = 0.6;

	// ---- Component pools ----
	constexpr int32 TargetPoolSize = 20;
	constexpr int32 PiecePoolSize = 32;
	/** Bomb telegraphs and halos on screen at once (each). */
	constexpr int32 MarkerPoolSize = 8;
	/** Floating notes kept; the oldest gives way. */
	constexpr int32 MaxNotes = 12;
	/** The swordsman is the shared avatar at the rig's scale (FBladeRig::Scale). */
	constexpr float SwordsmanScale = 0.64f;

	// ---- Targets (SamuraiScene.tsx) ----
	/** The prop meshes are unit radius (100 cm): a target draws at Radius * this (its contact radius too). */
	constexpr double TargetMeshScale = 0.75;
	/** Each new target starts turned by its id times this (radians), so neighbours don't spin in step. */
	constexpr double SpawnTurnPerId = 0.7;
	/** Spin about up and about across, as multiples of the target's own spin. */
	constexpr double SpinUpRate = 1.8;
	constexpr double SpinAcrossRate = 1.08;
	/** Halo and telegraph rings sit this far toward the camera from the target's centre (cm per unit scale). */
	constexpr double HaloForwardCm = 60.0;
	constexpr double TelegraphForwardCm = 110.0;
	/** The bomb's telegraph pulses around this scale. */
	constexpr double TelegraphPulseBase = 1.16;
	constexpr double TelegraphPulseDepth = 0.035;
	constexpr double TelegraphPulseRate = 7.0;
	/** Cut quality from which the halo marks "now" (and a cut counts as perfect for the juice). */
	constexpr double PreciseQuality = 0.85;
	/** Cut quality from which a featured cut flashes the screen. */
	constexpr double FlashQuality = 0.92;
	/** Ring meshes: the telegraph's barred octagon and the halo, radii in cm. */
	constexpr double TelegraphInnerCm = 98.0;
	constexpr double TelegraphOuterCm = 116.0;
	constexpr double HaloInnerCm = 70.0;
	constexpr double HaloOuterCm = 128.0;
	constexpr int32 HaloSegments = 40;
	constexpr float TelegraphGlow = 2.2f;
	constexpr float HaloGlow = 1.1f;
	constexpr float HaloOpacity = 0.2f;
	constexpr float FocusHaloGlow = 1.2f;
	constexpr float FocusHaloOpacity = 0.3f;

	// ---- Cut halves (cutEffects.ts) ----
	constexpr double PieceSeconds = 0.65;
	/** The halves shrink away over the last part of their life. */
	constexpr double PieceFadeSeconds = 0.18;
	/** Parting speed along the cut's normal, the toss up and the fall (m/s, m/s, m/s^2 halved). */
	constexpr double PieceDriftSpeed = 1.4;
	constexpr double PieceToss = 0.7;
	constexpr double PieceFall = 1.6;
	/** Tumble rate about the cut (rad/s), opposite for each half. */
	constexpr double PieceTumbleRate = 2.0;

	// ---- Katana trail (bladeTrail.tsx) ----
	constexpr int32 TrailSamples = 12;
	/** The ribbon keeps growing this long after the last meant sample, and clears a beat later. */
	constexpr double TrailLiveSeconds = 0.1;
	constexpr double TrailClearSeconds = 0.12;
	/** The ribbon's inner edge, as a fraction of the way from the blade's base to its tip. */
	constexpr double TrailInnerAlongBlade = 0.55;
	/** White-hot at heart, tinted this far toward the seat's colour, then overdriven to bloom. */
	constexpr float TrailTint = 0.55f;
	constexpr float TrailGlow = 2.4f;
	constexpr float TrailOpacity = 0.42f;

	// ---- Local aim: the mouse, sticks and keys move the tip (M9) ----
	constexpr double MouseMetresPerCount = 0.0042;
	constexpr double StickMetresPerSecond = 4.2;
	/** How far the tip may wander either side of the lane's centre, and its height range. */
	constexpr double TipReachX = 1.6;
	constexpr double TipMinY = 0.1;
	constexpr double TipMaxY = 3.1;
	/** Where a local tip starts, from the lane's centre (x) and the floor (y). */
	constexpr double TipStartOffsetX = 0.3;
	constexpr double TipStartY = 2.2;

	// ---- Blades ----
	/** The guard (kamae): the blade raised before the face, aimed at this point from the lane's centre. */
	constexpr double KamaeOffsetX = 0.25;
	constexpr double KamaeY = 2.55;
	constexpr double KamaeZ = 0.15;
	/** A phone that hasn't streamed for this long lowers its blade back to the guard. */
	constexpr double PhoneStaleSeconds = 0.36;
	/** A frame without its own interval is taken as 100 Hz. */
	constexpr double PhoneDefaultFrameMs = 10.0;
	/** Per-step blends back to the guard (phones gone quiet, idle bots) and into a bot's wind-up. */
	constexpr double PhoneGuardBlend = 0.1;
	constexpr double BotGuardBlend = 0.12;
	constexpr double BotWindUpBlend = 0.35;
	/** A bot draws its blade to the cut's start this many sweep-lengths before the cut. */
	constexpr double BotWindUpSweeps = 1.5;
	/** Guards the angular-speed divisions against a zero step. */
	constexpr double MinStepSeconds = 1e-4;
	/** Blade energy (the swordsman's eagerness): the turn rate (rad/s) that reads as full, and its smoothing. */
	constexpr double EnergyFullTurnRate = 12.0;
	constexpr double EnergyBlend = 0.25;
	/** The cut pose's strength: a tip sweep this long in one step is a full-power follow-through. */
	constexpr double CutPowerFullSweep = 0.12;
	constexpr double CutPowerMin = 0.35;

	// ---- House bots ----
	/** First look at the lane, staggered per lane so bots don't cut in step. */
	constexpr double BotFirstThinkSeconds = 0.6;
	constexpr double BotThinkStagger = 0.13;
	/** A target within this much progress of the apex is worth a cut; wild bots accept more. */
	constexpr double BotWindowSharp = 0.035;
	constexpr double BotWindowWild = 0.08;
	/** Nothing to cut: look again this soon. */
	constexpr double BotIdleRethinkSeconds = 0.05;
	/** A bomb this close to the chosen fruit makes a bot wait (sharper bots wait more often). */
	constexpr double BotBombWaryMetres = 1.15;
	constexpr double BotBombWaryChance = 0.4;
	constexpr double BotBombWaryChancePerSkill = 0.6;
	constexpr double BotBombWaitSeconds = 0.12;
	/** The planned sweep: from beside and above the fruit to beside and below it, across. */
	constexpr double BotSweepReachX = 0.4;
	constexpr double BotSweepReachY = 0.95;
	constexpr double BotSweepLeadSeconds = 0.12;
	constexpr double BotSweepSeconds = 0.16;
	/** A sweep's length before the first plan. */
	constexpr double BotInitialSweepSeconds = 0.18;
	/** After a sweep: a breath, plus up to this much more for a wild bot. */
	constexpr double BotRecoverSeconds = 0.22;
	constexpr double BotWildPauseSeconds = 0.35;
	constexpr int32 BotRngSeed = 0x5a3c1;

	// ---- Coaching the misses the rules don't call ----
	constexpr double HintCooldownSeconds = 1.3;
	/** A sweep that ends without a cut names the fruit nearest the blade, if one is this close. */
	constexpr double WhiffNearestMetres = 1.25;
	/** A blade dragged through fruit faster than this (deg/s) but without intent was "too slow". */
	constexpr double TooSlowMinTurnDegPerSecond = 20.0;
	/** The fixed steps per second, to turn a per-step angle into a rate. */
	constexpr double StepsPerSecond = 60.0;
	/** Notes float this far above the target they're about. */
	constexpr double NoteLift = 0.5;
	/** The rules' own miss or bomb callout in a lane is "fresh" (not to be talked over) this long. */
	constexpr double CalloutQuietMs = 700.0;
	/** A fruit past this progress fell out of its lane uncut. */
	constexpr double DroppedProgress = 0.97;
	constexpr double DroppedLiftCm = 40.0;
	/** Each note's life (s) and size. */
	constexpr double TooSlowNoteLife = 1.1;
	constexpr float TooSlowNoteScale = 0.8f;
	constexpr double WhiffNoteLife = 0.8;
	constexpr float WhiffNoteScale = 0.7f;
	constexpr double DroppedNoteLife = 0.8;
	constexpr float DroppedNoteScale = 0.7f;

	// ---- Impacts: hit-stop, camera punch and shake, flash, slow motion (impact.ts) ----
	constexpr double HitStopSeconds = 0.078;
	constexpr double HitPunch = 0.16;
	constexpr double HitPunchBase = 0.72;
	constexpr double HitPunchPerPower = 0.28;
	constexpr double HitShake = 0.16;
	constexpr double ComboShake = 0.32;
	constexpr double PerfectFlashPower = 0.55;
	constexpr double PerfectFlashFringe = 0.35;
	constexpr double PerfectFovKick = -2.2;
	/** Slow motion is for the rare moments (a double cut), not every Perfect. */
	constexpr double DoubleCutSlowMoSeconds = 0.55;
	constexpr double BombHitStopSeconds = 0.12;
	constexpr double BombPunch = 0.3;
	constexpr double BombShake = 0.8;
	constexpr double BombFlashFeatured = 1.0;
	constexpr double BombFlashOther = 0.4;
	constexpr double BombFovKick = 3.5;
	constexpr double FocusFlashPower = 0.7;
	constexpr double FocusFlashFringe = 0.2;
	/** Slow motion drops to this time scale at once and eases back from 30% of the beat. */
	constexpr double SlowMoScale = 0.4;
	constexpr double SlowMoEaseFrom = 0.3;
	constexpr double PracticeSlowMoSeconds = 0.5;
	/** Never two slow-motion beats closer than this after the last one ends. */
	constexpr double SlowMoCooldownSeconds = 4.0;
	/** Screen treatment decay rates (1/s) and the Focus mood's approach rate. */
	constexpr double FlashDecay = 9.0;
	constexpr double FringeDecay = 7.0;
	constexpr double FovKickDecay = 6.0;
	constexpr float FocusMoodRate = 4.f;
	constexpr float FocusPulseBase = 0.85f;
	constexpr float FocusPulseDepth = 0.15f;
	constexpr float FocusPulseRate = 5.f;

	// ---- Sounds (sound.ts) ----
	constexpr float SliceVolume = 0.55f;
	constexpr float SliceVolumePerPower = 0.45f;
	constexpr float SlicePitch = 0.92f;
	constexpr float SlicePitchPerPower = 0.22f;
	constexpr float HitVolume = 0.7f;
	constexpr float HitVolumePerPower = 0.3f;
	/** The combo bell climbs a semitone a cut from the second, up to an octave. */
	constexpr int32 BellMaxSemitones = 12;
	constexpr float SemitonesPerOctave = 12.f;
	constexpr float BellVolume = 0.6f;
	constexpr float BellVolumePerStep = 0.4f;
	constexpr float MissVolume = 0.85f;
	constexpr float TooSlowVolume = 0.25f;
	constexpr float TooSlowPitch = 1.6f;

	// ---- Banners and petals ----
	/** Every this-many combo shouts; from the second threshold it shouts in gold. */
	constexpr int32 ComboShoutEvery = 5;
	constexpr int32 GoldComboShout = 10;
	constexpr int32 ComboPetals = 18;
	constexpr double ComboPetalY = 3.0;
	constexpr double ComboPetalZ = 0.8;
	constexpr int32 FocusPetals = 40;
	constexpr double FocusPetalY = 3.2;
	constexpr double FocusPetalZ = 0.8;
	constexpr int32 WinnerPetals = 60;
	constexpr double WinnerPetalY = 3.4;
	constexpr double WinnerPetalZ = 1.0;

	// ---- Swordsman pose (SamuraiSwordsman.tsx) ----
	/** The arm, shoulder to palm, at the rig's .64 (Docs/Port/05 §3 rig): the pose's hand targets are in arms. */
	constexpr double ArmLength = 0.525;
	/** The off hand holds the grip this far below the sword hand. */
	constexpr double OffHandGrip = 0.13;
	/** The cut: a step into the stroke that snaps in and settles. */
	constexpr double StrikeSeconds = 0.6;
	constexpr double StrikeSnapSeconds = 0.05;
	constexpr double StrikeDecay = 5.5;
	constexpr double CrouchBase = 0.22;
	constexpr double CrouchPerEnergy = 0.15;
	constexpr double CrouchPerStrike = 0.22;
	constexpr double LeanBase = 0.06;
	constexpr double LeanPerEnergy = 0.16;
	constexpr double LeanPerDownBlade = 0.18;
	constexpr double LeanPerStrike = 0.2;
	constexpr double TwistPerBladeX = 0.35;
	constexpr double TwistPerStrike = 0.3;
	constexpr double LungePerStrike = 0.7;
	/** With nothing in the lane the swordsman watches this point; his eyes sit this high over his feet. */
	constexpr double IdleLookY = 1.55;
	constexpr double IdleLookZ = -1.2;
	constexpr double EyeHeightCm = 105.0;
	constexpr float LookWeight = 0.8f;
	/** Reactions ease in and out over these. */
	constexpr double ReactionInSeconds = 0.12;
	constexpr double ReactionOutSeconds = 0.25;
	constexpr float ComboCheer = 0.5f;
	constexpr float WinnerCheer = 0.85f;
	constexpr float LoserSlump = 0.5f;

	// ---- Camera (cameraDirector.ts arena, fitted to the lanes) ----
	constexpr double BaseFov = 45.0;
	/** Half the lanes' width: a lane each side of centre per this, plus the outer lane's margin. */
	constexpr double LaneHalfSpan = 1.8;
	constexpr double LaneEdgeMargin = 1.35;
	constexpr double FitAspectSlack = 0.98;
	constexpr double FitExtraDistance = 0.85;
	constexpr double MinDistance = 9.2;
	constexpr double SwayRate = 0.24;
	constexpr double SwayAmplitude = 0.22;
	constexpr double Height = 3.2;
	constexpr double BobRate = 0.31;
	constexpr double BobAmplitude = 0.1;
	constexpr double LookY = 1.9;
	/** The round-start sweep: from high and to the side onto the dojo. */
	constexpr double IntroSweepSeconds = 2.6;
	constexpr double IntroFromX = 5.5;
	constexpr double IntroFromY = 6.2;
	constexpr double IntroExtraDistance = 6.0;
	constexpr double IntroLookY = 2.6;
	constexpr double IntroLookZ = -3.0;
	constexpr double IntroFov = 52.0;
	/** The finale's push-in. */
	constexpr double FinaleSeconds = 3.2;
	constexpr double FinaleY = 2.4;
	constexpr double FinaleDistanceScale = 0.72;
	constexpr double FinaleLookY = 1.5;
	constexpr double FinaleLookZ = 0.6;
	/** Focus leans the lens in a touch (degrees). */
	constexpr double FocusFovLean = 1.6;
	/** Follow rates (1/s), the follow's longest step, and the impact decays (impact.ts: shake e^-9t, punch e^-14t). */
	constexpr double FollowRate = 5.5;
	constexpr double FollowMaxStep = 1.0 / 20.0;
	constexpr double FovFollowRate = 6.0;
	constexpr double ShakeDecay = 9.0;
	constexpr double PunchDecay = 14.0;
	constexpr double ShakeFrequencyX = 61.0;
	constexpr double ShakeFrequencyY = 53.0;
	constexpr double ShakePhaseY = 1.1;
	constexpr double ShakeAmplitudeX = 0.06;
	constexpr double ShakeAmplitudeY = 0.05;

	// ---- The practice apple (onboarding) ----
	/** The apple hangs this far right of the lane's centre; the tip waits up and to the left of it. */
	constexpr double PracticeOffsetX = 0.15;
	constexpr double PracticeTipOffsetX = -1.1;
	constexpr double PracticeTipY = 2.9;
	constexpr double PracticeSpinRate = 0.9;
	constexpr double PracticeBobAmplitude = 0.05;
	constexpr double PracticeBobRate = 2.2;
	/** The apple pops in over this long, and can't be cut before it has. */
	constexpr double PracticePopSeconds = 0.35;
	constexpr double PracticeHaloPulseDepth = 0.06;
	constexpr double PracticeHaloPulseRate = 6.0;
	/** -SamuraiPracticeDemo: the blade sweeps through between these ages, this far across and down. */
	constexpr double PracticeDemoFrom = 1.6;
	constexpr double PracticeDemoUntil = 2.2;
	constexpr double PracticeDemoSweepSeconds = 0.22;
	constexpr double PracticeDemoAcross = 2.6;
	constexpr double PracticeDemoDown = 1.6;
	/** The cut's juice, gentler than a round's. */
	constexpr double PracticeFlashPower = 0.6;
	constexpr double PracticeFlashFringe = 0.3;
	constexpr double PracticeHitStopSeconds = 0.07;
	constexpr double PracticePunch = 0.16;
	constexpr double PracticeNoteLift = 0.55;
	constexpr double PracticeCutNoteLife = 1.2;
	constexpr float PracticeCutNoteScale = 1.1f;
	constexpr float PracticeTooSlowNoteScale = 0.9f;
	/** "Clean cut!" holds this long before the round begins. */
	constexpr double PracticeCutHoldSeconds = 1.3;
	constexpr double PracticeHintCooldownSeconds = 1.0;
	/** Nobody cut it: the round starts anyway. */
	constexpr double PracticeTimeoutSeconds = 12.0;
}

/** The round's colours (sRGB hex as in the web original), parsed once. */
namespace MovecadeKart::Samurai::Palette
{
	inline const FLinearColor Telegraph = MovecadeKart::Art::Hex(TEXT("#d66d50"));
	inline const FLinearColor HaloLight = MovecadeKart::Art::Hex(TEXT("#fff5d9"));
	inline const FLinearColor FocusHalo = MovecadeKart::Art::Hex(TEXT("#ffcf6a"));
	inline const FLinearColor Gold = MovecadeKart::Art::Hex(TEXT("#ffd477"));
	inline const FLinearColor Jade = MovecadeKart::Art::Hex(TEXT("#7cf0c4"));
	inline const FLinearColor CutFlash = MovecadeKart::Art::Hex(TEXT("#fff2d6"));
	inline const FLinearColor BombFlash = MovecadeKart::Art::Hex(TEXT("#ff8a3d"));
	inline const FLinearColor ShardHot = MovecadeKart::Art::Hex(TEXT("#ffd08a"));
	inline const FLinearColor ShardCool = MovecadeKart::Art::Hex(TEXT("#ff4a1c"));
	inline const FLinearColor Dropped = MovecadeKart::Art::Hex(TEXT("#ff8a8a"));
	inline const FLinearColor Whiff(0.78f, 0.8f, 0.86f);
	inline const FLinearColor TrailCore(0.95f, 0.98f, 1.f);
}

/** The easing curves the round's presentation shares. */
namespace MovecadeKart::Samurai::Ease
{
	/** GLSL smoothstep: 0 below A, 1 above B. */
	inline double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** easings.net easeOutBack: overshoots a little before settling at 1. */
	inline double OutBack(double X)
	{
		constexpr double C1 = 1.70158, C3 = C1 + 1.0;
		return 1.0 + C3 * FMath::Pow(X - 1.0, 3.0) + C1 * FMath::Pow(X - 1.0, 2.0);
	}
}

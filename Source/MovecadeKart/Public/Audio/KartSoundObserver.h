#pragma once

#include "CoreMinimal.h"
#include "Audio/KartAudioMath.h"
#include "Sim/KartSim.h"

/**
 * One kart's share of the web's kart sound observer (games/kart/sound.ts createKartSound), plus the
 * physics one-shots the web fires from inside its physics frame (kartAudioEvents.ts playKartEvent:
 * hop, land, fall, respawn, wall impact), which the port derives from FKartState changes.
 *
 * Pure: it turns (sim state, input, race context) into commands for the mixer, so the whole mapping
 * is unit-tested without an audio device. UKartAudioComponent executes the commands.
 */
namespace MovecadeKart::Audio
{
	enum class ERacePhase : uint8
	{
		/** Lobby / grid not started: no kart sound. */
		Waiting,
		/** Engines idle in neutral and rev on the gas; no other layers. */
		Countdown,
		Racing,
		/** Results screen: no kart sound. */
		Results,
	};

	/** Item and status flags (the web KartAudioPlayer fields the sim does not have). The items layer sets them. */
	struct FKartAudioStatus
	{
		bool bRollingItem = false;
		/** heldItem != null */
		bool bHasItem = false;
		/** The held item's id, e.g. "LIGHTNING", "BLOOPER" (used when it is spent). */
		FName HeldItem;
		bool bInvincible = false;
		bool bBulletBill = false;
		bool bShrunk = false;
		/** screenEffect == INK_SPLATTER */
		bool bInk = false;
	};

	struct FKartAudioContext
	{
		ERacePhase Phase = ERacePhase::Racing;
		bool bPaused = false;
		/** Index of this kart among the karts sorted by spawn index, and their count (lane pan, 1/sqrt(N) mix). */
		int32 Lane = 0;
		int32 NumKarts = 1;
		int32 SpawnIndex = 0;
		/** Frame delta in seconds (the drivetrain uses sim steps while racing, this on the grid). */
		double FrameDt = 1.0 / 60.0;
	};

	enum class ELoopLayer : uint8
	{
		Skid,
		Wind,
		Dirt,
		Boost,
		Roulette,
		Aura,
		Rocket,
		Count
	};

	struct FLoopCommand
	{
		bool bActive = false;
		FName Cue;
		/** Option gain, before the cue's base gain (sink.setLoop opts.gain). */
		double Gain = 0.0;
		double Rate = 1.0;
		/** < 0: no filter. */
		double Lowpass = -1.0;
	};

	struct FOneShot
	{
		FName Cue;
		/** Option gain, before the cue's base gain. */
		double Gain = 1.0;
		double Rate = 1.0;
		double Pan = 0.0;
	};

	struct FKartAudioCommands
	{
		/** Stop every loop and the engine of this kart before applying the rest (stopLoops). */
		bool bStopAll = false;
		bool bEngine = false;
		double EngineRpm = IdleRpm;
		double EngineLoad = 0.0;
		double EngineCharacter = 0.0;
		/** Option gain for the engine (the mixer multiplies EngineGain 0.3 in). */
		double EngineMix = 1.0;
		double Pan = 0.0;
		FLoopCommand Loops[static_cast<int32>(ELoopLayer::Count)];
		TArray<FOneShot, TInlineAllocator<8>> Plays;
		/** The last drivetrain frame, for the debug readout. */
		FDrivetrainFrame Drivetrain;
		bool bRacing = false;
	};

	class MOVECADEKART_API FKartSoundObserver
	{
	public:
		/**
		 * Advance one rendered frame. State/Input/Track are the kart's after this frame's sim steps.
		 * Track may be null (no wall-impact probes then).
		 */
		FKartAudioCommands Observe(const FKartAudioContext& Context, const FKartState& State, const FKartInput& Input,
			const FKartAudioStatus& Status, const FTrack* Track);

		/** Forget everything, as on a new race (sound.ts reset). */
		void Reset();

		/** The web's isCollidingBarrier for this state, with the impact inputs (MultiplayerKart.tsx:1462-1490). */
		static bool BarrierContact(const FKartState& State, const FTrack& Track, double& OutPenetration, bool& bOutCritical);
		/** MultiplayerKart.tsx curIsBoosting: max speed above the 40 road top speed. */
		static bool IsBoosting(const FKartState& State);

	private:
		struct FSnapshot
		{
			double Speed = 0.0;
			double DriftPower = 0.0;
			bool bBoosting = false;
			FString ActiveBoost;
			bool bWrongWay = false;
			bool bFinished = false;
			int32 Laps = 0;
			double HopTime = -1.0;
			double OffTrackSince = -1.0;
			uint32 TeleportCount = 0;
			double WatchSince = 0.0;
			bool bBarrier = false;
			uint64 Frame = 0;
			FKartAudioStatus Status;
			ERacePhase Phase = ERacePhase::Waiting;
			bool bPaused = false;
		};

		TOptional<FDrivetrain> Drivetrain;
		FDrivetrainFrame LastEngine;
		bool bHasPrevious = false;
		FSnapshot Previous;
		int32 Backfires = 0;
	};
}

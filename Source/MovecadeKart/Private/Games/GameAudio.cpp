#include "Games/GameAudio.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FCue
	{
		const TCHAR* Asset;
		float Gain;
		float ThrottleMs;
	};

	/** cues.ts: file, base gain, throttle. */
	const TMap<FName, FCue>& Cues()
	{
		static const TMap<FName, FCue> Table = {
			{ TEXT("bowling.release"), { TEXT("bowling_release"), 0.55f, 120.f } },
			{ TEXT("bowling.roll"), { TEXT("bowling_roll"), 0.5f, 200.f } },
			{ TEXT("bowling.pins"), { TEXT("bowling_pins"), 0.7f, 120.f } },
			{ TEXT("bowling.gutter"), { TEXT("bowling_gutter"), 0.5f, 200.f } },
			{ TEXT("bowling.sweep"), { TEXT("bowling_sweep"), 0.35f, 300.f } },
			{ TEXT("bowling.strike"), { TEXT("bowling_strike"), 0.6f, 300.f } },
			{ TEXT("bowling.spare"), { TEXT("bowling_spare"), 0.55f, 300.f } },
			{ TEXT("darts.release"), { TEXT("darts_release"), 0.4f, 80.f } },
			{ TEXT("darts.thud"), { TEXT("darts_thud"), 0.6f, 40.f } },
			{ TEXT("darts.pub-chatter"), { TEXT("darts_pub_chatter"), 0.18f, 7000.f } },
			{ TEXT("darts.wire"), { TEXT("darts_wire"), 0.34f, 60.f } },
			{ TEXT("darts.bull"), { TEXT("darts_bull"), 0.62f, 120.f } },
			{ TEXT("darts.bounty"), { TEXT("darts_bounty"), 0.55f, 120.f } },
			{ TEXT("darts.miss"), { TEXT("darts_miss"), 0.3f, 80.f } },
			{ TEXT("darts.round"), { TEXT("darts_round"), 0.36f, 200.f } },
			{ TEXT("samurai.slice"), { TEXT("samurai_slice"), 0.5f, 60.f } },
			{ TEXT("samurai.hit"), { TEXT("samurai_hit"), 0.62f, 35.f } },
			{ TEXT("samurai.combo"), { TEXT("samurai_combo"), 0.42f, 40.f } },
			{ TEXT("samurai.bomb"), { TEXT("samurai_bomb"), 0.85f, 90.f } },
			{ TEXT("samurai.miss"), { TEXT("samurai_miss"), 0.3f, 90.f } },
			{ TEXT("stacker.drop"), { TEXT("stacker_drop"), 0.6f, 30.f } },
			{ TEXT("stacker.perfect"), { TEXT("stacker_perfect"), 0.5f, 30.f } },
			{ TEXT("stacker.debris"), { TEXT("stacker_debris"), 0.26f, 55.f } },
			{ TEXT("stacker.miss"), { TEXT("stacker_miss"), 0.34f, 90.f } },
			{ TEXT("stacker.altitude"), { TEXT("stacker_altitude"), 0.4f, 400.f } },
			// Sumo Bumpers (no recordings of its own: the other games' sounds, re-pitched by the game).
			{ TEXT("sumo.bump"), { TEXT("samurai_hit"), 0.55f, 45.f } },
			{ TEXT("sumo.crash"), { TEXT("bowling_pins"), 0.5f, 90.f } },
			{ TEXT("sumo.dash"), { TEXT("samurai_slice"), 0.5f, 60.f } },
			{ TEXT("sumo.block"), { TEXT("darts_wire"), 0.6f, 80.f } },
			{ TEXT("sumo.fall"), { TEXT("bowling_gutter"), 0.45f, 120.f } },
			{ TEXT("sumo.splash"), { TEXT("samurai_bomb"), 0.45f, 120.f } },
			{ TEXT("sumo.ko"), { TEXT("darts_bull"), 0.55f, 150.f } },
			{ TEXT("sumo.power"), { TEXT("stacker_perfect"), 0.55f, 80.f } },
			{ TEXT("sumo.spawn"), { TEXT("darts_bounty"), 0.35f, 300.f } },
			{ TEXT("sumo.warn"), { TEXT("stacker_altitude"), 0.5f, 600.f } },
			{ TEXT("sumo.crumble"), { TEXT("stacker_debris"), 0.6f, 300.f } },
			{ TEXT("sumo.round"), { TEXT("darts_round"), 0.4f, 600.f } },
			{ TEXT("sumo.win"), { TEXT("bowling_strike"), 0.6f, 600.f } },
			{ TEXT("sumo.draw"), { TEXT("stacker_miss"), 0.45f, 600.f } },
			{ TEXT("match.over"), { TEXT("match_over"), 0.5f, 800.f } },
			{ TEXT("lobby.tick"), { TEXT("lobby_tick"), 0.5f, 200.f } },
			{ TEXT("lobby.go"), { TEXT("lobby_go"), 0.6f, 400.f } },
		};
		return Table;
	}

	/**
	 * Each cue's wave, loaded once and held (so GC never unloads it between plays). Heap-allocated and never freed on
	 * purpose: strong refs must not be released by static destruction after the UObject system is gone.
	 */
	TMap<FName, TStrongObjectPtr<USoundWave>>& Waves()
	{
		static TMap<FName, TStrongObjectPtr<USoundWave>>* Map = new TMap<FName, TStrongObjectPtr<USoundWave>>();
		return *Map;
	}

	USoundWave* WaveFor(FName Cue, const FCue& Spec)
	{
		TMap<FName, TStrongObjectPtr<USoundWave>>& Map = Waves();
		if (const TStrongObjectPtr<USoundWave>* Found = Map.Find(Cue))
		{
			return Found->Get();
		}
		USoundWave* Wave = LoadObject<USoundWave>(nullptr, *FString::Printf(TEXT("/Game/Audio/Games/%s.%s"), Spec.Asset, Spec.Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
		// A missing file is remembered too (a null entry), so it is looked for once, not on every play.
		Map.Add(Cue, TStrongObjectPtr<USoundWave>(Wave));
		return Wave;
	}
}

namespace MovecadeKart::Games
{
	void FCuePlayer::Preload(const TCHAR* Prefix)
	{
		for (const TPair<FName, FCue>& Pair : Cues())
		{
			if (!Prefix || Pair.Key.ToString().StartsWith(Prefix))
			{
				WaveFor(Pair.Key, Pair.Value);
			}
		}
	}

	void FCuePlayer::Play(const UObject* WorldContext, FName Cue, float Gain, float Rate)
	{
		const FCue* Spec = Cues().Find(Cue);
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!Spec || !World)
		{
			return;
		}
		const double Now = World->GetRealTimeSeconds();
		if (const double* Last = LastPlayed.Find(Cue); Last && (Now - *Last) * 1000.0 < Spec->ThrottleMs)
		{
			return;
		}
		USoundWave* Wave = WaveFor(Cue, *Spec);
		if (!Wave)
		{
			return;
		}
		LastPlayed.Add(Cue, Now);
		UGameplayStatics::PlaySound2D(World, Wave, FMath::Clamp(Spec->Gain * Gain, 0.f, 2.f), FMath::Clamp(Rate, 0.25f, 4.f));
	}
}

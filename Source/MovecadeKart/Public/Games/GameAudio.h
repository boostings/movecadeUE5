#pragma once

#include "CoreMinimal.h"

class UObject;
class USoundWave;

/**
 * The party games' sound cues (movecade-v2 app/src/audio/cues.ts): each recorded file (/Game/Audio/Games, from
 * Tools/audio/games) with its base gain and a throttle that drops repeats closer than its window, played 2D with a
 * per-call gain and rate, like the web's audio service.
 */
namespace MovecadeKart::Games
{
	class MOVECADEKART_API FCuePlayer
	{
	public:
		/** Plays Cue (e.g. "bowling.strike") at Gain x its base gain and Rate. Unknown cues and missing files are silent. */
		void Play(const UObject* WorldContext, FName Cue, float Gain = 1.f, float Rate = 1.f);

	private:
		TMap<FName, double> LastPlayed;
	};
}

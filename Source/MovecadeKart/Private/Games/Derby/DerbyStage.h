#pragma once

#include "CoreMinimal.h"
#include "Games/Derby/DerbyLog.h"

/**
 * What ADerbyGame's source files share (private to Games/Derby): the cue names it plays and the stage's fixed points
 * in the ballpark's metres (home plate at the origin, centre field down +X, right field +Y).
 */

namespace MovecadeKart::Derby::Stage
{
	/** Metres to UE cm. */
	inline FVector ToCm(const FVector& Metres) { return Metres * 100.0; }

	/** The pitcher's release, the front of the plate (contact) and the backstop a missed pitch runs on to. */
	inline const FVector ReleaseM(16.9, 0.32, 1.88);
	inline const FVector PlateM(0.25, 0.0, 0.86);
	inline const FVector BackstopM(-17.0, 0.0, 0.5);

	/** The baseball reads from the stands at about twice its size. */
	constexpr double BallScale = 1.9;
}

/** GameAudio.cpp's derby cues. */
namespace MovecadeKart::Derby::Cue
{
	inline const FName StepIn(TEXT("derby.stepin"));
	inline const FName Swing(TEXT("derby.swing"));
	inline const FName Whiff(TEXT("derby.whiff"));
	inline const FName Pitch(TEXT("derby.pitch"));
	inline const FName Mitt(TEXT("derby.mitt"));
	inline const FName Out(TEXT("derby.out"));
	inline const FName Crack(TEXT("derby.crack"));
	inline const FName Sweet(TEXT("derby.sweet"));
	inline const FName Homer(TEXT("derby.homer"));
	inline const FName Moonshot(TEXT("derby.moonshot"));
	inline const FName Wall(TEXT("derby.wall"));
	inline const FName Land(TEXT("derby.land"));
	inline const FName Foul(TEXT("derby.foul"));
	inline const FName Firework(TEXT("derby.firework"));
	inline const FName Crowd(TEXT("derby.crowd"));
	inline const FName MatchOver(TEXT("match.over"));
}

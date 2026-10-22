#pragma once

#include "CoreMinimal.h"
#include "Games/Derby/DerbyRules.h"

/** What ADerbyBallpark's source files share (private to Games/Derby). */
namespace MovecadeKart::Derby::Ballpark
{
	/** On every component the park builds, so a rebuild finds and removes them. */
	inline const FName PartTag(TEXT("DerbyBallpark"));

	/** The fence's distance along a spray angle (degrees, + right field), metres: the rules' wall, built to match. */
	inline double FenceAt(double SprayDeg) { return FenceDistanceM(SprayDeg); }
}

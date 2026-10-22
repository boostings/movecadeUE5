#pragma once

#include "CoreMinimal.h"
#include "Games/Darts/DartsRules.h"

class FJsonObject;

/**
 * A phone's darts:release, from its JSON payload to a gesture the rules can trust (runtime.ts
 * dartsGestureFromPayload). ADartsGameMode parses it; ADartsGame bounds it before the throw. Pure.
 */
namespace MovecadeKart::Darts
{
	/** The payload's fields as a gesture (missing ones take the web's defaults); unset without a numeric power. */
	TOptional<FDartGesture> GestureFromRelease(const FJsonObject& Data);

	/** The payload's bounds: a non-finite field falls back to its default, and every field clamps to its range. */
	FDartGesture BoundRelease(const FDartGesture& Gesture);
}

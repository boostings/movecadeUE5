#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Integer reads from the track and scenery JSON. A JSON number is a double, and static_cast<int32> of one that is
 * NaN, infinite or outside int32 is undefined behaviour, so every integer field goes through these instead: a value
 * that isn't finite reads as the default, anything else is truncated toward zero (as the cast did) and clamped.
 */
namespace MovecadeKart::Json
{
	/** V as an int32: Default when V isn't finite, else trunc(V) clamped to [Min, Max]. Silent (for bulk arrays). */
	int32 ToInt32(double V, int32 Default, int32 Min, int32 Max);

	/** O[Key] by ToInt32, with a warning when the value had to be replaced or clamped. Default when O is null or the
	 *  field is missing or not a number. */
	int32 IntField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int32 Default, int32 Min, int32 Max);
}

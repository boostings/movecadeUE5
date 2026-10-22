#pragma once

#include "CoreMinimal.h"

namespace MovecadeKart::Net { struct FSensorFrame; }

/** How Sumo Bumpers reads a phone held flat as a tilt pad (pure, so the tests cover it). */
namespace MovecadeKart::Sumo
{
	/** Full tilt at this many degrees of phone tilt... */
	constexpr double PhoneTiltFullDeg = 24.0;
	/** ...and nothing under this dead zone, so a phone resting on a palm sits still. */
	constexpr double PhoneTiltDeadDeg = 2.5;

	/**
	 * The rules' tilt (the unit disc; X away from the camera, Y to its right) for a phone's sensor frame. The phone
	 * held flat is level; the normal of its attitude leans the way it tips: pitch away rolls forward, roll across
	 * rolls sideways. Frames without an attitude quaternion fall back to the DeviceOrientation angles.
	 */
	FVector2D PhoneTilt(const Net::FSensorFrame& Frame);
}

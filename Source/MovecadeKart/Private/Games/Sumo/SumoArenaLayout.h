#pragma once

#include "CoreMinimal.h"
#include "Games/Sumo/SumoRules.h"

/**
 * ASumoArena's shared layout (SumoArena.cpp, SumoArenaRing.cpp, SumoArenaProps.cpp): the tag its built parts carry,
 * the ring's band layout and colours, the moon's direction, and the conversion from the rules' metres.
 */
namespace MovecadeKart::Sumo::ArenaLayout
{
	/** Every component the arena builds carries it, so a rebuild can find and remove them. */
	inline FName ArenaTag()
	{
		static const FName Tag(TEXT("SumoArena"));
		return Tag;
	}

	/** Segments per band, outside in (the last is the core): finer at the rim so the pieces stay about a metre. */
	constexpr int32 BandSegments[] = { 36, 32, 28, 24, 20, 16, 12, 6 };
	/** The bands' neon edges alternate cyan and violet; the core's is pink. */
	const TCHAR* const TrimColors[] = { TEXT("#41e8ff"), TEXT("#b56bff") };
	const TCHAR* const CoreTrimColor = TEXT("#ff4fa8");
	/** The outer band's edge glows brightest. */
	constexpr float OuterTrimGlow = 5.f;
	constexpr float TrimGlow = 3.2f;
	/** Toward the moon, low over the far sea behind the ring. */
	const FVector MoonDirection = FVector(0.82, -0.42, 0.40).GetSafeNormal();

	/** A paper lantern's shape (an engine sphere, a little taller than wide): built and bobbed with it. */
	const FVector LanternScale(0.42, 0.42, 0.55);

	/** cm from rule metres. */
	inline FVector Cm(double X, double Y, double Z)
	{
		return FVector(X, Y, Z) * 100.0;
	}

	/** The ring's thickness at its rim (m) and how much deeper it gets per metre toward the middle. */
	constexpr double RimDepth = 0.32;
	constexpr double DepthPerMetreIn = 0.3;

	/** How deep the ring's underside is at a radius (m): thin at the rim, a keel toward the middle. */
	inline double Depth(double Radius)
	{
		return RimDepth + (ArenaRadius - Radius) * DepthPerMetreIn;
	}
}

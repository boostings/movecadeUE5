#pragma once

#include "CoreMinimal.h"

/**
 * AStackerRooftop's modelling data (RooftopEnvironment.tsx, RooftopProps.tsx, StackerCanvas.tsx): the golden-hour
 * look and every part's placement and size, in web units (x across, y up, z toward the camera) unless a name says
 * cm. Only StackerRooftop.cpp includes it; each Build* function reads its own part.
 */
namespace MovecadeKart::Stacker::RooftopShapes
{
	const TCHAR* const SkyMaterialPath = TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky");
	const TCHAR* const CloudTexturePath = TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds");

	/** StackerCanvas.tsx's golden hour: light directions (web, toward the light), colours, haze and sky tint. */
	struct FGoldenHour
	{
		static constexpr double SunX = 11.0, SunY = 15.0, SunZ = 10.0;
		static constexpr double RimX = 10.0, RimY = 5.0, RimZ = -14.0;
		static constexpr const TCHAR* SunColor = TEXT("#ffd09a");
		static constexpr const TCHAR* RimColor = TEXT("#9fc4ff");
		static constexpr const TCHAR* GroundColor = TEXT("#7a6a5a");
		static constexpr float GroundBounce = 0.5f;
		static constexpr const TCHAR* HorizonColor = TEXT("#e3ae94");
		static constexpr const TCHAR* SkyTopColor = TEXT("#2a5580");
		static constexpr const TCHAR* SkyBelowColor = TEXT("#8c7a6a");
		static constexpr const TCHAR* SkySunColor = TEXT("#ffcf94");
		static constexpr const TCHAR* SunHazeColor = TEXT("#ffc27a");
		static constexpr const TCHAR* CloudLitColor = TEXT("#ffe2c4");
		static constexpr float FogCutoff = 900000.f;
		static constexpr float FogHorizonShare = 0.75f;
		static constexpr float SunHazeShare = 0.6f;
		static constexpr float SunHazeExponent = 6.f;
		static constexpr double SaturationR = 1.08, SaturationG = 1.06, SaturationB = 1.0;
		static constexpr double Contrast = 1.06;
		static constexpr float SkyMidMix = 0.4f;
		static constexpr double SkySunX = 0.36, SkySunY = 0.085, SkySunZ = -1.0;
		static constexpr float SunDiscRadians = 0.035f;
		static constexpr float SunGlow = 1.1f;
		static constexpr float GlowPower = 9.0f;
		static constexpr float HorizonGlow = 0.9f;
		static constexpr float SkyBrightness = 1.0f;
		static constexpr float SkyClouds = 0.4f;
		static constexpr float SkyCloudScale = 0.35f;
		static constexpr float CloudShadeMix = 0.45f;
		static constexpr float CloudShadeDarken = 0.8f;
	};

	// The venue's modelling data (RooftopEnvironment.tsx / RooftopProps.tsx), in web units (x across, y up, z toward
	// the camera; Box sizes are x, y, z extents), grouped by part. A part's numbers are only read by its Build* function.

	/** A box placed relative to a part's anchor: centre offset and size. */
	struct FBoxPart
	{
		double X, Y, Z;
		double SizeX, SizeY, SizeZ;
		bool bShadows;
	};

	inline FVector OffsetOf(const FBoxPart& Part)
	{
		return FVector(Part.X, Part.Y, Part.Z);
	}

	inline FVector SizeOf(const FBoxPart& Part)
	{
		return FVector(Part.SizeX, Part.SizeY, Part.SizeZ);
	}

	/** The roof deck: its slab, the parapet, the lip along the front and the hazard stripe on it. */
	struct FDeckShape
	{
		static constexpr double SlabDrop = 0.05;
		static constexpr double SlabThick = 0.1;
		static constexpr double CentreZ = 1.0;
		static constexpr double Depth = 14.0;
		static constexpr double ParapetY = -1.03;
		static constexpr double ParapetThick = 0.66;
		static constexpr double LipRise = 0.25;
		static constexpr double LipZ = 8.0;
		static constexpr double LipHeight = 0.5;
		static constexpr double LipDeep = 0.3;
		static constexpr double StripeBlock = 0.8;
		static constexpr double StripeLift = 0.003;
		static constexpr double StripeZ = 7.3;
		static constexpr double StripeThick = 0.01;
		static constexpr double StripeDeep = 0.3;
	};

	/** A lane's work zone: a dashed outline in the lane's colour, the plinth, its cap and the painted number. */
	struct FZoneShape
	{
		static constexpr int32 FrontDashes = 9;
		static constexpr int32 SideDashes = 5;
		static constexpr double HalfWidth = 3.65;
		static constexpr double DashStep = 0.91;
		static constexpr double DashLift = 0.004;
		static constexpr double CentreZ = 1.0;
		static constexpr double HalfDepth = 2.4;
		static constexpr double SideDashStart = 0.25;
		static constexpr double SideDashStep = 1.1;
		static constexpr double DashLong = 0.5;
		static constexpr double DashThin = 0.08;
		static constexpr double DashThick = 0.01;
		static constexpr float DashShade = 0.9f;
		/** The plinth rises from the deck to just under the foundation block. */
		static constexpr double PlinthTop = 0.03;
		static constexpr double PlinthWide = 6.78;
		static constexpr double PlinthDeep = 2.08;
		static constexpr float PlinthShade = 0.75f;
		static constexpr double CapY = 0.02;
		static constexpr double CapWide = 6.9;
		static constexpr double CapThick = 0.02;
		static constexpr double CapDeep = 2.2;
		static constexpr double NumberX = 2.8;
		static constexpr double NumberLift = 0.01;
		static constexpr double NumberZ = 3.0;
		static constexpr float NumberSize = 60.f;
	};

	/** The building under the roof (its body runs down out of frame) and its window grid toward the camera. */
	struct FBuildingShape
	{
		static constexpr double TopY = -1.36;
		static constexpr double Height = 25.0;
		static constexpr double Inset = 0.3;
		static constexpr double Deep = 13.6;
		static constexpr int32 WindowRows = 14;
		static constexpr double WindowPitchX = 1.6;
		static constexpr double WindowPitchY = 1.7;
		static constexpr double FirstWindowY = -2.6;
		static constexpr double WindowZ = 7.82;
		static constexpr double WindowHalfWide = 0.4;
		static constexpr double WindowHalfTall = 0.475;
	};

	/** RooftopEnvironment.tsx makeCity: a band's spread, each tower's random size, its crown and window grid. */
	struct FCityShape
	{
		static constexpr double SpanBase = 26.0;
		static constexpr double SpanPerBand = 22.0;
		static constexpr double Jitter = 0.6;
		static constexpr double MinWidthShare = 0.7;
		static constexpr double MaxWidthShare = 0.95;
		static constexpr double MinDepth = 4.0;
		static constexpr double MaxDepth = 8.0;
		static constexpr double HeightBase = 14.0;
		static constexpr double HeightPerBand = 4.0;
		static constexpr double HeightRandom = 6.0;
		static constexpr double HeightRandomPerBand = 5.0;
		static constexpr double Base = -26.0;
		static constexpr double SetbackMinHeight = 18.0;
		static constexpr double SetbackChance = 0.6;
		static constexpr double SetbackRise = 2.0;
		static constexpr double SetbackShare = 0.7;
		static constexpr double SetbackHeight = 4.0;
		static constexpr double LedgeRise = 0.08;
		static constexpr double LedgeShare = 1.02;
		static constexpr double LedgeThick = 0.16;
		static constexpr double AntennaChance = 0.35;
		static constexpr double AntennaOffset = 0.2;
		static constexpr double AntennaRise = 1.6;
		static constexpr double AntennaThick = 0.08;
		static constexpr double AntennaHeight = 3.2;
		static constexpr double WindowPitchX = 0.9;
		static constexpr double WindowPitchY = 1.2;
		static constexpr double WindowFirstY = 1.0;
		static constexpr double WindowTopMargin = 0.6;
		static constexpr double WindowProud = 0.02;
		static constexpr double WindowWideShare = 0.25;
		static constexpr double WindowHalfTall = 0.3;
	};

	/** RooftopEnvironment.tsx: each cloud's puffs (offset and radii, web units) around its centre. */
	struct FPuff
	{
		double X, Y, Z;
		double RadiusX, RadiusY, RadiusZ;
	};
	const FPuff CloudPuffs[] = {
		{ 0.0, 0.0, 0.0, 8.0, 1.15, 2.6 },
		{ -2.0, 0.5, 0.2, 4.2, 1.9, 2.5 },
		{ 5.0, -0.15, 0.0, 3.5, 1.1, 2.0 },
		{ 2.2, 0.75, 0.4, 2.6, 1.3, 1.8 },
	};
	struct FCloudShape
	{
		static constexpr double Y = 9.0;
		static constexpr double AlternateRise = 2.0;
		static constexpr double Z = -62.0;
		static constexpr int32 Rings = 10;
		static constexpr int32 Segments = 18;
	};

	/** The crane behind the deck, from its base: a lattice mast (legs and rungs), then its fixed parts. */
	struct FCraneShape
	{
		static constexpr double BaseOutside = 2.0;
		static constexpr double BaseZ = -13.0;
		static constexpr double LegOffset = 0.45;
		static constexpr double LegThick = 0.12;
		static constexpr double MastHeight = 12.0;
		static constexpr int32 Rungs = 12;
		static constexpr double FirstRung = 0.5;
		static constexpr double RungWide = 0.9;
		static constexpr double RungThick = 0.08;
	};
	/** The jib and counter-jib (crane paint), then the counterweight, the cab top, the cable and the hook (steel). */
	const FBoxPart CraneJibs[] = {
		{ 6.0, 11.4, 0.0, 12.0, 0.5, 0.6, true },
		{ -2.5, 11.4, 0.0, 5.0, 0.5, 0.6, true },
	};
	const FBoxPart CraneSteel[] = {
		{ -4.2, 10.9, 0.0, 1.4, 1.0, 1.0, true },
		{ 0.0, 12.3, 0.0, 0.2, 1.8, 0.2, true },
		{ 9.0, 8.4, 0.0, 0.03, 6.0, 0.03, false },
		{ 9.0, 5.3, 0.0, 0.4, 0.3, 0.3, true },
	};

	/** The railing along the back of the deck and down its sides: two rails and posts about two metres apart. */
	struct FRailShape
	{
		static constexpr double Inset = 0.2;
		static constexpr double BackZ = -3.5;
		static constexpr double SideZ = 2.25;
		static constexpr double SideLength = 11.5;
		static constexpr double Thick = 0.05;
		static constexpr double PostSpacing = 2.0;
		static constexpr double PostCentreY = 0.5;
		static constexpr double PostHeight = 1.0;
	};
	const double RailHeights[] = { 0.45, 0.95 };

	/** The props near the deck's sides (x from the side line, W / 2 - SideInset): cones, a pallet of boxes, a spool. */
	struct FPropShape
	{
		static constexpr double SideInset = 1.6;
		static constexpr double ConeRadius = 0.18;
		static constexpr double ConeHeight = 0.55;
		static constexpr double SpoolX = 0.3;
		static constexpr double SpoolY = 0.45;
		static constexpr double SpoolZ = 2.2;
		static constexpr double SpoolDrum = 0.9;
		static constexpr double SpoolWidth = 0.5;
		static constexpr double SpoolCore = 0.4;
		static constexpr double SpoolCoreWidth = 0.52;
	};
	/** The two cones (x from the left side line, z), the pallet and its three boxes (x from the right side line). */
	const double ConeSpots[][2] = { { 0.0, 4.6 }, { 0.7, 5.2 } };
	const FBoxPart Pallet = { 0.0, 0.07, 4.0, 1.2, 0.14, 1.0, true };
	const FBoxPart PalletBoxes[] = {
		{ -0.25, 0.42, 4.0, 0.55, 0.56, 0.6, true },
		{ 0.3, 0.36, 3.85, 0.45, 0.44, 0.5, true },
		{ 0.05, 0.86, 3.95, 0.42, 0.32, 0.44, true },
	};

	/** The HVAC unit by the right side (from its foot), its grille and its four-bladed fan (cm under the fan pivot). */
	struct FHvacShape
	{
		static constexpr double OffsetX = -0.6;
		static constexpr double Z = -2.0;
		static constexpr int32 Blades = 4;
		static constexpr double BladeReachCm = 20.0;
		static constexpr double BladeLong = 0.4;
		static constexpr double BladeWide = 0.12;
		static constexpr double BladeThin = 0.02;
		static constexpr double FanY = 1.16;
	};
	const FBoxPart HvacBody = { 0.0, 0.55, 0.0, 1.8, 1.1, 1.2, true };
	const FBoxPart HvacGrille = { 0.0, 1.12, 0.0, 1.0, 0.04, 1.0, false };

	/** The two flags at the back corners: pole, hinge, the cloth hanging off it (cm, scale), and their wave. */
	struct FFlagShape
	{
		static constexpr double CornerInset = 0.6;
		static constexpr double Z = -3.2;
		static constexpr double PoleRadius = 0.04;
		static constexpr double PoleHeight = 3.2;
		static constexpr double HingeY = 2.85;
		static constexpr double ClothAcrossCm = 50.0;
		static constexpr double ClothDropCm = -20.0;
		static constexpr double ClothThin = 0.02;
		static constexpr double ClothLong = 1.0;
		static constexpr double ClothTall = 0.6;
		static constexpr double YawBase = 20.0;
		static constexpr double YawSwing = 14.0;
		static constexpr double YawRate = 1.7;
		static constexpr double YawPhase = 1.3;
		static constexpr double RollSwing = 4.0;
		static constexpr double RollRate = 3.1;
	};
}

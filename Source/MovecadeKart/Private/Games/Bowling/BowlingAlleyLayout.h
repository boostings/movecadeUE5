#pragma once

// ABowlingAlley's data: where every part of the house sits and what it looks like (venueLayout.ts, laneLayout.ts and
// the port's art pass). Web world units throughout (x across, y up, z toward the bowler; the head pin at DeckZ).
// BowlingAlley.cpp only reads it: one table per part, applied in order.

#include "Art/KartMaterials.h"
#include "CoreMinimal.h"
#include "Games/Bowling/BowlingAlley.h"
#include "Games/Bowling/BowlingPins.h"

/** One box of the house: its centre and size (web x, y, z), an optional yaw about web y, and whether it shadows. */
struct FAlleyBox
{
	double X = 0.0, Y = 0.0, Z = 0.0;
	double W = 1.0, H = 1.0, D = 1.0;
	double Yaw = 0.0;
	bool bShadows = true;
};

namespace MovecadeKart::Bowling::AlleyLayout
{
	// ---- The floor plan.

	/** Lane centres house-left to house-right; the player bowls the middle one. */
	constexpr double LaneCenters[] = { -7.1, -3.55, 0.0, 3.55, 7.1 };
	constexpr double DividerCenters[] = { -8.875, -5.325, -1.775, 1.775, 5.325, 8.875 };
	constexpr double DeckZ = ABowlingAlley::DeckZ;
	constexpr double LaneVisualWidth = ABowlingAlley::LaneVisualWidth;
	constexpr double LaneFrontZ = 1.35;
	constexpr double PitDepth = 2.25;
	constexpr double MachineZ = -16.82;
	constexpr double MaskingZ = -15.02;
	constexpr double FoulZ = 0.55;
	constexpr double LaneBackZ = DeckZ - PitDepth;
	constexpr double LaneSurfaceLength = LaneFrontZ - LaneBackZ;
	constexpr double LaneSurfaceCenterZ = (LaneFrontZ + LaneBackZ) / 2.0;
	/** The trough lights hang over each lane pair and the played lane. */
	constexpr double TroughCenters[] = { -5.325, 0.0, 5.325 };

	/** The rack and the ball, at the web's visual scales (BowlingPins.h). */
	constexpr double PinHeight = VisualPinHeight;
	constexpr double PinBelly = VisualPinRadius;
	constexpr double BallRadius = VisualBallRadius;
	constexpr double DeckY = VisualDeckY;

	// ---- Colour and light.

	/** The house neon: hot magenta, ice cyan and the house violet, and how hard each run glows. */
	const TCHAR* const NeonPink = TEXT("#ff3fb4");
	const TCHAR* const NeonCyan = TEXT("#38e1ff");
	const TCHAR* const NeonViolet = TEXT("#8a6cff");
	const TCHAR* const FoulLineColor = TEXT("#ff2a3d");
	constexpr float TubeGlow = 3.0f;
	constexpr float VioletGlow = 2.2f;
	constexpr float KickGlow = 2.4f;
	constexpr float FoulGlow = 1.6f;
	/** The played lane's arrows glow faintly, so a new bowler can see what to aim over. */
	constexpr float MarkerGlow = 0.9f;
	/** The steady lights: the masking panels, the cove under them, the trough lenses, the pendant bulbs, the art. */
	const TCHAR* const TroughColor = TEXT("#f3ecdc");
	constexpr float PanelGlow = 1.4f;
	constexpr float CoveGlow = 2.6f;
	constexpr float LensGlow = 2.4f;
	constexpr float TungstenGlow = 2.2f;
	constexpr float BillboardGlow = 1.15f;

	/** The house's surfaces: a colour and a finish each (roughness, metallic). */
	namespace Looks
	{
		inline Art::FSurfaceLook Carpet() { return { Art::Hex(TEXT("#2b2140")), TEXT("leafy_grass"), 0.6, 0.35, 0.5, 0.4, 0.95, 0.0, 0.0, false, 1.0 }; }
		inline Art::FSurfaceLook Wall() { return { Art::Hex(TEXT("#221d30")), TEXT("plastered_wall_02"), 2.4, 0.35, 0.5, 0.4, 0.9 }; }
		inline Art::FSurfaceLook Wallpaper() { return { Art::Hex(TEXT("#33284d")), TEXT("plastered_wall_02"), 1.6, 0.45, 0.6, 0.4, 0.88 }; }
		inline Art::FSurfaceLook Wainscot() { return { Art::Hex(TEXT("#5a3a26")), TEXT("dark_wooden_planks"), 1.2, 0.5, 0.7, 0.4, 0.45 }; }
		/** Lacquered maple, mirror-glossy so the neon and the troughs streak down the lane in its reflections. */
		inline Art::FSurfaceLook Maple() { return { Art::Hex(TEXT("#9a7450")), TEXT("dark_wooden_planks"), 0.9, 0.55, 0.3, 0.18, 0.09, 0.0, 0.0, false, 0.35 }; }
		inline Art::FSurfaceLook Approach() { return { Art::Hex(TEXT("#b9895a")), TEXT("dark_wooden_planks"), 1.4, 0.45, 0.35, 0.25, 0.2, 0.0, 0.0, false }; }
		/** Lacquered ivory pins with a glossy red neck: they catch the deck key like the real thing. */
		inline Art::FSurfaceLook PinIvory() { return { Art::Hex(TEXT("#fbf6ec")), TEXT("plastered_wall_02"), 0.4, 0.06, 0.15, 0.12, 0.14 }; }
		const TCHAR* const PinNeckColor = TEXT("#d0102c");
		constexpr double PinNeckRoughness = 0.16;

		const TCHAR* const CeilingColor = TEXT("#16141c");
		constexpr double CeilingRoughness = 0.9;
		const TCHAR* const SlabColor = TEXT("#6e4526");
		constexpr double SlabRoughness = 0.38;
		const TCHAR* const DarkColor = TEXT("#0f0f16");
		constexpr double DarkRoughness = 0.76;
		const TCHAR* const MachineColor = TEXT("#22242a");
		constexpr double MachineRoughness = 0.45;
		constexpr double MachineMetallic = 0.2;
		const TCHAR* const ReturnColor = TEXT("#f1e6d2");
		constexpr double ReturnRoughness = 0.36;
		const TCHAR* const GutterColor = TEXT("#9ea3ad");
		constexpr double GutterRoughness = 0.22;
		constexpr double GutterMetallic = 0.75;
		const TCHAR* const KickColor = TEXT("#15131d");
		constexpr double KickRoughness = 0.35;
		constexpr double KickMetallic = 0.2;
		const TCHAR* const PitColor = TEXT("#050507");
		constexpr double PitRoughness = 0.95;
		const TCHAR* const PanelColor = TEXT("#1a1630");
		const TCHAR* const CoveColor = TEXT("#fff0d4");
		const TCHAR* const IronColor = TEXT("#1b1a22");
		constexpr double IronRoughness = 0.55;
		constexpr double IronMetallic = 0.6;
		const TCHAR* const TungstenColor = TEXT("#ffd9a8");
		const TCHAR* const StandoffColor = TEXT("#0c0b12");
		constexpr double StandoffRoughness = 0.4;
		constexpr double StandoffMetallic = 0.5;
	}

	// ---- The room.

	namespace Shell
	{
		constexpr FAlleyBox Carpet[] = { { 0.0, -0.3, -4.0, 22.0, 0.14, 24.0 } };
		/** The back wall behind the machines, both side walls, and the near wall behind the concourse (the reverse shots). */
		constexpr FAlleyBox Walls[] = {
			{ 0.0, 4.2, -16.55 - 0.3, 24.0, 9.2, 0.55 }, { -11.4, 4.2, -4.0, 0.5, 9.2, 24.0 }, { 11.4, 4.2, -4.0, 0.5, 9.2, 24.0 },
			{ 0.0, 4.2, 8.4, 24.0, 9.2, 0.55 },
		};
		constexpr FAlleyBox Wallpaper[] = {
			{ 0.0, 4.55, -16.24 - 0.3, 22.4, 7.4, 0.02 }, { -11.13, 4.55, -4.0, 0.02, 7.4, 22.8 }, { 11.13, 4.55, -4.0, 0.02, 7.4, 22.8 },
		};
		constexpr FAlleyBox Wainscot[] = { { -11.1, 0.72, -4.0, 0.04, 1.55, 22.8 }, { 11.1, 0.72, -4.0, 0.04, 1.55, 22.8 } };
		/** The violet line where the wainscot meets the wallpaper. */
		constexpr FAlleyBox Accent[] = {
			{ -11.08, 1.52, -4.0, 0.03, 0.035, 22.6, 0.0, false }, { 11.08, 1.52, -4.0, 0.03, 0.035, 22.6, 0.0, false },
		};
		constexpr FAlleyBox Ceiling[] = { { 0.0, 5.85 + 0.16, -4.0, 24.0, 0.32, 24.0 } };
	}

	// ---- The lanes (per lane or per divider: X is an offset from its centre).

	namespace Lanes
	{
		constexpr FAlleyBox Approach[] = { { 0.0, -0.13, 2.55, 18.3, 0.22, 4.2 } };
		constexpr FAlleyBox FoulLine[] = { { 0.0, 0.015, FoulZ, 18.3, 0.025, 0.09, 0.0, false } };
		/** The boards over a dark slab. */
		constexpr FAlleyBox Slab[] = { { 0.0, -0.07, LaneSurfaceCenterZ, LaneVisualWidth, 0.14, LaneSurfaceLength } };
		constexpr FAlleyBox Maple[] = { { 0.0, 0.008, LaneSurfaceCenterZ, LaneVisualWidth - 0.06, 0.02, LaneSurfaceLength - 0.05 } };
		/** Targeting arrows at 4.6 m and guide dots at 2 m past the foul line: too small to shadow anything. */
		constexpr double ArrowOffsets[] = { -0.46, -0.23, 0.0, 0.23, 0.46 };
		constexpr FAlleyBox Arrow[] = { { 0.0, 0.02, -1.62, 0.07, 0.006, 0.18, UE_PI / 4.0, false } };
		constexpr double DotOffsets[] = { -0.38, -0.19, 0.0, 0.19, 0.38 };
		constexpr FAlleyBox Dot[] = { { 0.0, 0.02, -4.7, 0.05, 0.006, 0.05, 0.0, false } };
		/** A ball return on an inner divider at the approach: its hood, lid and mouth. */
		constexpr FAlleyBox ReturnHood[] = { { 0.0, 0.22, 1.75, 0.62, 0.44, 1.5 } };
		constexpr FAlleyBox ReturnLid[] = { { 0.0, 0.45, 1.77, 0.66, 0.04, 1.54 } };
		constexpr FAlleyBox ReturnMouth[] = { { 0.0, 0.2, 2.47, 0.38, 0.2, 0.08 } };
		/** The gutter channels sit just under the boards; the outer ones get a capping wall CappingOut further out. */
		constexpr double GutterY = -0.05;
		constexpr double CappingOut = 0.29;
		constexpr FAlleyBox Capping[] = { { 0.0, 0.02, LaneSurfaceCenterZ, 0.18, 0.24, LaneSurfaceLength - 0.2 } };
	}

	// ---- The pin decks and the machines behind them.

	namespace Deck
	{
		constexpr FAlleyBox MachineRun[] = { { 0.0, 1.85, MachineZ, 19.6, 3.8, 1.6 } };
		constexpr FAlleyBox MachineHood[] = { { 0.0, 1.85 + 1.55, MachineZ + 0.55, 19.0, 0.62, 1.9 } };
		/** Per divider: the machine's rib, and the kickback with its neon trim along the top. */
		constexpr FAlleyBox MachineRib[] = { { 0.0, 1.7, MachineZ + 0.55, 0.48, 3.5, 0.55 } };
		constexpr double KickFrom = DeckZ + 1.1;
		constexpr double KickTo = MaskingZ + 0.2;
		constexpr FAlleyBox Kickback[] = { { 0.0, 0.55, (KickFrom + KickTo) / 2.0, 0.14, 1.1, KickFrom - KickTo } };
		constexpr FAlleyBox KickbackNeon[] = {
			{ 0.0, 1.115, (KickFrom + KickTo) / 2.0, 0.05, 0.03, KickFrom - KickTo, 0.0, false },
			{ 0.0, 0.55, KickFrom + 0.005, 0.05, 1.1, 0.03, 0.0, false },
		};
		/** Per lane: the pinsetter housing, and the pit, a black well behind the deck. */
		constexpr FAlleyBox Housing[] = { { 0.0, 1.4, MachineZ + 0.86, 3.7, 2.35, 0.3 }, { 0.0, 2.4, MachineZ + 0.92, 3.5, 1.15, 0.12 } };
		constexpr FAlleyBox Pit[] = { { 0.0, -0.35, DeckZ - 1.6, LaneVisualWidth + 0.2, 0.6, 2.2 } };
		/** The masking units' art: the Movecade billboards. */
		const TCHAR* const BillboardPaths[] = {
			TEXT("/Game/Tracks/Shared/Textures/mc_billboard_0.mc_billboard_0"), TEXT("/Game/Tracks/Shared/Textures/mc_billboard_1.mc_billboard_1"),
			TEXT("/Game/Tracks/Shared/Textures/mc_billboard_2.mc_billboard_2"), TEXT("/Game/Tracks/Shared/Textures/mc_billboard_3.mc_billboard_3"),
		};
	}

	/** A masking unit over each deck (X an offset from the lane's centre): a backlit panel framed in rails. */
	namespace Masking
	{
		constexpr double PanelY = 2.05;
		constexpr FAlleyBox Frame[] = {
			{ 0.0, PanelY, MaskingZ - 0.12, 3.62, 1.98, 0.18 },
			{ -1.73, PanelY, MaskingZ + 0.06, 0.1, 1.98, 0.12 }, { 1.73, PanelY, MaskingZ + 0.06, 0.1, 1.98, 0.12 },
		};
		constexpr FAlleyBox Panel[] = { { 0.0, PanelY, MaskingZ, 3.34, 1.72, 0.02, 0.0, false } };
		constexpr FAlleyBox Art[] = { { 0.0, PanelY, MaskingZ + 0.012, 3.2, 1.6, 0.01, 0.0, false } };
		constexpr FAlleyBox Rails[] = {
			{ 0.0, PanelY + 0.93, MaskingZ + 0.06, 3.46, 0.14, 0.16 }, { 0.0, PanelY - 0.93, MaskingZ + 0.06, 3.46, 0.14, 0.16 },
		};
		constexpr FAlleyBox Shadow[] = { { 0.0, PanelY - 1.62, MaskingZ - 0.18, 3.3, 0.9, 0.02 } };
		/** The cove under the panel lights the rack from the front; a violet line runs along the top. */
		constexpr FAlleyBox Cove[] = { { 0.0, PanelY - 1.03, MaskingZ + 0.11, 3.3, 0.06, 0.06, 0.0, false } };
		constexpr FAlleyBox Accent[] = { { 0.0, PanelY + 0.99, MaskingZ + 0.11, 3.3, 0.022, 0.05, 0.0, false } };
	}

	/** The truss: paired chords every 3 m with hangers, heavier beams every 6 m, girders down the dividers. */
	namespace Ceiling
	{
		constexpr double ChordZ[] = { -13.2, -10.2, -7.2, -4.2, -1.2, 1.8, 4.8 };
		constexpr FAlleyBox Chords[] = { { 0.0, 5.5, 0.0, 23.0, 0.16, 0.16 }, { 0.0, 5.08, 0.0, 23.0, 0.16, 0.16 } };
		constexpr double HangerX[] = { -9.0, -6.0, -3.0, 0.0, 3.0, 6.0, 9.0 };
		constexpr FAlleyBox Hanger[] = { { 0.0, 5.29, 0.0, 0.06, 0.5, 0.06, 0.0, false } };
		constexpr double BeamZ[] = { -13.2, -7.2, -1.2, 4.8 };
		constexpr FAlleyBox Beam[] = { { 0.0, 5.18, 0.0, 23.0, 0.24, 0.26 } };
		constexpr double GirderX[] = { -8.875, -1.775, 1.775, 8.875 };
		constexpr FAlleyBox Girder[] = { { 0.0, 5.18, -4.0, 0.26, 0.24, 23.4 } };
		/** A linear light trough and its lens (per trough centre). */
		constexpr FAlleyBox Trough[] = { { 0.0, 5.24, -5.6, 0.62, 0.2, 16.2 } };
		constexpr FAlleyBox TroughLens[] = { { 0.0, 5.13, -5.6, 0.4, 0.04, 15.6, 0.0, false } };
		/** Two warm pendants over the approach settee: a rod, an iron shade, a bulb. */
		constexpr double PendantX[] = { -3.8, 3.8 };
		constexpr double PendantZ = 2.6;
		constexpr FAlleyBox PendantRod[] = { { 0.0, 5.24, PendantZ, 0.02, 0.62, 0.02, 0.0, false } };
		constexpr double ShadeY = 4.5, ShadeRadius = 0.32, ShadeHeight = 0.22;
		constexpr double BulbY = 4.44, BulbRadius = 0.2, BulbHeight = 0.06;
	}

	/** The neon runs: two tubes down each side wall on dark standoffs, two across the back wall. */
	namespace NeonRuns
	{
		constexpr FAlleyBox SidePink[] = { { -11.07, 3.05, -4.0, 0.05, 0.05, 22.4, 0.0, false }, { 11.07, 3.05, -4.0, 0.05, 0.05, 22.4, 0.0, false } };
		constexpr FAlleyBox SideCyan[] = { { -11.07, 3.3, -4.0, 0.05, 0.05, 22.4, 0.0, false }, { 11.07, 3.3, -4.0, 0.05, 0.05, 22.4, 0.0, false } };
		constexpr double StandoffX = 11.09;
		constexpr double StandoffFirstZ = -14.5, StandoffLastZ = 6.6, StandoffStep = 3.0;
		constexpr FAlleyBox Standoff[] = { { 0.0, 3.18, 0.0, 0.04, 0.4, 0.04, 0.0, false } };
		constexpr double BackZ = -16.22;
		constexpr FAlleyBox BackCyan[] = { { 0.0, 3.32, BackZ, 19.4, 0.05, 0.05, 0.0, false } };
		constexpr FAlleyBox BackPink[] = { { 0.0, 4.9, BackZ, 19.4, 0.05, 0.05, 0.0, false } };
	}

	/** The house sign over the played lane: a neon ball (a ring and its finger holes) between two neon pins. */
	namespace Sign
	{
		constexpr double LogoY = 4.12;
		constexpr double LogoAheadOfWall = 0.02;
		constexpr int32 RingSegments = 28;
		constexpr double RingRadius = 0.46;
		/** Each segment a little longer than its arc, so the ring reads as one tube. */
		constexpr double RingOverlap = 1.08;
		constexpr double FingerHoles[][2] = { { -0.12, 0.14 }, { 0.12, 0.14 }, { 0.0, -0.08 } };
		/** The pins: their base this far either side of the ball and below it, their outline in Steps per flank. */
		constexpr double PinX = 1.25;
		constexpr double PinBaseY = -0.62;
		constexpr double PinOutlineHeight = 1.24;
		constexpr double PinOutlineBelly = 0.2;
		constexpr int32 PinOutlineSteps = 14;
		/** The neck band sits at this share of the height. */
		constexpr double PinNeck = 0.69;
		/** The tubes' cross-sections (web units: across the wall, out of it), and how much each piece overlaps the next. */
		constexpr double RingTube = 0.05, HoleSize = 0.09, OutlineTube = 0.045, TubeDepth = 0.04;
		constexpr double OutlineOverlap = 1.12;
		/** The pin's base and neck band, as shares of the belly. */
		constexpr double BaseWidth = 0.9, NeckWidth = 0.75, NeckTube = 0.05;
	}

	// ---- The lights.

	/** The deck key: cool, tight and by far the brightest light, so the pins are the brightest thing in frame. */
	namespace DeckKeyLight
	{
		constexpr double FromY = 3.35, FromZ = DeckZ + 0.75;
		constexpr double ToY = 0.0, ToZ = DeckZ - 0.85;
		constexpr float Lumens = 5600.f;
		constexpr float RadiusCm = 700.f;
		constexpr float ConeRadians = 0.78f;
		constexpr float InnerCone = 0.4f;
		const TCHAR* const Color = TEXT("#dceaff");
		constexpr float SourceRadiusCm = 30.f;
	}

	/** A fill light: where (web units), how bright (lumens), how far (metres), what colour. They never shadow. */
	struct FFillLight
	{
		double X, Y, Z;
		float Lumens;
		float RadiusM;
		const TCHAR* Color;
	};
	constexpr FFillLight FillLights[] = {
		// The deck's front fill and its glow from above.
		{ 0.0, 1.55, DeckZ - 1.15, 1400.f, 5.f, TEXT("#fff0d4") },
		{ 0.0, 4.6, DeckZ - 0.25, 1700.f, 12.f, TEXT("#dcd2c0") },
		// The approach: warm, so the bowler reads against the cool lane.
		{ 0.0, 4.3, 1.4, 2400.f, 11.f, TEXT("#ffc78d") },
		{ 0.0, 4.3, -3.4, 1300.f, 10.f, TEXT("#ffbf82") },
		{ 8.4, 2.3, 7.4, 1400.f, 9.f, TEXT("#e0954a") },
		// The neon's spill on the side walls and the outer lanes.
		{ -10.4, 3.2, -9.0, 900.f, 7.f, TEXT("#ff3fb4") },
		{ -10.4, 3.2, -1.0, 700.f, 7.f, TEXT("#38e1ff") },
		{ 10.4, 3.2, -9.0, 900.f, 7.f, TEXT("#38e1ff") },
		{ 10.4, 3.2, -1.0, 700.f, 7.f, TEXT("#ff3fb4") },
		{ 0.0, 4.2, -15.6, 700.f, 5.f, TEXT("#ff3fb4") },
	};
	constexpr float FillSourceRadiusCm = 15.f;

	/** The soft rect light under each trough: its long highlight runs down the lacquer. */
	namespace TroughLight
	{
		constexpr double Y = 5.08, Z = -5.6;
		constexpr float WidthCm = 40.f, LengthCm = 1560.f;
		constexpr float PlayedLumens = 3200.f, HouseLumens = 2000.f;
		constexpr float RadiusCm = 1000.f;
		constexpr float BarnDoorDegrees = 60.f;
	}

	/** The show: four coloured spots in the truss, aimed across the deck, dark until a strike. */
	struct FShowSpot
	{
		double X, Y, Z;
		const TCHAR* Color;
	};
	constexpr FShowSpot ShowSpots[] = {
		{ -2.8, 5.0, -8.6, TEXT("#ff3fb4") }, { 2.8, 5.0, -8.6, TEXT("#38e1ff") },
		{ -2.2, 5.0, -11.4, TEXT("#38e1ff") }, { 2.2, 5.0, -11.4, TEXT("#ff3fb4") },
	};
	namespace ShowSpot
	{
		/** Each spot aims at a point on the deck pulled toward the middle (AimPull of its own x), and sweeps around it. */
		constexpr double AimPull = 0.4;
		constexpr double AimY = 0.2;
		constexpr double AimZ = DeckZ - 0.6;
		constexpr float RadiusCm = 1200.f;
		constexpr float OuterDegrees = 16.f, InnerDegrees = 6.f;
		/** At full energy; scaled by the show. */
		constexpr double Lumens = 12000.0;
		/** The pairs' base aim (x = Left + Step * (spot % 2)) and their sweep's reach and rates. */
		constexpr double BaseLeftX = -1.4, BaseStepX = 2.8;
		constexpr double SweepX = 1.2, SweepZ = 0.8, SweepRateX = 2.2, SweepRateZ = 1.7;
		/** A flash's floor and its beat-driven part. */
		constexpr double FlashFloor = 0.35, FlashBeat = 0.65;
		/** A turkey tints the spots this far toward its colour every frame. */
		constexpr float TurkeyTint = 0.1f;
	}

	// The light show (SetShow): the beat (radians per second; a turkey's strobe is faster), and how hard it pulses.
	constexpr double ShowBeat = 5.0;
	constexpr double TurkeyBeat = 9.0;
	/** Energy above this is "on": below it the room rests. */
	constexpr double ShowOnEnergy = 0.001;
	/** Energy past 1 only strobes faster; the brightness caps at this. */
	constexpr double MaxShowEnergy = 1.5;
	/** The neon brightens by Rest + Pulse of the energy on the beat; each tube a phase step behind the last. */
	constexpr double NeonRest = 0.4, NeonPulse = 0.8, NeonPhaseStep = 1.3;
	/** The deck key flashes up to this much brighter, at twice the beat. */
	constexpr double DeckKeyFlash = 0.6;

	// The grade: no sun indoors, so auto exposure is held to a narrow band set by the lane's light.
	namespace Grade
	{
		constexpr float ExposureMin = 6.3f;
		constexpr float ExposureMax = 6.9f;
		constexpr float Bloom = 0.85f;
		constexpr float BloomThreshold = 1.0f;
		constexpr float Vignette = 0.45f;
		constexpr float Saturation = 1.08f, SaturationBlue = 1.1f;
		constexpr float Contrast = 1.07f;
		constexpr float Fringe = 0.25f;
	}

	// ---- The meshes made in code.

	/** pinGeometry.ts PROFILE: (height 0..1, radius relative to the belly). */
	constexpr double PinProfile[][2] = {
		{ 0, 0 }, { 0.001, 0.426 }, { 0.012, 0.438 }, { 0.035, 0.52 }, { 0.07, 0.66 }, { 0.11, 0.8 }, { 0.16, 0.91 },
		{ 0.22, 0.978 }, { 0.3, 1 }, { 0.37, 0.975 }, { 0.43, 0.918 }, { 0.49, 0.835 }, { 0.545, 0.72 }, { 0.595, 0.57 },
		{ 0.64, 0.43 }, { 0.672, 0.348 }, { 0.7, 0.318 }, { 0.735, 0.362 }, { 0.775, 0.46 }, { 0.82, 0.53 }, { 0.86, 0.548 },
		{ 0.9, 0.51 }, { 0.935, 0.41 }, { 0.965, 0.25 }, { 0.985, 0.11 }, { 1, 0 },
	};
	/** The pin's lathe runs: (from, to, is a red neck band) along its height. */
	constexpr double PinBands[][3] = { { 0.0, 0.658, 0 }, { 0.658, 0.686, 1 }, { 0.686, 0.704, 0 }, { 0.704, 0.732, 1 }, { 0.732, 1.0, 0 } };
	/** Lathe density: profile steps per unit of height, and segments around. */
	constexpr double PinStepsPerHeight = 60.0;
	constexpr int32 PinSegments = 40;
	/** Bands stand a hair proud of the body so they never z-fight. */
	constexpr double BandProud = 1.006;
	/** The ball's three-hole grip (directions from the centre), the holes' size and depth as shares of the radius. */
	constexpr double BallHoles[][3] = { { 0.3, -0.18, 0.93 }, { 0.3, 0.18, 0.93 }, { -0.32, 0.0, 0.94 } };
	constexpr double HoleDepth = 0.975, HoleRadius = 0.13, HoleThickness = 0.04;
	constexpr int32 BallRings = 24, BallSegments = 36;
	/** The gutter's half pipe: its radius (UE cm) and segments. */
	constexpr double GutterRadiusCm = 20.0;
	constexpr int32 GutterSegments = 16;
}

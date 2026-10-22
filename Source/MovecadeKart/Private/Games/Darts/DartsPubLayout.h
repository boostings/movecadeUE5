#pragma once

#include "CoreMinimal.h"
#include "Art/KartMaterialCache.h"
#include "Games/Darts/DartsPub.h"

/**
 * ADartsPub's data: the room's dimensions, its looks (FDartsPubLooks, made once per build) and its layout, part by
 * part, as labelled tables the build functions in DartsPub.cpp walk. Web metres (x across, y up, z toward the
 * camera), pub-local (z offset by ADartsPub::PubZ) unless a table says otherwise.
 */
namespace MovecadeKart::Darts::PubLayout
{
	/** pubWidth(1): the 44 degree frame at the one-lane camera distance, wider than the cabinet bay. */
	constexpr double PubWidth = 8.14;
	constexpr double PubDepth = 22.0;
	constexpr double BarX = 2.61;
	constexpr double PrintX = -2.61;
	constexpr double BarZ = 1.05;
	constexpr double WainscotHeight = 1.35;
	/** The festoon: bulbs on a sagging cord, wall to wall across the brick above the cabinet. */
	constexpr int32 FestoonBulbs = 15;
	constexpr double FestoonY = 4.15;
	constexpr double FestoonSag = 0.4;
	constexpr double FestoonZ = 0.42;

	/** Glow strengths (times GlowGain) for the room's lit parts. */
	constexpr float WindowGlow = 1.4f;
	constexpr float ShelfGlow = 3.2f;
	constexpr float AccentGlow = 1.35f;
	constexpr float LampStripGlow = 2.6f;
	constexpr float FestoonGlow = 4.f;
	constexpr float JukeboxGlow = 2.2f;
	constexpr float JukeboxTealGlow = 1.8f;
	constexpr float PendantGlow = 3.2f;
	/** The lamps, strips and windows are matte under their glow. */
	constexpr double LampRoughness = 0.9;
	/** The framed prints are backlit, a touch warm. */
	inline const FLinearColor PrintTint(0.75f, 0.68f, 0.6f);
}

/** Every look the room uses, made once per build through the venue's material cache (DartsPub.tsx materials). */
struct FDartsPubLooks
{
	UMaterialInterface* Brick = nullptr;
	UMaterialInterface* Plaster = nullptr;
	UMaterialInterface* Wainscot = nullptr;
	UMaterialInterface* Floor = nullptr;
	UMaterialInterface* WoodDark = nullptr;
	UMaterialInterface* Ceiling = nullptr;
	UMaterialInterface* Brass = nullptr;
	UMaterialInterface* NightWindow = nullptr;
	UMaterialInterface* OcheMat = nullptr;
	UMaterialInterface* BarTop = nullptr;
	UMaterialInterface* Panel = nullptr;
	UMaterialInterface* Chalkboard = nullptr;
	/** A smoked mirror: Lumen reflects the bottles and the room in it. */
	UMaterialInterface* Mirror = nullptr;
	UMaterialInterface* ShelfStrip = nullptr;
	UMaterialInterface* Beer = nullptr;
	UMaterialInterface* Foam = nullptr;
	UMaterialInterface* CapSteel = nullptr;
	UMaterialInterface* Prints[3] = {};
	UMaterialInterface* Bottles[6] = {};
	UMaterialInterface* Cabinet = nullptr;
	UMaterialInterface* CabinetDark = nullptr;
	UMaterialInterface* LaneAccent = nullptr;
	UMaterialInterface* LampStrip = nullptr;
	UMaterialInterface* Collar = nullptr;
	/** The face's slots: single beds (cream, night), trebles and doubles (red, teal), the surround. */
	UMaterialInterface* Sisal[4] = {};
	UMaterialInterface* Surround = nullptr;
	UMaterialInterface* Wire = nullptr;
	UMaterialInterface* StoolSeat = nullptr;
	UMaterialInterface* Festoon = nullptr;
	UMaterialInterface* Cord = nullptr;
	UMaterialInterface* Jukebox = nullptr;
	UMaterialInterface* JukeboxTeal = nullptr;
	UMaterialInterface* Enamel = nullptr;
	UMaterialInterface* PendantBulb = nullptr;

	FDartsPubLooks(UObject* Outer, FKartMaterialCache& Cache);
};

/** One box of the room: a look, its centre and size (pub-local web metres, z offset by PubZ), and its shadow. */
struct FDartsPubBox
{
	UMaterialInterface* FDartsPubLooks::* Look = nullptr;
	FVector Centre;
	FVector Size;
	bool bShadows = true;
};

// The room's layout (DartsPub.tsx, pubLayout.ts), part by part. Web metres: x across, y up, z toward the camera.
namespace MovecadeKart::Darts::PubLayout
{
	using Look = FDartsPubLooks;

	constexpr double WallY = (ADartsPub::FloorY + ADartsPub::CeilingY) / 2.0;
	constexpr double WallH = ADartsPub::CeilingY - ADartsPub::FloorY;
	constexpr double MidZ = PubDepth / 2.0;
	constexpr double HalfWidth = PubWidth / 2.0;
	constexpr double FloorY = ADartsPub::FloorY;
	constexpr double CeilingY = ADartsPub::CeilingY;

	/** Brick behind the board and behind the camera, the wainscot and its brass rail along the back, skirting and
	 *  cornice, the floor and the ceiling. */
	inline const FDartsPubBox Envelope[] = {
		{ &Look::Brick, FVector(0.0, WallY, 0.11), FVector(PubWidth + 0.4, WallH, 0.2) },
		{ &Look::Wainscot, FVector(0.0, FloorY + WainscotHeight / 2.0, 0.23), FVector(PubWidth, WainscotHeight, 0.04) },
		{ &Look::Brass, FVector(0.0, FloorY + WainscotHeight, 0.27), FVector(PubWidth, 0.04, 0.04), false },
		{ &Look::WoodDark, FVector(0.0, FloorY + 0.09, 0.28), FVector(PubWidth, 0.18, 0.06) },
		{ &Look::WoodDark, FVector(0.0, CeilingY - 0.28, 0.28), FVector(PubWidth, 0.16, 0.12) },
		{ &Look::Floor, FVector(0.0, FloorY - 0.05, MidZ), FVector(PubWidth, 0.1, PubDepth) },
		{ &Look::Ceiling, FVector(0.0, CeilingY + 0.1, MidZ), FVector(PubWidth + 0.4, 0.2, PubDepth) },
		{ &Look::Brick, FVector(0.0, WallY, PubDepth + 0.1), FVector(PubWidth + 0.4, WallH, 0.2) },
	};

	/** Down each side wall (x is the inset from the wall's line, mirrored): plaster, wainscot, its brass rail. */
	inline const FDartsPubBox SideWall[] = {
		{ &Look::Plaster, FVector(-0.1, WallY, MidZ), FVector(0.2, WallH, PubDepth) },
		{ &Look::Wainscot, FVector(0.02, FloorY + WainscotHeight / 2.0, MidZ), FVector(0.04, WainscotHeight, PubDepth) },
		{ &Look::Brass, FVector(0.04, FloorY + WainscotHeight, MidZ), FVector(0.04, 0.04, PubDepth), false },
	};

	/** The ceiling beams across the room, at these depths. */
	constexpr double BeamZs[] = { 1.4, 5.2, 9.4, 13.6 };
	inline const FDartsPubBox Beam = { &Look::WoodDark, FVector(0.0, CeilingY - 0.18, 0.0), FVector(PubWidth, 0.3, 0.25) };

	/** The oche: a worn mat and the brass throw line (the lettering is OcheText). */
	inline const FDartsPubBox Oche[] = {
		{ &Look::OcheMat, FVector(0.0, FloorY + 0.012, ADartsPub::OcheZ - ADartsPub::PubZ + 0.25), FVector(3.0, 0.024, 1.3), false },
		{ &Look::Brass, FVector(0.0, FloorY + 0.03, 5.18), FVector(2.6, 0.03, 0.05), false },
	};
	inline const FVector OcheTextAt(0.0, FloorY + 0.03, 4.62);
	constexpr float OcheTextSize = 16.f;

	/** A night window on each side wall at these depths (x inset from the wall, mirrored): frame, glass, mullion. */
	constexpr double WindowZs[] = { 2.6, 7.2 };
	inline const FDartsPubBox Window[] = {
		{ &Look::WoodDark, FVector(0.04, 3.15, 0.0), FVector(0.06, 2.25, 1.58) },
		{ &Look::NightWindow, FVector(0.075, 3.15, 0.0), FVector(0.01, 2.05, 1.38), false },
		{ &Look::WoodDark, FVector(0.08, 3.15, 0.0), FVector(0.02, 0.05, 1.38), false },
	};

	/** The bar: worn top, panelled front, the carcass. Its brass foot rail is a lying cylinder (FootRail*). */
	inline const FDartsPubBox Bar[] = {
		{ &Look::BarTop, FVector(BarX, FloorY + 1.08, BarZ), FVector(2.7, 0.08, 0.72) },
		{ &Look::Panel, FVector(BarX, FloorY + 0.52, BarZ + 0.3), FVector(2.6, 1.04, 0.06) },
		{ &Look::WoodDark, FVector(BarX, FloorY + 0.52, BarZ), FVector(2.5, 1.04, 0.56) },
	};
	inline const FVector FootRailAt(BarX, FloorY + 0.2, BarZ + 0.42);
	inline const FVector FootRailScale(0.05, 0.05, 2.5);

	/** A pint: the beer and its head (Post radius, height), the head this far above the glass's foot. */
	struct FPint
	{
		double BeerRadius = 0.04;
		double BeerHeight = 0.15;
		double FoamRadius = 0.041;
		double FoamHeight = 0.025;
		double FoamAbove = 0.15;
	};
	inline const FPint Pint;
	/** Pints on the bar for the regulars (x across the bar), on the bar top. */
	constexpr double BarPintXs[] = { BarX - 0.62, BarX - 0.05, BarX + 0.6 };
	constexpr double BarPintY = FloorY + 1.12;
	constexpr double BarPintZ = BarZ + 0.18;

	/** Framed prints with picture lights, on the far side and above the cabinet: where, and how big. */
	struct FPrint
	{
		FVector At;
		double Scale = 1.0;
	};
	inline const FPrint Prints[] = { { FVector(PrintX, 1.75, 0.34), 0.88 }, { FVector(-1.65, 1.95, 0.34), 0.54 }, { FVector(0.0, 2.35, 0.34), 0.54 } };
	/** A print at scale 1: frame, picture (just proud of it) and the brass picture light above. */
	inline const FVector PrintFrame(1.44, 1.84, 0.06);
	inline const FVector PrintPicture(1.28, 1.68, 0.01);
	constexpr double PrintPictureProud = 0.035;
	inline const FVector PrintLight(0.5, 0.04, 0.08);
	constexpr double PrintLightAbove = 1.0;
	constexpr double PrintLightProud = 0.1;

	/** The back bar: shelf, backing, the smoked mirror, the warm strip that lights the bottles from below. */
	inline const FDartsPubBox BackBar[] = {
		{ &Look::WoodDark, FVector(BarX, FloorY + 2.05, 0.38), FVector(2.2, 0.05, 0.3) },
		{ &Look::WoodDark, FVector(BarX, FloorY + 2.32, 0.25), FVector(2.5, 1.0, 0.02) },
		{ &Look::Mirror, FVector(BarX, FloorY + 2.32, 0.265), FVector(2.3, 0.86, 0.01), false },
		{ &Look::ShelfStrip, FVector(BarX, FloorY + 2.02, 0.5), FVector(2.1, 0.012, 0.02), false },
	};
	/** Bottles along the shelf (x from the bar's centre); every other one stands BottleStagger further back. */
	constexpr double BottleXs[] = { -0.82, -0.49, -0.16, 0.16, 0.49, 0.82 };
	constexpr double BottleY = FloorY + 2.075;
	constexpr double BottleZ = 0.36;
	constexpr double BottleStagger = 0.05;

	/** The chalkboard over the back bar: frame and slate; the title and four score lines (cm on the board). */
	inline const FDartsPubBox Chalkboard[] = {
		{ &Look::WoodDark, FVector(BarX, 2.95, 0.3), FVector(2.06, 1.3, 0.06) },
		{ &Look::Chalkboard, FVector(BarX, 2.95, 0.335), FVector(1.86, 1.1, 0.01), false },
	};
	inline const FVector ChalkAt(BarX, 2.95, 0.345);
	constexpr double ChalkTitleZ = 38.0;
	constexpr float ChalkTitleSize = 15.f;
	constexpr int32 ChalkRows = 4;
	constexpr double ChalkFirstRowZ = 14.0;
	constexpr double ChalkRowPitch = 17.0;
	constexpr float ChalkRowSize = 12.f;

	/** The cabinet round the board (DartsScene.tsx Cabinet; board-centred): back, rails top and bottom, sides, the
	 *  lane accent, and the board lamp: a brass arm off the top, the hood, the glowing strip under it. */
	inline const FDartsPubBox Cabinet[] = {
		{ &Look::Cabinet, FVector(0.0, 0.0, -0.30), FVector(2.15, 2.35, 0.34) },
		{ &Look::CabinetDark, FVector(0.0, 1.12, -0.10), FVector(2.35, 0.12, 0.22) },
		{ &Look::CabinetDark, FVector(0.0, -1.12, -0.10), FVector(2.35, 0.12, 0.22) },
		{ &Look::CabinetDark, FVector(-1.12, 0.0, -0.10), FVector(0.12, 2.35, 0.22) },
		{ &Look::CabinetDark, FVector(1.12, 0.0, -0.10), FVector(0.12, 2.35, 0.22) },
		{ &Look::LaneAccent, FVector(0.0, -0.98, 0.0), FVector(0.9, 0.02, 0.03) },
	};
	inline const FDartsPubBox BoardLamp[] = {
		{ &Look::Brass, FVector(0.0, 1.32, 0.2), FVector(0.05, 0.05, 0.75) },
		{ &Look::Brass, FVector(0.0, 1.3, 0.62), FVector(0.95, 0.09, 0.24) },
		{ &Look::LampStrip, FVector(0.0, 1.25, 0.62), FVector(0.85, 0.012, 0.08) },
	};
	/** The collar behind the board: a disc this many board radii across, this thick, this far behind the face. */
	constexpr double CollarRadii = 1.42;
	constexpr double CollarThickness = 0.04;
	constexpr double CollarBehind = 0.1;
	/** The number ring, in board radii, just proud of the surround. */
	constexpr double NumberRingRadius = 1.25;
	constexpr double NumberRingLift = 0.004;
	constexpr float NumberSize = 8.5f;

	/** Stools: at the bar (each turned a little differently, as if just left: StoolYawPerX degrees per metre). */
	constexpr double BarStoolXs[] = { BarX - 0.85, BarX, BarX + 0.85 };
	constexpr double BarStoolZ = BarZ + 0.72;
	constexpr double StoolYawPerX = 37.0;

	/** A high table in the far corner with two stools and a couple of pints left on it. */
	constexpr double TableX = -2.75;
	constexpr double TableZ = 3.6;
	struct FTablePost
	{
		UMaterialInterface* FDartsPubLooks::* Look;
		double Y;
		double Radius;
		double Height;
	};
	inline const FTablePost TableParts[] = { { &Look::Brass, FloorY, 0.035, 1.02 }, { &Look::WoodDark, FloorY, 0.22, 0.03 }, { &Look::BarTop, FloorY + 1.02, 0.36, 0.04 } };
	struct FPlaced
	{
		double X;
		double Z;
		double Yaw;
	};
	inline const FPlaced TableStools[] = { { TableX - 0.55, TableZ + 0.2, 20.0 }, { TableX + 0.45, TableZ + 0.45, -35.0 } };
	inline const FVector2D TablePints[] = { FVector2D(TableX - 0.1, TableZ + 0.08), FVector2D(TableX + 0.14, TableZ - 0.06) };
	constexpr double TablePintY = FloorY + 1.06;

	/** The jukebox against the left wall, past the windows: cabinet, the glowing grille, its teal bands, the brass
	 *  kick plate (x from the jukebox's line). */
	constexpr double JukeboxX = -HalfWidth + 0.36;
	constexpr double JukeboxZ = 4.9;
	inline const FDartsPubBox Jukebox[] = {
		{ &Look::WoodDark, FVector(0.0, FloorY + 0.7, JukeboxZ), FVector(0.62, 1.4, 0.95) },
		{ &Look::Jukebox, FVector(0.315, FloorY + 1.05, JukeboxZ), FVector(0.01, 0.55, 0.7), false },
		{ &Look::JukeboxTeal, FVector(0.318, FloorY + 1.42, JukeboxZ), FVector(0.01, 0.08, 0.8), false },
		{ &Look::JukeboxTeal, FVector(0.318, FloorY + 0.62, JukeboxZ), FVector(0.01, 0.05, 0.8), false },
		{ &Look::Brass, FVector(0.32, FloorY + 0.3, JukeboxZ), FVector(0.02, 0.32, 0.62), false },
	};
	/** The festoon spans the brick this far in from each wall; its cord is this thick (cylinder scale). */
	constexpr double FestoonInset = 0.3;
	constexpr double FestoonCordScale = 0.008;

	/** The pendants: the oche's over the thrower, the bar's, the back corner's; each a cord to the ceiling, an
	 *  enamel shade and a bulb, with a warm light just under the shade. */
	struct FPendant
	{
		FVector At;
		float Lumens;
		float RadiusM;
	};
	inline const FPendant Pendants[] = { { FVector(-0.45, 3.05, 5.0), 5200.f, 8.f }, { FVector(BarX + 0.2, 3.85, 1.6), 4200.f, 6.f }, { FVector(-2.77, 3.2, 2.2), 3000.f, 5.f } };
	constexpr double PendantCordRadius = 0.006;
	constexpr double PendantCordAbove = 0.1;
	constexpr double ShadeRadius = 0.22;
	constexpr double ShadeHeight = 0.1;
	constexpr double PendantBulbRadius = 0.12;
	constexpr double PendantBulbHeight = 0.05;
	constexpr double PendantBulbDrop = 0.03;
	constexpr double PendantLightDrop = 0.2;
	constexpr const TCHAR* PendantLightColor = TEXT("#ffc98c");

	/** The other lights (web metres, pub-local unless noted): the back bar, the room's fill and rim, the festoon's glow,
	 *  the jukebox's and the lamp hood's. The fill is sized to the room the camera sees, not the whole hall. */
	struct FPubLight
	{
		FVector At;
		float Lumens;
		float RadiusM;
		const TCHAR* Color;
		/** At is already in world terms (the lamp hood sits on the board, not the pub group). */
		bool bWorld = false;
	};
	inline const FPubLight RoomLights[] = {
		{ FVector(BarX, FloorY + 2.75, 0.75), 1000.f, 2.5f, TEXT("#ffa860") },
		{ FVector(0.0, 4.2, 7.5), 14000.f, 12.f, TEXT("#d6c6b2") },
		{ FVector(0.0, 3.6, 3.2), 9500.f, 9.f, TEXT("#ffd8ac") },
		{ FVector(-2.0, 4.0, 0.9), 2600.f, 6.f, TEXT("#3fc9c2") },
		{ FVector(0.0, 3.4, 0.9), 2600.f, 5.f, TEXT("#ffb46a") },
		{ FVector(-HalfWidth + 1.0, FloorY + 1.1, 4.9), 900.f, 3.f, TEXT("#ff7a59") },
		{ FVector(0.0, 1.3, 0.62), 250.f, 2.5f, TEXT("#fff0d8"), true },
	};
	/** Practicals' soft source size (cm). */
	constexpr float PointSourceRadius = 8.f;

	/** The board spot hangs above and in front of the board, its cone opening a little past SpotConeRadians. */
	inline const FVector SpotFrom(0.0, ADartsPub::BoardY + 2.1, 3.2);
	constexpr float SpotOuterScale = 1.15f;
	constexpr float SpotInnerScale = 0.4f;
	constexpr float SpotSourceRadius = 12.f;
	constexpr const TCHAR* SpotColor = TEXT("#fff0d8");
}

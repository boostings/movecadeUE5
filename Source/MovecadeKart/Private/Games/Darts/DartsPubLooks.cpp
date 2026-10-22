// FDartsPubLooks: every look the pub uses, made once per build through the venue's material cache.

#include "Games/Darts/DartsPubLayout.h"

#include "Art/KartMaterials.h"

using MovecadeKart::Art::Hex;
using namespace MovecadeKart::Darts::PubLayout;

FDartsPubLooks::FDartsPubLooks(UObject* Outer, FKartMaterialCache& Cache)
{
	using MovecadeKart::Art::FSurfaceLook;
	// FSurfaceLook: colour, detail set, tile (m), albedo, normal, rough, roughness, metallic, emissive, triplanar, desaturate.
	// Plain: colour, roughness, metallic, emissive.
	auto Surface = [&](const FSurfaceLook& Look) { return Cache.Surface(Outer, Look); };
	// The room's lamps, strips and night windows: a lit colour with a strong emissive. Kept lit on purpose: the
	// pendant bulbs and strips catch the practicals right next to them and the windows the room's reflections,
	// which an unlit GlowOpaque of the same colour loses (the bulbs go dull and the windows black).
	auto Glow = [&](const TCHAR* Color, float Strength) { return Cache.Plain(Outer, Hex(Color), LampRoughness, 0.0, Strength * ADartsPub::GlowGain); };
	Brick = Surface({ Hex(TEXT("#6e4a3a")), TEXT("rock_face"), 1.1, 0.5, 0.9, 0.45, 0.92, 0.0, 0.0, true, 0.65 });
	Plaster = Surface({ Hex(TEXT("#4f7262")), TEXT("plastered_wall_02"), 1.4, 0.4, 0.55, 0.4, 0.95 });
	Wainscot = Surface({ Hex(TEXT("#7a4a36")), TEXT("dark_wooden_planks"), 0.9, 0.45, 0.7, 0.4, 0.55 });
	Floor = Surface({ Hex(TEXT("#7f5439")), TEXT("dark_wooden_planks"), 1.6, 0.5, 0.75, 0.4, 0.42, 0.0, 0.0, false, 0.3 });
	WoodDark = Surface({ Hex(TEXT("#3a2418")), TEXT("dark_wooden_planks"), 0.8, 0.3, 0.6, 0.4, 0.6 });
	Ceiling = Surface({ Hex(TEXT("#5e5246")), TEXT("plastered_wall_02"), 2.0, 0.35, 0.4, 0.4, 0.95 });
	Brass = Cache.Plain(Outer, Hex(TEXT("#c9a45c")), 0.3, 1.0);
	NightWindow = Glow(TEXT("#16223a"), WindowGlow);
	OcheMat = Cache.Plain(Outer, Hex(TEXT("#2a1a14")), 0.85);
	BarTop = Surface({ Hex(TEXT("#a8714e")), TEXT("dark_wooden_planks"), 0.7, 0.45, 0.6, 0.4, 0.3 });
	Panel = Surface({ Hex(TEXT("#6b3f2c")), TEXT("dark_wooden_planks"), 0.6, 0.45, 0.7, 0.4, 0.55 });
	Chalkboard = Surface({ Hex(TEXT("#1b2420")), TEXT("painted_concrete"), 1.0, 0.25, 0.3, 0.3, 0.95 });
	Mirror = Cache.Plain(Outer, Hex(TEXT("#7a6050")), 0.22, 0.75);
	ShelfStrip = Glow(TEXT("#ffb066"), ShelfGlow);
	Beer = Cache.Plain(Outer, Hex(TEXT("#b86a1c")), 0.12, 0.0, 0.4);
	Foam = Cache.Plain(Outer, Hex(TEXT("#f3ead8")), 0.6);
	CapSteel = Cache.Plain(Outer, Hex(TEXT("#d9d9d9")), 0.35, 1.0);
	const TCHAR* const PrintTextures[] = {
		TEXT("/Game/Tracks/Shared/Textures/mc_billboard_0.mc_billboard_0"),
		TEXT("/Game/Tracks/Shared/Textures/mc_billboard_2.mc_billboard_2"),
		TEXT("/Game/Tracks/Shared/Textures/mc_billboard_3.mc_billboard_3"),
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Prints); ++i)
	{
		Prints[i] = Cache.Unlit(Outer, PrintTextures[i], PrintTint * ADartsPub::GlowGain);
	}
	const TCHAR* const GlassColors[] = { TEXT("#2f6b3a"), TEXT("#8a5a1c"), TEXT("#c9d6dc"), TEXT("#5a1f2a"), TEXT("#3a5f7a"), TEXT("#b08a3c") };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Bottles); ++i)
	{
		Bottles[i] = Cache.Plain(Outer, Hex(GlassColors[i]) * 0.8f, 0.08, 0.0, 0.25);
	}
	Cabinet = Surface({ Hex(TEXT("#4a2e20")), TEXT("dark_wooden_planks"), 0.7, 0.45, 0.6, 0.4, 0.45 });
	CabinetDark = Surface({ Hex(TEXT("#2a1810")), TEXT("dark_wooden_planks"), 0.8, 0.3, 0.6, 0.4, 0.5 });
	LaneAccent = Glow(TEXT("#3fc9c2"), AccentGlow);
	LampStrip = Glow(TEXT("#fff0d8"), LampStripGlow);
	Collar = Cache.Plain(Outer, Hex(TEXT("#3fc9c2")) * 0.55f, 0.6);
	// Sisal: matte and clean-coloured, a visible fibre from the sand set so it reads as bristle, not plastic.
	const TCHAR* const SisalColors[] = { TEXT("#f4e4cc"), TEXT("#e8467f"), TEXT("#23113a"), TEXT("#4fc0de") };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Sisal); ++i)
	{
		Sisal[i] = Surface({ Hex(SisalColors[i]), TEXT("gravelly_sand"), 0.18, 0.14, 0.45, 0.25, 0.92, 0.0, 0.0, true, 1.0 });
	}
	Surround = Cache.Plain(Outer, Hex(TEXT("#2a1240")), 0.7);
	Wire = Cache.Plain(Outer, Hex(TEXT("#e4e9ec")), 0.16, 1.0);
	StoolSeat = Cache.Plain(Outer, Hex(TEXT("#5a1c22")), 0.55);
	Festoon = Glow(TEXT("#ffc47a"), FestoonGlow);
	Cord = Cache.Plain(Outer, Hex(TEXT("#111111")), 0.8);
	Jukebox = Glow(TEXT("#ff7a59"), JukeboxGlow);
	JukeboxTeal = Glow(TEXT("#3fc9c2"), JukeboxTealGlow);
	Enamel = Cache.Plain(Outer, Hex(TEXT("#1f3a33")), 0.32);
	PendantBulb = Glow(TEXT("#ffcf8a"), PendantGlow);
}

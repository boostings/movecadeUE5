// ADartsPub: the room (shell, bar, board cabinet, props, lights and grade), its materials and the live bits the game
// drives (the bounty wedge, the chalkboard). The meshes modelled in code are in DartsPubMeshes.cpp.

#include "Games/Darts/DartsPub.h"

#include "Art/KartMaterials.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Darts/DartsPubGeometry.h"
#include "Games/Darts/DartsRules.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace MovecadeKart::Darts;
using MovecadeKart::Art::Hex;

DEFINE_LOG_CATEGORY_STATIC(LogDartsPub, Log, All);

namespace
{
	const FName PubTag(TEXT("DartsPub"));
	const FName NameOpacity(TEXT("Opacity"));
	const FName NameColor(TEXT("Color"));

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

	/** The bounty wedge's glow: the translucent fill's opacity and the outline's brightness, each a floor plus the pulse. */
	constexpr float BountyFillOpacity = 0.26f;
	constexpr float BountyFillPulse = 0.2f;
	constexpr float BountyEdgeLevel = 0.7f;
	constexpr float BountyEdgePulse = 0.3f;
	/** The fill before the game first pulses it. */
	constexpr float BountyFillAtBuild = 0.35f;

	/** The board spot (DartsPub.tsx washLights): the brightest thing in the room is the board. */
	constexpr float SpotLumens = 1250.f;
	constexpr float SpotRadiusCm = 600.f;
	constexpr float SpotConeRadians = 0.36f;

	/** The called shot's gold. */
	const FLinearColor& BountyGold()
	{
		static const FLinearColor Gold = Hex(TEXT("#ffd166"));
		return Gold;
	}

	/** Glow strengths (times GlowGain) for the room's lit parts. */
	constexpr float WindowGlow = 1.4f;
	constexpr float ShelfGlow = 3.2f;
	constexpr float AccentGlow = 1.35f;
	constexpr float LampStripGlow = 2.6f;
	constexpr float FestoonGlow = 4.f;
	constexpr float JukeboxGlow = 2.2f;
	constexpr float JukeboxTealGlow = 1.8f;
	constexpr float PendantGlow = 3.2f;
	constexpr float BountyFillGlow = 3.2f;
	constexpr float BountyEdgeGlow = 6.f;
	/** The framed prints are backlit, a touch warm. */
	const FLinearColor PrintTint(0.75f, 0.68f, 0.6f);

	/** Static parts go under the static root; anything under the wobbling board pivot moves with it. */
	EComponentMobility::Type MobilityUnder(const USceneComponent* Parent)
	{
		return Parent && Parent->Mobility == EComponentMobility::Movable ? EComponentMobility::Movable : EComponentMobility::Static;
	}
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

	FDartsPubLooks(UObject* Outer, FKartMaterialCache& Cache)
	{
		using MovecadeKart::Art::FSurfaceLook;
		// FSurfaceLook: colour, detail set, tile (m), albedo, normal, rough, roughness, metallic, emissive, triplanar, desaturate.
		auto Surface = [&](const FSurfaceLook& Look) { return Cache.Surface(Outer, Look); };
		// The room's lamps, strips and night windows: a lit colour with a strong emissive. Kept lit on purpose: the
		// pendant bulbs and strips catch the practicals right next to them and the windows the room's reflections,
		// which an unlit GlowOpaque of the same colour loses (the bulbs go dull and the windows black).
		auto Glow = [&](const TCHAR* Color, float Strength) { return Cache.Plain(Outer, Hex(Color), 0.9, 0.0, Strength * ADartsPub::GlowGain); };
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
};

ADartsPub::ADartsPub()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// The room never moves: static instances are cheaper to render and trace. Only the board pivot's subtree moves.
	RootComponent->SetMobility(EComponentMobility::Static);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
}

FVector ADartsPub::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

FVector ADartsPub::BoardPoint(double X, double Y, double Lift)
{
	return FVector(X * BoardRadius, BoardY + Y * BoardRadius, Lift);
}

void ADartsPub::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Everything is laid out in world terms (the game's camera and darts assume it): the pub stays at the origin.
	if (!GetActorTransform().Equals(FTransform::Identity))
	{
		SetActorTransform(FTransform::Identity);
	}
	Build();
}

void ADartsPub::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), PubTag);
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	Groups.Reset();
	ChalkLines.Reset();
	ChalkShown.Reset();
	Materials.Reset();
	BoardPivot = nullptr;
	BountyWedge = nullptr;
}

void ADartsPub::Build()
{
	Clear();
	if (!Cube || !Cylinder)
	{
		UE_LOG(LogDartsPub, Warning, TEXT("The engine's basic shapes are missing; the pub is not built."));
		return;
	}
	if (!BoardMesh || !StoolMesh)
	{
		BuildMeshes();
	}
	Groups.Init(this, TEXT("PubGroup"), PubTag, EComponentMobility::Static);
	const FDartsPubLooks Looks(this, Materials);
	BuildShell(Looks);
	BuildBar(Looks);
	BuildBoard(Looks);
	BuildProps(Looks);
	BuildLights(Looks);
	BuildGrade();
}

void ADartsPub::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, bool bShadows)
{
	// The engine cube is 1 m: web (x, y, z) sizes become UE (z, x, y) scales.
	Groups.Find(Cube, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(C + FVector(0.0, 0.0, PubZ)), FVector(S.Z, S.X, S.Y)), true);
}

void ADartsPub::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows)
{
	Groups.Find(Cylinder, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, PubZ)),
		FVector(Radius * 2.0, Radius * 2.0, Height)), true);
}

UStaticMeshComponent* ADartsPub::Part(UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Relative)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("PubPart")));
	C->SetMobility(MobilityUnder(Parent));
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(Parent);
	C->SetRelativeTransform(Relative);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->ComponentTags.Add(PubTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

USceneComponent* ADartsPub::Anchor(FName Name, EComponentMobility::Type Mobility, const FVector& RelativeCm)
{
	USceneComponent* C = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
	// Mobility before it attaches: a movable anchor's children (the board) move with it.
	C->SetMobility(Mobility);
	C->SetupAttachment(RootComponent);
	C->SetRelativeLocation(RelativeCm);
	C->ComponentTags.Add(PubTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

UTextRenderComponent* ADartsPub::Text(USceneComponent* Parent, const FString& Value, const FVector& RelativeCm, float SizeCm, const FLinearColor& Color, const FRotator& Rotation)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this, MakeUniqueObjectName(this, UTextRenderComponent::StaticClass(), TEXT("PubText")));
	T->SetMobility(MobilityUnder(Parent));
	T->SetupAttachment(Parent);
	// Placed before it registers: a static component can't move afterwards.
	T->SetRelativeLocation(RelativeCm);
	T->SetRelativeRotation(Rotation);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetWorldSize(SizeCm);
	T->SetTextRenderColor(Color.ToFColor(true));
	T->SetText(FText::FromString(Value));
	T->SetCastShadow(false);
	T->ComponentTags.Add(PubTag);
	AddInstanceComponent(T);
	T->RegisterComponent();
	return T;
}

void ADartsPub::BuildShell(const FDartsPubLooks& L)
{
	const double WallY = (FloorY + CeilingY) / 2.0, WallH = CeilingY - FloorY;
	const double MidZ = PubDepth / 2.0;
	const double Half = PubWidth / 2.0;
	// Envelope: brick behind the board, plaster down the sides, a floor and a ceiling with beams.
	Box(L.Brick, FVector(0.0, WallY, 0.11), FVector(PubWidth + 0.4, WallH, 0.2));
	for (const double Side : { -1.0, 1.0 })
	{
		Box(L.Plaster, FVector(Side * (Half + 0.1), WallY, MidZ), FVector(0.2, WallH, PubDepth));
		Box(L.Wainscot, FVector(Side * (Half - 0.02), FloorY + WainscotHeight / 2.0, MidZ), FVector(0.04, WainscotHeight, PubDepth));
		Box(L.Brass, FVector(Side * (Half - 0.04), FloorY + WainscotHeight, MidZ), FVector(0.04, 0.04, PubDepth), false);
	}
	Box(L.Wainscot, FVector(0.0, FloorY + WainscotHeight / 2.0, 0.23), FVector(PubWidth, WainscotHeight, 0.04));
	Box(L.Brass, FVector(0.0, FloorY + WainscotHeight, 0.27), FVector(PubWidth, 0.04, 0.04), false);
	Box(L.WoodDark, FVector(0.0, FloorY + 0.09, 0.28), FVector(PubWidth, 0.18, 0.06));
	Box(L.WoodDark, FVector(0.0, CeilingY - 0.28, 0.28), FVector(PubWidth, 0.16, 0.12));
	Box(L.Floor, FVector(0.0, FloorY - 0.05, MidZ), FVector(PubWidth, 0.1, PubDepth));
	Box(L.Ceiling, FVector(0.0, CeilingY + 0.1, MidZ), FVector(PubWidth + 0.4, 0.2, PubDepth));
	Box(L.Brick, FVector(0.0, WallY, PubDepth + 0.1), FVector(PubWidth + 0.4, WallH, 0.2));
	for (const double Z : { 1.4, 5.2, 9.4, 13.6 })
	{
		Box(L.WoodDark, FVector(0.0, CeilingY - 0.18, Z), FVector(PubWidth, 0.3, 0.25));
	}
	// The oche: a worn mat, the brass throw line, the line's lettering.
	Box(L.OcheMat, FVector(0.0, FloorY + 0.012, OcheZ - PubZ + 0.25), FVector(3.0, 0.024, 1.3), false);
	Box(L.Brass, FVector(0.0, FloorY + 0.03, 5.18), FVector(2.6, 0.03, 0.05), false);
	Text(RootComponent, TEXT("OCHE  ·  2.37 M"), ToWorld(FVector(0.0, FloorY + 0.03, 4.62 + PubZ)), 16.f, Hex(TEXT("#efe2cc")), FRotator(90.0, 180.0, 0.0));
	// Night windows down both sides, and framed prints between them.
	for (const double Side : { -1.0, 1.0 })
	{
		for (const double Z : { 2.6, 7.2 })
		{
			Box(L.WoodDark, FVector(Side * (Half - 0.04), 3.15, Z), FVector(0.06, 2.25, 1.58));
			Box(L.NightWindow, FVector(Side * (Half - 0.075), 3.15, Z), FVector(0.01, 2.05, 1.38), false);
			Box(L.WoodDark, FVector(Side * (Half - 0.08), 3.15, Z), FVector(0.02, 0.05, 1.38), false);
		}
	}
}

void ADartsPub::BuildBar(const FDartsPubLooks& L)
{
	// The bar: worn top, panelled front, brass foot rail.
	Box(L.BarTop, FVector(BarX, FloorY + 1.08, BarZ), FVector(2.7, 0.08, 0.72));
	Box(L.Panel, FVector(BarX, FloorY + 0.52, BarZ + 0.3), FVector(2.6, 1.04, 0.06));
	Box(L.WoodDark, FVector(BarX, FloorY + 0.52, BarZ), FVector(2.5, 1.04, 0.56));
	Groups.Find(Cylinder, L.Brass, true)->AddInstance(FTransform(FRotator(0.0, 0.0, 90.0), ToWorld(FVector(BarX, FloorY + 0.2, BarZ + 0.42 + PubZ)),
		FVector(0.05, 0.05, 2.5)), true);
	BuildBackBar(L);
	// Pints on the bar for the regulars.
	for (const double X : { BarX - 0.62, BarX - 0.05, BarX + 0.6 })
	{
		Post(L.Beer, FVector(X, FloorY + 1.12, BarZ + 0.18), 0.04, 0.15);
		Post(L.Foam, FVector(X, FloorY + 1.27, BarZ + 0.18), 0.041, 0.025);
	}
	// Framed prints with picture lights, on the far side and above the cabinet.
	const FVector Prints[] = { FVector(PrintX, 1.75, 0.34), FVector(-1.65, 1.95, 0.34), FVector(0.0, 2.35, 0.34) };
	const double Scales[] = { 0.88, 0.54, 0.54 };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Prints); ++i)
	{
		const double S = Scales[i];
		Box(L.WoodDark, Prints[i], FVector(1.44 * S, 1.84 * S, 0.06));
		Box(L.Prints[i], Prints[i] + FVector(0.0, 0.0, 0.035), FVector(1.28 * S, 1.68 * S, 0.01), false);
		Box(L.Brass, Prints[i] + FVector(0.0, 1.0 * S, 0.1), FVector(0.5 * S, 0.04, 0.08), false);
	}
}

void ADartsPub::BuildBackBar(const FDartsPubLooks& L)
{
	// The back bar: shelf, mirror, the warm strip that lights the bottles from below; the chalkboard above.
	Box(L.WoodDark, FVector(BarX, FloorY + 2.05, 0.38), FVector(2.2, 0.05, 0.3));
	Box(L.WoodDark, FVector(BarX, FloorY + 2.32, 0.25), FVector(2.5, 1.0, 0.02));
	Box(L.Mirror, FVector(BarX, FloorY + 2.32, 0.265), FVector(2.3, 0.86, 0.01), false);
	Box(L.ShelfStrip, FVector(BarX, FloorY + 2.02, 0.5), FVector(2.1, 0.012, 0.02), false);
	const double BottleOffsets[] = { -0.82, -0.49, -0.16, 0.16, 0.49, 0.82 };
	UMaterialInterface* const Caps[] = { L.CapSteel, L.Brass };
	for (int32 i = 0; i < UE_ARRAY_COUNT(BottleOffsets); ++i)
	{
		// Every other bottle stands a little further back on the shelf.
		UStaticMeshComponent* Bottle = Part(BottleMesh, RootComponent, FTransform(ToWorld(FVector(BarX + BottleOffsets[i], FloorY + 2.075, 0.36 + (i % 2) * 0.05 + PubZ))));
		Bottle->SetMaterial(0, L.Bottles[i]);
		Bottle->SetMaterial(1, Caps[i % 2]);
	}
	Box(L.WoodDark, FVector(BarX, 2.95, 0.3), FVector(2.06, 1.3, 0.06));
	Box(L.Chalkboard, FVector(BarX, 2.95, 0.335), FVector(1.86, 1.1, 0.01), false);
	USceneComponent* Board = Anchor(TEXT("ChalkBoard"), EComponentMobility::Static, ToWorld(FVector(BarX, 2.95, 0.345 + PubZ)));
	Text(Board, TEXT("BULLSEYE BASH"), FVector(0.0, 0.0, 38.0), 15.f, Hex(TEXT("#f2efe6")));
	constexpr int32 ChalkRows = 4;
	for (int32 i = 0; i < ChalkRows; ++i)
	{
		ChalkLines.Add(Text(Board, FString(), FVector(0.0, 0.0, 14.0 - i * 17.0), 12.f, Hex(TEXT("#e8e2d2"))));
		ChalkShown.Add(FString());
	}
}

void ADartsPub::BuildBoard(const FDartsPubLooks& L)
{
	// The cabinet: back, rails and sides (DartsScene.tsx Cabinet), the lane accent, the collar behind the board.
	auto CabinetBox = [&](UMaterialInterface* M, const FVector& C, const FVector& S)
	{
		Groups.Find(Cube, M, true)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(FVector(0.0, BoardY, 0.0) + C), FVector(S.Z, S.X, S.Y)), true);
	};
	CabinetBox(L.Cabinet, FVector(0.0, 0.0, -0.30), FVector(2.15, 2.35, 0.34));
	CabinetBox(L.CabinetDark, FVector(0.0, 1.12, -0.10), FVector(2.35, 0.12, 0.22));
	CabinetBox(L.CabinetDark, FVector(0.0, -1.12, -0.10), FVector(2.35, 0.12, 0.22));
	CabinetBox(L.CabinetDark, FVector(-1.12, 0.0, -0.10), FVector(0.12, 2.35, 0.22));
	CabinetBox(L.CabinetDark, FVector(1.12, 0.0, -0.10), FVector(0.12, 2.35, 0.22));
	CabinetBox(L.LaneAccent, FVector(0.0, -0.98, 0.0), FVector(0.9, 0.02, 0.03));
	const double CollarDiameter = 1.42 * BoardRadius * 2.0;
	Groups.Find(Cylinder, L.Collar, true)->AddInstance(FTransform(FRotator(90.0, 0.0, 0.0), ToWorld(FVector(0.0, BoardY, -0.1)), FVector(CollarDiameter, CollarDiameter, 0.04)), true);
	// The board lamp: a brass arm off the cabinet top, the hood over the board, a glowing strip under it.
	CabinetBox(L.Brass, FVector(0.0, 1.32, 0.2), FVector(0.05, 0.05, 0.75));
	CabinetBox(L.Brass, FVector(0.0, 1.3, 0.62), FVector(0.95, 0.09, 0.24));
	CabinetBox(L.LampStrip, FVector(0.0, 1.25, 0.62), FVector(0.85, 0.012, 0.08));
	BoardPivot = Anchor(TEXT("BoardPivot"), EComponentMobility::Movable, ToWorld(FVector(0.0, BoardY, 0.0)));
	BuildBoardFace(L);
}

void ADartsPub::BuildBoardFace(const FDartsPubLooks& L)
{
	// The face (sisal, matte) and its colours (DartsScene.tsx:542-563).
	UStaticMeshComponent* Face = Part(BoardMesh, BoardPivot, FTransform::Identity);
	for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(L.Sisal); ++Slot)
	{
		Face->SetMaterial(Slot, L.Sisal[Slot]);
	}
	Face->SetMaterial(UE_ARRAY_COUNT(L.Sisal), L.Surround);
	UStaticMeshComponent* Wires = Part(WireMesh, BoardPivot, FTransform::Identity);
	Wires->SetMaterial(0, L.Wire);
	Wires->SetMaterial(1, L.Brass);
	Wires->SetCastShadow(false);
	// The number ring (cream on the surround), read upright all the way round, as on a real board.
	constexpr double NumberRingRadius = 1.25;
	for (int32 i = 0; i < SectorCount; ++i)
	{
		const double A = SectorCenterAngle(i);
		Text(BoardPivot, FString::FromInt(Sectors()[i]), PubGeometry::BoardLocal(NumberRingRadius * FMath::Cos(A), NumberRingRadius * FMath::Sin(A), 0.004), 8.5f,
			Hex(TEXT("#fbf1df")));
	}
	// The called shot: a translucent fill (the board shows through) and an opaque glowing outline, each its own
	// instance because ShowBounty pulses them.
	BountyWedge = Part(WedgeMesh, BoardPivot, FTransform::Identity);
	BountyMaterial = MovecadeKart::Art::Glow(this, BountyGold() * (BountyFillGlow * GlowGain));
	if (BountyMaterial)
	{
		BountyMaterial->SetScalarParameterValue(NameOpacity, BountyFillAtBuild);
	}
	BountyEdgeMaterial = MovecadeKart::Art::GlowOpaque(this, BountyGold() * (BountyEdgeGlow * GlowGain));
	BountyWedge->SetMaterial(0, BountyMaterial);
	BountyWedge->SetMaterial(1, BountyEdgeMaterial);
	BountyWedge->SetCastShadow(false);
}

void ADartsPub::BuildProps(const FDartsPubLooks& L)
{
	// The lived-in pub: stools at the bar, a high table with its pints, a jukebox glowing on the far wall, and a
	// festoon of warm bulbs strung across the brick.
	auto Stool = [&](double X, double Z, double Yaw)
	{
		// The stool mesh's second slot is its brass frame.
		Groups.Find(StoolMesh, L.StoolSeat, true, nullptr, L.Brass)->AddInstance(FTransform(FRotator(0.0, Yaw, 0.0), ToWorld(FVector(X, FloorY, Z + PubZ))), true);
	};
	for (const double X : { BarX - 0.85, BarX, BarX + 0.85 })
	{
		// Each turned a little differently, as if just left.
		Stool(X, BarZ + 0.72, X * 37.0);
	}
	// A high table and two stools in the far corner, a couple of pints left on it.
	{
		const double TX = -2.75, TZ = 3.6;
		Post(L.Brass, FVector(TX, FloorY, TZ), 0.035, 1.02);
		Post(L.WoodDark, FVector(TX, FloorY, TZ), 0.22, 0.03);
		Post(L.BarTop, FVector(TX, FloorY + 1.02, TZ), 0.36, 0.04);
		Stool(TX - 0.55, TZ + 0.2, 20.0);
		Stool(TX + 0.45, TZ + 0.45, -35.0);
		for (const FVector2D& P : { FVector2D(TX - 0.1, TZ + 0.08), FVector2D(TX + 0.14, TZ - 0.06) })
		{
			Post(L.Beer, FVector(P.X, FloorY + 1.06, P.Y), 0.04, 0.15);
			Post(L.Foam, FVector(P.X, FloorY + 1.21, P.Y), 0.041, 0.025);
		}
	}
	// The jukebox against the left wall, past the windows.
	{
		const double JX = -PubWidth / 2.0 + 0.36, JZ = 4.9;
		Box(L.WoodDark, FVector(JX, FloorY + 0.7, JZ), FVector(0.62, 1.4, 0.95));
		Box(L.Jukebox, FVector(JX + 0.315, FloorY + 1.05, JZ), FVector(0.01, 0.55, 0.7), false);
		Box(L.JukeboxTeal, FVector(JX + 0.318, FloorY + 1.42, JZ), FVector(0.01, 0.08, 0.8), false);
		Box(L.JukeboxTeal, FVector(JX + 0.318, FloorY + 0.62, JZ), FVector(0.01, 0.05, 0.8), false);
		Box(L.Brass, FVector(JX + 0.32, FloorY + 0.3, JZ), FVector(0.02, 0.32, 0.62), false);
	}
	// The festoon: a parabola of bulbs, each joined to the next by a cord segment.
	UInstancedStaticMeshComponent* Bulbs = Groups.Find(BulbMesh, L.Festoon, false);
	UInstancedStaticMeshComponent* Cords = Groups.Find(Cylinder, L.Cord, false);
	FVector Previous = FVector::ZeroVector;
	for (int32 i = 0; i < FestoonBulbs; ++i)
	{
		const double T = (i + 0.5) / FestoonBulbs;
		const double X = FMath::Lerp(-PubWidth / 2.0 + 0.3, PubWidth / 2.0 - 0.3, T);
		const FVector Point = ToWorld(FVector(X, FestoonY - FestoonSag * 4.0 * T * (1.0 - T), FestoonZ) + FVector(0.0, 0.0, PubZ));
		Bulbs->AddInstance(FTransform(Point), true);
		if (i > 0)
		{
			const FVector Dir = (Point - Previous).GetSafeNormal();
			Cords->AddInstance(FTransform(FRotationMatrix::MakeFromZ(Dir).Rotator(), (Previous + Point) / 2.0, FVector(0.008, 0.008, (Point - Previous).Size() / 100.0)), true);
		}
		Previous = Point;
	}
}

void ADartsPub::AddPointLight(const FVector& Web, float Lumens, float RadiusM, const FLinearColor& Color)
{
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("PubPoint")));
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetupAttachment(RootComponent);
	Light->SetRelativeLocation(ToWorld(Web));
	Light->SetIntensityUnits(ELightUnits::Lumens);
	Light->SetIntensity(Lumens);
	Light->SetAttenuationRadius(RadiusM * 100.f);
	Light->SetLightColor(Color);
	Light->SetSourceRadius(8.f);
	// One shadowed light in the room (the board spot): moving darts and the thrower would invalidate a shadowed
	// point light's six cube faces every frame.
	Light->SetCastShadows(false);
	Light->ComponentTags.Add(PubTag);
	AddInstanceComponent(Light);
	Light->RegisterComponent();
}

void ADartsPub::BuildLights(const FDartsPubLooks& L)
{
	USpotLightComponent* Spot = NewObject<USpotLightComponent>(this, MakeUniqueObjectName(this, USpotLightComponent::StaticClass(), TEXT("BoardSpot")));
	Spot->SetMobility(EComponentMobility::Movable);
	Spot->SetupAttachment(RootComponent);
	const FVector From = ToWorld(FVector(0.0, BoardY + 2.1, 3.2 + PubZ));
	const FVector To = ToWorld(FVector(0.0, BoardY, 0.0));
	Spot->SetRelativeLocation(From);
	Spot->SetRelativeRotation((To - From).Rotation());
	Spot->SetIntensityUnits(ELightUnits::Lumens);
	Spot->SetIntensity(SpotLumens);
	Spot->SetAttenuationRadius(SpotRadiusCm);
	Spot->SetOuterConeAngle(FMath::RadiansToDegrees(SpotConeRadians) * 1.15f);
	Spot->SetInnerConeAngle(FMath::RadiansToDegrees(SpotConeRadians) * 0.4f);
	Spot->SetLightColor(Hex(TEXT("#fff0d8")));
	Spot->SetSourceRadius(12.f);
	Spot->SetCastShadows(true);
	Spot->ComponentTags.Add(PubTag);
	AddInstanceComponent(Spot);
	Spot->RegisterComponent();

	// Practicals: the oche pendant over the thrower, the bar pendant, the back bar; then the room's soft fill and rim.
	struct FPendant { FVector At; float Lumens; float RadiusM; };
	const FPendant Pendants[] = { { FVector(-0.45, 3.05, 5.0), 5200.f, 8.f }, { FVector(BarX + 0.2, 3.85, 1.6), 4200.f, 6.f }, { FVector(-2.77, 3.2, 2.2), 3000.f, 5.f } };
	for (const FPendant& P : Pendants)
	{
		Post(L.Cord, P.At + FVector(0.0, 0.1, 0.0), 0.006, CeilingY - P.At.Y - 0.1, false);
		Post(L.Enamel, P.At, 0.22, 0.1);
		Post(L.PendantBulb, P.At + FVector(0.0, -0.03, 0.0), 0.12, 0.05, false);
	}
	const FLinearColor PendantWarm = Hex(TEXT("#ffc98c"));
	for (const FPendant& P : Pendants)
	{
		// Just under the shade.
		AddPointLight(P.At + FVector(0.0, -0.2, PubZ), P.Lumens, P.RadiusM, PendantWarm);
	}
	AddPointLight(FVector(BarX, FloorY + 2.75, 0.75 + PubZ), 1000.f, 2.5f, Hex(TEXT("#ffa860")));
	// The room's fill, warm and soft from over the camera, sized to the room the camera sees (not the whole hall).
	AddPointLight(FVector(0.0, 4.2, 7.5 + PubZ), 14000.f, 12.f, Hex(TEXT("#d6c6b2")));
	AddPointLight(FVector(0.0, 3.6, 3.2 + PubZ), 9500.f, 9.f, Hex(TEXT("#ffd8ac")));
	AddPointLight(FVector(-2.0, 4.0, 0.9 + PubZ), 2600.f, 6.f, Hex(TEXT("#3fc9c2")));
	// The festoon's glow along the brick, the jukebox's on the far wall, the lamp hood's on the board.
	AddPointLight(FVector(0.0, 3.4, 0.9 + PubZ), 2600.f, 5.f, Hex(TEXT("#ffb46a")));
	AddPointLight(FVector(-PubWidth / 2.0 + 1.0, FloorY + 1.1, 4.9 + PubZ), 900.f, 3.f, Hex(TEXT("#ff7a59")));
	AddPointLight(FVector(0.0, 1.3, 0.62), 250.f, 2.5f, Hex(TEXT("#fff0d8")));
}

void ADartsPub::BuildGrade()
{
	// pubMood.ts grade: a lit board in a dim room. Auto exposure would lift the room and burn the board out, so it
	// is held to a narrow band set by the board's light; bloom and vignette from the mood.
	UPostProcessComponent* Grade = NewObject<UPostProcessComponent>(this, MakeUniqueObjectName(this, UPostProcessComponent::StaticClass(), TEXT("PubGrade")));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
	Grade->Priority = 1.f;
	FPostProcessSettings& S = Grade->Settings;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = ExposureMinEV;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = ExposureMaxEV;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.75f;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = 1.0f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.45f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.02f, 1.02f, 1.02f, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.1f, 1.1f, 1.1f, 1.f);
	// Warm highlights, slightly cool shadows: the lamp-lit pub look, and the shadows stop going muddy red.
	S.bOverride_ColorGainHighlights = true;
	S.ColorGainHighlights = FVector4(1.04f, 1.0f, 0.95f, 1.f);
	S.bOverride_ColorGammaShadows = true;
	S.ColorGammaShadows = FVector4(0.98f, 1.0f, 1.04f, 1.f);
	S.bOverride_FilmToe = true;
	S.FilmToe = 0.5f;
	Grade->ComponentTags.Add(PubTag);
	AddInstanceComponent(Grade);
	Grade->RegisterComponent();
}

void ADartsPub::ShowBounty(int32 SectorIndex, float Pulse)
{
	if (!BountyWedge)
	{
		return;
	}
	BountyWedge->SetVisibility(true);
	// The wedge mesh is centred on +Y (pi/2): roll it about the board's normal onto the sector.
	BountyWedge->SetRelativeRotation(FQuat(FVector::ForwardVector, SectorCenterAngle(SectorIndex) - UE_DOUBLE_PI / 2.0));
	if (BountyMaterial)
	{
		BountyMaterial->SetScalarParameterValue(NameOpacity, BountyFillOpacity + BountyFillPulse * Pulse);
	}
	if (BountyEdgeMaterial)
	{
		// Opaque: the pulse is in the brightness (the translucent outline used to pulse its opacity over the dark sisal).
		BountyEdgeMaterial->SetVectorParameterValue(NameColor, BountyGold() * (BountyEdgeGlow * GlowGain * (BountyEdgeLevel + BountyEdgePulse * Pulse)));
	}
}

void ADartsPub::HideBounty()
{
	if (BountyWedge)
	{
		BountyWedge->SetVisibility(false);
	}
}

void ADartsPub::SetChalk(const TArray<FString>& Lines)
{
	ChalkShown.SetNum(ChalkLines.Num());
	for (int32 i = 0; i < ChalkLines.Num(); ++i)
	{
		const FString& Line = Lines.IsValidIndex(i) ? Lines[i] : FString();
		// SetText always rebuilds the text's render state: only touch a line whose text changed.
		if (ChalkLines[i] && !ChalkShown[i].Equals(Line, ESearchCase::CaseSensitive))
		{
			ChalkShown[i] = Line;
			ChalkLines[i]->SetText(FText::FromString(Line));
		}
	}
}

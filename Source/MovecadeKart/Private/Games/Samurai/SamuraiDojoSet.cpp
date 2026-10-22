// ASamuraiDojo: the set. Every part's placement lives in a small table per part below (web metres: x across, y up,
// z toward the camera; sizes are full extents), and its builder applies the table. Materials come from the named
// looks, cached per look in the dojo's FKartMaterialCache.

#include "Games/Samurai/SamuraiDojo.h"

#include "Art/KartMaterials.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace MovecadeKart::Art;

namespace
{
	/** A plain lit colour (Art::Plain). */
	struct FPlainLook
	{
		const TCHAR* Color;
		double Roughness;
		double Metallic;
		double Emissive;
	};

	/** A box: centre and size (web metres), yaw (radians) and whether it casts a shadow. */
	struct FBoxPart
	{
		FVector Centre;
		FVector Size;
		double Yaw;
		bool bShadows;
	};

	/** An upright cylinder standing on Base. */
	struct FPostPart
	{
		FVector Base;
		double Radius;
		double Height;
	};

	// ---- Looks ----
	namespace Looks
	{
		// Surfaces: colour, detail set, tile (m), albedo, normal, rough, roughness, metallic, emissive, triplanar, desaturate.
		const FSurfaceLook Earth{ Hex(TEXT("#4a3c2c")), TEXT("gravelly_sand"), 2.0, 0.5, 0.6, 0.4, 0.95, 0.0, 0.0, false, 0.4 };
		const FSurfaceLook Gravel{ Hex(TEXT("#b5aa95")), TEXT("gravelly_sand"), 0.8, 0.6, 0.8, 0.45, 0.92, 0.0, 0.0, false, 0.7 };
		const FSurfaceLook Moss{ Hex(TEXT("#3d5a33")), TEXT("leafy_grass"), 1.2, 0.5, 0.6, 0.4, 0.95, 0.0, 0.0, false, 0.2 };
		const FSurfaceLook Rake{ Hex(TEXT("#9a907e")), TEXT("gravelly_sand"), 0.8, 0.6, 0.8, 0.45, 0.95, 0.0, 0.0, false, 0.7 };
		const FSurfaceLook StageCedar{ Hex(TEXT("#8a5a3a")), TEXT("dark_wooden_planks"), 1.2, 0.5, 0.65, 0.4, 0.55, 0.0, 0.0, false };
		const FSurfaceLook StageEdge{ Hex(TEXT("#5e3a22")), TEXT("dark_wooden_planks"), 0.8, 0.4, 0.6, 0.4, 0.6 };
		const FSurfaceLook Tatami[] = {
			{ Hex(TEXT("#c9b77e")), TEXT("leafy_grass"), 0.45, 0.35, 0.5, 0.3, 0.8, 0.0, 0.0, false, 1.0 },
			{ Hex(TEXT("#bfae74")), TEXT("leafy_grass"), 0.45, 0.35, 0.5, 0.3, 0.8, 0.0, 0.0, false, 1.0 },
		};
		const FSurfaceLook WallCedar{ Hex(TEXT("#6e4428")), TEXT("dark_wooden_planks"), 0.9, 0.45, 0.6, 0.4, 0.6 };
		const FSurfaceLook Stone{ Hex(TEXT("#7d7a72")), TEXT("rock_face"), 0.6, 0.6, 0.9, 0.45, 0.9 };
		const FSurfaceLook StoneDark{ Hex(TEXT("#5d5a54")), TEXT("rock_face"), 0.5, 0.6, 0.9, 0.45, 0.9 };

		// Plain colours: colour, roughness, metallic, emissive.
		constexpr FPlainLook Binding[] = { { TEXT("#1f3b2b"), 0.6, 0.0, 0.0 }, { TEXT("#2b1d33"), 0.6, 0.0, 0.0 } };
		constexpr FPlainLook Vermilion{ TEXT("#d23c2a"), 0.42, 0.0, 0.0 };
		constexpr FPlainLook Lacquer{ TEXT("#16120f"), 0.3, 0.1, 0.0 };
		constexpr FPlainLook ShojiPaper{ TEXT("#f3e6c8"), 0.9, 0.0, 0.18 };
		constexpr FPlainLook Kumiko{ TEXT("#3b2414"), 0.6, 0.0, 0.0 };
		constexpr FPlainLook BambooCane{ TEXT("#5fa357"), 0.45, 0.0, 0.0 };
		constexpr FPlainLook BambooNode{ TEXT("#3f7a3a"), 0.5, 0.0, 0.0 };
		constexpr FPlainLook BambooLeaf{ TEXT("#4b8f44"), 0.6, 0.0, 0.0 };
		constexpr FPlainLook PineTrunk{ TEXT("#4a3426"), 0.85, 0.0, 0.0 };
		constexpr FPlainLook PineNeedles{ TEXT("#2f4a32"), 0.85, 0.0, 0.0 };
		/** Ridges fading into the haze: dusky blue to violet, nearest first. */
		constexpr FPlainLook Ridges[] = { { TEXT("#3a4a5e"), 0.95, 0.0, 0.0 }, { TEXT("#4d5873"), 0.95, 0.0, 0.0 }, { TEXT("#6a6a88"), 0.95, 0.0, 0.0 } };
		constexpr FPlainLook LanternPaper{ TEXT("#e23d33"), 0.8, 0.0, 0.9 };
		constexpr FPlainLook LanternCap{ TEXT("#141110"), 0.4, 0.0, 0.0 };
		constexpr FPlainLook LanternCord{ TEXT("#141110"), 0.8, 0.0, 0.0 };
		/** The stone lanterns' fire window and light. */
		const TCHAR* const Fire = TEXT("#ffb35c");
		constexpr float FireGlow = 3.f;
		/** The paper lanterns' light. */
		const TCHAR* const LanternLight = TEXT("#ff9a5a");
	}

	// ---- Layout shared by the parts ----
	constexpr double LaneSpacing = 3.6;
	/** The stage is at least this wide, else the lanes plus a margin. */
	constexpr double MinStageWidth = 13.0;
	constexpr double StageMargin = 4.0;
	constexpr double WallZ = -3.6;
	/** The wall's beam; the paper lanterns hang from it to LanternY, this far in front of the wall. */
	constexpr double BeamY = 5.55;
	constexpr double LanternY = 4.55;
	constexpr double LanternZ = -1.3;

	namespace Ground
	{
		const FBoxPart Earth{ FVector(0.0, -0.24, -6.0), FVector(90.0, 0.2, 40.0), 0.0, false };
		/** The gravel bed (and its raked lines) run this much wider than the stage. */
		constexpr double GravelY = -0.13;
		constexpr double GravelZ = -12.0;
		constexpr double GravelExtraWidth = 6.0;
		constexpr double GravelThickness = 0.02;
		constexpr double GravelDepth = 9.0;
		constexpr int32 RakeLines = 12;
		constexpr double RakeY = -0.118;
		constexpr double RakeFirstZ = -8.2;
		constexpr double RakePitch = 0.62;
		constexpr double RakeExtraWidth = 5.0;
		constexpr double RakeThickness = 0.01;
		constexpr double RakeWidth = 0.07;
		/** Moss beds either side, this far beyond the stage's edge. */
		constexpr double MossOutset = 4.0;
		constexpr double MossY = -0.12;
		constexpr double MossZ = -6.0;
		const FVector MossSize(8.0, 0.03, 20.0);
	}

	namespace Stage
	{
		constexpr double FloorY = -0.075;
		constexpr double FloorZ = 0.3;
		constexpr double FloorThickness = 0.15;
		constexpr double FloorDepth = 8.0;
		constexpr double EdgeY = -0.18;
		constexpr double EdgeZ = 4.3;
		constexpr double EdgeExtraWidth = 0.1;
		constexpr double EdgeThickness = 0.14;
		constexpr double EdgeDepth = 0.2;
		/** A tatami per lane, inset from its neighbours, bound in cloth on all four edges. */
		constexpr double TatamiZ = 0.15;
		constexpr double TatamiInset = 0.28;
		constexpr double TatamiThickness = 0.04;
		constexpr double TatamiDepth = 7.3;
		constexpr double BindingY = 0.022;
		constexpr double BindingSideX = 1.66;
		constexpr double BindingEndZ = 3.61;
		constexpr double BindingWidth = 0.07;
		constexpr double BindingThickness = 0.012;
	}

	namespace Torii
	{
		/** The pillars stand this far outside the outer lanes. */
		constexpr double PillarOutset = 0.55;
		constexpr double PillarRadius = 0.17;
		constexpr double PillarHeight = 4.2;
		constexpr double FootRadius = 0.2;
		constexpr double FootHeight = 0.32;
		/** The beams overhang the pillars by half this each side. */
		constexpr double BeamOverhang = 0.9;
		constexpr double ShimakiY = 4.02;
		constexpr double ShimakiHeight = 0.24;
		constexpr double ShimakiDepth = 0.42;
		constexpr double NukiY = 3.3;
		constexpr double NukiShare = 0.88;
		constexpr double NukiHeight = 0.2;
		constexpr double NukiDepth = 0.32;
		/** The kasagi mesh is 1 m half-length: scaled to the pillars plus this. */
		constexpr double KasagiY = 4.36;
		constexpr double KasagiExtraHalf = 0.85;
		/** The tablet (gakuzuka) between the beams, on its post. */
		const FBoxPart TabletPost{ FVector(0.0, 3.66, 0.0), FVector(0.22, 0.52, 0.26), 0.0, true };
		const FBoxPart Tablet{ FVector(0.0, 3.68, 0.14), FVector(0.5, 0.42, 0.04), 0.0, true };
	}

	namespace Wall
	{
		constexpr double BeamExtraWidth = 1.0;
		const FVector BeamSize(0.0, 0.34, 0.36);
		constexpr double SillY = 0.25;
		const FVector SillSize(0.0, 0.18, 0.3);
		/** Posts roughly every BayWidth, an even count, leaving the middle open onto the garden. */
		constexpr double BayWidth = 2.4;
		constexpr double OpenPostShare = 0.6;
		constexpr double PostY = 2.75;
		const FVector PostSize(0.2, 5.5, 0.22);
		/** Shoji: glowing paper behind a kumiko lattice of seven rails and two stiles. */
		constexpr double ShojiInset = 0.24;
		constexpr double PaperY = 2.3;
		constexpr double PaperZ = 0.05;
		constexpr double PaperHeight = 3.6;
		constexpr double PaperThickness = 0.02;
		constexpr int32 Rails = 6;
		constexpr double RailFirstY = 0.5;
		constexpr double RailPitch = 0.6;
		constexpr double LatticeZ = 0.08;
		constexpr double LatticeBar = 0.035;
		constexpr double LatticeThickness = 0.03;
		constexpr int32 StileDivisions = 3;
	}

	namespace Garden
	{
		/** Three stepping stones each side of the open middle, turning, spreading and shrinking as they go. */
		constexpr int32 Stones = 3;
		constexpr double StoneX = 1.6;
		constexpr double StoneSpreadX = 0.18;
		constexpr double StoneY = -0.03;
		constexpr double StoneZ = -5.2;
		constexpr double StonePitch = 2.1;
		constexpr double StoneTurnDegrees = 37.0;
		constexpr double StoneShrink = 0.08;
		/** A stone lantern each side. */
		const FVector ToroAt(2.35, -0.13, -7.4);
		/** Two pines each side, behind the wall. */
		constexpr int32 Pines = 2;
		constexpr double PineShareX = 0.37;
		constexpr double PineSpreadX = 2.2;
		constexpr double PineY = -0.13;
		constexpr double PineZ = -5.2;
		constexpr double PinePitch = 3.0;
		constexpr double PineTurnDegrees = 70.0;
		constexpr double PineRightTurnDegrees = 20.0;
		constexpr double PineScale = 1.15;
		constexpr double PineShrink = 0.2;
	}

	namespace Toro
	{
		// Offsets from the lantern's base.
		const FPostPart Base{ FVector(0.0, 0.0, 0.0), 0.42, 0.22 };
		const FPostPart Shaft{ FVector(0.0, 0.22, 0.0), 0.16, 0.9 };
		const FBoxPart Platform{ FVector(0.0, 1.2, 0.0), FVector(0.66, 0.12, 0.66), 0.0, true };
		const FBoxPart FireBox{ FVector(0.0, 1.5, 0.0), FVector(0.5, 0.46, 0.5), 0.0, true };
		const FBoxPart Window{ FVector(0.0, 1.5, 0.255), FVector(0.24, 0.26, 0.01), 0.0, false };
		const FBoxPart Roof{ FVector(0.0, 1.82, 0.0), FVector(0.92, 0.14, 0.92), UE_DOUBLE_PI / 4.0, true };
		const FBoxPart Cap{ FVector(0.0, 1.94, 0.0), FVector(0.5, 0.12, 0.5), UE_DOUBLE_PI / 4.0, true };
		const FPostPart Jewel{ FVector(0.0, 2.0, 0.0), 0.08, 0.16 };
		const FVector LightAt(0.0, 1.5, 0.4);
		constexpr float LightLumens = 400.f;
		constexpr float LightRadiusCm = 350.f;
	}

	namespace Bamboo
	{
		/** Five canes at each edge of the stage, of three heights, staggered in depth. */
		constexpr int32 Canes = 5;
		constexpr double EdgeInset = 0.25;
		constexpr double Pitch = 0.16;
		constexpr double Height = 3.2;
		constexpr double HeightStep = 0.6;
		constexpr int32 Heights = 3;
		constexpr double Z = -1.75;
		constexpr double ZStagger = 0.22;
		constexpr double Radius = 0.055;
		constexpr double FirstNodeY = 0.7;
		constexpr double NodePitch = 0.72;
		constexpr double NodeRadius = 0.064;
		constexpr double NodeHeight = 0.04;
		/** Three leaves near the top, angled out from the stage. */
		constexpr int32 Leaves = 3;
		constexpr double LeafDrop = 0.3;
		constexpr double LeafPitch = 0.5;
		constexpr double LeafOutset = 0.25;
		constexpr double LeafZ = 0.05;
		const FVector LeafSize(0.5, 0.025, 0.09);
		constexpr double LeafTurn = 0.5;
		constexpr double LeafTurnStep = 0.4;
	}

	namespace Distance
	{
		constexpr double FirstZ = -32.0;
		constexpr double ZPitch = 18.0;
		constexpr double FirstX = -9.0;
		constexpr double XPitch = 9.0;
		constexpr double Y = -0.5;
		constexpr double HeightStep = 0.25;
		constexpr double DepthScale = 0.75;
		constexpr double DepthStep = 0.35;
	}

	namespace LanternRig
	{
		/** Three lanterns across the stage: a share of its width either side of the middle. */
		constexpr double SpreadShare = 0.3;
		constexpr double CordZCm = -25.0;
		const FVector CordScale(0.015, 0.015, 0.5);
		constexpr float LightLumens = 900.f;
		constexpr float LightRadiusCm = 520.f;
	}
	UMaterialInterface* PlainOf(FKartMaterialCache& Cache, UObject* Outer, const FPlainLook& Look)
	{
		return Cache.Plain(Outer, Hex(Look.Color), Look.Roughness, Look.Metallic, Look.Emissive);
	}
}

double ASamuraiDojo::StageWidth() const
{
	return FMath::Max(MinStageWidth, Lanes * LaneSpacing + StageMargin);
}

// ---- Part helpers -------------------------------------------------------------------------------------------------

template <class TComponent>
TComponent* ASamuraiDojo::AddDojoComponent(const TCHAR* Name, USceneComponent* Parent, EComponentMobility::Type Mobility)
{
	TComponent* Component = NewObject<TComponent>(this, MakeUniqueObjectName(this, TComponent::StaticClass(), Name));
	Component->SetupAttachment(Parent ? Parent : RootComponent.Get());
	Component->SetMobility(Mobility);
	Component->ComponentTags.Add(DojoTag());
	AddInstanceComponent(Component);
	return Component;
}

void ASamuraiDojo::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad, bool bShadows)
{
	Groups.Find(Cube, Material, bShadows)->AddInstance(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(YawRad), 0.0), ToWorld(C), FVector(S.Z, S.X, S.Y)), true);
}

void ASamuraiDojo::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows)
{
	Groups.Find(Cylinder, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, 0.0)),
		FVector(Radius * 2.0, Radius * 2.0, Height)), true);
}

UStaticMeshComponent* ASamuraiDojo::Part(UStaticMesh* Mesh, const FTransform& World, bool bShadows)
{
	UStaticMeshComponent* Component = AddDojoComponent<UStaticMeshComponent>(TEXT("DojoPart"), nullptr, EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	Component->SetRelativeTransform(World);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(bShadows);
	Component->RegisterComponent();
	return Component;
}


// ---- The set ------------------------------------------------------------------------------------------------------

void ASamuraiDojo::BuildGround()
{
	using namespace Ground;
	UMaterialInterface* EarthMaterial = Materials.Surface(this, Looks::Earth);
	UMaterialInterface* GravelMaterial = Materials.Surface(this, Looks::Gravel);
	UMaterialInterface* MossMaterial = Materials.Surface(this, Looks::Moss);
	Box(EarthMaterial, Ground::Earth.Centre, Ground::Earth.Size, Ground::Earth.Yaw, Ground::Earth.bShadows);
	Box(GravelMaterial, FVector(0.0, GravelY, GravelZ), FVector(StageWidth() + GravelExtraWidth, GravelThickness, GravelDepth), 0.0, false);
	// Raked lines across the gravel.
	UMaterialInterface* RakeMaterial = Materials.Surface(this, Looks::Rake);
	for (int32 i = 0; i < RakeLines; ++i)
	{
		Box(RakeMaterial, FVector(0.0, RakeY, RakeFirstZ - i * RakePitch), FVector(StageWidth() + RakeExtraWidth, RakeThickness, RakeWidth), 0.0, false);
	}
	for (const double Side : { -1.0, 1.0 })
	{
		Box(MossMaterial, FVector(Side * (StageWidth() * 0.5 + MossOutset), MossY, MossZ), MossSize, 0.0, false);
	}
}

void ASamuraiDojo::BuildStage()
{
	using namespace Stage;
	UMaterialInterface* Cedar = Materials.Surface(this, Looks::StageCedar);
	UMaterialInterface* CedarSide = Materials.Surface(this, Looks::StageEdge);
	UMaterialInterface* Tatami[] = { Materials.Surface(this, Looks::Tatami[0]), Materials.Surface(this, Looks::Tatami[1]) };
	UMaterialInterface* Binding[] = { PlainOf(Materials, this, Looks::Binding[0]), PlainOf(Materials, this, Looks::Binding[1]) };
	const double W = StageWidth();
	// The floor: a raised cedar stage, its front edge toward the camera.
	Box(Cedar, FVector(0.0, FloorY, FloorZ), FVector(W, FloorThickness, FloorDepth));
	Box(CedarSide, FVector(0.0, EdgeY, EdgeZ), FVector(W + EdgeExtraWidth, EdgeThickness, EdgeDepth));
	// A tatami per lane in alternating tones, bound in cloth on all four edges.
	for (int32 Lane = 0; Lane < Lanes; ++Lane)
	{
		const double X = (Lane - (Lanes - 1) / 2.0) * LaneSpacing;
		UMaterialInterface* Cloth = Binding[Lane % 2];
		Box(Tatami[Lane % 2], FVector(X, 0.0, TatamiZ), FVector(LaneSpacing - TatamiInset, TatamiThickness, TatamiDepth));
		Box(Cloth, FVector(X - BindingSideX, BindingY, TatamiZ), FVector(BindingWidth, BindingThickness, TatamiDepth), 0.0, false);
		Box(Cloth, FVector(X + BindingSideX, BindingY, TatamiZ), FVector(BindingWidth, BindingThickness, TatamiDepth), 0.0, false);
		Box(Cloth, FVector(X, BindingY, TatamiZ - BindingEndZ), FVector(LaneSpacing - TatamiInset, BindingThickness, BindingWidth), 0.0, false);
		Box(Cloth, FVector(X, BindingY, TatamiZ + BindingEndZ), FVector(LaneSpacing - TatamiInset, BindingThickness, BindingWidth), 0.0, false);
	}
}

void ASamuraiDojo::BuildTorii()
{
	using namespace Torii;
	UMaterialInterface* Vermilion = PlainOf(Materials, this, Looks::Vermilion);
	UMaterialInterface* Lacquer = PlainOf(Materials, this, Looks::Lacquer);
	const double Half = Lanes * (LaneSpacing / 2.0) + PillarOutset;
	for (const double Side : { -1.0, 1.0 })
	{
		Post(Vermilion, FVector(Side * Half, 0.0, 0.0), PillarRadius, PillarHeight);
		Post(Lacquer, FVector(Side * Half, 0.0, 0.0), FootRadius, FootHeight);
	}
	// Shimaki and nuki (the beams), the kasagi on top with its upswept ends, the tablet between.
	Box(Vermilion, FVector(0.0, ShimakiY, 0.0), FVector(Half * 2.0 + BeamOverhang, ShimakiHeight, ShimakiDepth));
	Box(Vermilion, FVector(0.0, NukiY, 0.0), FVector((Half * 2.0 + BeamOverhang) * NukiShare, NukiHeight, NukiDepth));
	UStaticMeshComponent* Kasagi = Part(KasagiMesh, FTransform(FRotator::ZeroRotator, ToWorld(FVector(0.0, KasagiY, 0.0)), FVector(1.0, (Half + KasagiExtraHalf), 1.0)));
	Kasagi->SetMaterial(0, Lacquer);
	Box(Vermilion, TabletPost.Centre, TabletPost.Size, TabletPost.Yaw, TabletPost.bShadows);
	Box(Lacquer, Tablet.Centre, Tablet.Size, Tablet.Yaw, Tablet.bShadows);
}

void ASamuraiDojo::BuildWall()
{
	using namespace Wall;
	UMaterialInterface* Cedar = Materials.Surface(this, Looks::WallCedar);
	UMaterialInterface* Paper = PlainOf(Materials, this, Looks::ShojiPaper);
	UMaterialInterface* Kumiko = PlainOf(Materials, this, Looks::Kumiko);
	const double W = StageWidth();
	// The beam over the wall and the sill, then posts that leave the middle open onto the garden.
	Box(Cedar, FVector(0.0, BeamY, WallZ), FVector(W + BeamExtraWidth, BeamSize.Y, BeamSize.Z));
	Box(Cedar, FVector(0.0, SillY, WallZ), FVector(W + BeamExtraWidth, SillSize.Y, SillSize.Z));
	const int32 Bays = FMath::Max(2, FMath::RoundToInt32(W / BayWidth) / 2) * 2;
	const double Bay = W / Bays;
	for (int32 i = 0; i <= Bays; ++i)
	{
		const double X = -W / 2.0 + i * Bay;
		if (FMath::Abs(X) < Bay * OpenPostShare)
		{
			continue;
		}
		Box(Cedar, FVector(X, PostY, WallZ), PostSize);
	}
	// Shoji between the posts (not across the open middle): glowing paper behind a kumiko lattice.
	const double Panel = Bay - ShojiInset;
	for (int32 i = 0; i < Bays; ++i)
	{
		const double X = -W / 2.0 + (i + 0.5) * Bay;
		if (FMath::Abs(X) < Bay)
		{
			continue;
		}
		Box(Paper, FVector(X, PaperY, WallZ + PaperZ), FVector(Panel, PaperHeight, PaperThickness), 0.0, false);
		for (int32 r = 0; r <= Rails; ++r)
		{
			Box(Kumiko, FVector(X, RailFirstY + r * RailPitch, WallZ + LatticeZ), FVector(Panel, LatticeBar, LatticeThickness), 0.0, false);
		}
		for (int32 c = 1; c < StileDivisions; ++c)
		{
			Box(Kumiko, FVector(X - Panel / 2.0 + c * Panel / StileDivisions, PaperY, WallZ + LatticeZ), FVector(LatticeBar, PaperHeight, LatticeThickness), 0.0, false);
		}
	}
}

void ASamuraiDojo::BuildGarden()
{
	using namespace Garden;
	UMaterialInterface* StoneLook = Materials.Surface(this, Looks::Stone);
	const double W = StageWidth();
	for (const double Side : { -1.0, 1.0 })
	{
		// Stepping stones through the gravel, both sides of the open middle.
		for (int32 i = 0; i < Stones; ++i)
		{
			UStaticMeshComponent* Step = Part(StoneMesh, FTransform(FRotator(0.0, i * StoneTurnDegrees, 0.0),
				ToWorld(FVector(Side * (StoneX + i * StoneSpreadX), StoneY, StoneZ - i * StonePitch)), FVector(1.0 - i * StoneShrink)));
			Step->SetMaterial(0, StoneLook);
		}
		BuildStoneLantern(FVector(Side * ToroAt.X, ToroAt.Y, ToroAt.Z));
		BuildBamboo(Side);
		// Garden pines behind the wall.
		for (int32 i = 0; i < Pines; ++i)
		{
			UStaticMeshComponent* Pine = Part(PineMesh, FTransform(FRotator(0.0, i * PineTurnDegrees + (Side > 0 ? PineRightTurnDegrees : 0.0), 0.0),
				ToWorld(FVector(Side * (W * PineShareX + i * PineSpreadX), PineY, PineZ - i * PinePitch)), FVector(PineScale - i * PineShrink)));
			Pine->SetMaterial(0, PlainOf(Materials, this, Looks::PineTrunk));
			Pine->SetMaterial(1, PlainOf(Materials, this, Looks::PineNeedles));
		}
	}
}

void ASamuraiDojo::BuildStoneLantern(const FVector& At)
{
	using namespace Toro;
	// A stone lantern (toro): base, shaft, the lit fire box, the roof and its jewel.
	UMaterialInterface* Stone = Materials.Surface(this, Looks::Stone);
	UMaterialInterface* StoneDark = Materials.Surface(this, Looks::StoneDark);
	// The fire box's window is solid and never fades: opaque unlit, no translucency pass.
	UMaterialInterface* Fire = Materials.GlowOpaque(this, Hex(Looks::Fire) * Looks::FireGlow);
	auto PlacePost = [&](UMaterialInterface* Material, const FPostPart& Piece) { Post(Material, At + Piece.Base, Piece.Radius, Piece.Height); };
	auto PlaceBox = [&](UMaterialInterface* Material, const FBoxPart& Piece) { Box(Material, At + Piece.Centre, Piece.Size, Piece.Yaw, Piece.bShadows); };
	// The base post sits on the ground itself (At + 0 is At).
	Post(StoneDark, At, Base.Radius, Base.Height);
	PlacePost(Stone, Shaft);
	PlaceBox(Stone, Platform);
	PlaceBox(Stone, FireBox);
	PlaceBox(Fire, Window);
	PlaceBox(StoneDark, Roof);
	PlaceBox(StoneDark, Cap);
	PlacePost(Stone, Jewel);
	UPointLightComponent* Light = AddDojoComponent<UPointLightComponent>(TEXT("ToroLight"), nullptr, EComponentMobility::Movable);
	Light->SetRelativeLocation(ToWorld(At + LightAt));
	Light->SetIntensityUnits(ELightUnits::Lumens);
	Light->SetIntensity(LightLumens);
	Light->SetAttenuationRadius(LightRadiusCm);
	Light->SetLightColor(Hex(Looks::Fire));
	Light->SetCastShadows(false);
	Light->RegisterComponent();
}

void ASamuraiDojo::BuildBamboo(double Side)
{
	using namespace Bamboo;
	// Bamboo at the stage's edges, in front of the wall: canes, their nodes and a few leaves.
	UMaterialInterface* Cane = PlainOf(Materials, this, Looks::BambooCane);
	UMaterialInterface* Node = PlainOf(Materials, this, Looks::BambooNode);
	UMaterialInterface* Leaf = PlainOf(Materials, this, Looks::BambooLeaf);
	const double EdgeX = Side * (StageWidth() / 2.0 - EdgeInset);
	for (int32 i = 0; i < Canes; ++i)
	{
		const double X = EdgeX - Side * i * Pitch;
		const double H = Height + (i % Heights) * HeightStep;
		const double CaneZ = Z + (i % 2) * ZStagger;
		Post(Cane, FVector(X, 0.0, CaneZ), Radius, H);
		for (double Y = FirstNodeY; Y < H; Y += NodePitch)
		{
			Post(Node, FVector(X, Y, CaneZ), NodeRadius, NodeHeight, false);
		}
		for (int32 k = 0; k < Leaves; ++k)
		{
			const double Y = H - LeafDrop - k * LeafPitch;
			Box(Leaf, FVector(X - Side * LeafOutset, Y, CaneZ + LeafZ), LeafSize, Side * (LeafTurn + k * LeafTurnStep), false);
		}
	}
}

void ASamuraiDojo::BuildDistance()
{
	using namespace Distance;
	// Ridges fading into the haze, each farther and taller.
	for (int32 i = 0; i < UE_ARRAY_COUNT(Looks::Ridges); ++i)
	{
		const double RidgeZ = FirstZ - i * ZPitch;
		UStaticMeshComponent* Ridge = Part(RidgeMesh, FTransform(FRotator(0.0, 0.0, 0.0), ToWorld(FVector(i * XPitch + FirstX, Y, RidgeZ)),
			FVector(1.0, 1.0 + i * HeightStep, DepthScale + i * DepthStep)), false);
		Ridge->SetMaterial(0, PlainOf(Materials, this, Looks::Ridges[i]));
	}
}

void ASamuraiDojo::BuildLanterns()
{
	// Paper lanterns hung on cords from the beam, each on a pivot so it can sway; they glow and light the stage.
	UMaterialInterface* Paper = PlainOf(Materials, this, Looks::LanternPaper);
	UMaterialInterface* Cap = PlainOf(Materials, this, Looks::LanternCap);
	UMaterialInterface* Cord = PlainOf(Materials, this, Looks::LanternCord);
	const double W = StageWidth();
	const double Xs[] = { -W * LanternRig::SpreadShare, 0.0, W * LanternRig::SpreadShare };
	for (const double X : Xs)
	{
		USceneComponent* Pivot = AddDojoComponent<USceneComponent>(TEXT("LanternPivot"), nullptr, EComponentMobility::Movable);
		Pivot->SetRelativeLocation(ToWorld(FVector(X, BeamY, LanternZ)));
		Pivot->RegisterComponent();
		Lanterns.Add(Pivot);
		UStaticMeshComponent* Lantern = AddDojoComponent<UStaticMeshComponent>(TEXT("Lantern"), Pivot, EComponentMobility::Movable);
		Lantern->SetStaticMesh(LanternMesh);
		Lantern->SetMaterial(0, Paper);
		Lantern->SetMaterial(1, Cap);
		Lantern->SetRelativeLocation(FVector(0.0, 0.0, -(BeamY - LanternY) * 100.0));
		Lantern->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Swaying casters would invalidate their shadow pages every frame.
		Lantern->SetCastShadow(false);
		Lantern->RegisterComponent();
		UStaticMeshComponent* Line = AddDojoComponent<UStaticMeshComponent>(TEXT("LanternCord"), Pivot, EComponentMobility::Movable);
		Line->SetStaticMesh(Cylinder);
		Line->SetMaterial(0, Cord);
		Line->SetRelativeLocation(FVector(0.0, 0.0, LanternRig::CordZCm));
		Line->SetRelativeScale3D(LanternRig::CordScale);
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->RegisterComponent();
		UPointLightComponent* Glow = AddDojoComponent<UPointLightComponent>(TEXT("LanternLight"), Lantern, EComponentMobility::Movable);
		Glow->SetIntensityUnits(ELightUnits::Lumens);
		Glow->SetIntensity(LanternRig::LightLumens);
		Glow->SetAttenuationRadius(LanternRig::LightRadiusCm);
		Glow->SetLightColor(Hex(Looks::LanternLight));
		Glow->SetCastShadows(false);
		Glow->RegisterComponent();
	}
}

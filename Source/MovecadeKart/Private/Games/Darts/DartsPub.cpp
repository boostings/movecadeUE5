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
#include "Games/Darts/DartsPubLayout.h"
#include "Games/Darts/DartsRules.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace MovecadeKart::Darts;
using MovecadeKart::Art::Hex;
using namespace MovecadeKart::Darts::PubLayout;

DEFINE_LOG_CATEGORY_STATIC(LogDartsPub, Log, All);

namespace
{
	const FName PubTag(TEXT("DartsPub"));
	const FName NameOpacity(TEXT("Opacity"));
	const FName NameColor(TEXT("Color"));

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

	/** The bounty wedge's strengths (times GlowGain): the fill, and the outline brighter so it reads at TV distance. */
	constexpr float BountyFillGlow = 3.2f;
	constexpr float BountyEdgeGlow = 6.f;

	/** Static parts go under the static root; anything under the wobbling board pivot moves with it. */
	EComponentMobility::Type MobilityUnder(const USceneComponent* Parent)
	{
		return Parent && Parent->Mobility == EComponentMobility::Movable ? EComponentMobility::Movable : EComponentMobility::Static;
	}
}


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

void ADartsPub::AddBoxes(const FDartsPubLooks& Looks, TConstArrayView<FDartsPubBox> Boxes, const FVector& Offset)
{
	for (const FDartsPubBox& Part : Boxes)
	{
		Box(Looks.*Part.Look, Part.Centre + Offset, Part.Size, Part.bShadows);
	}
}

void ADartsPub::AddSideBoxes(const FDartsPubLooks& Looks, TConstArrayView<FDartsPubBox> Boxes, double Side, double Z)
{
	// Centre.X is the inset from the side wall's line, mirrored onto this side.
	for (const FDartsPubBox& Part : Boxes)
	{
		Box(Looks.*Part.Look, FVector(Side * (HalfWidth - Part.Centre.X), Part.Centre.Y, Part.Centre.Z + Z), Part.Size, Part.bShadows);
	}
}

void ADartsPub::AddPint(const FDartsPubLooks& Looks, double X, double Y, double Z)
{
	Post(Looks.Beer, FVector(X, Y, Z), Pint.BeerRadius, Pint.BeerHeight);
	Post(Looks.Foam, FVector(X, Y + Pint.FoamAbove, Z), Pint.FoamRadius, Pint.FoamHeight);
}

void ADartsPub::AddStool(const FDartsPubLooks& Looks, double X, double Z, double Yaw)
{
	// The stool mesh's second slot is its brass frame.
	Groups.Find(StoolMesh, Looks.StoolSeat, true, nullptr, Looks.Brass)->AddInstance(FTransform(FRotator(0.0, Yaw, 0.0), ToWorld(FVector(X, FloorY, Z + PubZ))), true);
}

void ADartsPub::BuildShell(const FDartsPubLooks& Looks)
{
	AddBoxes(Looks, Envelope);
	for (const double Side : { -1.0, 1.0 })
	{
		AddSideBoxes(Looks, SideWall, Side, 0.0);
		// Night windows down both sides (the prints hang between them on the back wall).
		for (const double Z : WindowZs)
		{
			AddSideBoxes(Looks, Window, Side, Z);
		}
	}
	for (const double Z : BeamZs)
	{
		AddBoxes(Looks, MakeArrayView(&Beam, 1), FVector(0.0, 0.0, Z));
	}
	AddBoxes(Looks, Oche);
	Text(RootComponent, TEXT("OCHE  ·  2.37 M"), ToWorld(OcheTextAt + FVector(0.0, 0.0, PubZ)), OcheTextSize, Hex(TEXT("#efe2cc")), FRotator(90.0, 180.0, 0.0));
}

void ADartsPub::BuildBar(const FDartsPubLooks& Looks)
{
	AddBoxes(Looks, Bar);
	Groups.Find(Cylinder, Looks.Brass, true)->AddInstance(FTransform(FRotator(0.0, 0.0, 90.0), ToWorld(FootRailAt + FVector(0.0, 0.0, PubZ)), FootRailScale), true);
	BuildBackBar(Looks);
	for (const double X : BarPintXs)
	{
		AddPint(Looks, X, BarPintY, BarPintZ);
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(Prints); ++i)
	{
		const FVector& At = Prints[i].At;
		const double S = Prints[i].Scale;
		Box(Looks.WoodDark, At, FVector(PrintFrame.X * S, PrintFrame.Y * S, PrintFrame.Z));
		Box(Looks.Prints[i], At + FVector(0.0, 0.0, PrintPictureProud), FVector(PrintPicture.X * S, PrintPicture.Y * S, PrintPicture.Z), false);
		Box(Looks.Brass, At + FVector(0.0, PrintLightAbove * S, PrintLightProud), FVector(PrintLight.X * S, PrintLight.Y, PrintLight.Z), false);
	}
}

void ADartsPub::BuildBackBar(const FDartsPubLooks& Looks)
{
	AddBoxes(Looks, BackBar);
	UMaterialInterface* const Caps[] = { Looks.CapSteel, Looks.Brass };
	for (int32 i = 0; i < UE_ARRAY_COUNT(BottleXs); ++i)
	{
		UStaticMeshComponent* Bottle = Part(BottleMesh, RootComponent, FTransform(ToWorld(FVector(BarX + BottleXs[i], BottleY, BottleZ + (i % 2) * BottleStagger + PubZ))));
		Bottle->SetMaterial(0, Looks.Bottles[i]);
		Bottle->SetMaterial(1, Caps[i % 2]);
	}
	AddBoxes(Looks, Chalkboard);
	USceneComponent* Board = Anchor(TEXT("ChalkBoard"), EComponentMobility::Static, ToWorld(ChalkAt + FVector(0.0, 0.0, PubZ)));
	Text(Board, TEXT("BULLSEYE BASH"), FVector(0.0, 0.0, ChalkTitleZ), ChalkTitleSize, Hex(TEXT("#f2efe6")));
	for (int32 i = 0; i < ChalkRows; ++i)
	{
		ChalkLines.Add(Text(Board, FString(), FVector(0.0, 0.0, ChalkFirstRowZ - i * ChalkRowPitch), ChalkRowSize, Hex(TEXT("#e8e2d2"))));
		ChalkShown.Add(FString());
	}
}

void ADartsPub::BuildBoard(const FDartsPubLooks& Looks)
{
	// The cabinet's parts are board-centred and in world terms (the board hangs at the origin, not in the pub group).
	const FVector BoardCentre(0.0, BoardY, 0.0);
	auto AddBoardBoxes = [&](TConstArrayView<FDartsPubBox> Parts)
	{
		for (const FDartsPubBox& Part : Parts)
		{
			Groups.Find(Cube, Looks.*Part.Look, Part.bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(BoardCentre + Part.Centre),
				FVector(Part.Size.Z, Part.Size.X, Part.Size.Y)), true);
		}
	};
	AddBoardBoxes(Cabinet);
	const double CollarDiameter = CollarRadii * BoardRadius * 2.0;
	Groups.Find(Cylinder, Looks.Collar, true)->AddInstance(FTransform(FRotator(90.0, 0.0, 0.0), ToWorld(FVector(0.0, BoardY, -CollarBehind)),
		FVector(CollarDiameter, CollarDiameter, CollarThickness)), true);
	AddBoardBoxes(BoardLamp);
	BoardPivot = Anchor(TEXT("BoardPivot"), EComponentMobility::Movable, ToWorld(BoardCentre));
	BuildBoardFace(Looks);
}

void ADartsPub::BuildBoardFace(const FDartsPubLooks& Looks)
{
	// The face (sisal, matte) and its colours (DartsScene.tsx:542-563).
	UStaticMeshComponent* Face = Part(BoardMesh, BoardPivot, FTransform::Identity);
	for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Looks.Sisal); ++Slot)
	{
		Face->SetMaterial(Slot, Looks.Sisal[Slot]);
	}
	Face->SetMaterial(UE_ARRAY_COUNT(Looks.Sisal), Looks.Surround);
	UStaticMeshComponent* Wires = Part(WireMesh, BoardPivot, FTransform::Identity);
	Wires->SetMaterial(0, Looks.Wire);
	Wires->SetMaterial(1, Looks.Brass);
	Wires->SetCastShadow(false);
	// The number ring (cream on the surround), read upright all the way round, as on a real board.
	for (int32 i = 0; i < SectorCount; ++i)
	{
		const double A = SectorCenterAngle(i);
		Text(BoardPivot, FString::FromInt(Sectors()[i]), PubGeometry::BoardLocal(NumberRingRadius * FMath::Cos(A), NumberRingRadius * FMath::Sin(A), NumberRingLift),
			NumberSize, Hex(TEXT("#fbf1df")));
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

void ADartsPub::BuildProps(const FDartsPubLooks& Looks)
{
	// The lived-in pub: stools at the bar, a high table with its pints, a jukebox glowing on the far wall, and a
	// festoon of warm bulbs strung across the brick.
	for (const double X : BarStoolXs)
	{
		AddStool(Looks, X, BarStoolZ, X * StoolYawPerX);
	}
	for (const FTablePost& TablePart : TableParts)
	{
		Post(Looks.*TablePart.Look, FVector(TableX, TablePart.Y, TableZ), TablePart.Radius, TablePart.Height);
	}
	for (const FPlaced& Stool : TableStools)
	{
		AddStool(Looks, Stool.X, Stool.Z, Stool.Yaw);
	}
	for (const FVector2D& At : TablePints)
	{
		AddPint(Looks, At.X, TablePintY, At.Y);
	}
	AddBoxes(Looks, Jukebox, FVector(JukeboxX, 0.0, 0.0));
	// The festoon: a parabola of bulbs, each joined to the next by a cord segment.
	UInstancedStaticMeshComponent* Bulbs = Groups.Find(BulbMesh, Looks.Festoon, false);
	UInstancedStaticMeshComponent* Cords = Groups.Find(Cylinder, Looks.Cord, false);
	FVector Previous = FVector::ZeroVector;
	for (int32 i = 0; i < FestoonBulbs; ++i)
	{
		const double T = (i + 0.5) / FestoonBulbs;
		const double X = FMath::Lerp(-HalfWidth + FestoonInset, HalfWidth - FestoonInset, T);
		const FVector Point = ToWorld(FVector(X, FestoonY - FestoonSag * 4.0 * T * (1.0 - T), FestoonZ) + FVector(0.0, 0.0, PubZ));
		Bulbs->AddInstance(FTransform(Point), true);
		if (i > 0)
		{
			const FVector Dir = (Point - Previous).GetSafeNormal();
			Cords->AddInstance(FTransform(FRotationMatrix::MakeFromZ(Dir).Rotator(), (Previous + Point) / 2.0,
				FVector(FestoonCordScale, FestoonCordScale, (Point - Previous).Size() / 100.0)), true);
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
	Light->SetSourceRadius(PointSourceRadius);
	// One shadowed light in the room (the board spot): moving darts and the thrower would invalidate a shadowed
	// point light's six cube faces every frame.
	Light->SetCastShadows(false);
	Light->ComponentTags.Add(PubTag);
	AddInstanceComponent(Light);
	Light->RegisterComponent();
}

void ADartsPub::BuildLights(const FDartsPubLooks& Looks)
{
	USpotLightComponent* Spot = NewObject<USpotLightComponent>(this, MakeUniqueObjectName(this, USpotLightComponent::StaticClass(), TEXT("BoardSpot")));
	Spot->SetMobility(EComponentMobility::Movable);
	Spot->SetupAttachment(RootComponent);
	const FVector From = ToWorld(SpotFrom + FVector(0.0, 0.0, PubZ));
	const FVector To = ToWorld(FVector(0.0, BoardY, 0.0));
	Spot->SetRelativeLocation(From);
	Spot->SetRelativeRotation((To - From).Rotation());
	Spot->SetIntensityUnits(ELightUnits::Lumens);
	Spot->SetIntensity(SpotLumens);
	Spot->SetAttenuationRadius(SpotRadiusCm);
	Spot->SetOuterConeAngle(FMath::RadiansToDegrees(SpotConeRadians) * SpotOuterScale);
	Spot->SetInnerConeAngle(FMath::RadiansToDegrees(SpotConeRadians) * SpotInnerScale);
	Spot->SetLightColor(Hex(SpotColor));
	Spot->SetSourceRadius(SpotSourceRadius);
	Spot->SetCastShadows(true);
	Spot->ComponentTags.Add(PubTag);
	AddInstanceComponent(Spot);
	Spot->RegisterComponent();

	for (const FPendant& P : Pendants)
	{
		Post(Looks.Cord, P.At + FVector(0.0, PendantCordAbove, 0.0), PendantCordRadius, CeilingY - P.At.Y - PendantCordAbove, false);
		Post(Looks.Enamel, P.At, ShadeRadius, ShadeHeight);
		Post(Looks.PendantBulb, P.At + FVector(0.0, -PendantBulbDrop, 0.0), PendantBulbRadius, PendantBulbHeight, false);
	}
	const FLinearColor PendantWarm = Hex(PendantLightColor);
	for (const FPendant& P : Pendants)
	{
		AddPointLight(P.At + FVector(0.0, -PendantLightDrop, PubZ), P.Lumens, P.RadiusM, PendantWarm);
	}
	for (const FPubLight& Light : RoomLights)
	{
		AddPointLight(Light.bWorld ? Light.At : Light.At + FVector(0.0, 0.0, PubZ), Light.Lumens, Light.RadiusM, Hex(Light.Color));
	}
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

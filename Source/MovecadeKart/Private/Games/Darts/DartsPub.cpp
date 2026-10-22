#include "Games/Darts/DartsPub.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Darts/DartsRules.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace MovecadeKart::Darts;

namespace
{
	const TCHAR* const PubTag = TEXT("DartsPub");

	/** pubWidth(1): the 44 degree frame at the one-lane camera distance, wider than the cabinet bay. */
	constexpr double PubWidth = 8.14;
	constexpr double PubDepth = 22.0;
	constexpr double BarX = 2.61;
	constexpr double PrintX = -2.61;
	constexpr double BarZ = 1.05;
	constexpr double WainscotHeight = 1.35;

	/** A point in the board pivot's frame (cm): board radii across and up, Lift metres toward the camera (UE -X). */
	FVector Local(double X, double Y, double Lift = 0.0)
	{
		return FVector(-Lift * 100.0, X * ADartsPub::BoardRadius * 100.0, Y * ADartsPub::BoardRadius * 100.0);
	}

	/** Static parts go under the static root; anything under the wobbling board pivot moves with it. */
	EComponentMobility::Type MobilityUnder(const USceneComponent* Parent)
	{
		return Parent && Parent->Mobility == EComponentMobility::Movable ? EComponentMobility::Movable : EComponentMobility::Static;
	}

	/** One annular sector of the board face (board radii, radians), facing the camera. */
	void Annulus(MovecadeKart::FMeshBuilder& B, FName Slot, double R0, double R1, double A0, double A1, double Lift, int32 Segments)
	{
		const int32 S = B.Section(Slot);
		const FVector N(-1.0, 0.0, 0.0);
		for (int32 i = 0; i < Segments; ++i)
		{
			const double T0 = FMath::Lerp(A0, A1, static_cast<double>(i) / Segments);
			const double T1 = FMath::Lerp(A0, A1, static_cast<double>(i + 1) / Segments);
			auto P = [&](double R, double T) { return Local(R * FMath::Cos(T), R * FMath::Sin(T), Lift); };
			auto UV = [&](double R, double T) { return FVector2D(0.5 + 0.5 * R * FMath::Cos(T), 0.5 - 0.5 * R * FMath::Sin(T)); };
			const int32 a = B.Vert(S, P(R0, T0), N, UV(R0, T0), FVector2D::ZeroVector);
			const int32 b = B.Vert(S, P(R1, T0), N, UV(R1, T0), FVector2D::ZeroVector);
			const int32 c = B.Vert(S, P(R1, T1), N, UV(R1, T1), FVector2D::ZeroVector);
			const int32 d = B.Vert(S, P(R0, T1), N, UV(R0, T1), FVector2D::ZeroVector);
			B.Tri(S, a, b, c);
			if (R0 > 1e-6)
			{
				B.Tri(S, a, c, d);
			}
		}
	}

	/** A flat strip from (X0, Y0) to (X1, Y1) on the face, Width board radii wide. */
	void Strip(MovecadeKart::FMeshBuilder& B, FName Slot, const FVector2D& From, const FVector2D& To, double Width, double Lift)
	{
		const int32 S = B.Section(Slot);
		const FVector N(-1.0, 0.0, 0.0);
		const FVector2D Side = FVector2D(-(To - From).Y, (To - From).X).GetSafeNormal() * Width * 0.5;
		const int32 a = B.Vert(S, Local(From.X - Side.X, From.Y - Side.Y, Lift), N, FVector2D(0, 0), FVector2D::ZeroVector);
		const int32 b = B.Vert(S, Local(From.X + Side.X, From.Y + Side.Y, Lift), N, FVector2D(1, 0), FVector2D::ZeroVector);
		const int32 c = B.Vert(S, Local(To.X + Side.X, To.Y + Side.Y, Lift), N, FVector2D(1, 1), FVector2D::ZeroVector);
		const int32 d = B.Vert(S, Local(To.X - Side.X, To.Y - Side.Y, Lift), N, FVector2D(0, 1), FVector2D::ZeroVector);
		B.Tri(S, a, b, c);
		B.Tri(S, a, c, d);
	}

	/** A torus about the board normal: Radius board radii, Tube metres, Lift metres out. */
	void Hoop(MovecadeKart::FMeshBuilder& B, FName Slot, double Radius, double Tube, double Lift, int32 Segments = 96, int32 Sides = 12)
	{
		TArray<FVector2D> Profile;
		for (int32 i = 0; i <= Sides; ++i)
		{
			const double T = 2.0 * UE_DOUBLE_PI * i / Sides;
			Profile.Add(FVector2D(Radius * ADartsPub::BoardRadius * 100.0 + Tube * 100.0 * FMath::Cos(T), Tube * 100.0 * FMath::Sin(T)));
		}
		B.Lathe(Slot, Profile, Segments, FVector(-Lift * 100.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), FVector::OneVector, FVector2D::ZeroVector);
	}

	/** A part of the dart lathed about +X: Profile is (radius, web height along the dart) in web metres. */
	void DartPart(MovecadeKart::FMeshBuilder& B, FName Slot, std::initializer_list<FVector2D> WebProfile, int32 Segments = 16)
	{
		// dartGeometry.ts: the tip is at web y 0.62, the render scale 0.48. The mesh puts the tip at the origin.
		constexpr double Scale = 0.48 * 100.0;
		TArray<FVector2D> Profile;
		for (const FVector2D& P : WebProfile)
		{
			Profile.Add(FVector2D(P.X * Scale, (P.Y - 0.62) * Scale));
		}
		B.Lathe(Slot, Profile, Segments, FVector::ZeroVector, FVector::ForwardVector, FVector::OneVector, FVector2D::ZeroVector);
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

UMaterialInterface* ADartsPub::Cached(const TCHAR* Key, TFunctionRef<UMaterialInterface*()> Make)
{
	if (TObjectPtr<UMaterialInterface>* Found = Materials.Find(Key))
	{
		return *Found;
	}
	UMaterialInterface* M = Make();
	Materials.Add(Key, M);
	return M;
}

UMaterialInterface* ADartsPub::SolidGlow(const TCHAR* Key, const FLinearColor& Color, float Strength)
{
	return Cached(Key, [this, Color, Strength]() -> UMaterialInterface* { return MovecadeKart::Art::Plain(this, Color, 0.9, 0.0, Strength); });
}

UInstancedStaticMeshComponent* ADartsPub::Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d"), *GetNameSafe(Mesh), *GetNameSafe(Material), bShadows ? 1 : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Groups.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("PubGroup")));
	C->SetMobility(EComponentMobility::Static);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, Material);
	C->SetupAttachment(RootComponent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(PubTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	Groups.Add(Key, C);
	return C;
}

void ADartsPub::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, bool bShadows)
{
	// The engine cube is 1 m: web (x, y, z) sizes become UE (z, x, y) scales.
	Group(Cube, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(C + FVector(0.0, 0.0, PubZ)), FVector(S.Z, S.X, S.Y)), true);
}

void ADartsPub::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows)
{
	Group(Cylinder, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, PubZ)), FVector(Radius * 2.0, Radius * 2.0, Height)), true);
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

UTextRenderComponent* ADartsPub::Text(USceneComponent* Parent, const FString& Value, const FVector& RelativeCm, float SizeCm, const FLinearColor& Color, const FRotator& Rotation)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this, MakeUniqueObjectName(this, UTextRenderComponent::StaticClass(), TEXT("PubText")));
	T->SetMobility(MobilityUnder(Parent));
	T->SetupAttachment(Parent);
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

void ADartsPub::BuildMeshes()
{
	using MovecadeKart::FMeshBuilder;
	using namespace MovecadeKart::Art;
	// The face: every sector's single beds and its treble and double, the bulls, the surround and the board's edge.
	{
		FMeshBuilder B;
		for (int32 i = 0; i < SectorCount; ++i)
		{
			const double C = SectorCenterAngle(i);
			const double A0 = C - SectorArc / 2.0, A1 = C + SectorArc / 2.0;
			const bool bEven = i % 2 == 0;
			const FName Single = bEven ? TEXT("Cream") : TEXT("Night");
			const FName Ring = bEven ? TEXT("Red") : TEXT("Teal");
			Annulus(B, Single, OuterBullRadius, TripleInnerRadius, A0, A1, 0.0, 4);
			Annulus(B, Ring, TripleInnerRadius, TripleOuterRadius, A0, A1, 0.0, 4);
			Annulus(B, Single, TripleOuterRadius, DoubleInnerRadius, A0, A1, 0.0, 4);
			Annulus(B, Ring, DoubleInnerRadius, DoubleOuterRadius, A0, A1, 0.0, 4);
		}
		Annulus(B, TEXT("Teal"), BullRadius, OuterBullRadius, 0.0, 2.0 * UE_DOUBLE_PI, 0.0, 32);
		Annulus(B, TEXT("Red"), 0.0, BullRadius, 0.0, 2.0 * UE_DOUBLE_PI, 0.0, 24);
		// The surround carries the number ring.
		Annulus(B, TEXT("Surround"), DoubleOuterRadius, 1.38, 0.0, 2.0 * UE_DOUBLE_PI, -0.001, 96);
		B.Cylinder(TEXT("Surround"), Local(0, 0, -0.045), Local(0, 0, -0.001), 1.38 * BoardRadius * 100.0, 96, FVector2D::ZeroVector, false);
		BoardMesh = B.Build(this, TEXT("DartBoard"));
	}
	// The spider: round wire rings at the six radii and twenty round spokes along the sector edges (boardWires.ts),
	// standing proud of the sisal so they catch the board light; the brass hoop round the number ring.
	{
		FMeshBuilder B;
		const double Tube = 0.0032;
		for (const double R : { BullRadius, OuterBullRadius, TripleInnerRadius, TripleOuterRadius, DoubleInnerRadius, DoubleOuterRadius })
		{
			Hoop(B, TEXT("Wire"), R, Tube, Tube, R < 0.2 ? 32 : 120, 6);
		}
		for (int32 i = 0; i < SectorCount; ++i)
		{
			const double A = SectorCenterAngle(i) - SectorArc / 2.0;
			const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
			B.Cylinder(TEXT("Wire"), Local(Dir.X * OuterBullRadius, Dir.Y * OuterBullRadius, Tube), Local(Dir.X * DoubleOuterRadius, Dir.Y * DoubleOuterRadius, Tube),
				Tube * 100.0, 6, FVector2D::ZeroVector, false);
		}
		Hoop(B, TEXT("Brass"), 1.12, 0.012, 0.018);
		WireMesh = B.Build(this, TEXT("DartWires"));
	}
	// The called shot: one sector from the outer bull to the double, centred on +Y (the game rolls it into place),
	// with a bright outline so the wedge reads at TV distance.
	{
		FMeshBuilder B;
		const double A0 = UE_DOUBLE_PI / 2.0 - SectorArc / 2.0, A1 = UE_DOUBLE_PI / 2.0 + SectorArc / 2.0;
		Annulus(B, TEXT("Wedge"), OuterBullRadius, DoubleOuterRadius, A0, A1, 0.008, 6);
		const double Edge = 0.026;
		Annulus(B, TEXT("Edge"), DoubleOuterRadius - Edge, DoubleOuterRadius + Edge * 0.5, A0, A1, 0.011, 8);
		Annulus(B, TEXT("Edge"), OuterBullRadius, OuterBullRadius + Edge, A0, A1, 0.011, 4);
		for (const double A : { A0, A1 })
		{
			const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
			Strip(B, TEXT("Edge"), Dir * OuterBullRadius, Dir * (DoubleOuterRadius + Edge * 0.5), Edge, 0.011);
		}
		WedgeMesh = B.Build(this, TEXT("DartWedge"));
	}
	// A reticle ring, 0.1-0.15 m, facing the camera.
	{
		FMeshBuilder B;
		Annulus(B, TEXT("Ring"), 0.1 / BoardRadius, 0.15 / BoardRadius, 0.0, 2.0 * UE_DOUBLE_PI, 0.0, 40);
		RingMesh = B.Build(this, TEXT("DartReticle"));
	}
	// The dart (dartGeometry.ts): steel point, nose and knurl rings, a tungsten barrel, the shaft, four flights.
	// Four sections (the metals share one): fewer draws per dart.
	{
		FMeshBuilder B;
		DartPart(B, TEXT("Steel"), { { 0.0, 0.30 }, { 0.012, 0.30 }, { 0.0, 0.62 } }, 12);
		DartPart(B, TEXT("Steel"), { { 0.0, 0.30 }, { 0.037, 0.30 }, { 0.014, 0.345 }, { 0.0, 0.345 } });
		DartPart(B, TEXT("Barrel"), { { 0.0, -0.1 }, { 0.043, -0.1 }, { 0.037, 0.3 }, { 0.0, 0.3 } });
		DartPart(B, TEXT("Shaft"), { { 0.0, -0.48 }, { 0.019, -0.48 }, { 0.016, -0.1 }, { 0.0, -0.1 } }, 12);
		for (int32 i = 0; i < 9; ++i)
		{
			const double Y = -0.065 + i * 0.037;
			TArray<FVector2D> Ring;
			for (int32 k = 0; k <= 6; ++k)
			{
				const double T = 2.0 * UE_DOUBLE_PI * k / 6.0;
				Ring.Add(FVector2D((0.042 + 0.0035 * FMath::Cos(T)) * 48.0, (Y + 0.0035 * FMath::Sin(T) - 0.62) * 48.0));
			}
			B.Lathe(TEXT("Steel"), Ring, 16, FVector::ZeroVector, FVector::ForwardVector, FVector::OneVector, FVector2D::ZeroVector);
		}
		// Flights: a fin outline in (out, along) web metres, both faces, every 90 degrees about the shaft.
		const FVector2D Fin[] = { { 0.012, -0.28 }, { 0.132, -0.39 }, { 0.152, -0.43 }, { 0.134, -0.49 }, { 0.072, -0.565 }, { 0.012, -0.535 } };
		const int32 S = B.Section(TEXT("Flight"));
		for (int32 k = 0; k < 4; ++k)
		{
			const double Spin = k * UE_DOUBLE_PI / 2.0;
			const FVector Out(0.0, FMath::Cos(Spin), FMath::Sin(Spin));
			const FVector Normal(0.0, -FMath::Sin(Spin), FMath::Cos(Spin));
			for (const double Face : { 1.0, -1.0 })
			{
				TArray<int32> Ids;
				for (const FVector2D& P : Fin)
				{
					const FVector Pos = FVector((P.Y - 0.62) * 48.0, 0.0, 0.0) + Out * P.X * 48.0 + Normal * Face * 0.14;
					Ids.Add(B.Vert(S, Pos, Normal * Face, FVector2D(P.X / 0.16, -P.Y), FVector2D::ZeroVector));
				}
				for (int32 t = 1; t + 1 < Ids.Num(); ++t)
				{
					B.Tri(S, Ids[0], Ids[t], Ids[t + 1]);
				}
			}
		}
		DartMesh = B.Build(this, TEXT("Dart"));
		DartMaterials = {
			Plain(this, Hex(TEXT("#eef3f6")), 0.16, 1.0),
			Plain(this, Hex(TEXT("#8f9aa3")), 0.34, 1.0),
			Plain(this, Hex(TEXT("#1c2227")), 0.35, 0.3),
			Plain(this, Hex(TEXT("#f7f1e5")), 0.7),
		};
	}
	// A bottle, lathed: body, shoulder, neck, cap.
	{
		FMeshBuilder B;
		TArray<FVector2D> Body = { { 0, 0 }, { 3.6, 0 }, { 3.8, 1 }, { 3.8, 17 }, { 3.2, 21 }, { 1.4, 24 }, { 1.2, 30 }, { 0, 30 } };
		B.Lathe(TEXT("Glass"), Body, 20, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		TArray<FVector2D> Cap = { { 0, 29.5 }, { 1.45, 29.5 }, { 1.45, 32 }, { 0, 32 } };
		B.Lathe(TEXT("Cap"), Cap, 12, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		BottleMesh = B.Build(this, TEXT("Bottle"));
	}
	// A bar stool (cm, origin on the floor): a padded seat on a brass frame with a foot ring.
	{
		FMeshBuilder B;
		B.Cylinder(TEXT("Seat"), FVector(0, 0, 70), FVector(0, 0, 78), 19.0, 24, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Seat"), FVector(0, 0, 78), FVector(19.0, 19.0, 3.5), 4, 24, FVector2D::ZeroVector);
		for (int32 k = 0; k < 4; ++k)
		{
			const double A = UE_DOUBLE_PI / 4.0 + k * UE_DOUBLE_PI / 2.0;
			const FVector Foot(FMath::Cos(A) * 21.0, FMath::Sin(A) * 21.0, 0.0);
			const FVector Top(FMath::Cos(A) * 12.0, FMath::Sin(A) * 12.0, 70.0);
			B.Cylinder(TEXT("Frame"), Foot, Top, 1.3, 8, FVector2D::ZeroVector);
		}
		TArray<FVector2D> Ring;
		for (int32 i = 0; i <= 8; ++i)
		{
			const double T = 2.0 * UE_DOUBLE_PI * i / 8.0;
			Ring.Add(FVector2D(18.4 + 1.1 * FMath::Cos(T), 26.0 + 1.1 * FMath::Sin(T)));
		}
		B.Lathe(TEXT("Frame"), Ring, 24, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		StoolMesh = B.Build(this, TEXT("Stool"));
	}
	// A festoon bulb (cm): a small glowing pear.
	{
		FMeshBuilder B;
		B.Ellipsoid(TEXT("Bulb"), FVector(0, 0, -3), FVector(2.6, 2.6, 3.4), 6, 10, FVector2D::ZeroVector);
		BulbMesh = B.Build(this, TEXT("Bulb"));
	}
}

void ADartsPub::Build()
{
	Clear();
	if (!BoardMesh || !StoolMesh)
	{
		BuildMeshes();
	}
	BuildShell();
	BuildBar();
	BuildBoard();
	BuildProps();
	BuildLights();
}

void ADartsPub::BuildShell()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Brick = Cached(TEXT("Brick"), [this] { return Surface(this, { Hex(TEXT("#6e4a3a")), TEXT("rock_face"), 1.1, 0.5, 0.9, 0.45, 0.92, 0.0, 0.0, true, 0.65 }); });
	UMaterialInterface* Plaster = Cached(TEXT("Plaster"), [this] { return Surface(this, { Hex(TEXT("#4f7262")), TEXT("plastered_wall_02"), 1.4, 0.4, 0.55, 0.4, 0.95 }); });
	UMaterialInterface* Wainscot = Cached(TEXT("Wainscot"), [this] { return Surface(this, { Hex(TEXT("#7a4a36")), TEXT("dark_wooden_planks"), 0.9, 0.45, 0.7, 0.4, 0.55 }); });
	UMaterialInterface* Floor = Cached(TEXT("Floor"), [this] { return Surface(this, { Hex(TEXT("#7f5439")), TEXT("dark_wooden_planks"), 1.6, 0.5, 0.75, 0.4, 0.42, 0.0, 0.0, false, 0.3 }); });
	UMaterialInterface* WoodDark = Cached(TEXT("WoodDark"), [this] { return Surface(this, { Hex(TEXT("#3a2418")), TEXT("dark_wooden_planks"), 0.8, 0.3, 0.6, 0.4, 0.6 }); });
	UMaterialInterface* Ceiling = Cached(TEXT("Ceiling"), [this] { return Surface(this, { Hex(TEXT("#5e5246")), TEXT("plastered_wall_02"), 2.0, 0.35, 0.4, 0.4, 0.95 }); });
	UMaterialInterface* Brass = Cached(TEXT("Brass"), [this] { return Plain(this, Hex(TEXT("#c9a45c")), 0.3, 1.0); });
	UMaterialInterface* Night = SolidGlow(TEXT("Night"), Hex(TEXT("#16223a")), 1.4f * GlowGain);
	UMaterialInterface* Mat = Cached(TEXT("Mat"), [this] { return Plain(this, Hex(TEXT("#2a1a14")), 0.85); });

	const double WallY = (FloorY + CeilingY) / 2.0, WallH = CeilingY - FloorY;
	const double MidZ = PubDepth / 2.0;
	const double Half = PubWidth / 2.0;
	// Envelope: brick behind the board, plaster down the sides, a floor and a ceiling with beams.
	Box(Brick, FVector(0.0, WallY, 0.11), FVector(PubWidth + 0.4, WallH, 0.2));
	for (const double Side : { -1.0, 1.0 })
	{
		Box(Plaster, FVector(Side * (Half + 0.1), WallY, MidZ), FVector(0.2, WallH, PubDepth));
		Box(Wainscot, FVector(Side * (Half - 0.02), FloorY + WainscotHeight / 2.0, MidZ), FVector(0.04, WainscotHeight, PubDepth));
		Box(Brass, FVector(Side * (Half - 0.04), FloorY + WainscotHeight, MidZ), FVector(0.04, 0.04, PubDepth), false);
	}
	Box(Wainscot, FVector(0.0, FloorY + WainscotHeight / 2.0, 0.23), FVector(PubWidth, WainscotHeight, 0.04));
	Box(Brass, FVector(0.0, FloorY + WainscotHeight, 0.27), FVector(PubWidth, 0.04, 0.04), false);
	Box(WoodDark, FVector(0.0, FloorY + 0.09, 0.28), FVector(PubWidth, 0.18, 0.06));
	Box(WoodDark, FVector(0.0, CeilingY - 0.28, 0.28), FVector(PubWidth, 0.16, 0.12));
	Box(Floor, FVector(0.0, FloorY - 0.05, MidZ), FVector(PubWidth, 0.1, PubDepth));
	Box(Ceiling, FVector(0.0, CeilingY + 0.1, MidZ), FVector(PubWidth + 0.4, 0.2, PubDepth));
	Box(Brick, FVector(0.0, WallY, PubDepth + 0.1), FVector(PubWidth + 0.4, WallH, 0.2));
	for (const double Z : { 1.4, 5.2, 9.4, 13.6 })
	{
		Box(WoodDark, FVector(0.0, CeilingY - 0.18, Z), FVector(PubWidth, 0.3, 0.25));
	}
	// The oche: a worn mat, the brass throw line, the line's lettering.
	Box(Mat, FVector(0.0, FloorY + 0.012, OcheZ - PubZ + 0.25), FVector(3.0, 0.024, 1.3), false);
	Box(Brass, FVector(0.0, FloorY + 0.03, 5.18), FVector(2.6, 0.03, 0.05), false);
	{
		// Rotated before it registers: a static component can't move afterwards.
		Text(RootComponent, TEXT("OCHE  ·  2.37 M"), ToWorld(FVector(0.0, FloorY + 0.03, 4.62 + PubZ)), 16.f, Hex(TEXT("#efe2cc")), FRotator(90.0, 180.0, 0.0));
	}
	// Night windows down both sides, and framed prints between them.
	for (const double Side : { -1.0, 1.0 })
	{
		for (const double Z : { 2.6, 7.2 })
		{
			Box(WoodDark, FVector(Side * (Half - 0.04), 3.15, Z), FVector(0.06, 2.25, 1.58));
			Box(Night, FVector(Side * (Half - 0.075), 3.15, Z), FVector(0.01, 2.05, 1.38), false);
			Box(WoodDark, FVector(Side * (Half - 0.08), 3.15, Z), FVector(0.02, 0.05, 1.38), false);
		}
	}
}

void ADartsPub::BuildBar()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Wood = Cached(TEXT("BarTop"), [this] { return Surface(this, { Hex(TEXT("#a8714e")), TEXT("dark_wooden_planks"), 0.7, 0.45, 0.6, 0.4, 0.3 }); });
	UMaterialInterface* WoodDark = Cached(TEXT("WoodDark"), [this] { return Surface(this, { Hex(TEXT("#3a2418")), TEXT("dark_wooden_planks"), 0.8, 0.3, 0.6, 0.4, 0.6 }); });
	UMaterialInterface* Panel = Cached(TEXT("Panel"), [this] { return Surface(this, { Hex(TEXT("#6b3f2c")), TEXT("dark_wooden_planks"), 0.6, 0.45, 0.7, 0.4, 0.55 }); });
	UMaterialInterface* Brass = Cached(TEXT("Brass"), [this] { return Plain(this, Hex(TEXT("#c9a45c")), 0.3, 1.0); });
	UMaterialInterface* Chalk = Cached(TEXT("Chalk"), [this] { return Surface(this, { Hex(TEXT("#1b2420")), TEXT("painted_concrete"), 1.0, 0.25, 0.3, 0.3, 0.95 }); });
	// The back bar: a smoked mirror (Lumen reflects the bottles and the room in it) lit by a warm strip under the shelf.
	UMaterialInterface* Mirror = Cached(TEXT("Mirror"), [this] { return Plain(this, Hex(TEXT("#7a6050")), 0.22, 0.75); });
	UMaterialInterface* ShelfGlow = SolidGlow(TEXT("ShelfGlow"), Hex(TEXT("#ffb066")), 3.2f * GlowGain);
	UMaterialInterface* Beer = Cached(TEXT("Beer"), [this] { return Plain(this, Hex(TEXT("#b86a1c")), 0.12, 0.0, 0.4); });
	UMaterialInterface* Foam = Cached(TEXT("Foam"), [this] { return Plain(this, Hex(TEXT("#f3ead8")), 0.6); });
	UMaterialInterface* PrintMats[] = {
		Cached(TEXT("Print0"), [this] { return Unlit(this, TEXT("/Game/Tracks/Shared/Textures/mc_billboard_0.mc_billboard_0"), FLinearColor(0.75f, 0.68f, 0.6f) * GlowGain); }),
		Cached(TEXT("Print2"), [this] { return Unlit(this, TEXT("/Game/Tracks/Shared/Textures/mc_billboard_2.mc_billboard_2"), FLinearColor(0.75f, 0.68f, 0.6f) * GlowGain); }),
		Cached(TEXT("Print3"), [this] { return Unlit(this, TEXT("/Game/Tracks/Shared/Textures/mc_billboard_3.mc_billboard_3"), FLinearColor(0.75f, 0.68f, 0.6f) * GlowGain); }),
	};

	// The bar: worn top, panelled front, brass foot rail.
	Box(Wood, FVector(BarX, FloorY + 1.08, BarZ), FVector(2.7, 0.08, 0.72));
	Box(Panel, FVector(BarX, FloorY + 0.52, BarZ + 0.3), FVector(2.6, 1.04, 0.06));
	Box(WoodDark, FVector(BarX, FloorY + 0.52, BarZ), FVector(2.5, 1.04, 0.56));
	Group(Cylinder, Brass, true)->AddInstance(FTransform(FRotator(0.0, 0.0, 90.0), ToWorld(FVector(BarX, FloorY + 0.2, BarZ + 0.42 + PubZ)), FVector(0.05, 0.05, 2.5)), true);
	// The back bar: shelf, mirror, the warm strip that lights the bottles from below; the chalkboard above.
	Box(WoodDark, FVector(BarX, FloorY + 2.05, 0.38), FVector(2.2, 0.05, 0.3));
	Box(WoodDark, FVector(BarX, FloorY + 2.32, 0.25), FVector(2.5, 1.0, 0.02));
	Box(Mirror, FVector(BarX, FloorY + 2.32, 0.265), FVector(2.3, 0.86, 0.01), false);
	Box(ShelfGlow, FVector(BarX, FloorY + 2.02, 0.5), FVector(2.1, 0.012, 0.02), false);
	{
		const TCHAR* const GlassColors[] = { TEXT("#2f6b3a"), TEXT("#8a5a1c"), TEXT("#c9d6dc"), TEXT("#5a1f2a"), TEXT("#3a5f7a"), TEXT("#b08a3c") };
		const double Offsets[] = { -0.82, -0.49, -0.16, 0.16, 0.49, 0.82 };
		UMaterialInterface* Caps[] = {
			Cached(TEXT("CapSteel"), [this] { return Plain(this, Hex(TEXT("#d9d9d9")), 0.35, 1.0); }),
			Brass,
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Offsets); ++i)
		{
			UStaticMeshComponent* Bottle = Part(BottleMesh, RootComponent, FTransform(ToWorld(FVector(BarX + Offsets[i], FloorY + 2.075, 0.36 + (i % 2) * 0.05 + PubZ))));
			Bottle->SetMaterial(0, Cached(GlassColors[i], [this, i, &GlassColors] { return Plain(this, Hex(GlassColors[i]) * 0.8f, 0.08, 0.0, 0.25); }));
			Bottle->SetMaterial(1, Caps[i % 2]);
		}
	}
	Box(WoodDark, FVector(BarX, 2.95, 0.3), FVector(2.06, 1.3, 0.06));
	Box(Chalk, FVector(BarX, 2.95, 0.335), FVector(1.86, 1.1, 0.01), false);
	{
		USceneComponent* Board = NewObject<USceneComponent>(this, TEXT("ChalkBoard"));
		Board->SetMobility(EComponentMobility::Static);
		Board->SetupAttachment(RootComponent);
		Board->SetRelativeLocation(ToWorld(FVector(BarX, 2.95, 0.345 + PubZ)));
		Board->ComponentTags.Add(PubTag);
		AddInstanceComponent(Board);
		Board->RegisterComponent();
		Text(Board, TEXT("BULLSEYE BASH"), FVector(0.0, 0.0, 38.0), 15.f, MovecadeKart::Art::Hex(TEXT("#f2efe6")));
		for (int32 i = 0; i < 4; ++i)
		{
			ChalkLines.Add(Text(Board, FString(), FVector(0.0, 0.0, 14.0 - i * 17.0), 12.f, MovecadeKart::Art::Hex(TEXT("#e8e2d2"))));
			ChalkShown.Add(FString());
		}
	}
	// Pints on the bar for the regulars.
	for (const double X : { BarX - 0.62, BarX - 0.05, BarX + 0.6 })
	{
		Post(Beer, FVector(X, FloorY + 1.12, BarZ + 0.18), 0.04, 0.15);
		Post(Foam, FVector(X, FloorY + 1.27, BarZ + 0.18), 0.041, 0.025);
	}
	// Framed prints with picture lights, on the far side and above the cabinet.
	const FVector Prints[] = { FVector(PrintX, 1.75, 0.34), FVector(-1.65, 1.95, 0.34), FVector(0.0, 2.35, 0.34) };
	const double Scales[] = { 0.88, 0.54, 0.54 };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Prints); ++i)
	{
		const double S = Scales[i];
		Box(WoodDark, Prints[i], FVector(1.44 * S, 1.84 * S, 0.06));
		Box(PrintMats[i], Prints[i] + FVector(0.0, 0.0, 0.035), FVector(1.28 * S, 1.68 * S, 0.01), false);
		Box(Brass, Prints[i] + FVector(0.0, 1.0 * S, 0.1), FVector(0.5 * S, 0.04, 0.08), false);
	}
}

void ADartsPub::BuildBoard()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Wood = Cached(TEXT("Cabinet"), [this] { return Surface(this, { Hex(TEXT("#4a2e20")), TEXT("dark_wooden_planks"), 0.7, 0.45, 0.6, 0.4, 0.45 }); });
	UMaterialInterface* WoodDark = Cached(TEXT("CabinetDark"), [this] { return Surface(this, { Hex(TEXT("#2a1810")), TEXT("dark_wooden_planks"), 0.8, 0.3, 0.6, 0.4, 0.5 }); });
	UMaterialInterface* Brass = Cached(TEXT("Brass"), [this] { return Plain(this, Hex(TEXT("#c9a45c")), 0.3, 1.0); });
	UMaterialInterface* Accent = SolidGlow(TEXT("Accent"), Hex(TEXT("#3fc9c2")), 1.35f * GlowGain);
	UMaterialInterface* Strip = SolidGlow(TEXT("LampStrip"), Hex(TEXT("#fff0d8")), 2.6f * GlowGain);
	UMaterialInterface* Collar = Cached(TEXT("Collar"), [this] { return Plain(this, Hex(TEXT("#3fc9c2")) * 0.55f, 0.6); });

	// The cabinet: back, rails and sides (DartsScene.tsx Cabinet), the lane accent, the collar behind the board.
	auto CabinetBox = [&](UMaterialInterface* M, const FVector& C, const FVector& S)
	{
		Group(Cube, M, true)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(FVector(0.0, BoardY, 0.0) + C), FVector(S.Z, S.X, S.Y)), true);
	};
	CabinetBox(Wood, FVector(0.0, 0.0, -0.30), FVector(2.15, 2.35, 0.34));
	CabinetBox(WoodDark, FVector(0.0, 1.12, -0.10), FVector(2.35, 0.12, 0.22));
	CabinetBox(WoodDark, FVector(0.0, -1.12, -0.10), FVector(2.35, 0.12, 0.22));
	CabinetBox(WoodDark, FVector(-1.12, 0.0, -0.10), FVector(0.12, 2.35, 0.22));
	CabinetBox(WoodDark, FVector(1.12, 0.0, -0.10), FVector(0.12, 2.35, 0.22));
	CabinetBox(Accent, FVector(0.0, -0.98, 0.0), FVector(0.9, 0.02, 0.03));
	Group(Cylinder, Collar, true)->AddInstance(FTransform(FRotator(90.0, 0.0, 0.0), ToWorld(FVector(0.0, BoardY, -0.1)), FVector(1.42 * BoardRadius * 2.0, 1.42 * BoardRadius * 2.0, 0.04)), true);
	// The board lamp: a brass arm off the cabinet top, the hood over the board, a glowing strip under it.
	CabinetBox(Brass, FVector(0.0, 1.32, 0.2), FVector(0.05, 0.05, 0.75));
	CabinetBox(Brass, FVector(0.0, 1.3, 0.62), FVector(0.95, 0.09, 0.24));
	CabinetBox(Strip, FVector(0.0, 1.25, 0.62), FVector(0.85, 0.012, 0.08));

	BoardPivot = NewObject<USceneComponent>(this, TEXT("BoardPivot"));
	// Movable before it attaches: the face, spider, numbers and wedge under it wobble.
	BoardPivot->SetMobility(EComponentMobility::Movable);
	BoardPivot->SetupAttachment(RootComponent);
	BoardPivot->SetRelativeLocation(ToWorld(FVector(0.0, BoardY, 0.0)));
	BoardPivot->ComponentTags.Add(PubTag);
	AddInstanceComponent(BoardPivot);
	BoardPivot->RegisterComponent();

	// The face (sisal, matte) and its colours (DartsScene.tsx:542-563).
	UStaticMeshComponent* Face = Part(BoardMesh, BoardPivot, FTransform::Identity);
	// Sisal: matte and clean-coloured, a visible fibre from the sand set so it reads as bristle, not plastic.
	auto Sisal = [this](const TCHAR* Color)
	{
		return Surface(this, { Hex(Color), TEXT("gravelly_sand"), 0.18, 0.14, 0.45, 0.25, 0.92, 0.0, 0.0, true, 1.0 });
	};
	Face->SetMaterial(0, Sisal(TEXT("#f4e4cc")));
	Face->SetMaterial(1, Sisal(TEXT("#e8467f")));
	Face->SetMaterial(2, Sisal(TEXT("#23113a")));
	Face->SetMaterial(3, Sisal(TEXT("#4fc0de")));
	Face->SetMaterial(4, Plain(this, Hex(TEXT("#2a1240")), 0.7));
	UStaticMeshComponent* Wires = Part(WireMesh, BoardPivot, FTransform::Identity);
	Wires->SetMaterial(0, Plain(this, Hex(TEXT("#e4e9ec")), 0.16, 1.0));
	Wires->SetMaterial(1, Brass);
	Wires->SetCastShadow(false);
	// The number ring (cream on the surround), read upright all the way round, as on a real board.
	for (int32 i = 0; i < SectorCount; ++i)
	{
		const double A = SectorCenterAngle(i);
		Text(BoardPivot, FString::FromInt(Sectors()[i]), Local(1.25 * FMath::Cos(A), 1.25 * FMath::Sin(A), 0.004), 8.5f, Hex(TEXT("#fbf1df")));
	}
	BountyWedge = Part(WedgeMesh, BoardPivot, FTransform::Identity);
	BountyMaterial = Glow(this, Hex(TEXT("#ffd166")) * (3.2f * GlowGain));
	if (BountyMaterial)
	{
		BountyMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.35f);
	}
	BountyEdgeMaterial = Glow(this, Hex(TEXT("#ffd166")) * (6.f * GlowGain));
	BountyWedge->SetMaterial(0, BountyMaterial);
	BountyWedge->SetMaterial(1, BountyEdgeMaterial);
	BountyWedge->SetCastShadow(false);
}

void ADartsPub::BuildProps()
{
	using namespace MovecadeKart::Art;
	// The lived-in pub: stools at the bar, a high table with its pints, a jukebox glowing on the far wall, and a
	// festoon of warm bulbs strung across the brick.
	UMaterialInterface* Seat = Cached(TEXT("StoolSeat"), [this] { return Plain(this, Hex(TEXT("#5a1c22")), 0.55); });
	UMaterialInterface* Frame = Cached(TEXT("Brass"), [this] { return Plain(this, Hex(TEXT("#c9a45c")), 0.3, 1.0); });
	UMaterialInterface* Top = Cached(TEXT("BarTop"), [this] { return Surface(this, { Hex(TEXT("#a8714e")), TEXT("dark_wooden_planks"), 0.7, 0.45, 0.6, 0.4, 0.3 }); });
	UMaterialInterface* WoodDark = Cached(TEXT("WoodDark"), [this] { return Surface(this, { Hex(TEXT("#3a2418")), TEXT("dark_wooden_planks"), 0.8, 0.3, 0.6, 0.4, 0.6 }); });
	UMaterialInterface* Beer = Cached(TEXT("Beer"), [this] { return Plain(this, Hex(TEXT("#b86a1c")), 0.12, 0.0, 0.4); });
	UMaterialInterface* Foam = Cached(TEXT("Foam"), [this] { return Plain(this, Hex(TEXT("#f3ead8")), 0.6); });
	UMaterialInterface* Bulb = SolidGlow(TEXT("Festoon"), Hex(TEXT("#ffc47a")), 4.f * GlowGain);
	UMaterialInterface* Cord = Cached(TEXT("Cord"), [this] { return Plain(this, Hex(TEXT("#111111")), 0.8); });
	UMaterialInterface* JukeGlow = SolidGlow(TEXT("Jukebox"), Hex(TEXT("#ff7a59")), 2.2f * GlowGain);
	UMaterialInterface* JukeTeal = SolidGlow(TEXT("JukeboxTeal"), Hex(TEXT("#3fc9c2")), 1.8f * GlowGain);

	auto Stool = [&](double X, double Z, double Yaw)
	{
		UInstancedStaticMeshComponent* G = Group(StoolMesh, Seat, true);
		if (G->GetNumMaterials() > 1)
		{
			G->SetMaterial(1, Frame);
		}
		G->AddInstance(FTransform(FRotator(0.0, Yaw, 0.0), ToWorld(FVector(X, FloorY, Z + PubZ))), true);
	};
	for (const double X : { BarX - 0.85, BarX, BarX + 0.85 })
	{
		Stool(X, BarZ + 0.72, X * 37.0);
	}
	// A high table and two stools in the far corner, a couple of pints left on it.
	{
		const double TX = -2.75, TZ = 3.6;
		Post(Frame, FVector(TX, FloorY, TZ), 0.035, 1.02);
		Post(WoodDark, FVector(TX, FloorY, TZ), 0.22, 0.03);
		Post(Top, FVector(TX, FloorY + 1.02, TZ), 0.36, 0.04);
		Stool(TX - 0.55, TZ + 0.2, 20.0);
		Stool(TX + 0.45, TZ + 0.45, -35.0);
		for (const FVector2D& P : { FVector2D(TX - 0.1, TZ + 0.08), FVector2D(TX + 0.14, TZ - 0.06) })
		{
			Post(Beer, FVector(P.X, FloorY + 1.06, P.Y), 0.04, 0.15);
			Post(Foam, FVector(P.X, FloorY + 1.21, P.Y), 0.041, 0.025);
		}
	}
	// The jukebox against the left wall, past the windows.
	{
		const double JX = -PubWidth / 2.0 + 0.36, JZ = 4.9;
		Box(WoodDark, FVector(JX, FloorY + 0.7, JZ), FVector(0.62, 1.4, 0.95));
		Box(JukeGlow, FVector(JX + 0.315, FloorY + 1.05, JZ), FVector(0.01, 0.55, 0.7), false);
		Box(JukeTeal, FVector(JX + 0.318, FloorY + 1.42, JZ), FVector(0.01, 0.08, 0.8), false);
		Box(JukeTeal, FVector(JX + 0.318, FloorY + 0.62, JZ), FVector(0.01, 0.05, 0.8), false);
		Box(Frame, FVector(JX + 0.32, FloorY + 0.3, JZ), FVector(0.02, 0.32, 0.62), false);
	}
	// The festoon: bulbs on a sagging cord, wall to wall across the brick above the cabinet.
	{
		constexpr int32 Bulbs = 15;
		const double Y0 = 4.15, Sag = 0.4, Z = 0.42;
		TArray<FVector> Points;
		for (int32 i = 0; i < Bulbs; ++i)
		{
			const double T = (i + 0.5) / Bulbs;
			const double X = FMath::Lerp(-PubWidth / 2.0 + 0.3, PubWidth / 2.0 - 0.3, T);
			const double Y = Y0 - Sag * 4.0 * T * (1.0 - T);
			Points.Add(FVector(X, Y, Z));
		}
		UInstancedStaticMeshComponent* Bulbs1 = Group(BulbMesh, Bulb, false);
		UInstancedStaticMeshComponent* Cords = Group(Cylinder, Cord, false);
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			Bulbs1->AddInstance(FTransform(ToWorld(Points[i] + FVector(0.0, 0.0, PubZ))), true);
			if (i + 1 < Points.Num())
			{
				const FVector A = ToWorld(Points[i] + FVector(0.0, 0.0, PubZ)), B = ToWorld(Points[i + 1] + FVector(0.0, 0.0, PubZ));
				const FVector Dir = (B - A).GetSafeNormal();
				Cords->AddInstance(FTransform(FRotationMatrix::MakeFromZ(Dir).Rotator(), (A + B) / 2.0, FVector(0.008, 0.008, (B - A).Size() / 100.0)), true);
			}
		}
	}
}

void ADartsPub::BuildLights()
{
	auto Point = [this](const FVector& Web, float Lumens, float RadiusM, const TCHAR* Color)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("PubPoint")));
		L->SetMobility(EComponentMobility::Movable);
		L->SetupAttachment(RootComponent);
		L->SetRelativeLocation(ToWorld(Web));
		L->SetIntensityUnits(ELightUnits::Lumens);
		L->SetIntensity(Lumens);
		L->SetAttenuationRadius(RadiusM * 100.f);
		L->SetLightColor(MovecadeKart::Art::Hex(Color));
		L->SetSourceRadius(8.f);
		// One shadowed light in the room (the board spot): moving darts and the thrower would invalidate a
		// shadowed point light's six cube faces every frame.
		L->SetCastShadows(false);
		L->ComponentTags.Add(PubTag);
		AddInstanceComponent(L);
		L->RegisterComponent();
		return L;
	};
	// The board spot (DartsPub.tsx washLights): the brightest thing in the room is the board.
	USpotLightComponent* Spot = NewObject<USpotLightComponent>(this, TEXT("BoardSpot"));
	Spot->SetMobility(EComponentMobility::Movable);
	Spot->SetupAttachment(RootComponent);
	const FVector From = ToWorld(FVector(0.0, BoardY + 2.1, 3.2 + PubZ));
	const FVector To = ToWorld(FVector(0.0, BoardY, 0.0));
	Spot->SetRelativeLocation(From);
	Spot->SetRelativeRotation((To - From).Rotation());
	Spot->SetIntensityUnits(ELightUnits::Lumens);
	Spot->SetIntensity(1250.f);
	Spot->SetAttenuationRadius(600.f);
	Spot->SetOuterConeAngle(FMath::RadiansToDegrees(0.36f) * 1.15f);
	Spot->SetInnerConeAngle(FMath::RadiansToDegrees(0.36f) * 0.4f);
	Spot->SetLightColor(MovecadeKart::Art::Hex(TEXT("#fff0d8")));
	Spot->SetSourceRadius(12.f);
	Spot->SetCastShadows(true);
	Spot->ComponentTags.Add(PubTag);
	AddInstanceComponent(Spot);
	Spot->RegisterComponent();

	// Practicals: the oche pendant over the thrower, the bar pendant, the back bar; then the room's soft fill and rim.
	using namespace MovecadeKart::Art;
	UMaterialInterface* Enamel = Cached(TEXT("Enamel"), [this] { return Plain(this, Hex(TEXT("#1f3a33")), 0.32); });
	UMaterialInterface* Bulb = SolidGlow(TEXT("PendantBulb"), Hex(TEXT("#ffcf8a")), 3.2f * GlowGain);
	UMaterialInterface* Cord = Cached(TEXT("Cord"), [this] { return Plain(this, Hex(TEXT("#111111")), 0.8); });
	const FVector Pendants[] = { FVector(-0.45, 3.05, 5.0), FVector(BarX + 0.2, 3.85, 1.6), FVector(-2.77, 3.2, 2.2) };
	for (const FVector& P : Pendants)
	{
		Post(Cord, P + FVector(0.0, 0.1, 0.0), 0.006, CeilingY - P.Y - 0.1, false);
		Post(Enamel, P, 0.22, 0.1);
		Post(Bulb, P + FVector(0.0, -0.03, 0.0), 0.12, 0.05, false);
	}
	Point(Pendants[0] + FVector(0.0, -0.2, PubZ), 5200.f, 8.f, TEXT("#ffc98c"));
	Point(Pendants[1] + FVector(0.0, -0.2, PubZ), 4200.f, 6.f, TEXT("#ffc98c"));
	Point(Pendants[2] + FVector(0.0, -0.2, PubZ), 3000.f, 5.f, TEXT("#ffc98c"));
	Point(FVector(BarX, FloorY + 2.75, 0.75 + PubZ), 1000.f, 2.5f, TEXT("#ffa860"));
	// The room's fill, warm and soft from over the camera, sized to the room the camera sees (not the whole hall).
	Point(FVector(0.0, 4.2, 7.5 + PubZ), 14000.f, 12.f, TEXT("#d6c6b2"));
	Point(FVector(0.0, 3.6, 3.2 + PubZ), 9500.f, 9.f, TEXT("#ffd8ac"));
	Point(FVector(-2.0, 4.0, 0.9 + PubZ), 2600.f, 6.f, TEXT("#3fc9c2"));
	// The festoon's glow along the brick, and the jukebox's on the far wall.
	Point(FVector(0.0, 3.4, 0.9 + PubZ), 2600.f, 5.f, TEXT("#ffb46a"));
	Point(FVector(-PubWidth / 2.0 + 1.0, FloorY + 1.1, 4.9 + PubZ), 900.f, 3.f, TEXT("#ff7a59"));
	Point(FVector(0.0, 1.3, 0.62), 250.f, 2.5f, TEXT("#fff0d8"));

	// pubMood.ts grade: a lit board in a dim room. Auto exposure would lift the room and burn the board out, so it
	// is held to a narrow band set by the board's light; bloom and vignette from the mood.
	UPostProcessComponent* Grade = NewObject<UPostProcessComponent>(this, TEXT("PubGrade"));
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

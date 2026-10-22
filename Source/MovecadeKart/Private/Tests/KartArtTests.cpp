#include "Art/KartMaterialCache.h"
#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartMaterialCacheTest, "MovecadeKart.Art.MaterialCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartMaterialCacheTest::RunTest(const FString& Parameters)
{
	using namespace MovecadeKart::Art;
	UObject* Outer = GetTransientPackage();
	FKartMaterialCache Cache;
	const FLinearColor Warm = Hex(TEXT("#ffd89a")) * 7.f;

	// Same look, same instance; any difference, its own instance.
	UMaterialInstanceDynamic* Lamp = Cache.GlowOpaque(Outer, Warm);
	if (!TestNotNull(TEXT("glow made (track masters imported)"), Lamp))
	{
		return false;
	}
	TestTrue(TEXT("same glow, same instance"), Cache.GlowOpaque(Outer, Warm) == Lamp);
	TestTrue(TEXT("translucent glow is another instance"), Cache.Glow(Outer, Warm) != Lamp);
	TestTrue(TEXT("a different colour is another instance"), Cache.GlowOpaque(Outer, Warm * 1.0001f) != Lamp);
	TestTrue(TEXT("same plain look, same instance"), Cache.Plain(Outer, Warm, 0.5) == Cache.Plain(Outer, Warm, 0.5));
	TestTrue(TEXT("plain roughness is part of the look"), Cache.Plain(Outer, Warm, 0.5) != Cache.Plain(Outer, Warm, 0.6));
	FSurfaceLook Wood;
	Wood.Detail = TEXT("dark_wooden_planks");
	FSurfaceLook Floor = Wood;
	Floor.bTriplanar = false;
	TestTrue(TEXT("same surface, same instance"), Cache.Surface(Outer, Wood) == Cache.Surface(Outer, Wood));
	TestTrue(TEXT("projection is part of the surface"), Cache.Surface(Outer, Wood) != Cache.Surface(Outer, Floor));
	const int32 Before = Cache.Num();
	int32 Calls = 0;
	auto Make = [&Calls, Outer]() -> UMaterialInterface* { ++Calls; return Plain(Outer, FLinearColor::Red); };
	UMaterialInterface* Custom = Cache.FindOrMake(TEXT("custom"), Make);
	TestTrue(TEXT("custom key made once"), Cache.FindOrMake(TEXT("custom"), Make) == Custom && Calls == 1);
	TestEqual(TEXT("one entry per look"), Cache.Num(), Before + 1);
	Cache.Reset();
	TestEqual(TEXT("reset forgets"), Cache.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartGlowOpaqueTest, "MovecadeKart.Art.GlowOpaque",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartGlowOpaqueTest::RunTest(const FString& Parameters)
{
	using namespace MovecadeKart::Art;
	const FLinearColor Neon = Hex(TEXT("#41e8ff")) * 8.f;
	UMaterialInstanceDynamic* Opaque = GlowOpaque(GetTransientPackage(), Neon);
	UMaterialInstanceDynamic* Translucent = Glow(GetTransientPackage(), Neon);
	if (!TestNotNull(TEXT("opaque glow made"), Opaque) || !TestNotNull(TEXT("glow made"), Translucent))
	{
		return false;
	}
	// The opaque path is the opaque unlit master with the same Color; the translucent one is unchanged.
	TestEqual(TEXT("opaque glow's master"), Opaque->Parent ? Opaque->Parent->GetName() : FString(), FString(TEXT("M_TrackUnlitTexture")));
	TestEqual(TEXT("glow keeps the translucent master"), Translucent->Parent ? Translucent->Parent->GetName() : FString(), FString(TEXT("M_TrackTranslucent")));
	FLinearColor Color;
	TestTrue(TEXT("opaque glow colour"), Opaque->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(TEXT("Color"))), Color) && Color.Equals(Neon));
	TestTrue(TEXT("opaque master draws opaque"), Opaque->GetBlendMode() == BLEND_Opaque);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartMeshBuilderTest, "MovecadeKart.Art.MeshBuilder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartMeshBuilderTest::RunTest(const FString& Parameters)
{
	using MovecadeKart::FMeshBuilder;
	FMeshBuilder M;
	// Slots number in the order of their first use.
	TestEqual(TEXT("first slot"), M.Section(TEXT("main")), 0);
	TestEqual(TEXT("second slot"), M.Section(TEXT("trim")), 1);
	TestEqual(TEXT("a slot again is the same section"), M.Section(TEXT("main")), 0);

	// A box: four corners and two triangles per face, every triangle facing out along its normal.
	M.Box(TEXT("main"), FVector::ZeroVector, FVector(10.0), FVector2D::ZeroVector);
	const FMeshBuilder::FSection& Box = M.Sections[0];
	TestEqual(TEXT("box corners"), Box.Verts.Num(), 24);
	TestEqual(TEXT("box indices"), Box.Indices.Num(), 36);
	for (int32 t = 0; t + 2 < Box.Indices.Num(); t += 3)
	{
		const FVector3f A = Box.Verts[Box.Indices[t]].P, B = Box.Verts[Box.Indices[t + 1]].P, C = Box.Verts[Box.Indices[t + 2]].P;
		// UE's front faces wind clockwise seen from outside: the right-hand normal points in.
		const FVector3f Face = FVector3f::CrossProduct(B - A, C - A);
		TestTrue(TEXT("faces outward"), FVector3f::DotProduct(Face, Box.Verts[Box.Indices[t]].N) < 0.f);
	}

	// A closed lathe (an ellipsoid) has a ring of verts per profile point.
	M.Ellipsoid(TEXT("trim"), FVector::ZeroVector, FVector(5.0), 4, 8, FVector2D::ZeroVector);
	TestEqual(TEXT("ellipsoid rings x segments"), M.Sections[1].Verts.Num(), 5 * 9);
	TestTrue(TEXT("ellipsoid has triangles"), M.Sections[1].Indices.Num() > 0 && M.Sections[1].Indices.Num() % 3 == 0);

	UStaticMesh* Mesh = M.Build(GetTransientPackage(), TEXT("SM_MeshBuilderTest"));
	if (TestNotNull(TEXT("built"), Mesh))
	{
		TestEqual(TEXT("one material slot per section"), Mesh->GetStaticMaterials().Num(), 2);
		TestEqual(TEXT("slot names kept"), Mesh->GetStaticMaterials()[1].MaterialSlotName, FName(TEXT("trim")));
	}
	return true;
}

#endif

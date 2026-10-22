#include "Art/KartMaterialCache.h"
#include "Art/KartMaterials.h"
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

#endif

#include "FX/KartFx.h"
#include "FX/KartItemMeshes.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFxDriftTiersTest, "MovecadeKart.Fx.DriftTiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartFxDriftTiersTest::RunTest(const FString& Parameters)
{
	// constants.ts: blue from 1 s of drift, orange from 3 s, purple from 6 s.
	TestEqual(TEXT("no sparks before 1 s"), DriftLevel(0.99).Level, 0);
	TestEqual(TEXT("blue at 1 s"), DriftLevel(1.0).Level, 1);
	TestEqual(TEXT("orange at 3 s"), DriftLevel(3.0).Level, 2);
	TestEqual(TEXT("purple at 6 s"), DriftLevel(6.0).Level, 3);
	TestTrue(TEXT("blue is blue"), DriftLevel(1.5).Color.B > DriftLevel(1.5).Color.R);
	TestTrue(TEXT("orange is orange"), DriftLevel(4.0).Color.R > DriftLevel(4.0).Color.B);
	TestTrue(TEXT("grade colours follow the tiers"), DriftGradeColor(TEXT("DRIFT_CLEAN_SUPER")).Equals(DriftLevel(3.0).Color)
		&& DriftGradeColor(TEXT("DRIFT_ULTRA")).Equals(DriftLevel(6.0).Color) && DriftGradeColor(TEXT("DRIFT_MINI")).Equals(DriftLevel(1.0).Color));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFxParticlesDueTest, "MovecadeKart.Fx.ParticlesDue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartFxParticlesDueTest::RunTest(const FString& Parameters)
{
	// kartTrails.ts particlesDue: the fraction carries over, so 70/s over 60 frames is exactly 70.
	double Carry = 0.0;
	int32 Total = 0;
	for (int32 i = 0; i < 60; ++i)
	{
		Total += ParticlesDue(Carry, 70.0, 1.0 / 60.0);
	}
	TestEqual(TEXT("70 per second"), Total, 70);
	TestEqual(TEXT("nothing for a negative step"), ParticlesDue(Carry, 70.0, -1.0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFxBurstsTest, "MovecadeKart.Fx.Bursts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartFxBurstsTest::RunTest(const FString& Parameters)
{
	auto Count = [](const TArray<FKartFxEmit>& Out, EField Field)
	{
		int32 N = 0;
		for (const FKartFxEmit& E : Out)
		{
			N += E.Field == Field ? E.Count : 0;
		}
		return N;
	};
	// kartFxBus.ts: sparks 34 x burst, smoke for every kind, fire and a ring only for heavy hits.
	TArray<FKartFxEmit> Hit, Wall, Faint;
	ImpactBurst(EImpact::ProjectileHit, FVector::ZeroVector, 1.0, Hit);
	ImpactBurst(EImpact::Wall, FVector::ZeroVector, 1.0, Wall);
	ImpactBurst(EImpact::Wall, FVector::ZeroVector, 0.0, Faint);
	TestEqual(TEXT("hit sparks"), Count(Hit, EField::Sparks), 34);
	TestTrue(TEXT("hit has fire and a ring"), Count(Hit, EField::Fire) > 0 && Count(Hit, EField::Ring) == 1);
	TestEqual(TEXT("wall has no ring"), Count(Wall, EField::Ring), 0);
	TestTrue(TEXT("intensity floors at 0.15"), Count(Faint, EField::Sparks) > 0 && Count(Faint, EField::Sparks) < Count(Wall, EField::Sparks));

	// The explosion's shock ring reaches the blast radius (ring sprites start at 1.2 m and scale by SizeEnd).
	TArray<FKartFxEmit> Boom;
	ExplosionBurst(FVector::ZeroVector, 6.0, Boom);
	double Reach = 0.0;
	for (const FKartFxEmit& E : Boom)
	{
		if (E.Field == EField::Ring)
		{
			Reach = FMath::Max(Reach, E.Size.Y * E.SizeEnd * 0.5);
		}
	}
	TestTrue(TEXT("ring reaches the 6 m radius"), FMath::IsNearlyEqual(Reach, 6.0, 0.01));
	TestTrue(TEXT("fire puffs and debris"), Count(Boom, EField::Smoke) > 20 && Count(Boom, EField::Sparks) > 20);

	TArray<FKartFxEmit> Party;
	ConfettiBurst(FVector::ZeroVector, FVector::ForwardVector, Party);
	TestTrue(TEXT("confetti"), Count(Party, EField::Confetti) >= 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFxItemsTest, "MovecadeKart.Fx.ItemMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartFxItemsTest::RunTest(const FString& Parameters)
{
	// Every item has its own colour, and every modelled mesh builds with the material slots the presenter paints.
	TSet<uint32> Colours;
	for (int32 i = 1; i < static_cast<int32>(EKartItem::Count); ++i)
	{
		const EKartItem Item = static_cast<EKartItem>(i);
		if (Item != EKartItem::TripleNitro)
		{
			Colours.Add(ItemColors(Item).Color.ToFColor(true).DWColor());
		}
	}
	TestEqual(TEXT("distinct item colours"), Colours.Num(), static_cast<int32>(EKartItem::Count) - 2);

	struct FCase
	{
		const TCHAR* Name;
		UStaticMesh* Mesh;
		TArray<FName> Slots;
	};
	UObject* Outer = GetTransientPackage();
	const TArray<FCase> Cases = {
		{ TEXT("shell"), BuildShellMesh(Outer), { TEXT("Dome"), TEXT("Rim"), TEXT("Belly") } },
		{ TEXT("sky shell"), BuildWingedShellMesh(Outer), { TEXT("Dome"), TEXT("Wings") } },
		{ TEXT("banana"), BuildBananaMesh(Outer), { TEXT("Peel"), TEXT("Tips") } },
		{ TEXT("bomb"), BuildBombMesh(Outer), { TEXT("Body"), TEXT("Cap"), TEXT("Fuse") } },
		{ TEXT("nitro"), BuildNitroMesh(Outer), { TEXT("Body"), TEXT("Band"), TEXT("Metal") } },
		{ TEXT("triple nitro"), BuildTripleNitroMesh(Outer), { TEXT("Body"), TEXT("Band"), TEXT("Metal") } },
		{ TEXT("prism"), BuildPrismMesh(Outer), { TEXT("Crystal") } },
		{ TEXT("lightning"), BuildLightningMesh(Outer), { TEXT("Bolt"), TEXT("Edge") } },
		{ TEXT("ink"), BuildInkMesh(Outer), { TEXT("Ink"), TEXT("Eyes"), TEXT("Pupils") } },
		{ TEXT("rocket"), BuildRocketMesh(Outer), { TEXT("Body"), TEXT("Band"), TEXT("Fins"), TEXT("Exhaust") } },
		{ TEXT("box labels"), BuildBoxLabelsMesh(Outer), { TEXT("Label") } },
		{ TEXT("strike"), BuildStrikeMesh(Outer, 7, 1000.0), { TEXT("Bolt") } },
	};
	for (const FCase& C : Cases)
	{
		if (!TestNotNull(C.Name, C.Mesh))
		{
			continue;
		}
		const FBox Bounds = C.Mesh->GetBoundingBox();
		TestTrue(FString::Printf(TEXT("%s has geometry"), C.Name), Bounds.IsValid && Bounds.GetSize().GetMin() > 1.0);
		for (const FName& Slot : C.Slots)
		{
			bool bFound = false;
			for (const FStaticMaterial& M : C.Mesh->GetStaticMaterials())
			{
				bFound |= M.MaterialSlotName == Slot;
			}
			TestTrue(FString::Printf(TEXT("%s has slot %s"), C.Name, *Slot.ToString()), bFound);
		}
	}
	// The strike lands where it is aimed: from its height down to the origin.
	const FBox Strike = BuildStrikeMesh(Outer, 3, 1000.0)->GetBoundingBox();
	TestTrue(TEXT("strike spans its height"), Strike.Min.Z < 5.0 && Strike.Max.Z > 990.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFxMeshNamesTest, "MovecadeKart.Fx.MeshNamesUnique",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartFxMeshNamesTest::RunTest(const FString& Parameters)
{
	// Building a mesh twice on one outer must make a second mesh, never reconstruct the first in place (a live
	// mesh rebuilt under a renderer is a use-after-free).
	UObject* Outer = NewObject<UPackage>(nullptr, MakeUniqueObjectName(nullptr, UPackage::StaticClass(), TEXT("/Temp/KartFxMeshNames")), RF_Transient);
	UStaticMesh* First = BuildShellMesh(Outer);
	const FBox Before = First->GetBoundingBox();
	UStaticMesh* Second = BuildShellMesh(Outer);
	TestTrue(TEXT("a new mesh each build"), First != Second);
	TestTrue(TEXT("the first is untouched"), IsValid(First) && First->GetBoundingBox() == Before && First->GetName() != Second->GetName());
	TestTrue(TEXT("the shared rocket is shared"), SharedRocketMesh() != nullptr && SharedRocketMesh() == SharedRocketMesh());
	return true;
}


#endif

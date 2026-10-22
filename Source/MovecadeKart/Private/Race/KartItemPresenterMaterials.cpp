// AKartItemPresenter: the code-built item meshes, their materials, and the lightning bolt pool.
#include "Race/KartItemPresenter.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFx.h"
#include "FX/KartFxMaterials.h"
#include "FX/KartItemMeshes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Race/KartItemPresenterTuning.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;

namespace
{
	const TCHAR* const PrismMaterialPath = TEXT("/Game/FX/Materials/M_ItemPrism.M_ItemPrism");
	const TCHAR* const BoxGlassMaterialPath = TEXT("/Game/FX/Materials/M_ItemBoxGlass.M_ItemBoxGlass");
	const TCHAR* const BoxLabelMaterialPath = TEXT("/Game/FX/Materials/M_ItemBoxLabel.M_ItemBoxLabel");
	const TCHAR* const FireballMaterialPath = TEXT("/Game/FX/Materials/M_FX_Fireball.M_FX_Fireball");
	const TCHAR* const BoltMaterialPath = TEXT("/Game/FX/Materials/M_FX_Bolt.M_FX_Bolt");

	/** Each bolt in the pool gets its own jagged shape. */
	constexpr int32 StrikeSeedBase = 101;
	constexpr int32 StrikeSeedStep = 37;

	/** Assigns a material to a named slot of the mesh, if both exist. */
	void SetSlot(UInstancedStaticMeshComponent* Component, FName Slot, UMaterialInterface* Material)
	{
		const int32 Index = Component->GetMaterialIndex(Slot);
		if (Material && Index != INDEX_NONE)
		{
			Component->SetMaterial(Index, Material);
		}
	}

	/** Item paint with the presenter's default clear coat. */
	UMaterialInstanceDynamic* ItemPaint(UObject* Outer, const FLinearColor& Base, const FLinearColor& Emissive, float EmissiveStrength, float Metallic, float Roughness,
		float ClearCoat = 0.6f)
	{
		return MakeItemMaterial(Outer, Base, Emissive, EmissiveStrength, Metallic, Roughness, ClearCoat);
	}

	UMaterialInstanceDynamic* Glow(UObject* Outer, const FLinearColor& Color, float Intensity, float Power, float Core)
	{
		UMaterialInstanceDynamic* M = MakeGlowMaterial(Outer, Power, Core);
		if (M)
		{
			M->SetVectorParameterValue(TEXT("Color"), Color);
			M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		}
		return M;
	}
}

void AKartItemPresenter::SetupMaterials()
{
	if (bMaterialsBuilt)
	{
		return;
	}
	bMaterialsBuilt = true;
	SetupItemMaterials();
	SetupBoxMaterials();
	if (UMaterialInterface* Fire = LoadObject<UMaterialInterface>(nullptr, FireballMaterialPath))
	{
		Blasts->SetMaterial(0, Fire);
	}
	BuildStrikes();
}

void AKartItemPresenter::SetupItemMaterials()
{
	auto Mesh = [this](EKartItem Item, UStaticMesh* Built)
	{
		UInstancedStaticMeshComponent* C = MeshesFor(Item);
		C->SetStaticMesh(Built);
		return C;
	};
	UStaticMesh* Shell = BuildShellMesh(this);
	Mesh(EKartItem::Shot, Shell);
	Mesh(EKartItem::Seeker, Shell);
	Mesh(EKartItem::SkySeeker, BuildWingedShellMesh(this));
	Mesh(EKartItem::Banana, BuildBananaMesh(this));
	Mesh(EKartItem::Bomb, BuildBombMesh(this));
	Mesh(EKartItem::Nitro, BuildNitroMesh(this));
	Mesh(EKartItem::TripleNitro, BuildTripleNitroMesh(this));
	Mesh(EKartItem::Prism, BuildPrismMesh(this));
	Mesh(EKartItem::Lightning, BuildLightningMesh(this));
	Mesh(EKartItem::Ink, BuildInkMesh(this));
	Mesh(EKartItem::Rocket, SharedRocketMesh());

	// Shells: clear-coated dome in the item colour, cream rim, belly in the inner colour (emissive 0.32 on the web).
	const FLinearColor Cream = Hex(TEXT("f4efe2"));
	for (EKartItem Item : { EKartItem::Shot, EKartItem::Seeker, EKartItem::SkySeeker })
	{
		const FItemColors C = ItemColors(Item);
		UInstancedStaticMeshComponent* M = MeshesFor(Item);
		SetSlot(M, TEXT("Dome"), ItemPaint(this, C.Color, C.Emissive, 0.6f, 0.3f, 0.32f, 1.0f));
		SetSlot(M, TEXT("Rim"), ItemPaint(this, Cream, Cream, 0.05f, 0.1f, 0.35f, 0.6f));
		SetSlot(M, TEXT("Belly"), ItemPaint(this, C.Inner, C.Inner, 0.15f, 0.08f, 0.56f, 0.2f));
		SetSlot(M, TEXT("Wings"), ItemPaint(this, FLinearColor(0.95f, 0.96f, 1.f), FLinearColor(0.6f, 0.7f, 1.f), 0.25f, 0.1f, 0.3f, 0.8f));
	}
	{
		const FItemColors C = ItemColors(EKartItem::Banana);
		SetSlot(MeshesFor(EKartItem::Banana), TEXT("Peel"), ItemPaint(this, C.Color, C.Emissive, 0.32f, 0.05f, 0.45f, 0.5f));
		SetSlot(MeshesFor(EKartItem::Banana), TEXT("Tips"), ItemPaint(this, C.Inner, C.Inner * 0.3f, 0.1f, 0.0f, 0.7f, 0.0f));
	}
	{
		const FItemColors C = ItemColors(EKartItem::Bomb);
		UInstancedStaticMeshComponent* M = MeshesFor(EKartItem::Bomb);
		SetSlot(M, TEXT("Body"), ItemPaint(this, C.Color, C.Emissive, 0.32f, 0.3f, 0.3f, 1.0f));
		SetSlot(M, TEXT("Cap"), ItemPaint(this, Hex(TEXT("8c8f96")), C.Inner, 0.0f, 0.9f, 0.3f, 0.3f));
		SetSlot(M, TEXT("Fuse"), ItemPaint(this, Hex(TEXT("d8c8a0")), Hex(TEXT("ff8a3a")), 0.2f, 0.0f, 0.8f, 0.0f));
	}
	for (EKartItem Item : { EKartItem::Nitro, EKartItem::TripleNitro })
	{
		const FItemColors C = ItemColors(Item);
		UInstancedStaticMeshComponent* M = MeshesFor(Item);
		SetSlot(M, TEXT("Body"), ItemPaint(this, C.Color, C.Emissive, 0.25f, 0.15f, 0.25f, 1.0f));
		SetSlot(M, TEXT("Band"), ItemPaint(this, C.Inner, C.Inner, 0.08f, 0.0f, 0.4f, 0.5f));
		SetSlot(M, TEXT("Metal"), ItemPaint(this, Hex(TEXT("c9ccd2")), Hex(TEXT("000000")), 0.0f, 1.0f, 0.18f, 0.2f));
	}
	if (UMaterialInterface* Prism = LoadObject<UMaterialInterface>(nullptr, PrismMaterialPath))
	{
		MeshesFor(EKartItem::Prism)->SetMaterial(0, Prism);
	}
	{
		const FItemColors C = ItemColors(EKartItem::Lightning);
		SetSlot(MeshesFor(EKartItem::Lightning), TEXT("Bolt"), ItemPaint(this, C.Color, C.Color, 1.2f, 0.1f, 0.3f, 1.0f));
		SetSlot(MeshesFor(EKartItem::Lightning), TEXT("Edge"), ItemPaint(this, Hex(TEXT("f08a00")), Hex(TEXT("ff9a1a")), 0.6f, 0.2f, 0.35f, 0.6f));
	}
	{
		const FItemColors C = ItemColors(EKartItem::Ink);
		UInstancedStaticMeshComponent* M = MeshesFor(EKartItem::Ink);
		SetSlot(M, TEXT("Ink"), ItemPaint(this, C.Color, C.Inner, 0.35f, 0.0f, 0.12f, 1.0f));
		SetSlot(M, TEXT("Eyes"), ItemPaint(this, FLinearColor::White, FLinearColor::White, 0.35f, 0.0f, 0.3f, 0.5f));
		SetSlot(M, TEXT("Pupils"), ItemPaint(this, Hex(TEXT("0a0a10")), FLinearColor::Black, 0.0f, 0.0f, 0.1f, 1.0f));
	}
	{
		// The toy rocket in the pop-up and the showcase (each kart's own rocket shell is UKartFxComponent's).
		UInstancedStaticMeshComponent* M = MeshesFor(EKartItem::Rocket);
		SetSlot(M, TEXT("Exhaust"), ItemPaint(this, Hex(TEXT("2a2a2e")), Hex(TEXT("ff6a2a")), 2.5f, 0.6f, 0.4f));
		SetSlot(M, TEXT("Body"), ItemPaint(this, Hex(TEXT("f2f4f8")), Hex(TEXT("c8d4ff")), 0.15f, 0.2f, 0.3f));
		SetSlot(M, TEXT("Band"), ItemPaint(this, Hex(TEXT("ff6a13")), Hex(TEXT("ff4a00")), 0.3f, 0.2f, 0.35f));
		SetSlot(M, TEXT("Fins"), ItemPaint(this, Hex(TEXT("21c7c0")), Hex(TEXT("0a5a58")), 0.2f, 0.4f, 0.35f));
	}
	Glows->SetMaterial(0, Glow(this, ItemColors(EKartItem::SkySeeker).Emissive * 2.f + FLinearColor(0.f, 0.05f, 0.3f), 3.0f, 1.6f, 0.08f));
	BombGlows->SetMaterial(0, Glow(this, ItemColors(EKartItem::Bomb).Emissive, 2.5f, 1.4f, 0.12f));
}

void AKartItemPresenter::SetupBoxMaterials()
{
	BoxCores->SetStaticMesh(BuildChamferCubeMesh(this));
	BoxLabels->SetStaticMesh(BuildBoxLabelsMesh(this));
	// Prize orb: violet glass with a cyan rim, an iridescent magenta gem inside, and a soft cyan halo.
	if (UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr, BoxGlassMaterialPath))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Glass, this);
		M->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("7a5cff")) * 0.6f);
		M->SetVectorParameterValue(TEXT("EdgeColor"), Hex(TEXT("5ff0ff")));
		Boxes->SetMaterial(0, M);
	}
	BoxCores->SetMaterial(0, ItemPaint(this, Hex(TEXT("ff5fd2")), Hex(TEXT("b45cff")), 0.9f, 0.8f, 0.15f, 1.0f));
	if (UMaterialInterface* Label = LoadObject<UMaterialInterface>(nullptr, BoxLabelMaterialPath))
	{
		BoxLabels->SetMaterial(0, Label);
	}
	BoxHalos->SetMaterial(0, Glow(this, Hex(TEXT("5ff0ff")), 0.9f, 2.6f, 0.0f));
	// The "?" labels belonged to the web's cube; the orb doesn't wear them.
	BoxLabels->SetVisibility(false);
}

void AKartItemPresenter::BuildStrikes()
{
	// Lightning strikes: a few different bolts, reused round robin.
	if (StrikeMeshes.Num() > 0)
	{
		return;
	}
	UMaterialInterface* Bolt = LoadObject<UMaterialInterface>(nullptr, BoltMaterialPath);
	for (int32 i = 0; i < ItemPresenter::StrikePool; ++i)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Strike%d"), i));
		C->SetStaticMesh(BuildStrikeMesh(this, StrikeSeedBase + i * StrikeSeedStep, ItemPresenter::StrikeHeight));
		C->SetupAttachment(RootComponent);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		C->RegisterComponent();
		UMaterialInstanceDynamic* M = Bolt ? UMaterialInstanceDynamic::Create(Bolt, this) : nullptr;
		if (M)
		{
			C->SetMaterial(0, M);
		}
		StrikeMeshes.Add(C);
		StrikeMaterials.Add(M);
		Strikes.Add(FStrike());
	}
}

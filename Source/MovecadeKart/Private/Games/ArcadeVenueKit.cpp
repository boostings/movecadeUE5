#include "Games/ArcadeVenueKit.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

void FArcadeInstanceGroups::Init(AActor* InOwner, const TCHAR* InNamePrefix, FName InTag, EComponentMobility::Type InMobility)
{
	check(InOwner);
	Owner = InOwner;
	NamePrefix = InNamePrefix ? InNamePrefix : TEXT("VenueGroup");
	Tag = InTag;
	Mobility = InMobility;
}

UInstancedStaticMeshComponent* FArcadeInstanceGroups::Find(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCastShadow,
	USceneComponent* Parent, UMaterialInterface* Material2)
{
	AActor* Actor = Owner.Get();
	if (!ensureMsgf(Actor, TEXT("FArcadeInstanceGroups::Find before Init (or after the owner died)")))
	{
		return nullptr;
	}
	const FArcadeInstanceGroupKey Key{ Mesh, Material, Material2, Parent, bCastShadow };
	if (const int32* Index = IndexByKey.Find(Key); Index && Components.IsValidIndex(*Index) && Components[*Index])
	{
		return Components[*Index];
	}
	UInstancedStaticMeshComponent* Group = NewObject<UInstancedStaticMeshComponent>(Actor,
		MakeUniqueObjectName(Actor, UInstancedStaticMeshComponent::StaticClass(), FName(*NamePrefix)));
	// Mobility first: a static child under a movable parent would abort the attach.
	Group->SetMobility(Mobility);
	Group->SetStaticMesh(Mesh);
	Group->SetMaterial(0, Material);
	if (Material2)
	{
		Group->SetMaterial(1, Material2);
	}
	Group->SetupAttachment(Parent ? Parent : Actor->GetRootComponent());
	Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Group->SetCastShadow(bCastShadow);
	if (!Tag.IsNone())
	{
		Group->ComponentTags.Add(Tag);
	}
	Actor->AddInstanceComponent(Group);
	Group->RegisterComponent();
	IndexByKey.Add(Key, Components.Add(Group));
	return Group;
}

void FArcadeInstanceGroups::Reset()
{
	Components.Reset();
	IndexByKey.Reset();
}

UMaterialInterface* FArcadeMaterialCache::FindOrAdd(FName Look, TFunctionRef<UMaterialInterface*()> Make)
{
	if (const TObjectPtr<UMaterialInterface>* Found = Materials.Find(Look))
	{
		return *Found;
	}
	UMaterialInterface* Material = Make();
	Materials.Add(Look, Material);
	return Material;
}

UMaterialInterface* FArcadeMaterialCache::Find(FName Look) const
{
	const TObjectPtr<UMaterialInterface>* Found = Materials.Find(Look);
	return Found ? Found->Get() : nullptr;
}

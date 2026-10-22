#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/ObjectKey.h"
#include "ArcadeVenueKit.generated.h"

class AActor;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;

/**
 * Helpers for the party games' code-built venues (ABowlingAlley, ADartsPub, AStackerRooftop, ...), which today each
 * carry their own copy: an instanced-mesh group per (mesh, materials, shadow, parent), so a venue of a few thousand
 * boxes draws in a few dozen batches. Hold it as a UPROPERTY member of the venue actor and Reset() it when the venue
 * rebuilds. The per-build material cache is Art's FKartMaterialCache (Art/KartMaterialCache.h).
 */

/** What makes two instances batchable: the same mesh, materials, shadow casting and parent. */
struct FArcadeInstanceGroupKey
{
	TObjectKey<UStaticMesh> Mesh;
	TObjectKey<UMaterialInterface> Material;
	TObjectKey<UMaterialInterface> Material2;
	TObjectKey<USceneComponent> Parent;
	bool bCastShadow = true;

	bool operator==(const FArcadeInstanceGroupKey& Other) const
	{
		return Mesh == Other.Mesh && Material == Other.Material && Material2 == Other.Material2 && Parent == Other.Parent
			&& bCastShadow == Other.bCastShadow;
	}

	friend uint32 GetTypeHash(const FArcadeInstanceGroupKey& Key)
	{
		uint32 Hash = HashCombine(GetTypeHash(Key.Mesh), GetTypeHash(Key.Material));
		Hash = HashCombine(Hash, GetTypeHash(Key.Material2));
		Hash = HashCombine(Hash, GetTypeHash(Key.Parent));
		return HashCombine(Hash, Key.bCastShadow ? 1u : 0u);
	}
};

/**
 * The venue's instanced-mesh groups, one UInstancedStaticMeshComponent per FArcadeInstanceGroupKey, created on first
 * use with collision off, attached to the parent (or the owner's root) and tagged so the venue can find its parts.
 *
 *   Groups.Init(this, TEXT("PubGroup"), PubTag, EComponentMobility::Static);
 *   Groups.Find(Cube, Wood, true)->AddInstance(Transform, true);
 */
USTRUCT()
struct MOVECADEKART_API FArcadeInstanceGroups
{
	GENERATED_BODY()

	/** Owner gets the components; NamePrefix names them uniquely; Tag goes on each; Mobility applies to all. */
	void Init(AActor* InOwner, const TCHAR* InNamePrefix, FName InTag, EComponentMobility::Type InMobility = EComponentMobility::Static);

	/** The group for this look, created and registered on first use. Material2 fills slot 1 when set. */
	UInstancedStaticMeshComponent* Find(UStaticMesh* Mesh, UMaterialInterface* Material, bool bCastShadow = true,
		USceneComponent* Parent = nullptr, UMaterialInterface* Material2 = nullptr);

	int32 Num() const { return Components.Num(); }
	/** Forgets the groups (the venue destroys its components when it rebuilds). */
	void Reset();

private:
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;

	TMap<FArcadeInstanceGroupKey, int32> IndexByKey;
	TWeakObjectPtr<AActor> Owner;
	FString NamePrefix = TEXT("VenueGroup");
	FName Tag;
	EComponentMobility::Type Mobility = EComponentMobility::Static;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KartItemPresenter.generated.h"

class AKartRaceManager;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

/**
 * Draws the item world (spec 02 §7): item boxes (hidden once every kart has collected them), projectiles,
 * explosion blasts, and each kart's item visuals (spin-out, shrink, explosion pop, Prism aura, Rocket),
 * and drives camera shake from hits, explosions, walls and bumps (kartFxBus.ts profiles).
 * Graybox meshes for now; the art pass swaps in proper meshes and Niagara.
 */
UCLASS()
class MOVECADEKART_API AKartItemPresenter : public AActor
{
	GENERATED_BODY()

public:
	AKartItemPresenter();

	void Bind(AKartRaceManager* InManager);
	virtual void Tick(float DeltaSeconds) override;

private:
	UInstancedStaticMeshComponent* MakeMeshes(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color);
	void HandleHaptic(int32 Seat, float Intensity, FName Type, bool bSustained);

	TWeakObjectPtr<AKartRaceManager> Manager;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Boxes;

	/** One instanced mesh per item type (indexed by MovecadeKart::EKartItem). */
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Projectiles;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Blasts;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	struct FBlast
	{
		FVector Center;
		double Radius;
		double Start;
	};
	TArray<FBlast> ActiveBlasts;
	TArray<FVector> BoxPositions;
};

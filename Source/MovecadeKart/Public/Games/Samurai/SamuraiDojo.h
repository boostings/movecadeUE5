#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SamuraiDojo.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Samurai Slice's dojo (movecade-v2 samurai/{DojoEnvironment,SamuraiSky,samuraiStage}.tsx, Docs/Port/08-samurai.md
 * §5), modelled in code: an open-air dojo at sunset. A tatami per lane on a cedar floor, the torii framing the
 * lanes, the shoji wall with its posts and beam (open in the middle onto the garden), paper lanterns swaying
 * overhead, bamboo at the sides, stone lanterns, stepping stones and raked gravel, pines and layered ridges fading
 * into the haze, under the painted sunset sky with a low warm sun and a rose rim light; petals drift through.
 *
 * Web world units map 1:1 to metres (UE cm via MovecadeKart::Coords). The lanes sit around x 0, 3.6 m apart; the
 * swordsmen stand at z 1.15 facing -z; the targets fly at z .55.
 */
UCLASS()
class MOVECADEKART_API ASamuraiDojo : public AActor
{
	GENERATED_BODY()

public:
	ASamuraiDojo();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Rebuilds the lane-dependent parts (tatami, torii, stage width) for LaneCount lanes. */
	void SetLanes(int32 LaneCount);
	int32 GetLanes() const { return Lanes; }

	/** Web world (x, y, z) to UE cm. */
	static FVector ToWorld(const FVector& Web);
	double StageWidth() const;

private:
	void Build();
	void Clear();
	void BuildLook();
	void BuildGround();
	void BuildStage();
	void BuildTorii();
	void BuildWall();
	void BuildGarden();
	void BuildDistance();
	void BuildLanterns();

	void Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad = 0.0, bool bShadows = true);
	void Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows = true);
	UInstancedStaticMeshComponent* Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows);
	UStaticMeshComponent* Part(UStaticMesh* Mesh, const FTransform& World, bool bShadows = true);

	UPROPERTY(EditAnywhere, Category = "Dojo")
	int32 Lanes = 1;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups;

	/** The swaying paper lanterns, by their hanging point. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Lanterns;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LanternMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RidgeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PineMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> StoneMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> KasagiMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Sphere;

	UPROPERTY(VisibleAnywhere, Category = "Dojo")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Dojo")
	TObjectPtr<UDirectionalLightComponent> Rim;

	UPROPERTY(VisibleAnywhere, Category = "Dojo")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Dojo")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	UPROPERTY(VisibleAnywhere, Category = "Dojo")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(VisibleAnywhere, Category = "Dojo")
	TObjectPtr<UPostProcessComponent> Grade;

	double Clock = 0.0;
	double PetalCarry = 0.0;
	FRandomStream PetalRng;
};

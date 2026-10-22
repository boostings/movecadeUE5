#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Art/KartMaterialCache.h"
#include "FX/KartFx.h"
#include "Games/ArcadeVenueKit.h"
#include "SamuraiDojo.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
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
 * Placed in L_Samurai (or spawned by ASamuraiGameMode); ASamuraiGame sets its lane count and drives its mood. Web
 * world units map 1:1 to metres (UE cm via MovecadeKart::Coords). The lanes sit around x 0, 3.6 m apart; the
 * swordsmen stand at z 1.15 facing -z; the targets fly at z .55. The set's parts are placed in SamuraiDojoSet.cpp,
 * the props' meshes are modelled in SamuraiDojoMeshes.cpp.
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

	/**
	 * The round's screen treatment over the grade: Focus 0..1 (gold, saturated, vignette), a Flash 0..1 in a colour
	 * (a perfect cut, a bomb) and Fringe 0..1 (chromatic fringing on impacts).
	 */
	void SetMood(float Focus, float Flash, const FLinearColor& FlashColor, float Fringe);
	/** A gust of cherry petals bursting from a point (web metres): Focus, big combos, the winner. */
	void PetalBurst(const FVector& Web, int32 Count);

	/** Web world (x, y, z) to UE cm. */
	static FVector ToWorld(const FVector& Web);
	double StageWidth() const;

private:
	/** The tag on every component the dojo builds (Clear removes them by it). */
	static FName DojoTag();
	void Build();
	void BuildMeshes();
	void Clear();
	void BuildLook();
	void BuildSky(const FLinearColor& Top, const FLinearColor& Mid, const FLinearColor& Horizon);
	void BuildFog(const FLinearColor& Mid, const FLinearColor& Horizon);
	void ApplyGrade();
	void BuildGround();
	void BuildStage();
	void BuildTorii();
	void BuildWall();
	void BuildGarden();
	void BuildStoneLantern(const FVector& Base);
	void BuildBamboo(double Side);
	void BuildDistance();
	void BuildLanterns();
	void DriftPetals(float DeltaSeconds);
	MovecadeKart::Fx::FKartFxEmit Petal(const FVector& Web, int32 Count, double Speed);

	/** An instanced box: centre C and size S in web metres (x across, y up, z toward the camera), turned YawRad. */
	void Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad = 0.0, bool bShadows = true);
	/** An instanced upright cylinder standing on Base. */
	void Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows = true);
	/** A one-off static mesh part at a UE transform. */
	UStaticMeshComponent* Part(UStaticMesh* Mesh, const FTransform& World, bool bShadows = true);
	/** Creates, tags and registers one of the dojo's own components (removed again by Clear). */
	template <class TComponent>
	TComponent* AddDojoComponent(const TCHAR* Name, USceneComponent* Parent, EComponentMobility::Type Mobility);

	UPROPERTY(EditAnywhere, Category = "Dojo")
	int32 Lanes = 1;

	/** Instanced boxes and posts, one component per (mesh, material, shadow). */
	UPROPERTY(Transient)
	FArcadeInstanceGroups Groups;

	/** Materials, made once per distinct look and kept across rebuilds (SetLanes rebuilds the geometry only). */
	UPROPERTY(Transient)
	FKartMaterialCache Materials;

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
	/** In a game world the first build waits for SetLanes (or the first tick), so the dojo isn't built twice. */
	bool bBuildPending = false;
	bool bBuilt = false;
	bool bLookBuilt = false;
	float MoodFocus = 0.f;
	float MoodFlash = 0.f;
	float MoodFringe = 0.f;
	FLinearColor MoodFlashColor = FLinearColor::White;
	/** Fractional petals owed by the drift (it emits whole ones). */
	double PetalCarry = 0.0;
	FRandomStream PetalRng;
	/** The drift's emits, reused every frame. */
	TArray<MovecadeKart::Fx::FKartFxEmit> PetalScratch;
};

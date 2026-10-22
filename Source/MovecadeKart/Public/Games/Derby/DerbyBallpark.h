#pragma once

#include "CoreMinimal.h"
#include "Art/KartMaterialCache.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeVenueKit.h"
#include "DerbyBallpark.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
struct FDerbyStandArc;

/**
 * Home Run Derby's night ballpark, modelled in code (Docs/Port/10-derby.md §5). UE space with home plate at the
 * origin, centre field down +X and right field toward +Y, metres in the helpers. A mown outfield inside a padded
 * wall (330 ft down the lines, 400 to centre) with its warning track, distance markers and yellow foul poles; the
 * infield diamond, mound, bases and chalk; packed bleachers beyond the wall and a grandstand round home, full of
 * fans who bounce when the crowd gets going; six light towers, a centre-field scoreboard, a night sky with stars.
 *
 * Placed in L_Derby (or spawned by ADerbyGameMode); it rebuilds itself in OnConstruction. ADerbyGame drives it
 * through Roar, SetBoard and FireworkSpots. The build is split by part: DerbyBallpark.cpp (the actor, the light and
 * the sky, the scoreboard, the crowd's bounce), DerbyBallparkField.cpp (the field, the wall, the light towers),
 * DerbyBallparkStands.cpp (the stands and the fans).
 */
UCLASS()
class MOVECADEKART_API ADerbyBallpark : public AActor
{
	GENERATED_BODY()

public:
	ADerbyBallpark();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Metres (X centre field, Y right field, Z up) to UE cm. */
	static FVector M(const FVector& Metres) { return Metres * 100.0; }
	/** A point on the ground at Distance metres along a spray angle (degrees, + right field), in cm. */
	static FVector Ground(double SprayDeg, double DistanceM, double HeightM = 0.0);

	/** The pitcher's rubber, metres from the plate (60 ft 6 in). */
	static constexpr double MoundX = 18.44;
	/** The batter's feet (right-handed box, third-base side), cm. */
	static FVector BatterSpot() { return M(FVector(0.0, -0.95, 0.0)); }
	static FVector PlateFront() { return M(FVector(0.3, 0.0, 0.0)); }

	/** 0 quiet .. 1 on its feet: how hard the fans bounce; it eases back on its own. */
	void Roar(float Level);
	/** The centre-field scoreboard's three lines; Accent colours the second line and the screen's bars. */
	void SetBoard(const FString& Line1, const FString& Line2, const FString& Line3, const FLinearColor& Accent);
	/** Where fireworks go up: over the bleachers and the scoreboard (cm). */
	void FireworkSpots(TArray<FVector>& Out) const;

private:
	void Build();
	void Clear();
	void BuildLook();
	void BuildSky();
	void BuildBoard();
	// DerbyBallparkField.cpp
	void BuildField();
	void BuildOutfieldGrass(UMaterialInterface* Stripe);
	void BuildDiamond(UMaterialInterface* Dirt, UMaterialInterface* Grass, UMaterialInterface* Rubber, UMaterialInterface* Chalk);
	void BuildWall();
	void BuildWallMarkings();
	void BuildTowers();
	// DerbyBallparkStands.cpp
	void BuildStands();
	/** One step of an arc of stands at MidDeg, its first row BaseM from the plate; AddFan seats a fan (feet, yaw). */
	void BuildStandArc(const FDerbyStandArc& Arc, double MidDeg, double BaseM, UMaterialInterface* Riser, UMaterialInterface* Seat,
		TFunctionRef<void(const FVector&, double)> AddFan);
	/** That step's back wall behind its last row: the facade, a roof if the arc has one, the neon trim. */
	void BuildStandBack(const FDerbyStandArc& Arc, double MidDeg, double BaseM, UMaterialInterface* Facade, UMaterialInterface* Trim);
	void BuildLineStands(UMaterialInterface* Riser, UMaterialInterface* Facade, TFunctionRef<void(const FVector&, double)> AddFan);

	/** Logs once per build when the engine's basic shapes (every instanced part) failed to load. */
	void WarnIfShapesMissing() const;
	/** Groups.Find, counting parts whose look came back null (Art had no master for it) for one warning per build. */
	UInstancedStaticMeshComponent* FindGroup(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows, USceneComponent* Parent = nullptr,
		UMaterialInterface* Material2 = nullptr);
	/** A box (centre and size in metres, yawed about Z) and a disc (a flat cylinder standing on its centre) as instances. */
	void Box(UMaterialInterface* Material, const FVector& CentreM, const FVector& SizeM, double YawDeg = 0.0, bool bShadows = false, USceneComponent* Parent = nullptr);
	void Disc(UMaterialInterface* Material, const FVector& CentreM, double RadiusM, double HeightM, bool bShadows = false);
	UTextRenderComponent* Text(const FString& String, const FVector& AtM, double YawDeg, float WorldSizeCm, const FLinearColor& Color);
	/** A tagged scene component under the root (a crowd section's parent). */
	USceneComponent* Pivot(const TCHAR* Name);
	/** A tagged, collision-free part of the park, registered on this actor. */
	void AddPart(USceneComponent* Part);

	UPROPERTY(Transient)
	FArcadeInstanceGroups Groups;

	UPROPERTY(Transient)
	FKartMaterialCache Materials;

	/** The fans, a parent per section that bounces on its own beat. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Crowd;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> BoardLines;

	/** The scoreboard's accent bars: its own instance (SetBoard recolours it), never shared through Materials. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BoardAccent;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Sphere;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> FanMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CheerMesh;

	UPROPERTY(VisibleAnywhere, Category = "Ballpark")
	TObjectPtr<UDirectionalLightComponent> Floods;

	UPROPERTY(VisibleAnywhere, Category = "Ballpark")
	TObjectPtr<UDirectionalLightComponent> Moon;

	UPROPERTY(VisibleAnywhere, Category = "Ballpark")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Ballpark")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	UPROPERTY(VisibleAnywhere, Category = "Ballpark")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(VisibleAnywhere, Category = "Ballpark")
	TObjectPtr<UPostProcessComponent> Grade;

	double Clock = 0.0;
	float Excitement = 0.15f;
	/** Parts the current build made without a material (FindGroup). */
	int32 PartsWithoutMaterial = 0;
};

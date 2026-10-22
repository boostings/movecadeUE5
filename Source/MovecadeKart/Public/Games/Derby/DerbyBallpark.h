#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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

/**
 * Home Run Derby's night ballpark, modelled in code (Docs/Port/10-derby.md §5). UE space with home plate at the
 * origin, centre field down +X and right field toward +Y, metres in the helpers. A mown outfield inside a padded
 * wall (330 ft down the lines, 400 to centre) with its warning track, distance markers and yellow foul poles; the
 * infield diamond, mound, bases and chalk; packed bleachers beyond the wall and a grandstand round home, full of
 * fans who bounce when the crowd gets going; six light towers, a centre-field scoreboard, a night sky with stars.
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
	/** A point on the ground at Distance metres along a spray angle (degrees, + right field). */
	static FVector Ground(double SprayDeg, double DistanceM, double HeightM = 0.0);

	static constexpr double MoundX = 18.44;
	/** The batter's feet (right-handed box, third-base side) and the pitcher's release. */
	static FVector BatterSpot() { return M(FVector(0.0, -0.95, 0.0)); }
	static FVector PlateFront() { return M(FVector(0.3, 0.0, 0.0)); }

	/** 0 quiet .. 1 on its feet: how hard the fans bounce; it eases back on its own. */
	void Roar(float Level);
	void SetBoard(const FString& Line1, const FString& Line2, const FString& Line3, const FLinearColor& Accent);
	/** Where fireworks go up: over the bleachers and the scoreboard. */
	void FireworkSpots(TArray<FVector>& Out) const;

private:
	void Build();
	void Clear();
	void BuildLook();
	void BuildField();
	void BuildWall();
	void BuildStands();
	void BuildTowers();
	void BuildBoard();
	void BuildSky();

	void Box(UMaterialInterface* Material, const FVector& CentreM, const FVector& SizeM, double YawDeg = 0.0, bool bShadows = false, USceneComponent* Parent = nullptr);
	void Disc(UMaterialInterface* Material, const FVector& CentreM, double RadiusM, double HeightM, bool bShadows = false);
	UInstancedStaticMeshComponent* Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows, USceneComponent* Parent = nullptr, UMaterialInterface* Material2 = nullptr);
	UTextRenderComponent* Text(const FString& String, const FVector& AtM, double YawDeg, float WorldSizeCm, const FLinearColor& Color);
	USceneComponent* Pivot(const TCHAR* Name);

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups;

	/** The fans, a parent per section that bounces on its own beat. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Crowd;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> BoardLines;

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
};

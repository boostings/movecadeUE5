#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/KartTrack.h"
#include "TrackBuilder.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;

/**
 * Loads Data/Tracks/<TrackId>.track.json, owns the sim's track data, and builds a graybox of it:
 * the Harbor ribbons (road, curbs, walls, beach, water), the sea, start line, boost pads and item
 * boxes, plus a basic sky and sun from the track's look. Built in the editor too (OnConstruction).
 */
UCLASS()
class MOVECADEKART_API ATrackBuilder : public AActor
{
	GENERATED_BODY()

public:
	ATrackBuilder();

	UPROPERTY(EditAnywhere, Category = "Track")
	FString TrackId = TEXT("sunset-harbor");

	/** Sun, sky light, atmosphere and fog, for an otherwise empty level. */
	UPROPERTY(EditAnywhere, Category = "Track")
	bool bAddLighting = true;

	/** Draw the checkpoint radii while playing. */
	UPROPERTY(EditAnywhere, Category = "Track")
	bool bShowCheckpoints = true;

	/** The parsed track, loading it on first use. Null if the file is missing or invalid. */
	TSharedPtr<const MovecadeKart::FTrack> GetTrack();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	static FString TrackPath(const FString& Id);

private:
	bool Load();
	void Build();
	void AddSection(const FString& Name, const FLinearColor& Color, TFunctionRef<void(TArray<FVector>&, TArray<int32>&, TArray<FVector>&)> Emit);

	UPROPERTY(VisibleAnywhere, Category = "Track")
	TObjectPtr<UProceduralMeshComponent> Ground;

	UPROPERTY(VisibleAnywhere, Category = "Track")
	TObjectPtr<UInstancedStaticMeshComponent> ItemBoxes;

	UPROPERTY(VisibleAnywhere, Category = "Track")
	TObjectPtr<UInstancedStaticMeshComponent> BoostPads;

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	TSharedPtr<MovecadeKart::FTrack> Track;
	TSharedPtr<class FJsonObject> Json;
	FString LoadedId;
	TArray<FVector> ItemBoxBase;
	int32 SectionCount = 0;
};

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/KartItems.h"
#include "KartItemPresenter.generated.h"

class AKartPawn;
class AKartRaceManager;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
namespace MovecadeKart::Fx
{
	struct FKartFxEmit;
	struct FKartFxLight;
}

/**
 * Draws the item world (spec 02 §7) and its VFX. AKartRaceManager spawns one and binds it to the race; it only reads
 * the manager's item world and events, and nothing here feeds back into the sim.
 * - Item boxes: a glass prize orb with a spinning gem, a soft halo and drifting twinkles; a pop and a ring when a box
 *   comes back and a burst when one is collected.
 * - Items: a modelled mesh and clear-coat material for each of the eleven (KartItemMeshes). Thrown items fly with a
 *   glowing trail in their colour (seekers shed sparks, the bomb has a lit fuse and a pulsing glow); a used item pops
 *   up over its kart, spins and bursts; an item that runs out vanishes in a puff.
 * - Hits, explosions (fireball lobes, rings, a light), lightning strikes, ink, prism and rocket starts and the finish
 *   (recipes in FX/KartItemFx.h), plus each kart's item-driven pose and camera shake (FKartItemVisual).
 * Per-kart continuous FX (boosts, drift, skids, dust, aura, rocket, screen effects) live in UKartFxComponent.
 *
 * Source files: KartItemPresenter.cpp (setup and the frame), KartItemPresenterMaterials.cpp, KartItemPresenterEvents.cpp,
 * KartItemPresenterWorld.cpp (boxes, projectiles, pop-ups, blasts, strikes, lights) and KartItemShowcase.cpp
 * (-KartFxShowcase: a cosmetic tour of the effects for screenshots).
 */
UCLASS()
class MOVECADEKART_API AKartItemPresenter : public AActor
{
	GENERATED_BODY()

public:
	AKartItemPresenter();

	/** Follows a race (again after a restart): the live boxes replace the track builder's preview ones. */
	void Bind(AKartRaceManager* InManager);
	virtual void Tick(float DeltaSeconds) override;

private:
	/** A used item hovering over its kart before it bursts. */
	struct FPopup
	{
		MovecadeKart::EKartItem Item = MovecadeKart::EKartItem::None;
		int32 Seat = 0;
		double Start = 0.0;
	};

	/** The trail (and spark stream) following one projectile, freed after it has faded. */
	struct FTrail
	{
		TObjectPtr<UNiagaraComponent> Trail;
		TObjectPtr<UNiagaraComponent> Sparks;
		MovecadeKart::EKartItem Item = MovecadeKart::EKartItem::None;
		FVector LastPosition = FVector::ZeroVector;
		double DeadSince = -1.0;
		bool bSeen = false;
	};

	/** One explosion's fireball: three lobes around the centre. */
	struct FBlast
	{
		FVector Center;
		double Radius;
		double Start;
		FVector Lobes[3];
	};

	struct FStrike
	{
		double Start = -1000.0;
		FVector At = FVector::ZeroVector;
	};

	/** A pooled flash light's envelope (Start < 0: the light is free). */
	struct FLightFlash
	{
		double Start = -1.0;
		double Peak = 0.0;
		double Seconds = 0.6;
	};

	/**
	 * This frame's instances for one pooled instanced mesh. The mesh keeps every instance it ever needed; Commit
	 * writes the frame's transforms over the first ones in one batch, folds away (zero scale) the ones shown last
	 * frame but not now, and does nothing at all while nothing is shown.
	 */
	struct FInstanceBatch
	{
		TArray<FTransform> Transforms;
		/** One custom float per instance, for the meshes that have one. */
		TArray<float> Custom;
		int32 Shown = 0;

		void Add(const FTransform& T) { Transforms.Add(T); }
		void Add(const FTransform& T, float Value) { Transforms.Add(T); Custom.Add(Value); }
	};
	static void Commit(UInstancedStaticMeshComponent* Component, FInstanceBatch& Batch);

	// Setup (KartItemPresenter.cpp, KartItemPresenterMaterials.cpp).
	UInstancedStaticMeshComponent* MakeMeshes(const TCHAR* Name, UStaticMesh* Mesh, int32 CustomFloats = 0);
	UMaterialInstanceDynamic* ItemMaterial(const FLinearColor& Base, const FLinearColor& Emissive, float EmissiveStrength, float Metallic, float Roughness, float ClearCoat = 0.6f);
	UMaterialInstanceDynamic* GlowMaterial(const FLinearColor& Color, float Intensity, float Power, float Core);
	void SetupMaterials();
	void SetupItemMaterials();
	void SetupBoxMaterials();
	void BuildStrikes();
	void ResetBoxes();

	// Events (KartItemPresenterEvents.cpp).
	void HandleCue(int32 Seat, FName Cue);
	void HandleEvents(const TArray<MovecadeKart::FKartItemEvent>& Events, double Time);
	void Strike(const FVector& Ground, double Time);

	// The item world, every frame (KartItemPresenterWorld.cpp).
	void UpdateBoxes(double Time, double Now);
	void UpdateItems(double Time);
	void UpdateProjectiles(double Time, TArray<MovecadeKart::Fx::FKartFxEmit>& Out);
	void UpdateTrails(const MovecadeKart::FKartItemProjectile& Projectile, const FVector& TrailAt);
	void RetireTrails(double Time, TArray<MovecadeKart::Fx::FKartFxEmit>& Out);
	void UpdatePopups(double Time, TArray<MovecadeKart::Fx::FKartFxEmit>& Out);
	void UpdateBlasts(double Time);
	void UpdateLights(double Time);
	void UpdateStrikes(double Time);
	void UpdateKartVisuals(double Time, double Now);
	/** A pooled point light that pops up in 0.05 s and fades out over its seconds. */
	void FlashLight(const MovecadeKart::Fx::FKartFxLight& Light, double Time);

	// -KartFxShowcase (KartItemShowcase.cpp).
	void StartShowcase(const AKartRaceManager& Race);
	void UpdateShowcase(double Time);
	void UpdateShowcaseCapture(double Time, float DeltaSeconds);
	void AddShowcaseItems(double Time);

	FVector KartGround(int32 Seat) const;
	AKartPawn* KartAt(int32 Seat) const;
	UInstancedStaticMeshComponent* MeshesFor(MovecadeKart::EKartItem Item) const;
	FInstanceBatch& BatchFor(MovecadeKart::EKartItem Item) { return ItemBatches[static_cast<int32>(Item)]; }

	TWeakObjectPtr<AKartRaceManager> Manager;
	FDelegateHandle CueHandle;
	/** Meshes and materials are built once per presenter, whatever number of Binds. */
	bool bMaterialsBuilt = false;

	/** Per-frame instance batches (pooled ISMs): one per item type, the glows and the fireballs. */
	TArray<FInstanceBatch> ItemBatches;
	FInstanceBatch GlowBatch;
	FInstanceBatch BombGlowBatch;
	FInstanceBatch BlastBatch;
	/** Orb, gem, halo and label instances, in that order. */
	FInstanceBatch BoxBatches[4];

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Boxes;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BoxCores;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BoxLabels;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BoxHalos;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> BoxTwinkles;

	/** One instanced mesh per item type (indexed by MovecadeKart::EKartItem): projectiles, pop-ups, showcase. */
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> ItemMeshes;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Glows;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> BombGlows;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Blasts;

	/** A small pool of flash lights (explosions, hits, the finish), reused oldest first. */
	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> BlastLights;
	TArray<FLightFlash> LightFlashes;

	/** Lightning bolts: a few prebuilt jagged meshes, each shown for a moment where it strikes. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> StrikeMeshes;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> StrikeMaterials;

	TArray<FStrike> Strikes;
	int32 NextStrike = 0;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> TrailComponents;

	TMap<int32, FTrail> Trails;
	TArray<FBlast> ActiveBlasts;
	TArray<FPopup> Popups;
	/** Where hits and explosions happened recently, so a projectile that ends there doesn't also puff out. */
	TArray<TPair<FVector, double>> RecentImpacts;

	TArray<FVector> BoxPositions;
	TArray<bool> BoxWasVisible;
	TArray<double> BoxShownAt;

	/** -KartFxShowcase: "" (all beats), a beat name, or "items". */
	bool bShowcase = false;
	FString ShowcaseBeat;
	double ShowcaseNext = -1.0;
	int32 ShowcaseIndex = 0;
	FVector ShowcaseOrigin = FVector::ZeroVector;
	FVector ShowcaseForward = FVector::ForwardVector;
	/** World times at which the showcase takes a screenshot (just after each beat). */
	TArray<double> ShowcaseShots;
	TArray<float> ShowcaseFrames;
};

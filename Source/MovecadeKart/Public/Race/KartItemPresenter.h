#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/KartItems.h"
#include "KartItemPresenter.generated.h"

class AKartRaceManager;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Draws the item world (spec 02 §7) and its VFX. It only reads the race manager's item world and events; nothing
 * here feeds back into the sim.
 * - Item boxes: a translucent gold glass cube with a spinning chamfered core, "?" glyphs on every face, a soft halo
 *   and drifting twinkles; a pop-and-ring when a box comes back and a gold burst when one is collected.
 * - Items: a modelled mesh and clear-coat material for each of the eleven (KartItemMeshes). Thrown items fly with a
 *   glowing trail in their colour (the sky seeker with sparks too, the bomb with a lit fuse and an accelerating glow
 *   pulse); a used item pops up over its kart, spins and bursts; an item that runs out vanishes in a puff.
 * - Hits: bursts from the item events and kart contacts (kartFxBus.ts profiles), tinted by the item that hit.
 * - The bomb explosion: toon fire puffs, a smoke column, debris, a flash, shock rings to the blast radius,
 *   swelling fireball lobes and a light. Lightning: a bolt strikes each victim and every view flashes.
 * - Ink splashes, prism and rocket starts, finish-line confetti, and the item-driven kart visuals and camera shake.
 * Per-kart continuous FX (boosts, drift, skids, dust, impacts, aura, rocket, screen effects) live in UKartFxComponent.
 *
 * -KartFxShowcase[=beat] runs a cosmetic tour of the effects on seat 0 for screenshots (explosion, lightning,
 * prism, rocket, ink, hit, confetti, box), or with =items floats every item mesh in a row ahead of the grid.
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

	UInstancedStaticMeshComponent* MakeMeshes(const TCHAR* Name, UStaticMesh* Mesh, int32 CustomFloats = 0);
	UMaterialInstanceDynamic* ItemMaterial(const FLinearColor& Base, const FLinearColor& Emissive, float EmissiveStrength, float Metallic, float Roughness, float ClearCoat = 0.6f);
	UMaterialInstanceDynamic* GlowMaterial(const FLinearColor& Color, float Intensity, float Power, float Core);
	void SetupMaterials();
	void HandleCue(int32 Seat, FName Cue);
	void HandleEvents(const TArray<MovecadeKart::FKartItemEvent>& Events, double Time);
	void UpdateBoxes(double Time, double Now);
	void UpdateItems(double Time);
	void UpdateBlasts(double Time);
	void UpdateStrikes(double Time);
	void UpdateShowcase(double Time);
	void Strike(const FVector& Ground, double Time);
	FVector KartGround(int32 Seat) const;
	UInstancedStaticMeshComponent* MeshesFor(MovecadeKart::EKartItem Item) const;

	TWeakObjectPtr<AKartRaceManager> Manager;
	FDelegateHandle CueHandle;
	/** Meshes and materials are built once per presenter, whatever number of Binds. */
	bool bMaterialsBuilt = false;

	/** Per-frame instance batches (pooled ISMs): one per item type, the glows and the fireballs. */
	TArray<FInstanceBatch> ItemBatches;
	FInstanceBatch GlowBatch;
	FInstanceBatch BombGlowBatch;
	FInstanceBatch BlastBatch;
	FInstanceBatch BoxBatches[4];

	// Item boxes.
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

	/** A small pool of explosion flash lights, reused oldest first (start < 0: free). */
	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> BlastLights;
	TArray<double> BlastLightStart;

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

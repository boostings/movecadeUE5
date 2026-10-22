#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DartsPub.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Bullseye Bash's pub (movecade-v2 darts/DartsPub.tsx, DartsScene.tsx, pubLayout.ts; Docs/Port/07-darts.md §2, §6),
 * modelled in code like the tracks: brick behind the board, bottle-green plaster and panelled wainscot, beams, the
 * oche and its brass line, a chalkboard (which chalks the live scores), the bar with bottles and a lit back bar,
 * pendants, and the cabinet with its lamp hood. The board is a real clock board built from its sectors and rings,
 * with the wire spider, the number ring and the brass hoop; it hangs from a pivot that wobbles when darts land.
 *
 * Web world units map 1:1 to metres (UE cm via MovecadeKart::Coords). The board's centre is (0, 0.45, 0) facing
 * +z (the camera), so UE -X; the pub group sits at z -0.75 as on the web.
 */
UCLASS()
class MOVECADEKART_API ADartsPub : public AActor
{
	GENERATED_BODY()

public:
	ADartsPub();

	virtual void OnConstruction(const FTransform& Transform) override;

	static constexpr double BoardRadius = 0.5;
	static constexpr double BoardY = 0.45;
	static constexpr double PubZ = -0.75;
	static constexpr double FloorY = -1.32;
	static constexpr double CeilingY = 5.5;
	/** The thrower's mark (world z) and where they stand. */
	static constexpr double OcheZ = 4.65;
	static constexpr double ThrowerX = -0.85;
	/** The exposure band (EV100) the pub's grade holds the camera to. */
	static constexpr float ExposureMinEV = 7.6f;
	static constexpr float ExposureMaxEV = 8.6f;
	/** Unlit and emissive colours are tuned for an auto-exposed scene; at this exposure they need this much more. */
	static constexpr float GlowGain = 7.f;
	/** The dart mesh's material slots: steel (point, nose, rings), barrel, shaft, flights (the thrower's colour). */
	static constexpr int32 DartSteelSlot = 0;
	static constexpr int32 DartFlightSlot = 3;

	/** Web world (x, y, z) to UE cm. */
	static FVector ToWorld(const FVector& Web);
	/** A point on the board in board radii (+x right, +y up) to web world, Lift metres out from the face. */
	static FVector BoardPoint(double X, double Y, double Lift = 0.0);

	/** The pivot the face, spider, numbers and bounty wedge hang from: the game wobbles it. */
	USceneComponent* GetBoardPivot() const { return BoardPivot; }
	/** The bounty wedge (one sector, oriented by the game). */
	UStaticMeshComponent* GetBountyWedge() const { return BountyWedge; }
	UMaterialInstanceDynamic* GetBountyMaterial() const { return BountyMaterial; }
	UStaticMesh* GetDartMesh() const { return DartMesh; }
	UMaterialInterface* GetDartMaterial(int32 Slot) const { return DartMaterials.IsValidIndex(Slot) ? DartMaterials[Slot].Get() : nullptr; }
	int32 GetDartMaterialCount() const { return DartMaterials.Num(); }
	UStaticMesh* GetRingMesh() const { return RingMesh; }

	/** The wedge's glowing outline material (pulses with the wedge). */
	UMaterialInstanceDynamic* GetBountyEdgeMaterial() const { return BountyEdgeMaterial; }

	/** Chalks "NAME  score" lines on the chalkboard (a line only re-renders when its text changes). */
	void SetChalk(const TArray<FString>& Lines);

private:
	void Build();
	void Clear();
	void BuildMeshes();
	void BuildShell();
	void BuildBar();
	void BuildBoard();
	void BuildLights();
	void BuildProps();

	/** One material per key per Build(): instance groups key on the material, so a duplicate would split a group. */
	UMaterialInterface* Cached(const TCHAR* Key, TFunctionRef<UMaterialInterface*()> Make);
	/** An opaque emissive (a lit colour with emissive) for solid glowing parts: no translucency pass. */
	UMaterialInterface* SolidGlow(const TCHAR* Key, const FLinearColor& Color, float Strength);

	/** A box centred at C with size S, in pub-local web units (z offset by PubZ). */
	void Box(UMaterialInterface* Material, const FVector& C, const FVector& S, bool bShadows = true);
	/** A cylinder along web Y, pub-local. */
	void Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows = true);
	UInstancedStaticMeshComponent* Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows);
	UStaticMeshComponent* Part(UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Relative);
	/** Text reads from its +X side; by default it faces the camera, which looks down UE +X. */
	UTextRenderComponent* Text(USceneComponent* Parent, const FString& Value, const FVector& RelativeCm, float SizeCm, const FLinearColor& Color,
		const FRotator& Rotation = FRotator(0.0, 180.0, 0.0));

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> BoardPivot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BountyWedge;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BountyMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BountyEdgeMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> ChalkLines;

	/** What each chalk line shows now. */
	TArray<FString> ChalkShown;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> StoolMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BulbMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BoardMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> WireMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> WedgeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DartMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RingMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BottleMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> DartMaterials;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;
};

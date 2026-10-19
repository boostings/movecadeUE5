#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "KartPawn.generated.h"

class UCameraComponent;
class USphereComponent;
class UStaticMeshComponent;
class UKartMovementComponent;
class UMaterialInterface;

/**
 * A kart: a sphere root that only carries the sim pose, a visual body with the web build's
 * presentation layers (slip yaw, body yaw, lean, hop), and the chase camera (Docs/Port/01 §9, §10).
 * The body is a placeholder until the kart model is imported.
 */
UCLASS()
class MOVECADEKART_API AKartPawn : public APawn
{
	GENERATED_BODY()

public:
	AKartPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	UKartMovementComponent* GetKartMovement() const { return Movement; }
	/** Snap the chase camera behind the kart (after spawn or respawn). */
	void SnapCamera();

	UPROPERTY(EditAnywhere, Category = "Kart")
	FLinearColor PaintColor = FLinearColor(0.871f, 0.084f, 0.270f); // #f0508e, the v2 "pink" paint

protected:
	virtual void BeginPlay() override;

private:
	void UpdateVisual();
	void UpdateCamera(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USphereComponent> Root;

	/** Slip yaw (kartRef). */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USceneComponent> SlipPivot;

	/** Drift and body yaw, lean and hop (groupRef). */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USceneComponent> BodyPivot;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> Driver;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TArray<TObjectPtr<UStaticMeshComponent>> Wheels;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UKartMovementComponent> Movement;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	FVector CameraPos = FVector::ZeroVector;
	FVector CameraLook = FVector::ZeroVector;
	int32 LoggedLaps = 0;
	double LastLapTime = 0.0;
};

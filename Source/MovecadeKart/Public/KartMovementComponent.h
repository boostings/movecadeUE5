#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Sim/KartSim.h"
#include "KartMovementComponent.generated.h"

/**
 * Runs MovecadeKart::FKartSim at a fixed 60 Hz and moves the pawn. The sim keeps web units; this
 * component converts once when it writes the transform, and interpolates between the last two
 * sim steps so motion stays smooth at any frame rate.
 */
UCLASS(ClassGroup = (Movecade), meta = (BlueprintSpawnableComponent))
class MOVECADEKART_API UKartMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UKartMovementComponent();

	void InitSim(TSharedPtr<const MovecadeKart::FTrack> InTrack, int32 GridSlot);
	void SetKartInput(const MovecadeKart::FKartInput& InInput) { Input = InInput; }
	void ResetToGrid();
	bool HasSim() const { return Track.IsValid(); }
	const MovecadeKart::FTrack* GetTrack() const { return Track.Get(); }

	const MovecadeKart::FKartState& SimState() const { return Sim.State(); }
	/** Interpolated pose for rendering, in sim units. */
	FVector2D RenderPosition() const { return RenderPos; }
	double RenderYaw() const { return RenderYawThree; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "Kart")
	float GetSpeedSU() const { return static_cast<float>(Sim.State().Speed); }

	/** Steps run at most this many times per frame; a long hitch drops time rather than spiralling. */
	UPROPERTY(EditAnywhere, Category = "Kart")
	int32 MaxStepsPerFrame = 4;

private:
	void ApplyTransform(float Alpha);

	TSharedPtr<const MovecadeKart::FTrack> Track;
	MovecadeKart::FKartSim Sim;
	MovecadeKart::FKartInput Input;
	FVector2D PrevPos = FVector2D::ZeroVector;
	double PrevYaw = 0.0;
	uint32 LastTeleportCount = 0;
	double Accumulator = 0.0;
	int32 Slot = 0;
	FVector2D RenderPos = FVector2D::ZeroVector;
	double RenderYawThree = 0.0;
};

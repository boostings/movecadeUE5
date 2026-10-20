#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Sim/KartSim.h"
#include "KartMovementComponent.generated.h"

/**
 * Owns one kart's MovecadeKart::FKartSim and moves the pawn. The sim keeps web units; this component
 * converts once when it writes the transform, interpolating between the last two sim steps so
 * motion stays smooth at any frame rate.
 *
 * Free mode: the component steps its own sim at a fixed 60 Hz from SetKartInput.
 * Race mode (SetExternallyStepped): AKartRaceManager steps every kart in lockstep with StepOnce and
 * then calls Present, so all karts share one clock and see each other at the same step.
 */
UCLASS(ClassGroup = (Movecade), meta = (BlueprintSpawnableComponent))
class MOVECADEKART_API UKartMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UKartMovementComponent();

	void InitSim(TSharedPtr<const MovecadeKart::FTrack> InTrack, int32 GridSlot);
	void SetKartInput(const MovecadeKart::FKartInput& InInput) { Input = InInput; }
	const MovecadeKart::FKartInput& GetKartInput() const { return Input; }
	void ResetToGrid();
	bool HasSim() const { return Track.IsValid(); }
	const MovecadeKart::FTrack* GetTrack() const { return Track.Get(); }

	/** Race mode: the race manager steps and presents this kart, so the component doesn't tick at all. */
	void SetExternallyStepped(bool bExternal)
	{
		bExternallyStepped = bExternal;
		SetComponentTickEnabled(!bExternal);
	}
	/** Race mode: one fixed step. */
	void StepOnce(const MovecadeKart::FKartInput& StepInput, const MovecadeKart::FKartRaceContext& Race);
	/** Race mode: write the transform at Alpha between the previous and current step. */
	void Present(float Alpha);

	const MovecadeKart::FKartState& SimState() const { return Sim.State(); }
	MovecadeKart::FKartSim& MutableSim() { return Sim; }
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
	TSharedPtr<const MovecadeKart::FTrack> Track;
	MovecadeKart::FKartSim Sim;
	MovecadeKart::FKartInput Input;
	FVector2D PrevPos = FVector2D::ZeroVector;
	double PrevYaw = 0.0;
	uint32 LastTeleportCount = 0;
	double Accumulator = 0.0;
	int32 Slot = 0;
	bool bExternallyStepped = false;
	FVector2D RenderPos = FVector2D::ZeroVector;
	double RenderYawThree = 0.0;
};

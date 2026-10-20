#pragma once

#include "CoreMinimal.h"
#include "FX/KartFx.h"
#include "Subsystems/WorldSubsystem.h"
#include "KartFxSubsystem.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;

/**
 * Spawns the race VFX. Each MovecadeKart::Fx::EField is one Niagara system (Content/FX/Niagara/NS_FX_*) that
 * implements the web's ParticleField emit() options as user parameters, so a recipe in KartFx.h becomes either a
 * one-shot burst (Emit, pooled) or a persistent stream whose rate and options are updated every frame
 * (MakeStream + UpdateStream). All systems are CPU sims; world space except the camera's speed lines.
 */
UCLASS()
class MOVECADEKART_API UKartFxSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UKartFxSubsystem* Get(const UObject* WorldContext);

	/** One-shot burst (pooled, auto-released). */
	void Emit(const MovecadeKart::Fx::FKartFxEmit& Options);
	void Emit(const TArray<MovecadeKart::Fx::FKartFxEmit>& Options);

	/** A persistent emitter of one field, attached to Parent (or free in the world if null). Starts idle. */
	UNiagaraComponent* MakeStream(MovecadeKart::Fx::EField Field, USceneComponent* Parent, FName Name, const FVector& RelativeLocation = FVector::ZeroVector);
	/** Sets a stream's emit options (Count is ignored) and its spawn rate in particles per second (0 = idle). */
	static void UpdateStream(UNiagaraComponent* Stream, const MovecadeKart::Fx::FKartFxEmit& Options, double Rate);

	UNiagaraSystem* GetSystem(MovecadeKart::Fx::EField Field) const;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	static void ApplyOptions(UNiagaraComponent* Component, const MovecadeKart::Fx::FKartFxEmit& Options);

	UPROPERTY()
	mutable TArray<TObjectPtr<UNiagaraSystem>> Systems;
	mutable TSet<int32> Missing;

	/** Bursts to deactivate once their one burst has spawned (the field emitters never stop on their own). */
	struct FPendingBurst
	{
		TWeakObjectPtr<UNiagaraComponent> Component;
		double DeactivateAt = 0.0;
	};
	TArray<FPendingBurst> Bursts;
};

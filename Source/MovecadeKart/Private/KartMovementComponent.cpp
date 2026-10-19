#include "KartMovementComponent.h"

#include "KartCoords.h"

UKartMovementComponent::UKartMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UKartMovementComponent::InitSim(TSharedPtr<const MovecadeKart::FTrack> InTrack, int32 GridSlot)
{
	Track = InTrack;
	Slot = GridSlot;
	ResetToGrid();
}

void UKartMovementComponent::ResetToGrid()
{
	if (!Track.IsValid())
	{
		return;
	}
	Sim.Init(Track.Get(), Slot);
	Accumulator = 0.0;
	PrevPos = Sim.State().Pos;
	PrevYaw = Sim.State().Yaw;
	LastTeleportCount = Sim.State().TeleportCount;
	ApplyTransform(1.0f);
}

void UKartMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Track.IsValid() || !UpdatedComponent || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	// The web clamps a frame to 1/30 s (MultiplayerKart.tsx:84,705); steps are a fixed 1/60 s.
	constexpr double Step = MovecadeKart::FKartSim::StepSeconds;
	Accumulator += FMath::Min(static_cast<double>(DeltaTime), 1.0 / 30.0);
	int32 Steps = 0;
	while (Accumulator >= Step && Steps < MaxStepsPerFrame)
	{
		PrevPos = Sim.State().Pos;
		PrevYaw = Sim.State().Yaw;
		Sim.Step(Input);
		Accumulator -= Step;
		++Steps;
		if (Sim.State().TeleportCount != LastTeleportCount)
		{
			LastTeleportCount = Sim.State().TeleportCount;
			PrevPos = Sim.State().Pos;
			PrevYaw = Sim.State().Yaw;
		}
	}
	if (Steps == MaxStepsPerFrame)
	{
		Accumulator = FMath::Min(Accumulator, Step);
	}

	const FVector2D Delta = (Sim.State().Pos - PrevPos) / Step;
	Velocity = FVector(-Delta.Y, Delta.X, 0.0) * 100.0;
	ApplyTransform(static_cast<float>(FMath::Clamp(Accumulator / Step, 0.0, 1.0)));
}

void UKartMovementComponent::ApplyTransform(float Alpha)
{
	const MovecadeKart::FKartState& S = Sim.State();
	RenderPos = FMath::Lerp(PrevPos, S.Pos, static_cast<double>(Alpha));
	RenderYawThree = PrevYaw + FMath::FindDeltaAngleRadians(PrevYaw, S.Yaw) * Alpha;
	if (UpdatedComponent)
	{
		UpdatedComponent->SetWorldLocationAndRotation(
			MovecadeKart::Coords::ToUE(RenderPos),
			FRotator(0.0, MovecadeKart::Coords::YawToUE(RenderYawThree), 0.0),
			false, nullptr, ETeleportType::TeleportPhysics);
	}
}

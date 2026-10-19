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
	Present(1.0f);
}

void UKartMovementComponent::StepOnce(const MovecadeKart::FKartInput& StepInput, const MovecadeKart::FKartRaceContext& Race)
{
	PrevPos = Sim.State().Pos;
	PrevYaw = Sim.State().Yaw;
	Input = StepInput; // what the audio drivetrain reads as the pedals
	Sim.Step(StepInput, Race);
	if (Sim.State().TeleportCount != LastTeleportCount)
	{
		// Snap rather than slide across the track after a respawn.
		LastTeleportCount = Sim.State().TeleportCount;
		PrevPos = Sim.State().Pos;
		PrevYaw = Sim.State().Yaw;
	}
	const FVector2D Delta = (Sim.State().Pos - PrevPos) / MovecadeKart::FKartSim::StepSeconds;
	Velocity = FVector(-Delta.Y, Delta.X, 0.0) * 100.0;
}

void UKartMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bExternallyStepped || !Track.IsValid() || !UpdatedComponent || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	// The web clamps a frame to 1/30 s (MultiplayerKart.tsx:84,705); steps are a fixed 1/60 s.
	constexpr double Step = MovecadeKart::FKartSim::StepSeconds;
	Accumulator += FMath::Min(static_cast<double>(DeltaTime), 1.0 / 30.0);
	int32 Steps = 0;
	while (Accumulator >= Step && Steps < MaxStepsPerFrame)
	{
		StepOnce(Input, MovecadeKart::FKartRaceContext());
		Accumulator -= Step;
		++Steps;
	}
	if (Steps == MaxStepsPerFrame)
	{
		Accumulator = FMath::Min(Accumulator, Step);
	}
	Present(static_cast<float>(FMath::Clamp(Accumulator / Step, 0.0, 1.0)));
}

void UKartMovementComponent::Present(float Alpha)
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

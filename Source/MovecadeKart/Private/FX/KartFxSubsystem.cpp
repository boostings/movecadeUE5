#include "FX/KartFxSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

using namespace MovecadeKart::Fx;

DEFINE_LOG_CATEGORY_STATIC(LogKartFx, Log, All);

namespace
{
	/** Content/FX/Niagara, built by Tools/ue/make_fx_niagara.py (Tools/fx/build_fx.ps1). */
	const TCHAR* FieldAsset(EField Field)
	{
		switch (Field)
		{
		case EField::Smoke: return TEXT("/Game/FX/Niagara/NS_FX_Smoke.NS_FX_Smoke");
		case EField::Fire: return TEXT("/Game/FX/Niagara/NS_FX_Fire.NS_FX_Fire");
		case EField::Sparks: return TEXT("/Game/FX/Niagara/NS_FX_Spark.NS_FX_Spark");
		case EField::Ring: return TEXT("/Game/FX/Niagara/NS_FX_Ring.NS_FX_Ring");
		case EField::Flare: return TEXT("/Game/FX/Niagara/NS_FX_Flare.NS_FX_Flare");
		case EField::Confetti: return TEXT("/Game/FX/Niagara/NS_FX_Confetti.NS_FX_Confetti");
		case EField::SpeedLines: return TEXT("/Game/FX/Niagara/NS_FX_SpeedLines.NS_FX_SpeedLines");
		default: return TEXT("");
		}
	}

	// User parameter names, shared by every system (make_fx_niagara.py USER).
	const FName PCount(TEXT("Count"));
	const FName PRate(TEXT("Rate"));
	const FName PDirection(TEXT("Direction"));
	const FName PSpread(TEXT("Spread"));
	const FName PSpeed(TEXT("SpeedRange"));
	const FName PLife(TEXT("LifeRange"));
	const FName PSize(TEXT("SizeRange"));
	const FName PSizeEnd(TEXT("SizeEnd"));
	const FName PColor(TEXT("Color"));
	const FName PColorEnd(TEXT("ColorEnd"));
	const FName PJitter(TEXT("Jitter"));
	const FName PInherit(TEXT("Inherit"));

	/** An idle stream (rate 0) is deactivated after this long; every field's particles live under 1.5 s. */
	constexpr double StreamIdleSeconds = 2.5;
	/** A burst's emitter is deactivated this long after it spawned its particles (they live on to the end). */
	constexpr double BurstSpawnSeconds = 0.1;
	/** Destroyed streams (finished projectile trails, departed karts) are forgotten this often. */
	constexpr double StreamSweepSeconds = 5.0;

	/** Particle budget per device: phones' device profiles lower it (Config/DefaultDeviceProfiles.ini); 1 on PC. */
	TAutoConsoleVariable<float> CVarSpawnScale(
		TEXT("kart.FX.SpawnScale"), 1.f,
		TEXT("Scales every kart FX burst count and stream rate (0.25-1). Set per device profile; 1 = the full look."),
		ECVF_Scalability);

	float SpawnScale()
	{
		return FMath::Clamp(CVarSpawnScale.GetValueOnGameThread(), 0.25f, 1.f);
	}

	/** A burst's count under the budget: never below one particle, so every cue still shows. */
	int32 ScaledCount(int32 Count)
	{
		const float Scale = SpawnScale();
		return Scale >= 1.f ? Count : FMath::Max(1, FMath::RoundToInt32(Count * Scale));
	}
}

UKartFxSubsystem* UKartFxSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UKartFxSubsystem>() : nullptr;
}

bool UKartFxSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UKartFxSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Systems load on first use, once the world is up.
	Systems.SetNum(static_cast<int32>(EField::Count));
}

UNiagaraSystem* UKartFxSubsystem::GetSystem(EField Field) const
{
	const int32 Index = static_cast<int32>(Field);
	if (!Systems.IsValidIndex(Index))
	{
		return nullptr;
	}
	if (!Systems[Index] && !Missing.Contains(Index))
	{
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, FieldAsset(Field));
		if (!System)
		{
			UE_LOG(LogKartFx, Warning, TEXT("Kart FX: missing %s (run Tools/fx/build_fx.ps1)"), FieldAsset(Field));
			Missing.Add(Index);
		}
		Systems[Index] = System;
	}
	return Systems[Index].Get();
}

void UKartFxSubsystem::ApplyOptions(UNiagaraComponent* C, const FKartFxEmit& O, FKartFxEmit* Sent)
{
	// Web units -> UE: metres to cm.
	const float Intensity = static_cast<float>(O.Intensity);
	if (!Sent || !O.Direction.Equals(Sent->Direction, 1e-3))
	{
		FVector Dir = O.Direction.GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			Dir = FVector::UpVector;
		}
		C->SetVariableVec3(PDirection, Dir);
		if (Sent)
		{
			Sent->Direction = O.Direction;
		}
	}
	if (!Sent || O.Spread != Sent->Spread)
	{
		C->SetVariableFloat(PSpread, static_cast<float>(FMath::Clamp(O.Spread, 0.0, UE_DOUBLE_PI)));
	}
	if (!Sent || O.Speed != Sent->Speed)
	{
		C->SetVariableVec2(PSpeed, FVector2D(O.Speed.X * 100.0, O.Speed.Y * 100.0));
	}
	if (!Sent || O.Life != Sent->Life)
	{
		C->SetVariableVec2(PLife, FVector2D(FMath::Max(0.01, O.Life.X), FMath::Max(0.01, O.Life.Y)));
	}
	if (!Sent || O.Size != Sent->Size)
	{
		C->SetVariableVec2(PSize, FVector2D(O.Size.X * 100.0, O.Size.Y * 100.0));
	}
	if (!Sent || O.SizeEnd != Sent->SizeEnd)
	{
		C->SetVariableFloat(PSizeEnd, static_cast<float>(O.SizeEnd));
	}
	const bool bIntensity = !Sent || O.Intensity != Sent->Intensity;
	if (bIntensity || !O.Color.Equals(Sent->Color, 1e-4f))
	{
		C->SetVariableLinearColor(PColor, FLinearColor(O.Color.R * Intensity, O.Color.G * Intensity, O.Color.B * Intensity, O.Color.A));
		if (Sent)
		{
			Sent->Color = O.Color;
		}
	}
	if (bIntensity || !O.ColorEnd.Equals(Sent->ColorEnd, 1e-4f))
	{
		C->SetVariableLinearColor(PColorEnd, FLinearColor(O.ColorEnd.R * Intensity, O.ColorEnd.G * Intensity, O.ColorEnd.B * Intensity, O.ColorEnd.A));
		if (Sent)
		{
			Sent->ColorEnd = O.ColorEnd;
		}
	}
	if (!Sent || O.Jitter != Sent->Jitter)
	{
		C->SetVariableFloat(PJitter, static_cast<float>(O.Jitter * 100.0));
	}
	if (!Sent || !O.InheritCm.Equals(Sent->InheritCm, 0.5))
	{
		C->SetVariableVec3(PInherit, O.InheritCm);
		if (Sent)
		{
			Sent->InheritCm = O.InheritCm;
		}
	}
	if (Sent)
	{
		// The exactly compared options are now what was pushed.
		Sent->Spread = O.Spread;
		Sent->Speed = O.Speed;
		Sent->Life = O.Life;
		Sent->Size = O.Size;
		Sent->SizeEnd = O.SizeEnd;
		Sent->Intensity = O.Intensity;
		Sent->Jitter = O.Jitter;
	}
}

void UKartFxSubsystem::Emit(const FKartFxEmit& O)
{
	UNiagaraSystem* System = GetSystem(O.Field);
	UWorld* World = GetWorld();
	if (!System || !World || O.Count <= 0)
	{
		return;
	}
	UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, O.Position, FRotator::ZeroRotator, FVector(1.0),
		/*bAutoDestroy*/ true, /*bAutoActivate*/ false, ENCPoolMethod::AutoRelease, /*bPreCullCheck*/ true);
	if (!C)
	{
		return;
	}
	ApplyOptions(C, O);
	C->SetVariableInt(PCount, ScaledCount(O.Count));
	C->SetVariableFloat(PRate, 0.f);
	C->Activate(true);
	Bursts.Add({ C, World->GetTimeSeconds() + BurstSpawnSeconds });
}

void UKartFxSubsystem::Emit(const TArray<FKartFxEmit>& Options)
{
	for (const FKartFxEmit& O : Options)
	{
		Emit(O);
	}
}

void UKartFxSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now >= NextStreamSweep)
	{
		// Forget streams that were destroyed (projectile trails, karts that left).
		NextStreamSweep = Now + StreamSweepSeconds;
		for (auto It = Streams.CreateIterator(); It; ++It)
		{
			if (!It.Key().ResolveObjectPtr())
			{
				It.RemoveCurrent();
			}
		}
	}
	for (int32 i = Bursts.Num() - 1; i >= 0; --i)
	{
		if (Now >= Bursts[i].DeactivateAt)
		{
			if (UNiagaraComponent* C = Bursts[i].Component.Get())
			{
				// Lets the burst's particles finish, then the pooled component completes and is released.
				C->Deactivate();
			}
			Bursts.RemoveAtSwap(i);
		}
	}
}

TStatId UKartFxSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKartFxSubsystem, STATGROUP_Tickables);
}

UNiagaraComponent* UKartFxSubsystem::MakeStream(EField Field, USceneComponent* Parent, FName Name, const FVector& RelativeLocation)
{
	UNiagaraSystem* System = GetSystem(Field);
	if (!System)
	{
		return nullptr;
	}
	UObject* Outer = Parent ? static_cast<UObject*>(Parent->GetOwner()) : static_cast<UObject*>(GetWorld());
	UNiagaraComponent* C = NewObject<UNiagaraComponent>(Outer, Name);
	C->SetAsset(System);
	C->SetAutoActivate(false);
	C->SetAutoDestroy(false);
	if (Parent)
	{
		C->SetupAttachment(Parent);
		C->SetRelativeLocation(RelativeLocation);
	}
	C->RegisterComponentWithWorld(GetWorld());
	C->SetVariableInt(PCount, 0);
	C->SetVariableFloat(PRate, 0.f);
	return C;
}

void UKartFxSubsystem::UpdateStream(UNiagaraComponent* Stream, const FKartFxEmit& Options, double Rate)
{
	if (!Stream)
	{
		return;
	}
	UKartFxSubsystem* Self = Get(Stream);
	if (!Self)
	{
		// No subsystem to remember the stream: push everything and never deactivate.
		ApplyOptions(Stream, Options);
		Stream->SetVariableFloat(PRate, static_cast<float>(FMath::Max(0.0, Rate)) * SpawnScale());
		if (Rate > 0.0 && !Stream->IsActive())
		{
			Stream->SetVariableInt(PCount, 0);
			Stream->Activate(true);
		}
		return;
	}
	FStreamState& State = Self->Streams.FindOrAdd(Stream);
	if (Rate > 0.0)
	{
		ApplyOptions(Stream, Options, State.bSent ? &State.Sent : nullptr);
		if (!State.bSent)
		{
			State.Sent = Options;
			State.bSent = true;
		}
		const float NewRate = static_cast<float>(Rate) * SpawnScale();
		if (NewRate != State.Rate)
		{
			Stream->SetVariableFloat(PRate, NewRate);
			State.Rate = NewRate;
		}
		State.IdleSince = -1.0;
		// A deactivated stream reports active until its last particle is gone: restart it either way.
		if (State.bDeactivated || !Stream->IsActive())
		{
			State.bDeactivated = false;
			Stream->SetVariableInt(PCount, 0);
			Stream->Activate(true);
		}
		return;
	}
	if (State.bDeactivated || !Stream->IsActive())
	{
		return;
	}
	const UWorld* World = Stream->GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (State.Rate != 0.f || State.IdleSince < 0.0)
	{
		Stream->SetVariableFloat(PRate, 0.f);
		State.Rate = 0.f;
		State.IdleSince = Now;
	}
	else if (Now - State.IdleSince > StreamIdleSeconds)
	{
		// Its particles are long gone: stop ticking the system until the stream is wanted again.
		Stream->Deactivate();
		State.bDeactivated = true;
	}
}

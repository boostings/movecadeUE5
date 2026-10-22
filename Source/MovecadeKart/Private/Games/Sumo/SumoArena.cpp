// ASumoArena's lifecycle, its component helpers and its animation: the bands falling and rising, the warned band's
// flicker, the floating props bobbing on the swell.

#include "Games/Sumo/SumoArena.h"

#include "Art/KartMaterials.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Sumo/SumoArenaLayout.h"
#include "Games/Sumo/SumoFx.h"
#include "Games/Sumo/SumoPresentation.h"
#include "Games/Sumo/SumoRules.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace MovecadeKart::Sumo;
using namespace MovecadeKart::Sumo::ArenaLayout;
namespace Art = MovecadeKart::Art;

namespace
{
	const TCHAR* const SlotColors[] = { TEXT("#ff3f8e"), TEXT("#29d8ff"), TEXT("#ffb21f"), TEXT("#7cf25a") };
	/** The sky sphere's radius, in the engine sphere's 100 cm units. */
	constexpr double SkyDomeScale = 20000.0;
	constexpr float LightSourceRadiusCm = 20.f;
	/** The posts' unlit tassels when nobody holds their seat. */
	constexpr float IdleTasselGlow = 0.6f;
	constexpr float TasselGlow = 6.f;

	/** A band's fall: a shudder just before, gravity (scaled up for a snappier drop), a drift outward, a tumble. */
	constexpr double FallSeconds = 1.6;
	constexpr double ShudderSeconds = 0.25;
	constexpr double ShudderRate = 70.0;
	constexpr double ShudderCm = 3.0;
	constexpr double GravityCm = 980.0;
	constexpr double FallGravityScale = 1.4;
	constexpr double DriftCmPerSecond = 120.0;
	/** Far enough under the sea to hide a segment (it shrinks to nothing there). */
	constexpr double SunkDepthCm = 300.0;
	constexpr double SunkScale = 0.001;
	constexpr double SplashScale = 0.9;
	/** Splashes sit just over the sea's surface (cm). */
	constexpr double SplashOverWaterCm = 10.0;
	/** A band rises back out of the sea in a wave, overshooting a touch. */
	constexpr double RiseSeconds = 0.9;
	constexpr double RiseDelayShare = 0.6;
	constexpr double RiseOvershoot = 0.12;
	constexpr double RiseOvershootCm = 40.0;
	constexpr double RiseSpinShare = 0.6;
	/** The warned band's flicker quickens from Base to Base + Extra (rad/s) as its drop nears. */
	constexpr double WarnFlickerBase = 8.0;
	constexpr double WarnFlickerExtra = 18.0;
	constexpr float WarnTrimLow = 4.f;
	constexpr float WarnTrimHigh = 14.f;
	constexpr double WarnWashBase = 0.08;
	constexpr double WarnWashPulse = 0.32;

	/** The floaters on the swell: a slow bob (rad/s, phase per floater, cm) and a gentle rock (rates in rad/s, deg). */
	constexpr double FloatBobRate = 0.7;
	constexpr double FloatBobPhase = 1.7;
	constexpr double FloatBobCm = 18.0;
	constexpr double FloatPitchRate = 0.5;
	constexpr double FloatRollRate = 0.6;
	constexpr double FloatRockDeg = 2.0;
	/** The lanterns: the swell's phase runs across the sea (rad per cm along X and Y), so neighbours bob together. */
	constexpr double LanternBobRate = 1.1;
	constexpr double SwellPhasePerCmX = 0.003;
	constexpr double SwellPhasePerCmY = 0.002;
	constexpr double LanternBobCm = 8.0;
	constexpr double LanternPitchRate = 0.9;
	constexpr double LanternRollRate = 0.8;
	constexpr double LanternRollPhase = 0.7;
	constexpr double LanternRockDeg = 6.0;
}

ASumoArena::ASumoArena()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	Sphere = SphereMesh.Object;
	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(RootComponent);
	Moon->SetMobility(EComponentMobility::Movable);
	Rim = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Rim"));
	Rim->SetupAttachment(RootComponent);
	Rim->SetMobility(EComponentMobility::Movable);
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(RootComponent);
	SkyDome->SetStaticMesh(SphereMesh.Object);
	SkyDome->SetRelativeScale3D(FVector(SkyDomeScale));
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDome->SetCastShadow(false);
	SkyDome->SetMobility(EComponentMobility::Movable);
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetMobility(EComponentMobility::Movable);
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
}

FLinearColor ASumoArena::SlotColor(int32 Slot)
{
	// Parsed once: the pucks and the HUD ask every frame.
	static const FLinearColor Colors[] = { Art::Hex(SlotColors[0]), Art::Hex(SlotColors[1]), Art::Hex(SlotColors[2]), Art::Hex(SlotColors[3]) };
	return Colors[((Slot % 4) + 4) % 4];
}

void ASumoArena::BeginPlay()
{
	Super::BeginPlay();
	// A level loads the editor's build without its bookkeeping (bands, lanterns): rebuild then. A fresh spawn has it.
	if (Bands.Num() == 0)
	{
		Build();
	}
}

void ASumoArena::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The ring is the rules' origin: the arena never moves.
	SetActorTransform(FTransform::Identity);
	Build();
}

void ASumoArena::SetPlayers(int32 Count)
{
	static const FLinearColor Idle = Art::Hex(TEXT("#3a3550")) * IdleTasselGlow;
	Players = FMath::Clamp(Count, 1, 4);
	for (int32 i = 0; i < TasselMats.Num(); ++i)
	{
		const bool bOn = i < Players;
		if (TasselMats[i])
		{
			TasselMats[i]->SetVectorParameterValue(TEXT("Color"), bOn ? SlotColor(i) * TasselGlow : Idle);
		}
		if (TasselLights.IsValidIndex(i) && TasselLights[i])
		{
			TasselLights[i]->SetVisibility(bOn);
		}
	}
}

void ASumoArena::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), ArenaTag());
	UE_LOG(LogSumo, Verbose, TEXT("SumoArena: clearing %d components (%d on the actor)"), Old.Num(), GetComponents().Num());
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	Groups.Reset();
	Materials.Reset();
	BandMeshes.Reset();
	BandTrims.Reset();
	BandWarnings.Reset();
	BandWarningMats.Reset();
	CoreDecor.Reset();
	TasselMats.Reset();
	TasselLights.Reset();
	Floaters.Reset();
	FloaterRest.Reset();
	RimLights.Reset();
	LanternRest.Reset();
	Lanterns = nullptr;
	Bands.Reset();
}

void ASumoArena::Build()
{
	Clear();
	Groups.Init(this, TEXT("ArenaGroup"), ArenaTag(), EComponentMobility::Movable);
	BuildLook();
	BuildSky();
	BuildRing();
	BuildKeel();
	BuildSea();
	BuildProps();
	SetPlayers(Players);
}

UStaticMeshComponent* ASumoArena::Mesh(UStaticMesh* InMesh, UMaterialInterface* Material, const FTransform& Transform, bool bShadows, USceneComponent* Parent)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("ArenaMesh")));
	C->SetStaticMesh(InMesh);
	if (Material)
	{
		for (int32 i = 0; i < FMath::Max(1, InMesh ? InMesh->GetStaticMaterials().Num() : 1); ++i)
		{
			C->SetMaterial(i, Material);
		}
	}
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetRelativeTransform(Transform);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(ArenaTag());
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

UInstancedStaticMeshComponent* ASumoArena::OwnInstances(UStaticMesh* InMesh, const TCHAR* Name, bool bShadows)
{
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), Name));
	Ism->SetStaticMesh(InMesh);
	Ism->SetupAttachment(RootComponent);
	Ism->SetMobility(EComponentMobility::Movable);
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCastShadow(bShadows);
	Ism->ComponentTags.Add(ArenaTag());
	AddInstanceComponent(Ism);
	Ism->RegisterComponent();
	return Ism;
}

UPointLightComponent* ASumoArena::Light(const FVector& At, const FLinearColor& Color, float Lumens, float RadiusCm, USceneComponent* Parent)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("ArenaLight")));
	L->SetupAttachment(Parent ? Parent : RootComponent.Get());
	L->SetMobility(EComponentMobility::Movable);
	L->SetRelativeLocation(At);
	L->SetIntensityUnits(ELightUnits::Lumens);
	L->SetIntensity(Lumens);
	L->SetAttenuationRadius(RadiusCm);
	L->SetLightColor(Color);
	L->SetSourceRadius(LightSourceRadiusCm);
	L->SetCastShadows(false);
	L->SetVolumetricScatteringIntensity(0.f);
	L->ComponentTags.Add(ArenaTag());
	AddInstanceComponent(L);
	L->RegisterComponent();
	return L;
}

USceneComponent* ASumoArena::Floater(const FVector& Rest)
{
	USceneComponent* Float = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("Floater")));
	Float->SetupAttachment(RootComponent);
	Float->SetMobility(EComponentMobility::Movable);
	Float->SetRelativeLocation(Rest);
	Float->ComponentTags.Add(ArenaTag());
	AddInstanceComponent(Float);
	Float->RegisterComponent();
	Floaters.Add(Float);
	FloaterRest.Add(Rest);
	return Float;
}

void ASumoArena::SetRing(int32 ShrinksDone, bool bPending, double InWarnAge)
{
	for (int32 b = 0; b < Bands.Num(); ++b)
	{
		FBand& Band = Bands[b];
		const bool bGone = b < ShrinksDone;
		if (bGone && (Band.State == EBandState::Intact || Band.State == EBandState::Rising))
		{
			Band.State = EBandState::Falling;
			Band.Since = Clock;
			for (FSegmentFall& F : Band.Falls)
			{
				F.bSplashed = false;
			}
		}
		else if (!bGone && (Band.State == EBandState::Gone || Band.State == EBandState::Falling))
		{
			UE_LOG(LogSumo, Verbose, TEXT("SumoArena: band %d rises (was %d) at %.2f"), b, static_cast<int32>(Band.State), Clock);
			Band.State = EBandState::Rising;
			Band.Since = Clock;
		}
	}
	WarnBand = bPending ? ShrinksDone : INDEX_NONE;
	WarnAge = InWarnAge;
}

void ASumoArena::Splash(const FVector& At, double Scale)
{
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		TArray<MovecadeKart::Fx::FKartFxEmit> Out;
		SumoFx::BandSplash(At, Scale, Out);
		FxSys->Emit(Out);
	}
}

void ASumoArena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	AnimateBands();
	AnimateProps();
}

void ASumoArena::AnimateBands()
{
	for (int32 b = 0; b < Bands.Num(); ++b)
	{
		FBand& Band = Bands[b];
		UInstancedStaticMeshComponent* Ism = BandMeshes.IsValidIndex(b) ? BandMeshes[b].Get() : nullptr;
		if (!Ism)
		{
			continue;
		}
		if (b == Bands.Num() - 1)
		{
			// The core's rope and start lines go with it.
			for (UStaticMeshComponent* C : CoreDecor)
			{
				if (C)
				{
					C->SetVisibility(Band.State == EBandState::Intact);
				}
			}
		}
		if ((Band.State == EBandState::Falling || Band.State == EBandState::Rising) && AnimateSegments(Band, *Ism))
		{
			Band.State = Band.State == EBandState::Falling ? EBandState::Gone : EBandState::Intact;
			UE_LOG(LogSumo, Verbose, TEXT("SumoArena: band %d -> %s at %.2f"), b, Band.State == EBandState::Gone ? TEXT("gone") : TEXT("intact"), Clock);
			if (Band.State == EBandState::Intact)
			{
				for (int32 s = 0; s < Band.Segments; ++s)
				{
					Ism->UpdateInstanceTransform(s, Band.Rest[s], false, s == Band.Segments - 1, true);
				}
			}
		}
		AnimateWarning(b);
	}
}

bool ASumoArena::AnimateSegments(FBand& Band, UInstancedStaticMeshComponent& Ism)
{
	const double Age = Clock - Band.Since;
	bool bDone = true;
	for (int32 s = 0; s < Band.Segments; ++s)
	{
		FSegmentFall& F = Band.Falls[s];
		const FTransform& Rest = Band.Rest[s];
		FTransform T = Rest;
		if (Band.State == EBandState::Falling)
		{
			// A shudder, then the drop: gravity, a drift outward, a tumble; a splash where it meets the sea.
			const double Local = Age - F.Delay;
			bDone &= Local >= FallSeconds;
			if (Local > -ShudderSeconds && Local < 0.0)
			{
				T.AddToTranslation(FVector(0.0, 0.0, FMath::Sin(Clock * ShudderRate + s) * ShudderCm));
			}
			else if (Local >= 0.0)
			{
				const double Drop = 0.5 * GravityCm * FallGravityScale * Local * Local;
				const FVector Out = Rest.GetLocation().GetSafeNormal2D();
				// The core's centre segment has no outward direction: it drifts along X.
				const FVector Pos = Rest.GetLocation() + (Out.IsNearlyZero() ? FVector(F.Drift, 0.0, 0.0) : Out) * (F.Drift * DriftCmPerSecond * Local) - FVector(0.0, 0.0, Drop);
				if (Pos.Z < WaterZ && !F.bSplashed)
				{
					F.bSplashed = true;
					// Every other piece splashes: a full ring of splashes is a wall of particles.
					if (s % 2 == 0)
					{
						Splash(FVector(Pos.X, Pos.Y, WaterZ + SplashOverWaterCm), SplashScale);
					}
				}
				if (Pos.Z < WaterZ - SunkDepthCm)
				{
					T.SetScale3D(FVector(SunkScale));
				}
				T.SetLocation(Pos);
				T.SetRotation(FQuat(F.Axis, FMath::DegreesToRadians(F.Spin * Local)) * Rest.GetRotation());
			}
		}
		else
		{
			const double Local = FMath::Clamp((Age - F.Delay * RiseDelayShare) / RiseSeconds, 0.0, 1.0);
			bDone &= Local >= 1.0;
			const double K = 1.0 - Local;
			const double Over = FMath::Sin(Local * PI) * RiseOvershoot;
			T.SetLocation(Rest.GetLocation() + FVector(0.0, 0.0, (WaterZ - SunkDepthCm) * K * K + Over * RiseOvershootCm));
			T.SetRotation(FQuat(F.Axis, FMath::DegreesToRadians(F.Spin * RiseSpinShare * K)) * Rest.GetRotation());
		}
		// Only the last update marks the render state dirty: one rebuild per band per frame.
		Ism.UpdateInstanceTransform(s, T, false, s == Band.Segments - 1, true);
	}
	return bDone;
}

void ASumoArena::AnimateWarning(int32 Index)
{
	// The warned band: the edge and the wash flush red and flicker faster as the drop nears.
	static const FLinearColor WarnLow = Art::Hex(TEXT("#ff2030")) * WarnTrimLow;
	static const FLinearColor WarnHigh = Art::Hex(TEXT("#ff6040")) * WarnTrimHigh;
	const FBand& Band = Bands[Index];
	const bool bWarn = Index == WarnBand && Band.State == EBandState::Intact;
	const double Urgency = FMath::Clamp(WarnAge / (ShrinkWarningMs / 1000.0), 0.0, 1.0);
	const double Pulse = bWarn ? 0.5 + 0.5 * FMath::Sin(Clock * (WarnFlickerBase + WarnFlickerExtra * Urgency)) : 0.0;
	if (BandTrims.IsValidIndex(Index) && BandTrims[Index])
	{
		BandTrims[Index]->SetVectorParameterValue(TEXT("Color"), bWarn ? FMath::Lerp(WarnLow, WarnHigh, static_cast<float>(Pulse)) : Band.TrimColor);
	}
	if (BandWarnings.IsValidIndex(Index) && BandWarnings[Index])
	{
		BandWarnings[Index]->SetVisibility(bWarn);
		if (bWarn && BandWarningMats[Index])
		{
			BandWarningMats[Index]->SetScalarParameterValue(TEXT("Opacity"), static_cast<float>(WarnWashBase + WarnWashPulse * Pulse * (0.5 + 0.5 * Urgency)));
		}
	}
}

void ASumoArena::AnimateProps()
{
	for (int32 i = 0; i < Floaters.Num(); ++i)
	{
		if (Floaters[i] && FloaterRest.IsValidIndex(i))
		{
			const double Bob = FMath::Sin(Clock * FloatBobRate + i * FloatBobPhase) * FloatBobCm;
			Floaters[i]->SetRelativeLocationAndRotation(FloaterRest[i] + FVector(0.0, 0.0, Bob),
				FRotator(FMath::Sin(Clock * FloatPitchRate + i) * FloatRockDeg, 0.0, FMath::Cos(Clock * FloatRollRate + i) * FloatRockDeg));
		}
	}
	if (!Lanterns || LanternRest.Num() != Lanterns->GetInstanceCount())
	{
		return;
	}
	for (int32 i = 0; i < LanternRest.Num(); ++i)
	{
		const FVector P = LanternRest[i];
		const double Bob = FMath::Sin(Clock * LanternBobRate + P.X * SwellPhasePerCmX + P.Y * SwellPhasePerCmY) * LanternBobCm;
		const FRotator Rock(FMath::Sin(Clock * LanternPitchRate + i) * LanternRockDeg, 0.0, FMath::Cos(Clock * LanternRollRate + i * LanternRollPhase) * LanternRockDeg);
		Lanterns->UpdateInstanceTransform(i, FTransform(Rock, P + FVector(0.0, 0.0, Bob), LanternScale), false, i == LanternRest.Num() - 1, true);
	}
}

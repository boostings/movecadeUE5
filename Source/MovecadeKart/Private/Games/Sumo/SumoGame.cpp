#include "Games/Sumo/SumoGame.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/SArcadeOverlay.h"
#include "Games/Sumo/SSumoHud.h"
#include "Games/Sumo/SumoArena.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/RoomProtocol.h"
#include "NiagaraComponent.h"
#include "UI/KartUIStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogSumo, Log, All);

using namespace MovecadeKart::Sumo;
namespace Fx = MovecadeKart::Fx;
using MovecadeKart::Art::Hex;

namespace
{
	constexpr double IntroSeconds = 3.6;
	/** The showcase holds this long before the room hears the result. */
	constexpr double ShowcaseSeconds = 6.0;
	constexpr float RiderScale = 0.64f;
	/** The puck is modelled 85 cm across the bumper and scaled to the rules' radius. */
	constexpr double MeshRadius = 85.0;
	const double PuckScale = PuckRadius * 100.0 / MeshRadius;
	/** The deck the rider stands on, cm above the ring. */
	constexpr double DeckZ = 47.0;
	/** Full tilt at this many degrees of phone tilt, nothing under the dead zone. */
	constexpr double TiltFullDeg = 24.0;
	constexpr double TiltDeadDeg = 2.5;
	constexpr double PhoneQuietSeconds = 1.0;

	FVector Cm(const FVector2D& P, double Z = 0.0) { return FVector(P.X * 100.0, P.Y * 100.0, Z); }

	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	const TCHAR* PowerName(EPower P)
	{
		switch (P)
		{
		case EPower::Heavy: return TEXT("HEAVY!");
		case EPower::Shield: return TEXT("SHIELD!");
		case EPower::Turbo: return TEXT("TURBO!");
		default: return TEXT("");
		}
	}

	FLinearColor PowerColor(EPower P)
	{
		switch (P)
		{
		case EPower::Heavy: return Hex(TEXT("#ffcf5a"));
		case EPower::Shield: return Hex(TEXT("#7ff6ff"));
		case EPower::Turbo: return Hex(TEXT("#ff6be6"));
		default: return FLinearColor::White;
		}
	}

	/** One lathed ring of a torus (centre radius R, tube radius T, at height Z), as a closed profile. */
	void Torus(MovecadeKart::FMeshBuilder& B, FName Slot, double R, double T, double Z, int32 Segments = 40)
	{
		TArray<FVector2D> Profile;
		for (int32 i = 0; i <= 12; ++i)
		{
			const double A = -HALF_PI + 2.0 * PI * i / 12.0;
			Profile.Add(FVector2D(R + T * FMath::Cos(A), Z + T * FMath::Sin(A)));
		}
		B.Lathe(Slot, Profile, Segments, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
	}

	/**
	 * The bumper puck (UE cm, facing +X): a dark skirt, a fat rubber bumper all round, a glossy painted shell with a
	 * neon trim line, a flat deck on top for the rider and two headlamps at the front. Slots: Paint, Rubber, Base,
	 * Trim (the lamps share the trim).
	 */
	UStaticMesh* BuildPuckMesh(UObject* Outer)
	{
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Paint"));
		B.Section(TEXT("Rubber"));
		B.Section(TEXT("Base"));
		B.Section(TEXT("Trim"));
		const double R = MeshRadius;
		B.Lathe(TEXT("Base"), { { 0.0, 3.0 }, { R - 14.0, 3.0 }, { R - 9.0, 8.0 }, { R - 10.0, 18.0 }, { 0.0, 18.0 } }, 40, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		Torus(B, TEXT("Rubber"), R - 15.0, 15.0, 22.0, 48);
		B.Lathe(TEXT("Paint"), { { R - 17.0, 26.0 }, { R - 15.5, 33.0 }, { R - 19.0, 40.0 }, { R - 30.0, 45.5 }, { R - 42.0, DeckZ }, { 0.0, DeckZ } }, 48,
			FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		Torus(B, TEXT("Trim"), R - 16.2, 1.8, 34.0, 48);
		// The deck's ring and the lamps.
		Torus(B, TEXT("Base"), R - 44.0, 2.4, DeckZ, 32);
		for (const double Side : { -1.0, 1.0 })
		{
			const double A = Side * 0.38;
			B.Ellipsoid(TEXT("Trim"), FVector(FMath::Cos(A) * (R - 17.0), FMath::Sin(A) * (R - 17.0), 36.0), FVector(4.0, 7.0, 4.0), 6, 10, FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("SumoPuck"));
	}
}

ASumoGame::ASumoGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

UStaticMeshComponent* ASumoGame::MakeMesh(USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Relative, bool bShadows)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("SumoMesh")));
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetMobility(EComponentMobility::Movable);
	C->SetRelativeTransform(Relative);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->RegisterComponent();
	return C;
}

void ASumoGame::Setup(ASumoArena* InArena, const TArray<FSumoSeat>& InSeats, APlayerController* InViewer, uint32 InSeed)
{
	Arena = InArena;
	Seats = InSeats;
	Viewer = InViewer;
	Seed = InSeed;
	TArray<FSeed> MatchSeeds;
	for (int32 i = 0; i < Seats.Num() && i < MaxPlayers; ++i)
	{
		MatchSeeds.Add({ Seats[i].Id, Seats[i].Name });
		FPilot& P = Pilots.AddDefaulted_GetRef();
		P.Kind = Seats[i].Kind;
		P.Brain = MakeBot(Seats[i].Skill, Seed * 2654435761u + 977u * (i + 1));
	}
	Seats.SetNum(Pilots.Num());
	Match = CreateMatch(MatchSeeds, Seed);
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		Pilots[i].PrevPos = Match.Pucks[i].Pos;
		Pilots[i].Yaw = FMath::RadiansToDegrees(FMath::Atan2(Match.Pucks[i].Facing.Y, Match.Pucks[i].Facing.X));
	}
	if (Arena)
	{
		Arena->SetPlayers(Pilots.Num());
		Arena->SetRing(0, false, 0.0);
	}
	PuckMesh = BuildPuckMesh(this);
	BuildPucks();
	BuildPowerPool();
	for (FSumoSeat& S : Seats)
	{
		S.Color = ASumoArena::SlotColor(&S - Seats.GetData());
	}

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SSumoHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	}
	LastEventId = 0;
	for (const FRingEvent& E : Match.Events)
	{
		LastEventId = FMath::Max(LastEventId, E.Id);
	}
	SetStage(ESumoStage::Intro);
}

void ASumoGame::BuildPucks()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Rubber = Plain(this, Hex(TEXT("#17141f")), 0.55, 0.0);
	UMaterialInterface* Base = Plain(this, Hex(TEXT("#2b2836")), 0.35, 0.85);
	UMaterialInterface* Iron = Plain(this, Hex(TEXT("#3c3f48")), 0.35, 0.9);
	UMaterialInterface* Additive = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackAdditive.M_TrackAdditive"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	// The heavy plate: an iron ring with bolts round the shell.
	MovecadeKart::FMeshBuilder W;
	Torus(W, TEXT("Iron"), MeshRadius - 10.0, 9.0, 38.0, 40);
	UStaticMesh* WeightMesh = W.Build(this, TEXT("SumoWeight"));
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		FPuckParts& P = Parts.AddDefaulted_GetRef();
		const FLinearColor C = ASumoArena::SlotColor(i);
		P.Root = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("Puck")));
		P.Root->SetupAttachment(RootComponent);
		P.Root->SetMobility(EComponentMobility::Movable);
		P.Root->RegisterComponent();
		P.Tilt = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("PuckTilt")));
		P.Tilt->SetupAttachment(P.Root);
		P.Tilt->SetMobility(EComponentMobility::Movable);
		P.Tilt->RegisterComponent();
		P.Body = MakeMesh(P.Tilt, PuckMesh, FTransform::Identity, true);
		P.Paint = Plain(this, C * 0.85f, 0.18, 0.15, 0.06);
		P.Trim = Glow(this, C * 8.f);
		P.Body->SetMaterial(0, P.Paint);
		P.Body->SetMaterial(1, Rubber);
		P.Body->SetMaterial(2, Base);
		P.Body->SetMaterial(3, P.Trim);
		P.Weight = MakeMesh(P.Tilt, WeightMesh, FTransform::Identity, true);
		P.Weight->SetMaterial(0, Iron);
		P.Weight->SetVisibility(false);
		if (Additive && Sphere)
		{
			P.ShieldMat = UMaterialInstanceDynamic::Create(Additive, this);
			P.ShieldMat->SetVectorParameterValue(TEXT("Color"), PowerColor(EPower::Shield) * 1.4f);
			P.ShieldMat->SetScalarParameterValue(TEXT("Opacity"), 0.22f);
			P.Shield = MakeMesh(P.Root, Sphere, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 70.0 * PuckScale), FVector(2.15, 2.15, 1.75) * PuckScale), false);
			P.Shield->SetMaterial(0, P.ShieldMat);
			P.Shield->SetVisibility(false);
		}
		UArcadeAvatarComponent* A = NewObject<UArcadeAvatarComponent>(this, MakeUniqueObjectName(this, UArcadeAvatarComponent::StaticClass(), TEXT("Rider")));
		A->AvatarScale = RiderScale;
		A->SetupAttachment(P.Tilt);
		A->SetRelativeLocation(FVector(0.0, 0.0, DeckZ));
		A->RegisterComponent();
		A->SetLook(Seats[i].Look);
		P.Rider = A;
		P.Light = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("PuckLight")));
		P.Light->SetupAttachment(P.Root);
		P.Light->SetMobility(EComponentMobility::Movable);
		P.Light->SetRelativeLocation(FVector(0.0, 0.0, 30.0));
		P.Light->SetIntensityUnits(ELightUnits::Lumens);
		P.Light->SetIntensity(260.f);
		P.Light->SetAttenuationRadius(380.f);
		P.Light->SetLightColor(C);
		P.Light->SetCastShadows(false);
		P.Light->SetSourceRadius(40.f);
		P.Light->RegisterComponent();
		if (FxSys)
		{
			P.Trail = FxSys->MakeStream(Fx::EField::Fire, P.Root, MakeUniqueObjectName(this, UNiagaraComponent::StaticClass(), TEXT("DashTrail")), FVector(0.0, 0.0, 25.0));
			P.Sparks = FxSys->MakeStream(Fx::EField::Sparks, P.Root, MakeUniqueObjectName(this, UNiagaraComponent::StaticClass(), TEXT("DashSparks")), FVector(0.0, 0.0, 8.0));
		}
	}
}

void ASumoGame::BuildPowerPool()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Additive = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackAdditive.M_TrackAdditive"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	// The icons: a kettlebell (heavy), a bubble round a core (shield), a lightning bolt (turbo).
	UStaticMesh* Meshes[3] = {};
	{
		MovecadeKart::FMeshBuilder B;
		B.Ellipsoid(TEXT("Body"), FVector(0.0, 0.0, 0.0), FVector(26.0, 26.0, 24.0), 10, 16, FVector2D::ZeroVector);
		for (int32 i = 0; i < 10; ++i)
		{
			const double A0 = PI * i / 10.0, A1 = PI * (i + 1) / 10.0;
			B.Limb(TEXT("Body"), FVector(FMath::Cos(A0) * 18.0, 0.0, 18.0 + FMath::Sin(A0) * 20.0), FVector(FMath::Cos(A1) * 18.0, 0.0, 18.0 + FMath::Sin(A1) * 20.0), 5.0, 5.0, 8, FVector2D::ZeroVector);
		}
		B.Box(TEXT("Glow"), FVector(0.0, 0.0, -8.0), FVector(27.0, 27.0, 2.0), FVector2D::ZeroVector);
		Meshes[0] = B.Build(this, TEXT("SumoIconHeavy"));
	}
	{
		MovecadeKart::FMeshBuilder B;
		B.Ellipsoid(TEXT("Body"), FVector::ZeroVector, FVector(12.0, 12.0, 12.0), 8, 12, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Glow"), FVector::ZeroVector, FVector(30.0, 30.0, 30.0), 12, 20, FVector2D::ZeroVector);
		Meshes[1] = B.Build(this, TEXT("SumoIconShield"));
	}
	{
		MovecadeKart::FMeshBuilder B;
		const FVector Pts[] = { { 0.0, 10.0, 34.0 }, { 0.0, -8.0, 4.0 }, { 0.0, 6.0, 2.0 }, { 0.0, -10.0, -32.0 } };
		B.Section(TEXT("Body"));
		for (int32 i = 0; i + 1 < UE_ARRAY_COUNT(Pts); ++i)
		{
			B.Limb(TEXT("Glow"), Pts[i], Pts[i + 1], 6.0, 6.0, 8, FVector2D::ZeroVector);
		}
		Meshes[2] = B.Build(this, TEXT("SumoIconTurbo"));
	}
	UMaterialInterface* IronMat = Plain(this, Hex(TEXT("#2d3038")), 0.3, 0.9);
	const EPower Kinds[3] = { EPower::Heavy, EPower::Shield, EPower::Turbo };
	for (int32 k = 0; k < MaxPowerUps + 1; ++k)
	{
		FPowerParts& P = Powers.AddDefaulted_GetRef();
		P.Root = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("Power")));
		P.Root->SetupAttachment(RootComponent);
		P.Root->SetMobility(EComponentMobility::Movable);
		P.Root->RegisterComponent();
		for (int32 i = 0; i < 3; ++i)
		{
			UStaticMeshComponent* M = MakeMesh(P.Root, Meshes[i], FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 95.0)), true);
			M->SetMaterial(0, i == 0 ? IronMat : Glow(this, PowerColor(Kinds[i]) * 3.f));
			M->SetMaterial(1, Glow(this, PowerColor(Kinds[i]) * (i == 1 ? 1.2f : 7.f)));
			if (i == 1 && Additive)
			{
				UMaterialInstanceDynamic* Bubble = UMaterialInstanceDynamic::Create(Additive, this);
				Bubble->SetVectorParameterValue(TEXT("Color"), PowerColor(Kinds[i]) * 2.f);
				Bubble->SetScalarParameterValue(TEXT("Opacity"), 0.35f);
				M->SetMaterial(1, Bubble);
			}
			M->SetVisibility(false);
			P.Icons[i] = M;
		}
		if (Additive)
		{
			MovecadeKart::FMeshBuilder H;
			const int32 S = H.Section(TEXT("Halo"));
			const int32 Steps = 48;
			for (int32 i = 0; i < Steps; ++i)
			{
				const double A0 = 2.0 * PI * i / Steps, A1 = 2.0 * PI * (i + 1) / Steps;
				const int32 a = H.Vert(S, FVector(FMath::Cos(A0) * 40.0, FMath::Sin(A0) * 40.0, 2.0), FVector::UpVector, FVector2D(0, 0), FVector2D::ZeroVector);
				const int32 b = H.Vert(S, FVector(FMath::Cos(A0) * 62.0, FMath::Sin(A0) * 62.0, 2.0), FVector::UpVector, FVector2D(1, 0), FVector2D::ZeroVector);
				const int32 c = H.Vert(S, FVector(FMath::Cos(A1) * 62.0, FMath::Sin(A1) * 62.0, 2.0), FVector::UpVector, FVector2D(1, 1), FVector2D::ZeroVector);
				const int32 d = H.Vert(S, FVector(FMath::Cos(A1) * 40.0, FMath::Sin(A1) * 40.0, 2.0), FVector::UpVector, FVector2D(0, 1), FVector2D::ZeroVector);
				H.Tri(S, a, b, c);
				H.Tri(S, a, c, d);
			}
			P.HaloMat = UMaterialInstanceDynamic::Create(Additive, this);
			P.HaloMat->SetScalarParameterValue(TEXT("Opacity"), 0.6f);
			P.Halo = MakeMesh(P.Root, H.Build(this, TEXT("SumoHalo")), FTransform::Identity, false);
			P.Halo->SetMaterial(0, P.HaloMat);
			P.Halo->SetVisibility(false);
		}
		P.Light = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("PowerLight")));
		P.Light->SetupAttachment(P.Root);
		P.Light->SetMobility(EComponentMobility::Movable);
		P.Light->SetRelativeLocation(FVector(0.0, 0.0, 90.0));
		P.Light->SetIntensityUnits(ELightUnits::Lumens);
		P.Light->SetIntensity(300.f);
		P.Light->SetAttenuationRadius(320.f);
		P.Light->SetCastShadows(false);
		P.Light->SetVisibility(false);
		P.Light->RegisterComponent();
	}
}

void ASumoGame::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Hud.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
		}
		Hud.Reset();
	}
	Super::EndPlay(Reason);
}

bool ASumoGame::HasLocal() const
{
	return Seats.ContainsByPredicate([](const FSumoSeat& S) { return S.Kind == ESumoPilot::Local; });
}

bool ASumoGame::IsPhoneQuiet(int32 Slot) const
{
	return Pilots.IsValidIndex(Slot) && Pilots[Slot].Kind == ESumoPilot::Phone && Clock - Pilots[Slot].PhoneAt > PhoneQuietSeconds;
}

FLinearColor ASumoGame::ColorOf(int32 Slot) const
{
	return ASumoArena::SlotColor(Slot);
}

double ASumoGame::GetFightAge() const
{
	return Match.Phase == EPhase::Countdown ? -1.0 : Match.RoundMs / 1000.0;
}

bool ASumoGame::ScreenOf(const FVector& World, FVector2D& Out01) const
{
	FVector2D Pixels;
	int32 X = 0, Y = 0;
	if (!Viewer || !UGameplayStatics::ProjectWorldToScreen(Viewer, World, Pixels))
	{
		return false;
	}
	Viewer->GetViewportSize(X, Y);
	if (X <= 0 || Y <= 0)
	{
		return false;
	}
	Out01 = Pixels / FVector2D(X, Y);
	return true;
}

FVector2D ASumoGame::DrawnPos(int32 Slot, double Alpha) const
{
	return FMath::Lerp(Pilots[Slot].PrevPos, Match.Pucks[Slot].Pos, Alpha);
}

FVector ASumoGame::PuckWorld(int32 Slot) const
{
	return Parts.IsValidIndex(Slot) && Parts[Slot].Root ? Parts[Slot].Root->GetComponentLocation() : FVector::ZeroVector;
}

void ASumoGame::SetStage(ESumoStage Next)
{
	Stage = Next;
	StageAt = Clock;
	if (Next == ESumoStage::Match)
	{
		if (OnMatchStart) OnMatchStart();
		if (OnRoundStart) OnRoundStart(Match.Round);
	}
	if (Next == ESumoStage::Finished)
	{
		Cues.Play(this, TEXT("match.over"));
		const int32 W = Match.MatchWinner;
		if (Match.Pucks.IsValidIndex(W))
		{
			Callout(FString::Printf(TEXT("%s IS CHAMPION!"), *Match.Pucks[W].Name.ToUpper()), ColorOf(W), FVector::ZeroVector, true, FString(), 3.5);
			TArray<Fx::FKartFxEmit> Out;
			Fx::ConfettiBurst(PuckWorld(W) + FVector(0.0, 0.0, 250.0), FVector::ForwardVector, Out);
			if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
			{
				FxSys->Emit(Out);
			}
		}
	}
}

void ASumoGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	const int32 i = Match.IndexOf(PlayerId);
	if (!Pilots.IsValidIndex(i))
	{
		return;
	}
	FPilot& P = Pilots[i];
	// The phone held flat is level; its attitude's normal leans the way it tips: roll across, pitch away.
	double RollDeg = 0.0, PitchDeg = 0.0;
	if (Frame.bHasQuaternion)
	{
		const double X = Frame.Quaternion.X, Y = Frame.Quaternion.Y, Z = Frame.Quaternion.Z, Wq = Frame.Quaternion.W;
		const double Nx = 2.0 * (X * Z + Wq * Y);
		const double Ny = 2.0 * (Y * Z - Wq * X);
		const double Nz = 1.0 - 2.0 * (X * X + Y * Y);
		RollDeg = FMath::RadiansToDegrees(FMath::Atan2(Nx, Nz));
		PitchDeg = FMath::RadiansToDegrees(FMath::Atan2(Ny, Nz));
	}
	else
	{
		RollDeg = Frame.Gamma;
		PitchDeg = -Frame.Beta;
	}
	const FVector2D Deg(PitchDeg, RollDeg);
	const double Mag = Deg.Size();
	const double Amount = FMath::Clamp((Mag - TiltDeadDeg) / (TiltFullDeg - TiltDeadDeg), 0.0, 1.0);
	P.PhoneTilt = Mag > 1e-6 ? Deg / Mag * Amount : FVector2D::ZeroVector;
	P.PhoneAt = Clock;
}

void ASumoGame::PressDash(const FString& PlayerId)
{
	const int32 i = Match.IndexOf(PlayerId);
	if (Pilots.IsValidIndex(i) && Stage == ESumoStage::Match && !bPaused)
	{
		Pilots[i].bDashQueued = true;
	}
}

void ASumoGame::ReadLocal(FPilot& Pilot, float Dt)
{
	if (!Viewer || SArcadeOverlay::IsMenuOpen(GetWorld()))
	{
		Pilot.LocalTilt = FVector2D::ZeroVector;
		return;
	}
	FVector2D Stick(Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftY), Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftX));
	FVector2D Keys((Viewer->IsInputKeyDown(EKeys::W) || Viewer->IsInputKeyDown(EKeys::Up) ? 1.0 : 0.0) - (Viewer->IsInputKeyDown(EKeys::S) || Viewer->IsInputKeyDown(EKeys::Down) ? 1.0 : 0.0),
		(Viewer->IsInputKeyDown(EKeys::D) || Viewer->IsInputKeyDown(EKeys::Right) ? 1.0 : 0.0) - (Viewer->IsInputKeyDown(EKeys::A) || Viewer->IsInputKeyDown(EKeys::Left) ? 1.0 : 0.0));
	if (Stick.Size() < 0.15)
	{
		Stick = FVector2D::ZeroVector;
	}
	// Keys ramp in like a tilting palm instead of snapping to full.
	const FVector2D KeyTarget = Keys.IsNearlyZero() ? FVector2D::ZeroVector : Keys.GetSafeNormal();
	const FVector2D Target = Stick.IsNearlyZero() ? KeyTarget : Stick;
	Pilot.LocalTilt += (Target - Pilot.LocalTilt) * (1.0 - FMath::Exp(-14.0 * Dt));
	if (Viewer->WasInputKeyJustPressed(EKeys::SpaceBar) || Viewer->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || Viewer->WasInputKeyJustPressed(EKeys::LeftMouseButton)
		|| Viewer->WasInputKeyJustPressed(EKeys::Gamepad_RightTrigger) || Viewer->WasInputKeyJustPressed(EKeys::Enter))
	{
		Pilot.bDashQueued = true;
	}
}

void ASumoGame::StepFixed()
{
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		FPilot& P = Pilots[i];
		P.PrevPos = Match.Pucks[i].Pos;
		P.PrevVel = Match.Pucks[i].Vel;
		FInput In;
		switch (P.Kind)
		{
		case ESumoPilot::Local:
			In.Tilt = P.LocalTilt;
			break;
		case ESumoPilot::Phone:
			In.Tilt = Clock - P.PhoneAt <= PhoneQuietSeconds ? P.PhoneTilt : FVector2D::ZeroVector;
			break;
		case ESumoPilot::Bot:
			In = ThinkBot(Match, i, P.Brain);
			break;
		}
		In.bDash |= P.bDashQueued;
		P.bDashQueued = false;
		SetInput(Match, i, In);
	}
	StepMatch(Match);
	for (const FRingEvent& E : Match.Events)
	{
		if (E.Id > LastEventId)
		{
			LastEventId = E.Id;
			Present(E);
			if (OnEvent) OnEvent(E);
		}
	}
}

void ASumoGame::Callout(const FString& Text, const FLinearColor& Color, const FVector& World, bool bBig, const FString& Sub, double Duration)
{
	FSumoCallout& C = Callouts.AddDefaulted_GetRef();
	C.Text = Text;
	C.Sub = Sub;
	C.Color = Color;
	C.World = World;
	C.bCentre = World.IsZero();
	C.bBig = bBig;
	C.At = Clock;
	C.Duration = Duration;
	if (Callouts.Num() > 12)
	{
		Callouts.RemoveAt(0);
	}
}

void ASumoGame::Impact(const FVector& At, const FLinearColor& A, const FLinearColor& B, double Strength, bool bDash)
{
	const double P = FMath::Clamp(Strength / 14.0, 0.15, 1.0);
	TArray<Fx::FKartFxEmit> Out;
	for (const FLinearColor& Color : { A, B })
	{
		Fx::FKartFxEmit Sparks;
		Sparks.Field = Fx::EField::Sparks;
		Sparks.Position = At;
		Sparks.Count = FMath::RoundToInt32(6.0 + 22.0 * P) * (bDash ? 2 : 1) / 2;
		Sparks.Spread = 1.4;
		Sparks.Speed = FVector2D(3.0, 7.0 + 5.0 * P);
		Sparks.Life = FVector2D(0.25, 0.6);
		Sparks.Size = FVector2D(0.025, 0.05);
		Sparks.Color = FMath::Lerp(Color, FLinearColor::White, 0.55f);
		Sparks.ColorEnd = Color;
		Sparks.Intensity = 3.0;
		Sparks.Jitter = 0.1;
		Out.Add(Sparks);
	}
	Fx::FKartFxEmit Flash;
	Flash.Field = Fx::EField::Flare;
	Flash.Position = At;
	Flash.Speed = FVector2D::ZeroVector;
	Flash.Life = FVector2D(0.14, 0.18);
	Flash.Size = FVector2D(1.2 + 1.4 * P, 1.2 + 1.4 * P);
	Flash.SizeEnd = 0.3;
	Flash.Color = FLinearColor::White;
	Flash.ColorEnd = FMath::Lerp(A, B, 0.5f);
	Flash.Intensity = 2.6;
	Out.Add(Flash);
	if (bDash || P > 0.6)
	{
		Fx::FKartFxEmit Ring;
		Ring.Field = Fx::EField::Ring;
		Ring.Position = FVector(At.X, At.Y, 6.0);
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.32, 0.32);
		Ring.Size = FVector2D(1.0, 1.0);
		Ring.SizeEnd = 3.4;
		Ring.Color = FLinearColor::White;
		Ring.ColorEnd = FMath::Lerp(A, B, 0.5f);
		Ring.Intensity = 2.2;
		Out.Add(Ring);
	}
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		FxSys->Emit(Out);
	}
	Shake = FMath::Max(Shake, 0.25 + 0.75 * P * (bDash ? 1.0 : 0.7));
	// Hit-stop: the frame holds for a beat on a real hit (the rules simply wait, so nothing drifts).
	HitStop = FMath::Max(HitStop, bDash ? 0.085 : (P > 0.5 ? 0.05 : 0.0));
	if (bDash || P > 0.55)
	{
		Cues.Play(this, TEXT("sumo.crash"), 0.6f + 0.4f * static_cast<float>(P), 0.9f + 0.25f * FMath::FRand());
	}
	Cues.Play(this, TEXT("sumo.bump"), 0.5f + 0.5f * static_cast<float>(P), 0.8f + 0.5f * (1.f - static_cast<float>(P)) + 0.1f * FMath::FRand());
}

void ASumoGame::Present(const FRingEvent& E)
{
	const bool bA = Match.Pucks.IsValidIndex(E.A);
	const bool bB = Match.Pucks.IsValidIndex(E.B);
	const FVector At = Cm(E.Pos, 40.0);
	TArray<Fx::FKartFxEmit> Out;
	switch (E.Kind)
	{
	case EEventKind::RoundStart:
		if (Arena)
		{
			Arena->SetRing(0, false, 0.0);
		}
		Feed.Reset();
		LastCountdown = 0;
		for (int32 i = 0; i < Pilots.Num(); ++i)
		{
			Pilots[i].PrevPos = Match.Pucks[i].Pos;
			Pilots[i].FallSpin = 0.0;
		}
		if (E.Round > 1)
		{
			Cues.Play(this, TEXT("sumo.round"));
			if (OnRoundStart) OnRoundStart(E.Round);
		}
		break;
	case EEventKind::Fight:
		Cues.Play(this, TEXT("lobby.go"));
		Callout(TEXT("HAKKE-YOI!"), KartUI::Color::Sun, FVector::ZeroVector, true, FString(), 1.1);
		break;
	case EEventKind::Bump:
	case EEventKind::DashHit:
		if (bA && bB)
		{
			Impact(At, ColorOf(E.A), ColorOf(E.B), E.Strength, E.Kind == EEventKind::DashHit);
			const FVector2D Dir = (Match.Pucks[E.B].Pos - Match.Pucks[E.A].Pos).GetSafeNormal();
			Pilots[E.A].HitAt = Pilots[E.B].HitAt = Clock;
			Pilots[E.A].HitDir = -Dir;
			Pilots[E.B].HitDir = Dir;
			const double Sq = FMath::Clamp(E.Strength / 12.0, 0.1, 1.0);
			Pilots[E.A].Squash = FMath::Max(Pilots[E.A].Squash, Sq);
			Pilots[E.B].Squash = FMath::Max(Pilots[E.B].Squash, Sq);
			if (E.Kind == EEventKind::DashHit && E.Strength > 9.0)
			{
				Callout(TEXT("SMASH!"), KartUI::Color::Sun, At + FVector(0.0, 0.0, 150.0), false, FString(), 0.8);
			}
		}
		break;
	case EEventKind::ShieldBlock:
		if (bA)
		{
			Impact(At, PowerColor(EPower::Shield), bB ? ColorOf(E.B) : FLinearColor::White, FMath::Max(10.0, E.Strength), true);
			Cues.Play(this, TEXT("sumo.block"));
			Callout(TEXT("BLOCKED!"), PowerColor(EPower::Shield), PuckWorld(E.A) + FVector(0.0, 0.0, 190.0), false, FString(), 0.9);
		}
		break;
	case EEventKind::Dash:
		if (bA)
		{
			Pilots[E.A].DashAt = Clock;
			Cues.Play(this, TEXT("sumo.dash"), 0.8f, 0.95f + 0.15f * FMath::FRand());
			Fx::FKartFxEmit Ring;
			Ring.Field = Fx::EField::Ring;
			Ring.Position = Cm(E.Pos, 8.0);
			Ring.Speed = FVector2D::ZeroVector;
			Ring.Life = FVector2D(0.25, 0.25);
			Ring.Size = FVector2D(1.4, 1.4);
			Ring.SizeEnd = 2.4;
			Ring.Color = FMath::Lerp(ColorOf(E.A), FLinearColor::White, 0.4f);
			Ring.ColorEnd = ColorOf(E.A);
			Ring.Intensity = 1.6;
			Out.Add(Ring);
			Fx::FKartFxEmit Puff;
			Puff.Field = Fx::EField::Smoke;
			Puff.Position = Cm(E.Pos, 10.0);
			Puff.Count = 8;
			Puff.Spread = 1.4;
			Puff.Direction = -FVector(FMath::Cos(E.Strength), FMath::Sin(E.Strength), -0.3).GetSafeNormal();
			Puff.Speed = FVector2D(1.0, 2.5);
			Puff.Life = FVector2D(0.35, 0.6);
			Puff.Size = FVector2D(0.18, 0.3);
			Puff.SizeEnd = 1.8;
			Puff.Color = Hex(TEXT("#b9b3d9"));
			Puff.ColorEnd = Hex(TEXT("#3c3760"));
			Puff.Intensity = 0.8;
			Out.Add(Puff);
		}
		break;
	case EEventKind::RingOut:
		if (bA)
		{
			Pilots[E.A].FallSpin = (FMath::FRand() < 0.5f ? -1.0 : 1.0) * FMath::FRandRange(240.f, 520.f);
			Cues.Play(this, TEXT("sumo.fall"));
			const FString Victim = Match.Pucks[E.A].Name;
			if (bB)
			{
				Callout(TEXT("KNOCKOUT!"), ColorOf(E.B), PuckWorld(E.A) + FVector(0.0, 0.0, 160.0), true, FString::Printf(TEXT("%s  >  %s"), *Match.Pucks[E.B].Name, *Victim), 1.6);
				FSumoFeedLine& L = Feed.AddDefaulted_GetRef();
				L.Killer = E.B;
				L.Victim = E.A;
				L.At = Clock;
				Pilots[E.B].CheerAt = Clock;
				Cues.Play(this, TEXT("sumo.ko"));
			}
			else
			{
				Callout(TEXT("RING OUT!"), ColorOf(E.A), PuckWorld(E.A) + FVector(0.0, 0.0, 160.0), false, Victim, 1.4);
				FSumoFeedLine& L = Feed.AddDefaulted_GetRef();
				L.Victim = E.A;
				L.At = Clock;
			}
			Shake = FMath::Max(Shake, 0.5);
		}
		break;
	case EEventKind::Splash:
		if (bA && Arena)
		{
			const FVector Sea(E.Pos.X * 100.0, E.Pos.Y * 100.0, ASumoArena::WaterZ + 10.0);
			Cues.Play(this, TEXT("sumo.splash"));
			Fx::FKartFxEmit Spray;
			Spray.Field = Fx::EField::Sparks;
			Spray.Position = Sea;
			Spray.Count = 46;
			Spray.Spread = 0.45;
			Spray.Speed = FVector2D(5.0, 11.0);
			Spray.Life = FVector2D(0.7, 1.3);
			Spray.Size = FVector2D(0.04, 0.08);
			Spray.Color = Hex(TEXT("#e6f2ff"));
			Spray.ColorEnd = ColorOf(E.A);
			Spray.Intensity = 2.0;
			Spray.Jitter = 0.5;
			Out.Add(Spray);
			Fx::FKartFxEmit Foam;
			Foam.Field = Fx::EField::Smoke;
			Foam.Position = Sea;
			Foam.Count = 18;
			Foam.Spread = 0.7;
			Foam.Speed = FVector2D(1.5, 4.0);
			Foam.Life = FVector2D(0.8, 1.5);
			Foam.Size = FVector2D(0.5, 0.9);
			Foam.SizeEnd = 2.2;
			Foam.Color = Hex(TEXT("#d7e4ff"));
			Foam.ColorEnd = Hex(TEXT("#4b5d9c"));
			Foam.Intensity = 0.8;
			Foam.Jitter = 0.7;
			Out.Add(Foam);
			Fx::FKartFxEmit Ring;
			Ring.Field = Fx::EField::Ring;
			Ring.Position = Sea;
			Ring.Speed = FVector2D::ZeroVector;
			Ring.Life = FVector2D(0.9, 0.9);
			Ring.Size = FVector2D(1.5, 1.5);
			Ring.SizeEnd = 6.0;
			Ring.Color = Hex(TEXT("#cfe3ff"));
			Ring.ColorEnd = ColorOf(E.A);
			Ring.Intensity = 1.2;
			Out.Add(Ring);
		}
		break;
	case EEventKind::PowerSpawn:
		Cues.Play(this, TEXT("sumo.spawn"));
		break;
	case EEventKind::PowerPickup:
		if (bA)
		{
			Cues.Play(this, TEXT("sumo.power"));
			Callout(PowerName(E.Power), PowerColor(E.Power), PuckWorld(E.A) + FVector(0.0, 0.0, 190.0), false, FString(), 1.0);
			Fx::FKartFxEmit Flash;
			Flash.Field = Fx::EField::Flare;
			Flash.Position = Cm(E.Pos, 90.0);
			Flash.Count = 6;
			Flash.Spread = 1.4;
			Flash.Speed = FVector2D(1.0, 3.0);
			Flash.Life = FVector2D(0.3, 0.6);
			Flash.Size = FVector2D(0.4, 0.9);
			Flash.SizeEnd = 0.2;
			Flash.Color = FLinearColor::White;
			Flash.ColorEnd = PowerColor(E.Power);
			Flash.Intensity = 2.2;
			Out.Add(Flash);
		}
		break;
	case EEventKind::PowerExpire:
		break;
	case EEventKind::ShrinkWarning:
		Cues.Play(this, TEXT("sumo.warn"));
		Callout(E.Strength > 0.0 ? TEXT("THE RING SHRINKS!") : TEXT("THE CORE IS GOING!"), KartUI::Color::Hot, FVector::ZeroVector, false, FString(), 1.6);
		break;
	case EEventKind::Shrink:
		Cues.Play(this, TEXT("sumo.crumble"));
		Shake = FMath::Max(Shake, 0.4);
		break;
	case EEventKind::RoundWin:
		if (bA)
		{
			RoundOverAt = Clock;
			Pilots[E.A].CheerAt = Clock;
			Cues.Play(this, TEXT("sumo.win"));
			const bool bMatch = Match.Pucks[E.A].RoundWins >= WinsForMatch;
			Callout(FString::Printf(TEXT("%s TAKES ROUND %d"), *Match.Pucks[E.A].Name.ToUpper(), E.Round), ColorOf(E.A), FVector::ZeroVector, true,
				bMatch ? FString(TEXT("and the match!")) : FString::Printf(TEXT("%d of %d round wins"), Match.Pucks[E.A].RoundWins, WinsForMatch), 3.0);
			Fx::ConfettiBurst(PuckWorld(E.A) + FVector(0.0, 0.0, 220.0), FVector::ForwardVector, Out);
		}
		break;
	case EEventKind::RoundDraw:
		RoundOverAt = Clock;
		Cues.Play(this, TEXT("sumo.draw"));
		Callout(TEXT("NO CONTEST"), KartUI::Color::White, FVector::ZeroVector, true, TEXT("everyone fell: replay the round"), 3.0);
		break;
	case EEventKind::MatchOver:
		break;
	}
	if (Out.Num() > 0)
	{
		if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
		{
			FxSys->Emit(Out);
		}
	}
}

void ASumoGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	Clock += Dt;
	for (FPilot& P : Pilots)
	{
		if (P.Kind == ESumoPilot::Local)
		{
			ReadLocal(P, Dt);
		}
	}
	switch (Stage)
	{
	case ESumoStage::Intro:
		for (FPilot& P : Pilots)
		{
			P.bDashQueued = false;
		}
		if (GetStageAge() >= IntroSeconds)
		{
			SetStage(ESumoStage::Match);
		}
		break;
	case ESumoStage::Match:
		if (HitStop > 0.0)
		{
			HitStop -= Dt;
		}
		else
		{
			Accumulator += Dt * 1000.0;
			int32 Guard = 0;
			while (Accumulator >= StepMs && Guard++ < 8)
			{
				StepFixed();
				Accumulator -= StepMs;
				if (HitStop > 0.0)
				{
					break;
				}
			}
			Accumulator = FMath::Min(Accumulator, StepMs);
		}
		// The 3-2-1 ticks.
		if (Match.Phase == EPhase::Countdown)
		{
			const int32 N = FMath::CeilToInt32((CountdownMs - Match.PhaseMs) / 1000.0);
			if (N != LastCountdown && N >= 1 && N <= 3)
			{
				LastCountdown = N;
				Cues.Play(this, TEXT("lobby.tick"));
			}
		}
		if (Match.Phase == EPhase::MatchOver)
		{
			SetStage(ESumoStage::Finished);
			UE_LOG(LogSumo, Log, TEXT("Match over: %s wins after %d rounds"), Match.Pucks.IsValidIndex(Match.MatchWinner) ? *Match.Pucks[Match.MatchWinner].Name : TEXT("nobody"), Match.RoundsPlayed);
		}
		break;
	case ESumoStage::Finished:
		// The rules keep coasting the pucks to a stop under the showcase.
		Accumulator += Dt * 1000.0;
		while (Accumulator >= StepMs)
		{
			for (int32 i = 0; i < Pilots.Num(); ++i)
			{
				Pilots[i].PrevPos = Match.Pucks[i].Pos;
			}
			StepMatch(Match);
			Accumulator -= StepMs;
		}
		if (GetStageAge() >= ShowcaseSeconds)
		{
			ArcadeCommitResults();
		}
		break;
	}
	if (Arena)
	{
		const double WarnAge = Match.IsShrinkPending() ? (Match.RoundMs - (ShrinkAtMs(Match.ShrinksDone) - ShrinkWarningMs)) / 1000.0 : 0.0;
		Arena->SetRing(Match.ShrinksDone, Match.IsShrinkPending() && Match.Phase == EPhase::Fight, WarnAge);
	}
	for (int32 i = Callouts.Num() - 1; i >= 0; --i)
	{
		if (Clock - Callouts[i].At > Callouts[i].Duration)
		{
			Callouts.RemoveAt(i);
		}
	}
	const double Alpha = HitStop > 0.0 ? 1.0 : FMath::Clamp(Accumulator / StepMs, 0.0, 1.0);
	UpdatePucks(HitStop > 0.0 ? 0.f : Dt, Alpha);
	UpdatePowerUps(Dt);
	UpdateCamera(Dt);
}

void ASumoGame::UpdatePucks(float Dt, double Alpha)
{
	const bool bRoundOver = Match.Phase == EPhase::RoundOver || Match.Phase == EPhase::MatchOver;
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		FPilot& Pilot = Pilots[i];
		FPuckParts& Pt = Parts[i];
		const FPuck& Puck = Match.Pucks[i];
		const FVector2D Pos = DrawnPos(i, Alpha);
		const FLinearColor C = ColorOf(i);
		// Height: on the ring, or the fall into the sea (gravity eased to meet the splash), out of sight once in.
		double Z = 0.0;
		const double FallT = Puck.bFalling ? FMath::Clamp(Puck.FallMs / FallMs, 0.0, 1.0) : (Puck.bOut ? 1.0 : 0.0);
		Z = ASumoArena::WaterZ * FallT * FallT;
		const bool bVisible = !Puck.bOut;
		if (Pt.Root->IsVisible() != bVisible)
		{
			Pt.Root->SetVisibility(bVisible, true);
		}
		if (!bVisible)
		{
			if (Pt.Trail) UKartFxSubsystem::UpdateStream(Pt.Trail, Fx::FKartFxEmit(), 0.0);
			if (Pt.Sparks) UKartFxSubsystem::UpdateStream(Pt.Sparks, Fx::FKartFxEmit(), 0.0);
			continue;
		}
		// Yaw: the rider steers toward the tilt; without one the puck turns with its roll.
		const FVector2D Want = Puck.Input.Tilt.SizeSquared() > 0.05 ? Puck.Input.Tilt : (Puck.Vel.SizeSquared() > 0.5 ? Puck.Vel : FVector2D::ZeroVector);
		if (!Want.IsZero() && Puck.bStanding)
		{
			const double Target = FMath::RadiansToDegrees(FMath::Atan2(Want.Y, Want.X));
			Pilot.Yaw += FMath::FindDeltaAngleDegrees(Pilot.Yaw, Target) * (1.0 - FMath::Exp(-7.0 * Dt));
		}
		if (Puck.bFalling)
		{
			Pilot.Yaw += Pilot.FallSpin * Dt;
		}
		// The lean into the acceleration (the change in velocity, smoothed), a wobble after a hit.
		const FVector2D Change = Dt > 0.f ? (Puck.Vel - Pilot.PrevVel) / StepSeconds : FVector2D::ZeroVector;
		Pilot.AccelSmooth += (FVector2D(FMath::Clamp(Change.X, -40.0, 40.0), FMath::Clamp(Change.Y, -40.0, 40.0)) - Pilot.AccelSmooth) * (1.0 - FMath::Exp(-10.0 * Dt));
		const double HitAge = Clock - Pilot.HitAt;
		const double Wobble = HitAge < 0.6 ? FMath::Sin(HitAge * 32.0) * FMath::Exp(-HitAge * 6.0) * 9.0 : 0.0;
		FVector2D LeanV = Pilot.AccelSmooth * 0.35 + Pilot.HitDir * Wobble;
		if (LeanV.Size() > 12.0)
		{
			LeanV = LeanV.GetSafeNormal() * 12.0;
		}
		Pilot.Squash *= FMath::Exp(-7.0 * Dt);
		const double SqT = Pilot.Squash * FMath::Cos(HitAge * 26.0);
		const double Bob = FMath::Sin(Clock * 3.0 + i * 1.3) * 1.5;
		Pt.Root->SetWorldLocationAndRotation(Cm(Pos, Z + Bob), FRotator(0.0, Pilot.Yaw, 0.0));
		// Lean in world terms, applied in the puck's frame: pitch forward/back, roll left/right.
		const double Yr = FMath::DegreesToRadians(Pilot.Yaw);
		const double Fwd = LeanV.X * FMath::Cos(Yr) + LeanV.Y * FMath::Sin(Yr);
		const double Side = -LeanV.X * FMath::Sin(Yr) + LeanV.Y * FMath::Cos(Yr);
		const double Tumble = Puck.bFalling ? FallT * 70.0 : 0.0;
		Pt.Tilt->SetRelativeRotation(FRotator(-Fwd - Tumble, 0.0, Side + Tumble * 0.4));
		const double Heavy = (Puck.Power == EPower::Heavy ? 1.1 : 1.0) * PuckScale;
		Pt.Tilt->SetRelativeScale3D(FVector(Heavy * (1.0 + 0.16 * SqT), Heavy * (1.0 + 0.16 * SqT), PuckScale * (1.0 - 0.24 * SqT)));
		// The trim says when the dash is ready: dim while it reloads, a flash when it comes back.
		const bool bReady = Puck.DashCooldownMs <= 0.0;
		if (bReady && !Pilot.bWasReady)
		{
			Pilot.ReadyFlashAt = Clock;
		}
		Pilot.bWasReady = bReady;
		const double Flash = FMath::Max(0.0, 1.0 - (Clock - Pilot.ReadyFlashAt) / 0.35);
		const double Cool = Puck.DashCooldownMs > 0.0 ? 1.0 - Puck.DashCooldownMs / DashCooldownMs : 1.0;
		Pt.Trim->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(C * 1.2f, C * 8.f, static_cast<float>(bReady ? 1.0 : 0.3 * Cool)) + FLinearColor::White * static_cast<float>(6.0 * Flash));
		Pt.Paint->SetScalarParameterValue(TEXT("Emissive"), static_cast<float>(0.06 + 0.5 * Flash + (Puck.DashMs > 0.0 ? 0.35 : 0.0)));
		Pt.Weight->SetVisibility(Puck.Power == EPower::Heavy);
		if (Pt.Shield)
		{
			const bool bShield = Puck.Power == EPower::Shield;
			Pt.Shield->SetVisibility(bShield);
			if (bShield && Pt.ShieldMat)
			{
				const double Ending = Puck.PowerMs < 1500.0 ? (FMath::Sin(Clock * 20.0) > 0.0 ? 0.4 : 1.0) : 1.0;
				Pt.ShieldMat->SetScalarParameterValue(TEXT("Opacity"), static_cast<float>((0.16 + 0.06 * FMath::Sin(Clock * 6.0)) * Ending));
				Pt.Shield->SetRelativeRotation(FRotator(0.0, Clock * 40.0, 0.0));
			}
		}
		Pt.Light->SetIntensity(static_cast<float>(260.0 * (1.0 + 2.0 * Flash + (Puck.DashMs > 0.0 ? 2.0 : 0.0))));
		// The dash's fire trail and the sparks off the skirt; turbo keeps a small flame lit.
		const double Speed = Puck.Vel.Size();
		const FVector Back = -FVector(Puck.Vel.X, Puck.Vel.Y, 0.0).GetSafeNormal();
		if (Pt.Trail)
		{
			Fx::FKartFxEmit Fire;
			Fire.Field = Fx::EField::Fire;
			Fire.Direction = (Back + FVector(0.0, 0.0, 0.4)).GetSafeNormal();
			Fire.Spread = 0.5;
			Fire.Speed = FVector2D(1.0, 2.5);
			Fire.Life = FVector2D(0.2, 0.38);
			Fire.Size = FVector2D(0.35, 0.55);
			Fire.SizeEnd = 0.15;
			Fire.Color = FMath::Lerp(C, FLinearColor::White, 0.5f);
			Fire.ColorEnd = C;
			Fire.Intensity = 2.6;
			Fire.Jitter = 0.3;
			const double Rate = Puck.DashMs > 0.0 ? 160.0 : (Puck.Power == EPower::Turbo && Speed > 2.0 ? 30.0 : 0.0);
			UKartFxSubsystem::UpdateStream(Pt.Trail, Fire, Puck.bStanding ? Rate : 0.0);
		}
		if (Pt.Sparks)
		{
			Fx::FKartFxEmit Sp;
			Sp.Field = Fx::EField::Sparks;
			Sp.Direction = (Back + FVector(0.0, 0.0, 0.5)).GetSafeNormal();
			Sp.Spread = 0.7;
			Sp.Speed = FVector2D(2.0, 5.0);
			Sp.Life = FVector2D(0.15, 0.35);
			Sp.Size = FVector2D(0.02, 0.04);
			Sp.Color = Hex(TEXT("#fff1c4"));
			Sp.ColorEnd = C;
			Sp.Intensity = 2.5;
			Sp.Jitter = 0.4;
			UKartFxSubsystem::UpdateStream(Pt.Sparks, Sp, Puck.bStanding && (Puck.DashMs > 0.0 || Speed > 9.0) ? 70.0 : 0.0);
		}

		// The rider: a sumo crouch, leaning into the roll, both palms out in a dash (tsuppari), arms flung up as it
		// falls, a cheer for a won round and a slump for the losers at the end.
		UArcadeAvatarComponent* A = Pt.Rider;
		FArcadePoseInput& Pose = A->Pose();
		const double DashAge = Clock - Pilot.DashAt;
		const double Push = DashAge < 0.45 ? FMath::Sin(FMath::Clamp(DashAge / 0.45, 0.0, 1.0) * PI) : 0.0;
		const double Drive = FMath::Clamp(Puck.Input.Tilt.Size(), 0.0, 1.0);
		Pose.Crouch = static_cast<float>(0.42 + 0.18 * Drive + (Puck.Power == EPower::Heavy ? 0.15 : 0.0) - 0.2 * Push);
		Pose.Lean = static_cast<float>(0.12 + 0.22 * Drive + 0.45 * Push + FMath::Clamp(Fwd / 30.0, -0.25, 0.25));
		Pose.Twist = static_cast<float>(FMath::Clamp(-Side / 40.0, -0.3, 0.3) + 0.06 * FMath::Sin(Clock * 2.0 + i));
		const double Balance = 0.3 + 0.15 * FMath::Sin(Clock * 3.1 + i);
		const double Reach = Push;
		Pose.Hand[0] = FMath::Lerp(FVector(0.25, -0.75, -0.05 + 0.1 * Balance), FVector(0.98, -0.12, 0.12), Reach);
		Pose.Hand[1] = FMath::Lerp(FVector(0.25, 0.75, -0.05 + 0.1 * (1.0 - Balance)), FVector(0.98, 0.12, 0.12), Reach);
		Pose.HandWeight[0] = Pose.HandWeight[1] = 1.f;
		Pose.Look = FVector(1.0, 0.0, -0.15);
		Pose.LookWeight = 0.6f;
		Pose.Idle = 1.f;
		float Cheer = 0.f, Slump = 0.f;
		if (Puck.bFalling)
		{
			Cheer = 0.85f;
			Pose.Crouch = 0.1f;
			Pose.Lean = -0.2f;
		}
		const double CheerAge = Clock - Pilot.CheerAt;
		if (CheerAge < 1.6)
		{
			Cheer = FMath::Max(Cheer, static_cast<float>(Smooth(0.0, 0.15, CheerAge) * Smooth(1.6, 1.1, CheerAge)));
		}
		if (bRoundOver)
		{
			const bool bWinner = Match.Phase == EPhase::MatchOver ? i == Match.MatchWinner : i == Match.RoundWinner;
			if (bWinner)
			{
				Cheer = 1.f;
			}
			else if (Puck.bStanding || Match.Phase == EPhase::MatchOver)
			{
				Slump = 0.6f;
			}
		}
		Pose.Cheer = Cheer;
		Pose.Slump = Slump;
		if (Cheer > 0.3f)
		{
			Pose.HandWeight[0] = Pose.HandWeight[1] = 0.f;
			Pose.Lift = static_cast<float>(bRoundOver && !Puck.bFalling ? FMath::Max(0.0, FMath::Sin(Clock * 9.0)) * 0.12 : 0.0);
		}
		else
		{
			Pose.Lift = 0.f;
		}
		// Look at the nearest rival.
		double Best = 1e9;
		for (const FPuck& Q : Match.Pucks)
		{
			if (Q.Slot != i && Q.bStanding)
			{
				const double D = FVector2D::DistSquared(Q.Pos, Puck.Pos);
				if (D < Best)
				{
					Best = D;
					const FVector To = Cm(Q.Pos, 120.0) - A->GetComponentLocation() - FVector(0.0, 0.0, 70.0);
					Pose.Look = A->GetComponentTransform().InverseTransformVectorNoScale(To).GetSafeNormal();
					Pose.LookWeight = 0.75f;
				}
			}
		}
	}
}

void ASumoGame::UpdatePowerUps(float Dt)
{
	// Match the pool to the rules' power-ups by id; a taken or expired one shrinks away.
	for (FPowerParts& P : Powers)
	{
		if (P.Id != INDEX_NONE && P.GoneAt < 0.0 && !Match.PowerUps.ContainsByPredicate([&P](const FPowerUp& U) { return U.Id == P.Id; }))
		{
			P.GoneAt = Clock;
		}
		if (P.GoneAt >= 0.0 && Clock - P.GoneAt > 0.25)
		{
			P.Id = INDEX_NONE;
			P.GoneAt = -1.0;
		}
	}
	for (const FPowerUp& U : Match.PowerUps)
	{
		if (Powers.ContainsByPredicate([&U](const FPowerParts& P) { return P.Id == U.Id; }))
		{
			continue;
		}
		if (FPowerParts* Free = Powers.FindByPredicate([](const FPowerParts& P) { return P.Id == INDEX_NONE; }))
		{
			Free->Id = U.Id;
			Free->Kind = U.Kind;
			Free->Pos = U.Pos;
			Free->BornAt = Clock;
			Free->GoneAt = -1.0;
		}
	}
	for (FPowerParts& P : Powers)
	{
		const bool bOn = P.Id != INDEX_NONE;
		const int32 Icon = static_cast<int32>(P.Kind) - 1;
		for (int32 i = 0; i < 3; ++i)
		{
			P.Icons[i]->SetVisibility(bOn && i == Icon);
		}
		if (P.Halo) P.Halo->SetVisibility(bOn);
		P.Light->SetVisibility(bOn);
		if (!bOn)
		{
			continue;
		}
		const double Age = Clock - P.BornAt;
		const double In = KartUI::Ease::OutBack(static_cast<float>(FMath::Clamp(Age / 0.4, 0.0, 1.0)), 2.2f);
		const double Out = P.GoneAt >= 0.0 ? FMath::Clamp(1.0 - (Clock - P.GoneAt) / 0.25, 0.0, 1.0) : 1.0;
		const double S = FMath::Max(0.01, In * Out);
		const FLinearColor C = PowerColor(P.Kind);
		P.Root->SetWorldLocation(Cm(P.Pos, FMath::Sin(Clock * 2.6 + P.Id) * 12.0));
		P.Root->SetWorldScale3D(FVector(S));
		if (P.Icons[FMath::Clamp(Icon, 0, 2)])
		{
			P.Icons[FMath::Clamp(Icon, 0, 2)]->SetRelativeRotation(FRotator(0.0, Clock * 120.0, 0.0));
		}
		if (P.HaloMat)
		{
			P.HaloMat->SetVectorParameterValue(TEXT("Color"), C * 2.f);
			P.HaloMat->SetScalarParameterValue(TEXT("Opacity"), static_cast<float>(0.35 + 0.25 * FMath::Sin(Clock * 5.0)));
		}
		P.Light->SetLightColor(C);
	}
	(void)Dt;
}

void ASumoGame::UpdateCamera(float Dt)
{
	const double Aspect = [this]()
	{
		FVector2D Size(16.0, 9.0);
		if (const UGameViewportClient* V = GetWorld()->GetGameViewport())
		{
			V->GetViewportSize(Size);
		}
		return Size.Y > 0.0 ? Size.X / Size.Y : 16.0 / 9.0;
	}();
	// The play view: from the front and above, fitting the ring that's left (and closing in as it shrinks),
	// drifting a little toward the action.
	const double R = FMath::Max(3.5, Match.Radius);
	FVector2D Centroid = FVector2D::ZeroVector;
	int32 Standing = 0;
	for (const FPuck& P : Match.Pucks)
	{
		if (P.bStanding)
		{
			Centroid += P.Pos;
			++Standing;
		}
	}
	Centroid = Standing > 0 ? Centroid / Standing : FVector2D::ZeroVector;
	double Fov = 40.0;
	const double Pitch = FMath::DegreesToRadians(50.0);
	// Fit the ring's width in the horizontal field (with a margin), and its foreshortened depth in the vertical.
	const double HalfH = FMath::DegreesToRadians(Fov / 2.0);
	const double HalfW = FMath::Atan(FMath::Tan(HalfH) * Aspect);
	const double Dist = FMath::Max((R + 1.4) / FMath::Tan(HalfW), (R * FMath::Sin(Pitch) + 1.9) / FMath::Tan(HalfH));
	FVector Look(Centroid.X * 0.15 - 0.8, Centroid.Y * 0.15, -0.4);
	FVector Pos = Look + FVector(-FMath::Cos(Pitch), 0.0, FMath::Sin(Pitch)) * Dist;
	if (Stage == ESumoStage::Intro)
	{
		// In from the sea: low over the water past a tasselled post, rising to the play view.
		const double T = Smooth(0.0, 1.0, GetStageAge() / (IntroSeconds - 0.3));
		const FVector From(-24.0, -15.0, 1.5), FromLook(4.0, 2.0, 1.2);
		Pos = FMath::Lerp(From, Pos, T);
		Look = FMath::Lerp(FromLook, Look, T);
		Fov = FMath::Lerp(50.0, Fov, T);
	}
	else if (Stage == ESumoStage::Finished || ((Match.Phase == EPhase::RoundOver) && Match.Pucks.IsValidIndex(Match.RoundWinner)))
	{
		// A round's winner: push in. The champion: a slow low orbit with the moon and the torii behind.
		const int32 W = Stage == ESumoStage::Finished ? Match.MatchWinner : Match.RoundWinner;
		if (Match.Pucks.IsValidIndex(W))
		{
			const FVector At = PuckWorld(W) / 100.0 + FVector(0.0, 0.0, 0.8);
			if (Stage == ESumoStage::Finished)
			{
				const double A = -0.35 + 0.22 * FMath::Sin(GetStageAge() * 0.25) + GetStageAge() * 0.05;
				Pos = At + FVector(-FMath::Cos(A) * 6.2, FMath::Sin(A) * 6.2, 1.6);
				Look = At + FVector(0.0, 0.0, 0.3);
				Fov = 42.0;
			}
			else
			{
				const double T = Smooth(0.3, 1.4, Clock - RoundOverAt);
				Pos = FMath::Lerp(Pos, At + FVector(-7.0, 0.0, 5.5), T * 0.85);
				Look = FMath::Lerp(Look, At, T * 0.85);
			}
		}
	}
	if (!bCamPlaced)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-(Stage == ESumoStage::Intro ? 12.0 : 4.0) * FMath::Min<double>(Dt, 1.0 / 20.0));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * K;
	CamFov += (Fov - CamFov) * K;
	Shake *= FMath::Exp(-7.0 * Dt);
	const FVector Jolt(0.0, FMath::Sin(Clock * 61.0) * Shake * 0.16, FMath::Sin(Clock * 53.0 + 1.1) * Shake * 0.12);
	const FVector From = (CamPos + Jolt) * 100.0;
	const FVector To = CamLook * 100.0;
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation() + FRotator(0.0, 0.0, FMath::Sin(Clock * 47.0) * Shake * 1.2));
	const double HFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CamFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HFov));
}

void ASumoGame::Rematch()
{
	TArray<FSeed> MatchSeeds;
	for (const FSumoSeat& S : Seats)
	{
		MatchSeeds.Add({ S.Id, S.Name });
	}
	Seed = Seed * 1664525u + 1013904223u;
	Match = CreateMatch(MatchSeeds, Seed);
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		FPilot& P = Pilots[i];
		P.Brain = MakeBot(Seats[i].Skill, Seed * 2654435761u + 977u * (i + 1));
		P.PrevPos = Match.Pucks[i].Pos;
		P.PrevVel = FVector2D::ZeroVector;
		P.bDashQueued = false;
		P.LocalTilt = FVector2D::ZeroVector;
		P.CheerAt = P.HitAt = P.DashAt = -10.0;
	}
	LastEventId = 0;
	for (const FRingEvent& E : Match.Events)
	{
		LastEventId = FMath::Max(LastEventId, E.Id);
	}
	if (Arena)
	{
		Arena->SetRing(0, false, 0.0);
	}
	Callouts.Reset();
	Feed.Reset();
	Accumulator = 0.0;
	HitStop = 0.0;
	bResultsCommitted = false;
	SetStage(ESumoStage::Intro);
}

void ASumoGame::ArcadeCommitResults()
{
	if (!bResultsCommitted && Stage == ESumoStage::Finished)
	{
		bResultsCommitted = true;
		if (OnFinished)
		{
			OnFinished();
		}
	}
}

void ASumoGame::ArcadeRestart()
{
	if (OnRematch) OnRematch(); else Rematch();
}

void ASumoGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	// The match's order (round wins, then score); places shared only on a true tie.
	const TArray<int32> Order = Standings(Match);
	const int32 Base = Out.Num();
	for (int32 k = 0; k < Order.Num(); ++k)
	{
		const FPuck& P = Match.Pucks[Order[k]];
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		Row.Name = P.Name;
		Row.Color = ColorOf(P.Slot);
		Row.bHuman = Seats.IsValidIndex(P.Slot) && Seats[P.Slot].Kind != ESumoPilot::Bot;
		Row.Score = P.Score;
		const FPuck* Prev = k > 0 ? &Match.Pucks[Order[k - 1]] : nullptr;
		Row.Place = Prev && Prev->RoundWins == P.RoundWins && Prev->Score == P.Score ? Out[Base + k - 1].Place : k + 1;
	}
}

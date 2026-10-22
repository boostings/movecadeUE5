#include "Games/Derby/DerbyGame.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Derby/DerbyBallpark.h"
#include "Games/Derby/SDerbyHud.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/RoomProtocol.h"
#include "NiagaraComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDerby, Log, All);

using namespace MovecadeKart::Derby;
namespace Fx = MovecadeKart::Fx;

namespace
{
	constexpr double IntroSeconds = 3.4;
	constexpr double StepInSeconds = 2.3;
	constexpr double SetSeconds = 1.0;
	/** A phone batter gets longer to settle before the pitch (or presses READY). */
	constexpr double PhoneSetSeconds = 2.4;
	/** The swing's animation: from its start to the bat meeting the ball, and the whole swing. */
	constexpr double SwingLead = 0.11;
	constexpr double SwingSeconds = 0.62;
	/** How long a full load takes to build while the button is held. */
	constexpr double LoadSeconds = 0.55;
	/** A phone's swing arrives this much after it happened (the network), on top of the detector's own delay. */
	constexpr double PhoneLatency = 0.06;
	/** How long after the arrival a phone may still swing (its event is on the way). */
	constexpr double PhoneGrace = 0.35;
	const FVector ReleaseM(16.9, 0.32, 1.88);
	const FVector PlateM(0.25, 0.0, 0.86);
	const FVector BackstopM(-17.0, 0.0, 0.5);

	FVector Mt(const FVector& Metres) { return Metres * 100.0; }

	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** A bat along +Z: knob, handle, taper, barrel, rounded end (cm). Slot 0 wood, slot 1 grip tape. */
	UStaticMesh* BuildBat(UObject* Outer)
	{
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Wood"));
		B.Section(TEXT("Grip"));
		B.Lathe(TEXT("Grip"), { FVector2D(0.0, -2.0), FVector2D(2.2, -1.6), FVector2D(2.2, -0.6), FVector2D(1.35, 0.0), FVector2D(1.35, 18.0), FVector2D(1.4, 19.0) }, 12,
			FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		B.Lathe(TEXT("Wood"), { FVector2D(1.4, 18.5), FVector2D(1.6, 30.0), FVector2D(2.6, 50.0), FVector2D(3.3, 64.0), FVector2D(3.35, 80.0), FVector2D(3.0, 83.0),
			FVector2D(1.8, 85.0), FVector2D(0.0, 85.6) }, 14, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DerbyBat"));
	}

	/** A baseball: a sphere with two raised red seam bands (cm, radius 3.7). */
	UStaticMesh* BuildBall(UObject* Outer)
	{
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Leather"));
		B.Section(TEXT("Seam"));
		B.Ellipsoid(TEXT("Leather"), FVector::ZeroVector, FVector(3.7), 10, 16, FVector2D::ZeroVector);
		for (int32 i = 0; i < 24; ++i)
		{
			// The seam's figure-eight, as stitches.
			const double T = 2.0 * PI * i / 24.0;
			const FVector P(FMath::Cos(T) * 0.8 + 0.2 * FMath::Cos(3.0 * T), FMath::Sin(T) * 0.8 - 0.2 * FMath::Sin(3.0 * T), 0.6 * FMath::Sin(2.0 * T));
			B.Ellipsoid(TEXT("Seam"), P.GetSafeNormal() * 3.65, FVector(0.6), 3, 5, FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("DerbyBall"));
	}

	UStaticMesh* BuildGlove(UObject* Outer)
	{
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Leather"));
		B.Ellipsoid(TEXT("Leather"), FVector(0.0, 0.0, 6.0), FVector(5.0, 11.0, 13.0), 6, 10, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Leather"), FVector(0.0, 8.0, 2.0), FVector(4.0, 4.0, 7.0), 4, 8, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DerbyGlove"));
	}

	FLinearColor FireworkColor(int32 i)
	{
		const TCHAR* const C[] = { TEXT("#ff4f5e"), TEXT("#ffd34f"), TEXT("#4fd6ff"), TEXT("#b47bff"), TEXT("#5cff9d"), TEXT("#ff9a3c") };
		return MovecadeKart::Art::Hex(C[((i % 6) + 6) % 6]);
	}
}

ADerbyGame::ADerbyGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(0xba7);
}

void ADerbyGame::Setup(ADerbyBallpark* InPark, const TArray<FDerbySeat>& InSeats, APlayerController* InViewer, uint32 InSeed)
{
	Park = InPark;
	Seats = InSeats;
	Viewer = InViewer;
	Seed = InSeed;
	TArray<FString> Order;
	for (const FDerbySeat& S : Seats)
	{
		Order.Add(S.Id);
	}
	Match = CreateMatch(Order, Seed);
	UE_LOG(LogDerby, Log, TEXT("Derby: %d batter(s), seed %u"), Seats.Num(), Seed);

	using namespace MovecadeKart::Art;
	auto Mesh = [this](const TCHAR* Name, UStaticMesh* M, bool bShadows)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name));
		C->SetStaticMesh(M);
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(bShadows);
		C->RegisterComponent();
		return C;
	};
	Ball = Mesh(TEXT("Ball"), BuildBall(this), true);
	Ball->SetMaterial(0, Plain(this, Hex(TEXT("#f6f3ea")), 0.55, 0.0, 0.35));
	Ball->SetMaterial(1, Plain(this, Hex(TEXT("#c8202b")), 0.6));
	// Read from the stands: a baseball twice its size.
	Ball->SetWorldScale3D(FVector(1.9));
	Bat = Mesh(TEXT("Bat"), BuildBat(this), true);
	Bat->SetMaterial(0, Plain(this, Hex(TEXT("#c9935a")), 0.35, 0.0));
	Bat->SetMaterial(1, Plain(this, Hex(TEXT("#1c1c22")), 0.8));
	Glove = Mesh(TEXT("Glove"), BuildGlove(this), true);
	Glove->SetMaterial(0, Plain(this, Hex(TEXT("#7a4524")), 0.7));

	Batter = NewObject<UArcadeAvatarComponent>(this, TEXT("Batter"));
	Batter->AvatarScale = 0.86f;
	Batter->SetupAttachment(RootComponent);
	Batter->RegisterComponent();
	Pitcher = NewObject<UArcadeAvatarComponent>(this, TEXT("Pitcher"));
	Pitcher->AvatarScale = 0.88f;
	Pitcher->SetupAttachment(RootComponent);
	Pitcher->RegisterComponent();
	FKartLook PitcherLook = FKartLook::MakeVaried(11);
	Pitcher->SetLook(PitcherLook);
	Pitcher->SetWorldLocationAndRotation(Mt(FVector(ADerbyBallpark::MoundX + 0.15, 0.0, 0.33)), FRotator(0.0, 180.0, 0.0));
	Batter->SetWorldLocationAndRotation(ADerbyBallpark::BatterSpot() + FVector(0.0, 0.0, 9.0), FRotator(0.0, 90.0, 0.0));
	if (Seats.Num() > 0)
	{
		Batter->SetLook(Seats[0].Look);
		DressedFor = Seats[0].Id;
	}
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		Trail = FxSys->MakeStream(Fx::EField::Fire, nullptr, TEXT("DerbyTrail"));
	}

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SDerbyHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	}
	SetPhase(EDerbyPhase::Intro);
	UpdateBoard();
}

void ADerbyGame::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Hud.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
		}
		Hud.Reset();
	}
	if (Trail)
	{
		Trail->DestroyComponent();
		Trail = nullptr;
	}
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	Super::EndPlay(Reason);
}

const FDerbySeat* ADerbyGame::SeatOf(const FString& Id) const
{
	return Seats.FindByPredicate([&Id](const FDerbySeat& S) { return S.Id == Id; });
}

const FDerbySeat* ADerbyGame::GetBatter() const
{
	return Seats.IsValidIndex(ShownUp) ? &Seats[ShownUp] : nullptr;
}

const FBatter* ADerbyGame::GetBatterStats() const
{
	return Match.Batters.IsValidIndex(ShownUp) ? &Match.Batters[ShownUp] : nullptr;
}

bool ADerbyGame::HasLocal() const
{
	return Seats.ContainsByPredicate([](const FDerbySeat& S) { return S.Kind == EDerbyKind::Local; });
}

bool ADerbyGame::ScreenOf(const FVector& World, FVector2D& Out01) const
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

bool ADerbyGame::GetLanding(FVector& Out) const
{
	if (!bHasOutcome || !bLanded || Outcome.Flight.Path.Num() == 0)
	{
		return false;
	}
	Out = HitBall(Outcome.Flight.HangS);
	return true;
}

FVector ADerbyGame::GetBallWorld() const
{
	return Ball ? Ball->GetComponentLocation() : FVector::ZeroVector;
}

void ADerbyGame::SetPhase(EDerbyPhase Next)
{
	const EDerbyPhase Was = Phase;
	Phase = Next;
	PhaseAt = Clock;
	// Broadcast cuts: the hero shot, back to the plate after a hit or a turn, the finale.
	if (Next == EDerbyPhase::StepIn || Next == EDerbyPhase::Finished || (Next == EDerbyPhase::Set && (Was == EDerbyPhase::StepIn || (Was == EDerbyPhase::Result && bHasOutcome && Outcome.Flight.Path.Num() > 0))))
	{
		bCamPlaced = false;
	}
	if (Next == EDerbyPhase::Finished)
	{
		Cues.Play(this, TEXT("match.over"));
		if (Park)
		{
			Park->Roar(0.8f);
		}
		// The winner takes the plate for the finale.
		const TArray<int32> Rank = Ranking(Match);
		if (Rank.Num() > 0 && Seats.IsValidIndex(Rank[0]))
		{
			ShownUp = Rank[0];
			Batter->SetLook(Seats[Rank[0]].Look);
			DressedFor = Seats[Rank[0]].Id;
		}
		Fireworks(6, 0.6);
		UpdateBoard();
		ArcadeCommitResults();
	}
}

void ADerbyGame::BeginTurn()
{
	if (Match.bComplete)
	{
		SetPhase(EDerbyPhase::Finished);
		UE_LOG(LogDerby, Log, TEXT("Derby over"));
		return;
	}
	ShownUp = Match.Up;
	ShownPitchCount = 0;
	ShownStrikeCount = 0;
	bHasOutcome = false;
	bHasPitch = false;
	Local = FDerbyLocal { Local.Aim };
	if (const FDerbySeat* Up = GetBatter())
	{
		if (Up->Id != DressedFor)
		{
			DressedFor = Up->Id;
			Batter->SetLook(Up->Look);
		}
		if (Up->Id != AnnouncedBatter)
		{
			AnnouncedBatter = Up->Id;
			if (OnTurn)
			{
				OnTurn(Up->Id);
			}
		}
	}
	Cues.Play(this, TEXT("derby.stepin"));
	SetPhase(EDerbyPhase::StepIn);
	UpdateBoard();
}

void ADerbyGame::BeginPitch()
{
	const FPitch* Next = Match.NextPitch();
	if (!Next)
	{
		BeginTurn();
		return;
	}
	Pitch = *Next;
	bHasPitch = true;
	bHasOutcome = false;
	bSwingTaken = false;
	bResolved = false;
	bLanded = false;
	bBotSwings = false;
	SwingAnimAt = -100.0;
	SlowMo = 1.0;
	bReady = false;
	SetPhase(EDerbyPhase::Windup);
}

void ADerbyGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	FSwingDetector& D = Detectors.FindOrAdd(PlayerId);
	const TOptional<FSwingEvent> E = D.Feed(Frame);
	const FDerbySeat* Up = GetBatter();
	if (!Up || Up->Id != PlayerId || Up->Kind != EDerbyKind::Phone)
	{
		return;
	}
	PhoneCharge = D.Charge() > PhoneCharge ? D.Charge() : FMath::Lerp(PhoneCharge, D.Charge(), 0.15);
	if (!E.IsSet() || bPaused)
	{
		return;
	}
	const double PeakAt = Clock - (D.ClockMs() - E->PeakAtMs) / 1000.0 - PhoneLatency;
	UE_LOG(LogDerby, Log, TEXT("Phone swing %s: %.0f dps, strength %.2f, direction %.2f (%s), %.0f ms ago"), *PlayerId, E->PeakDps, E->Strength, E->Direction,
		E->Handed > 0.0 ? TEXT("R") : TEXT("L"), (Clock - PeakAt) * 1000.0);
	if (Phase == EDerbyPhase::Pitch && !bSwingTaken)
	{
		SubmitSwing(PeakAt, E->Direction, FMath::Max(0.25, E->Strength), SwingLead);
	}
	else if (Phase == EDerbyPhase::Set || Phase == EDerbyPhase::Windup || Phase == EDerbyPhase::StepIn)
	{
		// A practice swing: the batter takes it too, nothing counts.
		SwingAnimAt = Clock - SwingLead;
	}
}

void ADerbyGame::PressButton(const FString& PlayerId)
{
	const FDerbySeat* Up = GetBatter();
	if (!Up || Up->Id != PlayerId || bPaused)
	{
		return;
	}
	if (Phase == EDerbyPhase::Set || Phase == EDerbyPhase::StepIn)
	{
		bReady = true;
	}
	else if (Phase == EDerbyPhase::Pitch && !bSwingTaken)
	{
		// No gyro, or no faith in it: the button swings, a little after the press.
		SubmitSwing(Clock + 0.07, 0.0, 0.7, SwingLead);
	}
}

void ADerbyGame::SubmitSwing(double PeakAt, double Aim, double Strength, double AnimLead)
{
	bSwingTaken = true;
	SwingPeakAt = PeakAt;
	SwingAim = Aim;
	SwingStrength = Strength;
	SwingAnimAt = PeakAt - AnimLead;
	Cues.Play(this, TEXT("derby.swing"), 0.6f + 0.4f * static_cast<float>(Strength), 0.9f + 0.25f * static_cast<float>(Strength));
}

void ADerbyGame::Resolve(const FSwing& Swing, double InContactAt)
{
	bResolved = true;
	Outcome = ResolvePitch(Pitch, Swing, Match.Strikes);
	bHasOutcome = true;
	OutcomeAt = Clock;
	const FDerbySeat* Up = GetBatter();
	UE_LOG(LogDerby, Log, TEXT("Pitch %d (%s %.0f mph) to %s: %s timing %+.0f ms, aim %.2f, strength %.2f -> %s %d ft, %d pts"), Pitch.Index + 1, PitchName(Pitch.Type),
		Pitch.SpeedMph, Up ? *Up->Name : TEXT("?"), Swing.bSwung ? TEXT("swing") : TEXT("take"), Swing.TimingMs, Swing.Aim, Swing.Strength, ResultName(Outcome.Result),
		Outcome.DistanceFt, Outcome.Points);
	const bool bContact = Outcome.Result == EResult::Foul || Outcome.Result == EResult::InPlay || IsHomeRun(Outcome.Result);
	if (!bContact)
	{
		if (Outcome.Result == EResult::Whiff)
		{
			Cues.Play(this, TEXT("derby.whiff"));
		}
		// The ball goes on to the backstop; the call comes as it gets there.
		ContactAt = Clock;
		return;
	}
	ContactAt = InContactAt;
	FlightClock = FMath::Max(0.0, Clock - InContactAt);
	SlowMo = 1.0;
	bLanded = false;
	HitStop = Outcome.Contact.Quality > 0.85 ? 0.08 : 0.04;
	ContactBurst(Outcome);
	SetPhase(EDerbyPhase::Flight);
}

void ADerbyGame::ContactBurst(const FOutcome& Hit)
{
	const float Q = static_cast<float>(Hit.Contact.Quality);
	const bool bBig = IsHomeRun(Hit.Result) || (Hit.Result == EResult::InPlay && Hit.Contact.ExitMph > 95.0);
	Cues.Play(this, TEXT("derby.crack"), 0.6f + 0.5f * Q, 0.85f + 0.35f * Q);
	if (Hit.Result == EResult::Moonshot || (IsHomeRun(Hit.Result) && Q > 0.9f))
	{
		Cues.Play(this, TEXT("derby.sweet"), 0.9f, 1.f);
	}
	Shake = FMath::Max(Shake, 0.25 + 0.5 * Q);
	if (Park)
	{
		Park->Roar(bBig ? 0.7f : 0.35f);
	}
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		const FVector At = Mt(PlateM);
		const double A = FMath::DegreesToRadians(Hit.Contact.SprayDeg);
		const FVector Dir = FVector(FMath::Cos(A), FMath::Sin(A), FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Hit.Contact.LaunchDeg, -10.0, 60.0)))).GetSafeNormal();
		TArray<Fx::FKartFxEmit> Out;
		Fx::FKartFxEmit Sparks;
		Sparks.Field = Fx::EField::Sparks;
		Sparks.Position = At;
		Sparks.Direction = Dir;
		Sparks.Count = FMath::RoundToInt32(10.f + 26.f * Q);
		Sparks.Spread = 0.7;
		Sparks.Speed = FVector2D(3.0, 8.0);
		Sparks.Life = FVector2D(0.2, 0.45);
		Sparks.Size = FVector2D(0.02, 0.04);
		Sparks.Color = FLinearColor::White;
		Sparks.ColorEnd = MovecadeKart::Art::Hex(TEXT("#ffd34f"));
		Sparks.Intensity = 3.0;
		Out.Add(Sparks);
		Fx::FKartFxEmit Flash;
		Flash.Field = Fx::EField::Flare;
		Flash.Position = At;
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.14, 0.14);
		Flash.Size = FVector2D(0.6 + 0.9 * Q, 0.6 + 0.9 * Q);
		Flash.SizeEnd = 0.3;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = MovecadeKart::Art::Hex(TEXT("#ffd34f"));
		Flash.Intensity = 3.0;
		Out.Add(Flash);
		if (Q > 0.85f)
		{
			Fx::FKartFxEmit Ring;
			Ring.Field = Fx::EField::Ring;
			Ring.Position = At;
			Ring.Direction = Dir;
			Ring.Speed = FVector2D::ZeroVector;
			Ring.Life = FVector2D(0.3, 0.3);
			Ring.Size = FVector2D(0.5, 0.5);
			Ring.SizeEnd = 3.0;
			Ring.Color = FLinearColor::White;
			Ring.ColorEnd = MovecadeKart::Art::Hex(TEXT("#ffd34f"));
			Ring.Intensity = 2.0;
			Out.Add(Ring);
		}
		FxSys->Emit(Out);
	}
}

void ADerbyGame::Land()
{
	bLanded = true;
	const FVector At = HitBall(Outcome.Flight.HangS);
	TArray<Fx::FKartFxEmit> Out;
	switch (Outcome.Result)
	{
	case EResult::Moonshot:
		Fireworks(10, 0.0);
		Cues.Play(this, TEXT("derby.homer"), 1.1f, 1.08f);
		Cues.Play(this, TEXT("derby.moonshot"));
		if (Park) Park->Roar(1.f);
		break;
	case EResult::HomeRun:
		Fireworks(5, 0.0);
		Cues.Play(this, TEXT("derby.homer"));
		if (Park) Park->Roar(1.f);
		break;
	case EResult::InPlay:
	{
		Cues.Play(this, Outcome.Flight.bOffWall ? TEXT("derby.wall") : TEXT("derby.land"));
		if (Park) Park->Roar(Outcome.Flight.bOffWall ? 0.6f : 0.3f);
		Fx::FKartFxEmit Dust;
		Dust.Field = Fx::EField::Smoke;
		Dust.Position = At;
		Dust.Count = 14;
		Dust.Spread = 1.2;
		Dust.Speed = FVector2D(0.6, 1.6);
		Dust.Life = FVector2D(0.6, 1.0);
		Dust.Size = FVector2D(0.4, 0.7);
		Dust.SizeEnd = 2.0;
		Dust.Color = MovecadeKart::Art::Hex(TEXT("#b08a63"));
		Dust.ColorEnd = MovecadeKart::Art::Hex(TEXT("#6d5a44"));
		Dust.Intensity = 0.6;
		Out.Add(Dust);
		break;
	}
	default:
		Cues.Play(this, TEXT("derby.foul"));
		if (Park) Park->Roar(0.25f);
		break;
	}
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this); FxSys && Out.Num() > 0)
	{
		FxSys->Emit(Out);
	}
}

void ADerbyGame::Fireworks(int32 Bursts, double Delay)
{
	if (!Park)
	{
		return;
	}
	TArray<FVector> Spots;
	Park->FireworkSpots(Spots);
	for (int32 i = 0; i < Bursts && Spots.Num() > 0; ++i)
	{
		const FVector Jitter(BotRng.FRandRange(-1500.f, 1500.f), BotRng.FRandRange(-1500.f, 1500.f), BotRng.FRandRange(-600.f, 1200.f));
		PendingFireworks.Add(TPair<double, FVector>(Clock + Delay + i * 0.32 + BotRng.FRandRange(0.f, 0.15f), Spots[(i * 3 + Rematches) % Spots.Num()] + Jitter));
	}
}

void ADerbyGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	float Dt = FMath::Min(DeltaSeconds, 0.1f);
	// Hit-stop: the frame of contact holds a beat.
	if (HitStop > 0.0)
	{
		HitStop -= Dt;
		Dt *= 0.08f;
	}
	Clock += Dt;
	const double Age = GetPhaseAge();
	const FDerbySeat* Up = GetBatter();
	UpdateLocal(Dt);
	switch (Phase)
	{
	case EDerbyPhase::Intro:
		if (Age >= IntroSeconds)
		{
			BeginTurn();
		}
		break;
	case EDerbyPhase::StepIn:
		if (Age >= StepInSeconds)
		{
			SetPhase(EDerbyPhase::Set);
		}
		break;
	case EDerbyPhase::Set:
	{
		const bool bPhone = Up && Up->Kind == EDerbyKind::Phone;
		if (Age >= (bPhone ? PhoneSetSeconds : SetSeconds) || (bReady && Age >= 0.5))
		{
			BeginPitch();
		}
		break;
	}
	case EDerbyPhase::Windup:
		if (Age >= Pitch.WindupMs / 1000.0)
		{
			PitchReleasedAt = Clock;
			SetPhase(EDerbyPhase::Pitch);
			Cues.Play(this, TEXT("derby.pitch"), 0.8f, Pitch.Type == EPitchType::Fastball ? 1.1f : 0.92f);
			if (Up && OnPitch)
			{
				OnPitch(Up->Id, Pitch);
			}
			UpdateBot();
		}
		break;
	case EDerbyPhase::Pitch:
	{
		const double Arrive = ArriveAt();
		if (!bResolved && bSwingTaken && Clock >= SwingPeakAt)
		{
			FSwing S;
			S.bSwung = true;
			S.TimingMs = (SwingPeakAt - Arrive) * 1000.0;
			S.Aim = SwingAim;
			S.Strength = SwingStrength;
			Resolve(S, SwingPeakAt);
			break;
		}
		const double Grace = Up && Up->Kind == EDerbyKind::Phone ? PhoneGrace : 0.02;
		if (!bResolved && !bSwingTaken && Clock > Arrive + Pitch.ContactMs / 1000.0 + Grace)
		{
			Resolve(FSwing(), Clock);
		}
		// A take or a whiff: the call as the ball reaches the backstop (or the catcher's spot).
		if (bResolved && Clock >= Arrive + 0.32)
		{
			Cues.Play(this, TEXT("derby.mitt"));
			FRecord R;
			const int32 Before = Match.Strikes;
			R = Record(Match, Outcome);
			ShownPitchCount += 1;
			ShownStrikeCount = Before + (Outcome.bStrike ? 1 : 0);
			if (OnOutcome && Up) OnOutcome(Up->Id, Outcome);
			if (R.bStruckOut)
			{
				Cues.Play(this, TEXT("derby.out"));
			}
			SetPhase(EDerbyPhase::Result);
			UpdateBoard();
		}
		break;
	}
	case EDerbyPhase::Flight:
	{
		// Moonshots hang in slow motion through the top of the arc.
		const double T = Outcome.Flight.HangS > 0.0 ? FlightClock / Outcome.Flight.HangS : 1.0;
		const double Want = Outcome.Result == EResult::Moonshot && T > 0.12 && T < 0.82 ? 0.38 : 1.0;
		SlowMo += (Want - SlowMo) * (1.0 - FMath::Exp(-6.0 * Dt));
		FlightClock += Dt * SlowMo;
		if (FlightClock >= Outcome.Flight.HangS)
		{
			FlightClock = Outcome.Flight.HangS;
			SlowMo = 1.0;
			Land();
			const int32 Before = Match.Strikes;
			const FRecord R = Record(Match, Outcome);
			ShownPitchCount += 1;
			ShownStrikeCount = Before + (Outcome.bStrike ? 1 : 0);
			if (OnOutcome && Up) OnOutcome(Up->Id, Outcome);
			if (R.bStruckOut)
			{
				Cues.Play(this, TEXT("derby.out"));
			}
			SetPhase(EDerbyPhase::Result);
			UpdateBoard();
		}
		break;
	}
	case EDerbyPhase::Result:
	{
		double Hold = 1.4;
		switch (Outcome.Result)
		{
		case EResult::Moonshot: Hold = 4.2; break;
		case EResult::HomeRun: Hold = 3.2; break;
		case EResult::InPlay: Hold = 2.2; break;
		case EResult::Foul: Hold = 1.5; break;
		default: break;
		}
		const bool bTurnOver = Match.bComplete || Match.Up != ShownUp || (Match.Pitch == 0 && ShownPitchCount > 0);
		if (Age >= Hold + (bTurnOver ? 0.9 : 0.0))
		{
			if (bTurnOver)
			{
				BeginTurn();
			}
			else
			{
				SetPhase(EDerbyPhase::Set);
			}
		}
		break;
	}
	case EDerbyPhase::Finished:
		break;
	}
	// Fireworks due.
	for (int32 i = PendingFireworks.Num() - 1; i >= 0; --i)
	{
		if (Clock < PendingFireworks[i].Key)
		{
			continue;
		}
		const FVector At = PendingFireworks[i].Value;
		PendingFireworks.RemoveAt(i);
		const int32 Hue = BotRng.RandRange(0, 5);
		TArray<Fx::FKartFxEmit> Out;
		Fx::FKartFxEmit Burst;
		Burst.Field = Fx::EField::Sparks;
		Burst.Position = At;
		Burst.Count = 90;
		Burst.Spread = PI;
		Burst.Speed = FVector2D(9.0, 15.0);
		Burst.Life = FVector2D(1.1, 1.9);
		Burst.Size = FVector2D(0.35, 0.6);
		Burst.SizeEnd = 0.25;
		Burst.Color = FMath::Lerp(FireworkColor(Hue), FLinearColor::White, 0.35f);
		Burst.ColorEnd = FireworkColor(Hue + 1);
		Burst.Intensity = 7.0;
		Burst.Jitter = 0.6;
		Out.Add(Burst);
		Fx::FKartFxEmit Flash;
		Flash.Field = Fx::EField::Flare;
		Flash.Position = At;
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.3, 0.3);
		Flash.Size = FVector2D(14.0, 14.0);
		Flash.SizeEnd = 0.3;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = FireworkColor(Hue);
		Flash.Intensity = 5.0;
		Out.Add(Flash);
		Fx::FKartFxEmit Glitter;
		Glitter.Field = Fx::EField::Flare;
		Glitter.Position = At;
		Glitter.Count = 26;
		Glitter.Spread = PI;
		Glitter.Speed = FVector2D(4.0, 8.0);
		Glitter.Life = FVector2D(1.4, 2.4);
		Glitter.Size = FVector2D(0.5, 0.9);
		Glitter.SizeEnd = 0.1;
		Glitter.Color = FLinearColor::White;
		Glitter.ColorEnd = FireworkColor(Hue + 2);
		Glitter.Intensity = 4.0;
		Glitter.Jitter = 2.0;
		Out.Add(Glitter);
		if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
		{
			FxSys->Emit(Out);
		}
		Cues.Play(this, TEXT("derby.firework"), 0.5f + BotRng.FRand() * 0.3f, 0.8f + BotRng.FRand() * 0.4f);
	}
	// The crowd's murmur under everything (the cue's throttle paces it).
	Cues.Play(this, TEXT("derby.crowd"), Park ? 0.8f + 0.6f * static_cast<float>(Phase == EDerbyPhase::Result) : 1.f);
	UpdateBall(Dt);
	UpdatePitcher(Dt);
	UpdateBatter(Dt);
	UpdateCamera(Dt);
}

void ADerbyGame::UpdateLocal(float Dt)
{
	const FDerbySeat* Up = GetBatter();
	if (!Viewer || !Up || Up->Kind != EDerbyKind::Local || Phase == EDerbyPhase::Finished || Phase == EDerbyPhase::Intro)
	{
		Local.bLoading = false;
		return;
	}
	APlayerController* PC = Viewer;
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K); };
	const FKey SwingKeys[] = { EKeys::SpaceBar, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton, EKeys::Enter };
	bool bDown = false;
	for (const FKey& K : SwingKeys)
	{
		bDown |= Down(K);
	}
	// Aim: the mouse, A/D or the arrows, either stick. Left pulls, right goes the other way.
	float MouseX = 0.f, MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	double Steer = (Down(EKeys::D) || Down(EKeys::Right) ? 1.0 : 0.0) - (Down(EKeys::A) || Down(EKeys::Left) ? 1.0 : 0.0);
	const double Stick = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX) + PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	if (FMath::Abs(Stick) > 0.15)
	{
		Local.Aim = FMath::FInterpTo(Local.Aim, FMath::Clamp(Stick, -1.0, 1.0), Dt, 8.0);
	}
	Local.Aim = FMath::Clamp(Local.Aim + Steer * 1.6 * Dt + MouseX * 0.004, -1.0, 1.0);
	const bool bSwinging = Clock - SwingAnimAt < SwingSeconds;
	if (bDown && !Local.bLoading && !bSwinging)
	{
		Local.bLoading = true;
		Local.LoadAt = Clock;
	}
	if (Local.bLoading)
	{
		Local.Load = FMath::Clamp((Clock - Local.LoadAt) / LoadSeconds, 0.0, 1.0);
	}
	if (Local.bLoading && !bDown)
	{
		Local.bLoading = false;
		const double Strength = 0.3 + 0.7 * Local.Load;
		Local.Load = 0.0;
		if (Phase == EDerbyPhase::Pitch && !bSwingTaken)
		{
			SubmitSwing(Clock + SwingLead, Local.Aim, Strength, SwingLead);
		}
		else if (Phase != EDerbyPhase::Pitch && Phase != EDerbyPhase::Flight)
		{
			// Too soon: a practice cut while the pitcher gets ready.
			SwingAnimAt = Clock;
		}
	}
}

void ADerbyGame::UpdateBot()
{
	const FDerbySeat* Up = GetBatter();
	if (!Up || Up->Kind != EDerbyKind::Bot)
	{
		return;
	}
	const double S = FMath::Clamp(static_cast<double>(Up->Skill), 0.0, 1.0);
	bBotSwings = BotRng.FRand() < 0.96;
	if (!bBotSwings)
	{
		return;
	}
	// Timing error: wider for a shaky hitter and for the curve; the changeup fools them early.
	const double U1 = FMath::Max(1e-6, static_cast<double>(BotRng.FRand()));
	const double U2 = BotRng.FRand();
	const double Gauss = FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * PI * U2);
	const double Sigma = 16.0 + 90.0 * (1.0 - S) + (Pitch.Type == EPitchType::Curve ? 18.0 : 0.0);
	const double Mean = Pitch.Type == EPitchType::Changeup ? -30.0 * (1.0 - S) : 0.0;
	BotTiming = Mean + Gauss * Sigma;
	BotAim = BotRng.FRandRange(-0.45f, 0.45f);
	BotStrength = FMath::Clamp(0.5 + 0.45 * S + BotRng.FRandRange(-0.15f, 0.12f), 0.3, 1.0);
	SubmitSwing(ArriveAt() + BotTiming / 1000.0, BotAim, BotStrength, SwingLead);
}

FVector ADerbyGame::PitchBall(double T) const
{
	const double Flight = Pitch.FlightMs / 1000.0;
	const double S = Flight > 0.0 ? T / Flight : 1.0;
	const double Arc = Pitch.Type == EPitchType::Fastball ? 0.12 : (Pitch.Type == EPitchType::Changeup ? 0.28 : 0.55);
	auto At = [this, Arc](double U)
	{
		FVector P = FMath::Lerp(ReleaseM, PlateM, U);
		P.Z += Arc * 4.0 * U * (1.0 - U);
		// The break comes late: away from a right-handed batter and down.
		P.Y += 0.42 * Pitch.Break * U * U * U;
		P.Z -= 0.3 * Pitch.Break * U * U * U;
		return P;
	};
	if (S <= 1.0)
	{
		return Mt(At(FMath::Max(0.0, S)));
	}
	// Past the plate: on along its last direction to the backstop, dropping.
	const FVector End = At(1.0);
	const FVector Vel = (At(1.0) - At(0.97)) / (0.03 * Flight);
	FVector P = End + Vel * (T - Flight);
	if (P.X < BackstopM.X)
	{
		P.X = BackstopM.X;
	}
	P.Z = FMath::Max(0.1, End.Z - 0.8 * (T - Flight));
	return Mt(P);
}

FVector ADerbyGame::HitBall(double T) const
{
	const FVector2D P = Outcome.Flight.At(T);
	const double A = FMath::DegreesToRadians(Outcome.Contact.SprayDeg);
	const FVector From = Mt(FVector(PlateM.X, PlateM.Y, 0.0));
	return From + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * P.X * 100.0 + FVector(0.0, 0.0, P.Y * 100.0);
}

void ADerbyGame::UpdateBall(float Dt)
{
	FVector At;
	bool bTrail = false;
	bool bShow = true;
	switch (Phase)
	{
	case EDerbyPhase::Pitch:
	case EDerbyPhase::Result:
		if (bHasOutcome && Outcome.Flight.Path.Num() > 0)
		{
			At = HitBall(Outcome.Flight.HangS);
			bShow = Phase != EDerbyPhase::Result || GetPhaseAge() < 1.8;
		}
		else if (bHasPitch)
		{
			At = PitchBall(Clock - PitchReleasedAt);
			bTrail = Phase == EDerbyPhase::Pitch;
			bShow = Phase == EDerbyPhase::Pitch || GetPhaseAge() < 0.6;
		}
		else
		{
			bShow = false;
		}
		break;
	case EDerbyPhase::Flight:
		At = HitBall(FlightClock);
		bTrail = true;
		break;
	default:
		// In the pitcher's throwing hand.
		At = Pitcher->BoneWorld(TEXT("rightHand")) + FVector(-4.0, 0.0, 0.0);
		break;
	}
	Ball->SetVisibility(bShow);
	Ball->SetWorldLocationAndRotation(At, FRotator(Clock * -1400.0, Clock * 300.0, 0.0));
	if (Trail)
	{
		Trail->SetWorldLocation(At);
		const FDerbySeat* Up = GetBatter();
		Fx::FKartFxEmit O;
		O.Field = Fx::EField::Fire;
		O.Direction = FVector::UpVector;
		O.Spread = 0.3;
		O.Speed = FVector2D(0.0, 0.15);
		const bool bHit = Phase == EDerbyPhase::Flight;
		O.Life = bHit ? FVector2D(0.45, 0.7) : FVector2D(0.12, 0.18);
		O.Size = bHit ? FVector2D(0.22, 0.3) : FVector2D(0.07, 0.1);
		O.SizeEnd = 0.15;
		O.Color = FLinearColor::White;
		O.ColorEnd = Up ? Up->Color : FLinearColor(1.f, 0.8f, 0.3f);
		O.Intensity = bHit ? 3.5 : 1.6;
		UKartFxSubsystem::UpdateStream(Trail, O, bTrail && bShow ? (bHit ? 220.0 : 90.0) : 0.0);
	}
	(void)Dt;
}

void ADerbyGame::UpdatePitcher(float Dt)
{
	FArcadePoseInput& Pose = Pitcher->Pose();
	Pose.Idle = 1.f;
	Pose.HandWeight[0] = Pose.HandWeight[1] = 1.f;
	Pose.Lunge = 0.f;
	Pose.Lift = 0.f;
	Pose.Step = 0.f;
	Pose.Cheer = 0.f;
	Pose.Slump = 0.f;
	// Hands together at the chest (the set), both relative to their shoulders, in reach.
	FVector Glv(0.45, 0.32, -0.38), Throw(0.45, -0.3, -0.42);
	float Crouch = 0.12f, Lean = 0.12f, Twist = 0.f, Lunge = 0.f;
	if (Phase == EDerbyPhase::Windup)
	{
		const double W = FMath::Clamp(GetPhaseAge() / (Pitch.WindupMs / 1000.0), 0.0, 1.0);
		// Rock and lift (0-.4): hands up over the cap, the body turns away.
		const double Lift = Smooth(0.05, 0.4, W) * (1.0 - Smooth(0.45, 0.6, W));
		Glv = FMath::Lerp(Glv, FVector(0.3, 0.3, 0.55), Lift);
		Throw = FMath::Lerp(Throw, FVector(0.3, -0.25, 0.5), Lift);
		Twist = static_cast<float>(-0.55 * Lift);
		Crouch = static_cast<float>(0.12 - 0.08 * Lift);
		// Stride (.45-.8): glove out front, the ball back and up.
		const double Stride = Smooth(0.45, 0.8, W);
		Glv = FMath::Lerp(Glv, FVector(0.85, 0.05, 0.05), Stride);
		Throw = FMath::Lerp(Throw, FVector(-0.75, -0.35, 0.25), Stride);
		Lunge = static_cast<float>(0.75 * Stride);
		Twist = FMath::Lerp(Twist, -0.35f, static_cast<float>(Stride));
		// Whip (.85-1): over the top.
		const double Whip = Smooth(0.82, 1.0, W);
		Throw = FMath::Lerp(Throw, FVector(0.35, -0.2, 0.85), Whip);
		Glv = FMath::Lerp(Glv, FVector(0.3, 0.45, -0.3), Whip);
		Twist = FMath::Lerp(Twist, 0.35f, static_cast<float>(Whip));
		Lean = static_cast<float>(0.12 + 0.35 * Whip);
	}
	else if (Phase == EDerbyPhase::Pitch || (Phase == EDerbyPhase::Flight && GetPhaseAge() < 0.6))
	{
		// The follow-through, then square up to field.
		const double Since = Clock - PitchReleasedAt;
		const double F = 1.0 - Smooth(0.35, 1.1, Since);
		Throw = FMath::Lerp(FVector(0.4, -0.3, -0.4), FVector(0.85, 0.35, -0.75), F);
		Glv = FMath::Lerp(FVector(0.45, 0.32, -0.3), FVector(0.2, 0.55, -0.35), F);
		Lean = static_cast<float>(0.15 + 0.5 * F);
		Twist = static_cast<float>(0.45 * F);
		Lunge = static_cast<float>(0.75 * F);
		Crouch = static_cast<float>(0.2 + 0.2 * F);
	}
	else if (Phase == EDerbyPhase::Flight || (Phase == EDerbyPhase::Result && bHasOutcome && Outcome.Flight.Path.Num() > 0))
	{
		// Turns to watch it go.
		Pose.HandWeight[0] = Pose.HandWeight[1] = 0.2f;
		Crouch = 0.1f;
		if (Phase == EDerbyPhase::Result && IsHomeRun(Outcome.Result))
		{
			Pose.Slump = 0.7f;
		}
	}
	Pose.Hand[0] = Glv;
	Pose.Hand[1] = Throw;
	Pose.Crouch = Crouch;
	Pose.Lean = Lean;
	Pose.Twist = Twist;
	Pose.Lunge = Lunge;
	// Eyes on the plate, or on the ball once it is hit.
	const FVector Eye = Pitcher->GetComponentLocation() + FVector(0.0, 0.0, 160.0);
	const FVector Target = Phase == EDerbyPhase::Flight ? Ball->GetComponentLocation() : Mt(PlateM + FVector(0.0, -0.3, 0.3));
	Pose.Look = Pitcher->GetComponentTransform().InverseTransformVectorNoScale(Target - Eye).GetSafeNormal();
	Pose.LookWeight = 0.8f;
	const FVector Hand = Pitcher->BoneWorld(TEXT("leftHand"));
	Glove->SetWorldLocationAndRotation(Hand, Pitcher->GetComponentRotation() + FRotator(0.0, -90.0, 0.0));
	(void)Dt;
}

void ADerbyGame::UpdateBatter(float Dt)
{
	FArcadePoseInput& Pose = Batter->Pose();
	const FDerbySeat* Up = GetBatter();
	Pose.Idle = 1.f;
	Pose.HandWeight[0] = Pose.HandWeight[1] = 1.f;
	Pose.Cheer = 0.f;
	Pose.Slump = 0.f;
	Pose.Lift = 0.f;
	// The swing as a barrel angle round the body: A, degrees in the batter's frame (0 back toward the catcher, 90
	// out over the plate, 180 toward the pitcher), and its elevation E.
	double A = -12.0, E = 64.0;
	double Coil = 0.0;
	const double U = Clock - SwingAnimAt;
	const double Contact = SwingLead;
	if (Phase == EDerbyPhase::Finished)
	{
		// The winner's bat flip: up over the shoulder, both fists up.
		A = 250.0;
		E = 45.0;
	}
	else if (U >= 0.0 && U < SwingSeconds + 1.6)
	{
		if (U < Contact)
		{
			const double K = U / Contact;
			A = FMath::Lerp(-25.0, 90.0, K * K);
			E = FMath::Lerp(68.0, -3.0, Smooth(0.0, 1.0, K));
		}
		else if (U < SwingSeconds)
		{
			const double K = (U - Contact) / (SwingSeconds - Contact);
			A = FMath::Lerp(90.0, 245.0, 1.0 - (1.0 - K) * (1.0 - K));
			E = FMath::Lerp(-3.0, 40.0, Smooth(0.0, 1.0, K));
		}
		else
		{
			// Hold the finish, then settle back into the stance.
			const double K = Smooth(0.9, 1.6, U - SwingSeconds);
			A = FMath::Lerp(245.0, -12.0, K);
			E = FMath::Lerp(40.0, 64.0, K);
		}
	}
	else
	{
		// Waiting: the load (a held button, a phone's waggle), the bat twitching with the pitcher's windup.
		double Load = 0.0;
		if (Up && Up->Kind == EDerbyKind::Local && Local.bLoading)
		{
			Load = Local.Load;
		}
		else if (Up && Up->Kind == EDerbyKind::Phone)
		{
			Load = FMath::Min(1.0, PhoneCharge * 2.0);
		}
		else if (Phase == EDerbyPhase::Windup)
		{
			Load = Smooth(0.6, 1.0, GetPhaseAge() / (Pitch.WindupMs / 1000.0)) * 0.7;
		}
		else if (Phase == EDerbyPhase::Pitch)
		{
			Load = 0.7;
		}
		Coil = Load;
		A = -12.0 - 16.0 * Load + 4.0 * FMath::Sin(Clock * 2.3) * (1.0 - Load);
		E = 64.0 + 8.0 * Load;
	}
	const double Ar = FMath::DegreesToRadians(A), Er = FMath::DegreesToRadians(E);
	// Hands round a pivot at the chest: close in when the bat is up, out in front at contact.
	const double Radius = 0.14 + 0.32 * FMath::Cos(Er);
	const FVector Hands = FVector(0.08, 0.0, 1.12) + FVector(FMath::Sin(Ar), FMath::Cos(Ar), 0.0) * Radius + FVector(0.0, 0.0, 0.36 * FMath::Sin(Er));
	const FVector BatDir = FVector(FMath::Sin(Ar) * FMath::Cos(Er), FMath::Cos(Ar) * FMath::Cos(Er), FMath::Sin(Er)).GetSafeNormal();
	const double Turn = FMath::Clamp((A + 25.0) / 270.0, 0.0, 1.0);
	const float Twist = static_cast<float>(-0.22 - 0.12 * Coil + 1.0 * Turn);
	// Into each shoulder's frame (the torso turns with the twist), in reach.
	const FQuat Untwist(FVector::UpVector, -Twist);
	const double Reach = 0.58;
	const FVector LShoulder(0.0, -0.19, 1.46), RShoulder(0.0, 0.19, 1.46);
	Pose.Hand[0] = Untwist.RotateVector(Hands + BatDir * 0.02 - LShoulder) / Reach;
	Pose.Hand[1] = Untwist.RotateVector(Hands + BatDir * 0.11 - RShoulder) / Reach;
	Pose.Twist = Twist;
	Pose.Crouch = static_cast<float>(0.28 + 0.1 * Coil);
	Pose.Lean = static_cast<float>(0.2 + 0.06 * Coil);
	// The stride: the front foot goes on the swing.
	Pose.Lunge = static_cast<float>(0.35 * Smooth(-0.1, 0.05, U) * (1.0 - Smooth(0.9, 1.8, U)));
	Pose.Step = 0.f;
	// Eyes on the pitcher, then on the ball.
	const FVector Eye = Batter->GetComponentLocation() + FVector(0.0, 0.0, 150.0);
	FVector Target = Mt(ReleaseM);
	if (Phase == EDerbyPhase::Pitch || Phase == EDerbyPhase::Flight)
	{
		Target = Ball->GetComponentLocation();
	}
	Pose.Look = Batter->GetComponentTransform().InverseTransformVectorNoScale(Target - Eye).GetSafeNormal();
	Pose.LookWeight = 0.85f;
	// The call: a home run trot's fist pump, a strikeout's slump.
	if (Phase == EDerbyPhase::Result && bHasOutcome)
	{
		const double R = GetPhaseAge();
		const float In = static_cast<float>(Smooth(0.0, 0.25, R));
		if (IsHomeRun(Outcome.Result))
		{
			Pose.Cheer = In;
			Pose.Lift = static_cast<float>(0.12 * FMath::Abs(FMath::Sin(R * 7.0)) * In);
		}
		else if (Outcome.bStrike && ShownStrikeCount >= StrikesPerOut)
		{
			Pose.Slump = In;
		}
	}
	if (Phase == EDerbyPhase::Finished)
	{
		Pose.Cheer = 1.f;
		Pose.Lift = static_cast<float>(0.1 * FMath::Abs(FMath::Sin(GetPhaseAge() * 6.0)));
	}
	// The bat in the hands (as they are now), along the barrel's direction.
	const FVector L = Batter->BoneWorld(TEXT("leftHand")), R = Batter->BoneWorld(TEXT("rightHand"));
	const FVector World = Batter->GetComponentTransform().TransformVectorNoScale(BatDir);
	const bool bCheering = Pose.Cheer > 0.4f;
	if (bCheering)
	{
		// Flipped away: the bat lies on the grass by the plate.
		Bat->SetWorldLocationAndRotation(Mt(FVector(0.6, -1.7, 0.04)), FRotator(0.0, 25.0, 90.0));
	}
	else
	{
		const FQuat Q = FQuat::FindBetweenNormals(FVector::UpVector, World);
		Bat->SetWorldLocationAndRotation((L + R) / 2.0 - World * 9.0, Q);
	}
	(void)Dt;
}

void ADerbyGame::UpdateCamera(float Dt)
{
	double Aspect = 16.0 / 9.0;
	if (const UGameViewportClient* V = GetWorld()->GetGameViewport())
	{
		FVector2D Size;
		V->GetViewportSize(Size);
		Aspect = Size.Y > 0.0 ? Size.X / Size.Y : Aspect;
	}
	// The plate view: behind and a touch above the catcher, the batter on the left, the mound in the middle.
	FVector Pos(-5.6, 0.75, 2.25), Look(14.0, -0.25, 1.35);
	double Fov = 44.0, Response = 5.0;
	const double Age = GetPhaseAge();
	switch (Phase)
	{
	case EDerbyPhase::Intro:
	{
		const double T = Smooth(0.0, 1.0, Age / IntroSeconds);
		Pos = FMath::Lerp(FVector(-38.0, 46.0, 34.0), FVector(-14.0, 6.0, 7.0), T);
		Look = FMath::Lerp(FVector(70.0, 0.0, 8.0), FVector(18.0, 0.0, 1.5), T);
		Fov = FMath::Lerp(58.0, 44.0, T);
		Response = 30.0;
		break;
	}
	case EDerbyPhase::StepIn:
		// The hero shot: in front of the batter, low, drifting.
		Pos = FVector(1.9 + 0.25 * Age, 2.6 - 0.15 * Age, 1.25);
		Look = FVector(0.0, -0.95, 1.25);
		Fov = 34.0;
		Response = 30.0;
		break;
	case EDerbyPhase::Pitch:
		Pos += FVector(0.35, 0.0, -0.05) * Smooth(0.0, 1.0, (Clock - PitchReleasedAt) / FMath::Max(0.2, Pitch.FlightMs / 1000.0));
		break;
	case EDerbyPhase::Flight:
	case EDerbyPhase::Result:
		if (bHasOutcome && Outcome.Flight.Path.Num() > 0)
		{
			// The broadcast's high home camera on the ball, zooming as it carries.
			const FVector BallM = (Phase == EDerbyPhase::Flight ? HitBall(FlightClock) : HitBall(Outcome.Flight.HangS)) / 100.0;
			const double A = FMath::DegreesToRadians(Outcome.Contact.SprayDeg);
			Pos = FVector(-20.0, 0.0, 13.0) + FVector(-FMath::Sin(A) * 0.0, FMath::Sin(A) * 10.0, 0.0);
			if (IsHomeRun(Outcome.Result))
			{
				// A home run rides closer behind it, then watches it come down.
				const FVector Behind = FVector(FMath::Cos(A), FMath::Sin(A), 0.0);
				const double Far = FMath::Min(1.0, FVector(BallM.X, BallM.Y, 0.0).Size() / 140.0);
				Pos = FMath::Lerp(Pos, FVector(BallM.X, BallM.Y, 0.0) - Behind * 45.0 + FVector(0.0, 0.0, 22.0), Smooth(0.15, 0.7, Far) * 0.75);
			}
			Look = BallM;
			const double D = FMath::Max(1.0, (BallM - Pos).Size());
			Fov = FMath::Clamp(2.0 * FMath::RadiansToDegrees(FMath::Atan(16.0 / D)), 16.0, 52.0);
			Response = Phase == EDerbyPhase::Flight ? 7.0 : 2.5;
			if (Phase == EDerbyPhase::Flight && FlightClock < 0.25)
			{
				Response = 12.0;
			}
		}
		break;
	case EDerbyPhase::Finished:
		// The finale: round the winner at the plate, the park lit behind.
		Pos = FVector(FMath::Cos(Clock * 0.16) * 6.5 + 1.0, FMath::Sin(Clock * 0.16) * 6.5 - 0.95, 2.3);
		if (Pos.X < -2.0)
		{
			Pos.X = -2.0;
		}
		Look = FVector(0.0, -0.95, 1.25);
		Fov = 42.0;
		Response = 2.0;
		break;
	default:
		break;
	}
	if (!bCamPlaced)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-Response * FMath::Min<double>(Dt, 1.0 / 20.0));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * FMath::Min(1.0, K * 1.6);
	CamFov += (Fov - CamFov) * (1.0 - FMath::Exp(-5.0 * Dt));
	Shake *= FMath::Exp(-8.0 * Dt);
	const FVector Jolt(0.0, FMath::Sin(Clock * 63.0) * Shake * 0.06, FMath::Sin(Clock * 51.0 + 1.3) * Shake * 0.05);
	const FVector From = Mt(CamPos + Jolt);
	const FVector To = Mt(CamLook);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	const double HFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CamFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HFov));
}

void ADerbyGame::UpdateBoard()
{
	if (!Park)
	{
		return;
	}
	const FDerbySeat* Up = GetBatter();
	const FBatter* Stats = GetBatterStats();
	if (Phase == EDerbyPhase::Finished)
	{
		const TArray<int32> Rank = Ranking(Match);
		const FDerbySeat* Win = Rank.Num() > 0 && Seats.IsValidIndex(Rank[0]) ? &Seats[Rank[0]] : nullptr;
		Park->SetBoard(TEXT("DERBY CHAMPION"), Win ? Win->Name.ToUpper() : FString(), Win ? FString::Printf(TEXT("%d PTS  ·  %d HR"), Match.Batters[Rank[0]].Score, Match.Batters[Rank[0]].HomeRuns) : FString(),
			Win ? Win->Color : FLinearColor::White);
		return;
	}
	if (!Up || !Stats)
	{
		return;
	}
	if (bHasOutcome && IsHomeRun(Outcome.Result) && Phase == EDerbyPhase::Result)
	{
		Park->SetBoard(Outcome.Result == EResult::Moonshot ? TEXT("MOONSHOT!") : TEXT("HOME RUN!"), FString::Printf(TEXT("%d FT"), Outcome.DistanceFt),
			FString::Printf(TEXT("%s  ·  %d HR  ·  %d PTS"), *Up->Name.ToUpper(), Stats->HomeRuns, Stats->Score), Up->Color);
		return;
	}
	Park->SetBoard(TEXT("NOW BATTING"), Up->Name.ToUpper(), FString::Printf(TEXT("HR %d  ·  LONG %d FT  ·  %d PTS"), Stats->HomeRuns, Stats->LongestFt, Stats->Score), Up->Color);
}

void ADerbyGame::Rematch()
{
	++Rematches;
	TArray<FString> Order;
	for (const FDerbySeat& S : Seats)
	{
		Order.Add(S.Id);
	}
	Match = CreateMatch(Order, Seed + 7919u * static_cast<uint32>(Rematches));
	ShownUp = 0;
	ShownPitchCount = ShownStrikeCount = 0;
	bHasOutcome = bHasPitch = false;
	bResultsCommitted = false;
	AnnouncedBatter.Reset();
	PendingFireworks.Reset();
	SwingAnimAt = -100.0;
	if (Seats.Num() > 0)
	{
		Batter->SetLook(Seats[0].Look);
		DressedFor = Seats[0].Id;
	}
	SetPhase(EDerbyPhase::Intro);
	bCamPlaced = false;
	UpdateBoard();
}

void ADerbyGame::ArcadeCommitResults()
{
	if (!bResultsCommitted && Phase == EDerbyPhase::Finished)
	{
		bResultsCommitted = true;
		if (OnFinished)
		{
			OnFinished();
		}
	}
}

void ADerbyGame::ArcadeRestart()
{
	if (OnRematch) OnRematch(); else Rematch();
}

void ADerbyGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (int32 i = 0; i < Match.Batters.Num(); ++i)
	{
		const FBatter& B = Match.Batters[i];
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		const FDerbySeat* Seat = SeatOf(B.Id);
		Row.Name = Seat ? Seat->Name : B.Id;
		Row.Color = Seat ? Seat->Color : FLinearColor::White;
		Row.bHuman = Seat && Seat->Kind != EDerbyKind::Bot;
		Row.Score = B.Score;
	}
	RankByScore(Out);
}

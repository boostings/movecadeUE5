#include "Games/Derby/DerbyGame.h"

#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Games/Derby/DerbyBallpark.h"
#include "Games/Derby/DerbyStage.h"
#include "Games/Derby/SDerbyHud.h"
#include "NiagaraComponent.h"

using namespace MovecadeKart::Derby;

namespace
{
	/** The bots' and the fireworks' stream: fixed, so a seeded run (-DerbySeed) replays exactly. */
	constexpr int32 BotRngSeed = 0xba7;
	/** Each rematch draws its schedule from the match seed plus this prime times the rematch number. */
	constexpr uint32 RematchSeedStep = 7919u;
	/** The HUD paints over the game viewport's other widgets (the overlay sits above it). */
	constexpr int32 HudZOrder = 10;
	/** The finale: the crowd's roar and the fireworks over the seats (bursts, seconds before the first). */
	constexpr float FinaleRoar = 0.8f;
	constexpr int32 FinaleFireworks = 6;
	constexpr double FinaleFireworksDelay = 0.6;
	/** The crowd's murmur: louder through the call. */
	constexpr float CrowdGain = 0.8f;
	constexpr float CrowdCallGain = 0.6f;

	TArray<FString> BatterIds(const TArray<FDerbySeat>& Seats)
	{
		TArray<FString> Ids;
		Ids.Reserve(Seats.Num());
		for (const FDerbySeat& Seat : Seats)
		{
			Ids.Add(Seat.Id);
		}
		return Ids;
	}
}

ADerbyGame::ADerbyGame()
{
	PrimaryActorTick.bCanEverTick = true;
	// After physics: the avatars' bones (the bat's hands, the ball in the pitcher's hand) are posed by then.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(BotRngSeed);
}

void ADerbyGame::Setup(ADerbyBallpark* InPark, const TArray<FDerbySeat>& InSeats, APlayerController* InViewer, uint32 InSeed)
{
	Park = InPark;
	Seats = InSeats;
	Viewer = InViewer;
	Seed = InSeed;
	Match = CreateMatch(BatterIds(Seats), Seed);
	// GetSeat(i) relies on it: the match's batters are the seats, in the same order.
	check(Match.Batters.Num() == Seats.Num());
	UE_LOG(LogDerby, Log, TEXT("Derby: %d batter(s), seed %u"), Seats.Num(), Seed);

	BuildStage();
	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SDerbyHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), HudZOrder);
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
	return Seats.FindByPredicate([&Id](const FDerbySeat& Seat) { return Seat.Id == Id; });
}

const FBatter* ADerbyGame::GetBatterStats() const
{
	return Match.Batters.IsValidIndex(ShownUp) ? &Match.Batters[ShownUp] : nullptr;
}

bool ADerbyGame::IsPhoneUp() const
{
	const FDerbySeat* Up = GetBatter();
	return Up && Up->Kind == EDerbyKind::Phone;
}

// ---- The loop ---------------------------------------------------------------------------------------------------

void ADerbyGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	float Dt = FMath::Min(DeltaSeconds, Tuning.MaxFrameSeconds);
	// Hit-stop: the frame of contact holds a beat.
	if (HitStop > 0.0)
	{
		HitStop -= Dt;
		Dt *= Tuning.HitStopTimeScale;
	}
	Clock += Dt;
	UpdateLocal(Dt);
	TickPhase(Dt);
	LaunchDueFireworks();
	// The crowd's murmur under everything (the cue's own throttle paces it).
	Cues.Play(this, Cue::Crowd, Park ? CrowdGain + CrowdCallGain * static_cast<float>(Phase == EDerbyPhase::Result) : 1.f);
	UpdateBall();
	UpdatePitcher();
	UpdateBatter();
	UpdateCamera(Dt);
}

void ADerbyGame::TickPhase(float Dt)
{
	const double Age = GetPhaseAge();
	switch (Phase)
	{
	case EDerbyPhase::Intro:
		if (Age >= Tuning.IntroSeconds)
		{
			BeginTurn();
		}
		break;
	case EDerbyPhase::StepIn:
		if (Age >= Tuning.StepInSeconds)
		{
			SetPhase(EDerbyPhase::Set);
		}
		break;
	case EDerbyPhase::Set:
		TickSet(Age);
		break;
	case EDerbyPhase::Windup:
		if (Age >= Pitch.WindupMs / 1000.0)
		{
			ReleasePitch();
		}
		break;
	case EDerbyPhase::Pitch:
		TickPitch();
		break;
	case EDerbyPhase::Flight:
		TickFlight(Dt);
		break;
	case EDerbyPhase::Result:
		TickResult(Age);
		break;
	case EDerbyPhase::Finished:
		break;
	}
}

void ADerbyGame::SetPhase(EDerbyPhase Next)
{
	const EDerbyPhase Was = Phase;
	Phase = Next;
	PhaseAt = Clock;
	// Broadcast cuts: the hero shot, back to the plate after a turn or a hit, the finale.
	const bool bBackFromHit = Was == EDerbyPhase::Result && HasHitFlight();
	if (Next == EDerbyPhase::StepIn || Next == EDerbyPhase::Finished || (Next == EDerbyPhase::Set && (Was == EDerbyPhase::StepIn || bBackFromHit)))
	{
		bCamPlaced = false;
	}
	if (Next == EDerbyPhase::Finished)
	{
		EnterFinished();
	}
}

void ADerbyGame::EnterFinished()
{
	Cues.Play(this, Cue::MatchOver);
	if (Park)
	{
		Park->Roar(FinaleRoar);
	}
	// The winner takes the plate for the finale.
	const TArray<int32> Rank = Ranking(Match);
	if (Rank.Num() > 0 && Seats.IsValidIndex(Rank[0]))
	{
		ShownUp = Rank[0];
		DressBatter(Rank[0]);
	}
	QueueFireworks(FinaleFireworks, FinaleFireworksDelay);
	UpdateBoard();
	ArcadeCommitResults();
}

void ADerbyGame::DressBatter(int32 SeatIndex)
{
	Batter->SetLook(Seats[SeatIndex].Look);
	DressedFor = Seats[SeatIndex].Id;
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
	// The aim carries over from the last batter; the load does not.
	Local = FDerbyLocalSwing{ Local.Aim };
	if (const FDerbySeat* Up = GetBatter())
	{
		if (Up->Id != DressedFor)
		{
			DressBatter(ShownUp);
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
	Cues.Play(this, Cue::StepIn);
	SetPhase(EDerbyPhase::StepIn);
	UpdateBoard();
}

void ADerbyGame::TickSet(double Age)
{
	const double Wait = IsPhoneUp() ? Tuning.PhoneSetSeconds : Tuning.SetSeconds;
	if (Age >= Wait || (bReady && Age >= Tuning.ReadyMinSetSeconds))
	{
		BeginPitch();
	}
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
	SwingAnimAt = -100.0;
	SlowMo = 1.0;
	bReady = false;
	SetPhase(EDerbyPhase::Windup);
}

void ADerbyGame::ReleasePitch()
{
	PitchReleasedAt = Clock;
	SetPhase(EDerbyPhase::Pitch);
	// The fastball's whoosh a touch higher.
	Cues.Play(this, Cue::Pitch, 0.8f, Pitch.Type == EPitchType::Fastball ? 1.1f : 0.92f);
	const FDerbySeat* Up = GetBatter();
	if (Up && OnPitch)
	{
		OnPitch(Up->Id, Pitch);
	}
	UpdateBot();
}

void ADerbyGame::TickPitch()
{
	const double Arrive = ArriveAt();
	if (!bResolved && bSwingTaken && Clock >= SwingPeakAt)
	{
		// The swing peaked: judge it. A miss is called on a later tick, as the ball gets to the backstop.
		FSwing Swing;
		Swing.bSwung = true;
		Swing.TimingMs = (SwingPeakAt - Arrive) * 1000.0;
		Swing.Aim = SwingAim;
		Swing.Strength = SwingStrength;
		Resolve(Swing, SwingPeakAt);
		return;
	}
	const double Grace = IsPhoneUp() ? Tuning.PhoneGraceSeconds : Tuning.TakeGraceSeconds;
	if (!bResolved && !bSwingTaken && Clock > Arrive + Pitch.ContactMs / 1000.0 + Grace)
	{
		Resolve(FSwing(), Clock);
	}
	// A take or a whiff: the call as the ball reaches the backstop (or the catcher's spot).
	if (bResolved && Clock >= Arrive + Tuning.CallDelaySeconds)
	{
		Cues.Play(this, Cue::Mitt);
		RecordOutcome();
	}
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
			Cues.Play(this, Cue::Whiff);
		}
		// The ball goes on to the backstop; the call comes as it gets there.
		ContactAt = Clock;
		return;
	}
	ContactAt = InContactAt;
	// The swing peaked a little before this tick: the flight starts that far in.
	FlightClock = FMath::Max(0.0, Clock - InContactAt);
	SlowMo = 1.0;
	bLanded = false;
	HitStop = Outcome.Contact.Quality > Tuning.HardContactQuality ? Tuning.HardHitStopSeconds : Tuning.HitStopSeconds;
	ContactBurst(Outcome);
	SetPhase(EDerbyPhase::Flight);
}

void ADerbyGame::TickFlight(float Dt)
{
	const double Progress = Outcome.Flight.HangS > 0.0 ? FlightClock / Outcome.Flight.HangS : 1.0;
	// Moonshots hang in slow motion through the top of the arc; the rest play a touch fast, as a broadcast cuts the hang time.
	const bool bHang = Outcome.Result == EResult::Moonshot && Progress > Tuning.MoonshotSlowFrom && Progress < Tuning.MoonshotSlowTo;
	const double Want = bHang ? Tuning.MoonshotTimeScale : Tuning.FlightTimeScale;
	SlowMo += (Want - SlowMo) * (1.0 - FMath::Exp(-Tuning.TimeScaleResponse * Dt));
	FlightClock += Dt * SlowMo;
	if (FlightClock < Outcome.Flight.HangS)
	{
		return;
	}
	FlightClock = Outcome.Flight.HangS;
	SlowMo = Tuning.FlightTimeScale;
	Land();
	RecordOutcome();
}

void ADerbyGame::RecordOutcome()
{
	const int32 StrikesBefore = Match.Strikes;
	const FRecord Recorded = Record(Match, Outcome);
	ShownPitchCount += 1;
	ShownStrikeCount = StrikesBefore + (Outcome.bStrike ? 1 : 0);
	const FDerbySeat* Up = GetBatter();
	if (OnOutcome && Up)
	{
		OnOutcome(Up->Id, Outcome);
	}
	if (Recorded.bStruckOut)
	{
		Cues.Play(this, Cue::Out);
	}
	SetPhase(EDerbyPhase::Result);
	UpdateBoard();
}

double ADerbyGame::ResultHoldSeconds() const
{
	switch (Outcome.Result)
	{
	case EResult::Moonshot: return Tuning.MoonshotHoldSeconds;
	case EResult::HomeRun: return Tuning.HomeRunHoldSeconds;
	case EResult::InPlay: return Tuning.InPlayHoldSeconds;
	default: return Tuning.OtherHoldSeconds;
	}
}

void ADerbyGame::TickResult(double Age)
{
	// Record has already moved the match on: a new batter (or the first pitch of a turn) means this one is done.
	const bool bTurnOver = Match.bComplete || Match.Up != ShownUp || (Match.Pitch == 0 && ShownPitchCount > 0);
	if (Age < ResultHoldSeconds() + (bTurnOver ? Tuning.TurnOverHoldSeconds : 0.0))
	{
		return;
	}
	if (bTurnOver)
	{
		BeginTurn();
	}
	else
	{
		SetPhase(EDerbyPhase::Set);
	}
}

// ---- Rematch and IArcadeGame --------------------------------------------------------------------------------------

void ADerbyGame::Rematch()
{
	++Rematches;
	Match = CreateMatch(BatterIds(Seats), Seed + RematchSeedStep * static_cast<uint32>(Rematches));
	ShownUp = 0;
	ShownPitchCount = ShownStrikeCount = 0;
	bHasOutcome = bHasPitch = false;
	bResultsCommitted = false;
	AnnouncedBatter.Reset();
	PendingFireworks.Reset();
	SwingAnimAt = -100.0;
	if (Seats.Num() > 0)
	{
		DressBatter(0);
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
	if (OnRematch)
	{
		OnRematch();
	}
	else
	{
		Rematch();
	}
}

void ADerbyGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (int32 i = 0; i < Match.Batters.Num(); ++i)
	{
		const FBatter& Stats = Match.Batters[i];
		const FDerbySeat* Seat = GetSeat(i);
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		Row.Name = Seat ? Seat->Name : Stats.Id;
		Row.Color = Seat ? Seat->Color : FLinearColor::White;
		Row.bHuman = Seat && Seat->Kind != EDerbyKind::Bot;
		Row.Score = Stats.Score;
	}
	RankByScore(Out);
}

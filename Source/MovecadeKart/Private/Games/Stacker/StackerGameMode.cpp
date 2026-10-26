#include "Games/Stacker/StackerGameMode.h"

#include "Games/Stacker/StackerDeviceControls.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Stacker/StackerGame.h"
#include "Games/Stacker/StackerRooftop.h"
#include "Net/RoomHostSubsystem.h"

using namespace MovecadeKart::Stacker;

namespace
{
	const TCHAR* const HouseNames[] = { TEXT("Rosa"), TEXT("Mika"), TEXT("Dante"), TEXT("Priya") };
	/** The local builder plays sharp; house bots vary by seat (0.5, 0.6, 0.7); room bots sit in between. */
	constexpr float LocalSkill = 0.9f;
	constexpr float HouseBotSkill = 0.5f;
	constexpr float HouseBotSkillStep = 0.1f;
	constexpr int32 HouseBotSkillSteps = 3;
	constexpr float RoomBotSkill = 0.65f;
	/** Local seats wear varied looks from this offset (clear of the kart racers' defaults). */
	constexpr int32 LocalLookSeed = 8;
	/** runtime.ts 465-477: DROP has no payload and resolves at receipt (the cooldown de-dups repeats). */
	const TCHAR* const DropEvent = TEXT("stacker:drop");
	/** A perfect this deep into a combo gets the big toast. */
	constexpr int32 BigComboToast = 3;
}

AStackerGameMode::AStackerGameMode()
{
	ArcadeId = TEXT("stacker");
	CommandLinePrefix = TEXT("Stacker");
}

void AStackerGameMode::SpawnVenue()
{
	for (TActorIterator<AStackerRooftop> It(GetWorld()); It; ++It)
	{
		Rooftop = *It;
		return;
	}
	Rooftop = GetWorld()->SpawnActor<AStackerRooftop>();
}

void AStackerGameMode::BuildRoomSeats(TConstArrayView<FMcSeat> Players)
{
	for (const FMcSeat& Player : Players)
	{
		FStackerSeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EBuilderKind::Bot : EBuilderKind::Phone;
		Seat.Skill = RoomBotSkill;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
}

void AStackerGameMode::BuildLocalSeats(const FArcadeLaunchOptions& InLaunch)
{
	const int32 Locals = InLaunch.Locals;
	for (int32 i = 0; i < Locals + InLaunch.Bots; ++i)
	{
		FStackerSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - Locals) % UE_ARRAY_COUNT(HouseNames)];
		// The device seat plays through the phone path (AArcadeGameModeBase::MakeDeviceControls).
		Seat.Kind = bLocal && !InLaunch.bAutoPlay ? (i == 0 && InLaunch.bDeviceSeat ? EBuilderKind::Phone : EBuilderKind::Local) : EBuilderKind::Bot;
		Seat.Skill = bLocal ? LocalSkill : HouseBotSkill + HouseBotSkillStep * (i % HouseBotSkillSteps);
		Seat.Color = AStackerRooftop::LaneColor(i);
		Seat.Look = FKartLook::MakeVaried(i + LocalLookSeed);
	}
}

void AStackerGameMode::SpawnGame()
{
	Game = SpawnArcadeGame<AStackerGame>();
	Game->Setup(Rooftop, Seats, GetWorld()->GetFirstPlayerController());
	Game->OnQuit = MakeQuitCallback();
	Game->OnRematch = MakeRematchCallback();
}

void AStackerGameMode::BindRoomCallbacks(URoomHostSubsystem& Room)
{
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = &Room;
	// Everyone builds at once: no turn, and the engine's countdown.
	Game->OnMatchStart = [WeakRoom]()
	{
		if (URoomHostSubsystem* R = WeakRoom.Get())
		{
			R->AdvanceTurn(FString());
			R->PublishMatchCountdown(static_cast<int32>(CountdownMs));
		}
	};
	Game->OnEffect = [WeakThis = TWeakObjectPtr<AStackerGameMode>(this)](const FEffect& Effect)
	{
		if (const AStackerGameMode* Mode = WeakThis.Get())
		{
			Mode->SendEffectFeedback(Effect);
		}
	};
	Game->OnFinished = [WeakThis = TWeakObjectPtr<AStackerGameMode>(this)]()
	{
		if (AStackerGameMode* Mode = WeakThis.Get())
		{
			Mode->CompleteRoomMatch();
		}
	};
}

void AStackerGameMode::SendEffectFeedback(const FEffect& E) const
{
	URoomHostSubsystem* Room = GetRoom();
	const FTower* Tower = Game ? Game->GetMatch().FindTower(E.PlayerId) : nullptr;
	if (!Room || !Tower || E.Label.IsEmpty())
	{
		return;
	}
	const bool bBad = E.Kind == EEffectKind::Miss || E.Kind == EEffectKind::Topout;
	const bool bCelebrate = E.Kind == EEffectKind::Perfect || E.Kind == EEffectKind::Complete;
	const bool bBig = E.Kind == EEffectKind::Complete || (E.Kind == EEffectKind::Perfect && Tower->Combo >= BigComboToast);
	const FString Detail = FString::Printf(TEXT("+%d points · %d lives · %d blocks"), FMath::Max(0, E.ScoreDelta), Tower->Lives, Tower->Height());
	Room->SendPlayFeedback(E.PlayerId, E.Label, Detail, bBad ? TEXT("warning") : TEXT("success"),
		bBad ? TEXT("warning") : (bCelebrate ? TEXT("celebration") : TEXT("impact")), bBig);
}

void AStackerGameMode::HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame)
{
	if (Game)
	{
		Game->FeedPhone(Seat.PlayerId, Frame);
	}
}

void AStackerGameMode::HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data)
{
	if (Game && Event.Equals(DropEvent, ESearchCase::CaseSensitive))
	{
		Game->Drop(FromPlayerId);
	}
}

bool AStackerGameMode::CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const
{
	// Nothing to report while the 3-2-1 runs (the intro included: the rules haven't started).
	if (!Game || Game->GetMatch().Phase == EMatchPhase::Countdown)
	{
		return false;
	}
	for (const FTower& Tower : Game->GetMatch().Towers)
	{
		FMcScoreEntry& Entry = OutScores.AddDefaulted_GetRef();
		Entry.PlayerId = Tower.PlayerId;
		Entry.Score = Tower.Score;
	}
	return true;
}

TSharedPtr<MovecadeKart::Input::IDeviceControlScheme> AStackerGameMode::MakeDeviceControls()
{
	return MakeShared<FStackerDeviceControls>();
}

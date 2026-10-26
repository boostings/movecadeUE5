#include "Games/Bowling/BowlingGameMode.h"

#include "Games/Bowling/BowlingDeviceControls.h"
#include "Art/KartMaterials.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Bowling/BowlingAlley.h"
#include "Games/Bowling/BowlingGame.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"

namespace
{
	/** The web's lane colours, one per seat. */
	const TCHAR* const SeatColors[] = { TEXT("#6d8bff"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Mika"), TEXT("Dante"), TEXT("Priya"), TEXT("Rosa") };
	/** Local people bowl at this skill when -BowlAuto hands their seats to the house AI. */
	constexpr float LocalAutoSkill = 0.85f;
	/** House bots range from wild to steady by seat: 0.55, 0.65, 0.75. */
	constexpr float HouseSkillBase = 0.55f;
	constexpr float HouseSkillStep = 0.1f;
	constexpr int32 HouseSkillLevels = 3;
	/** The room's bots bowl at the house's middle skill. */
	constexpr float RoomBotSkill = 0.6f;

	const FString ReleaseEvent(TEXT("bowling:release"));
	const FString ReplayEvent(TEXT("bowling:replay-outcome"));

	/** presentation.ts beatLabel, for the phones' toasts and the replay event. */
	FString BeatLabel(EBowlingBeat Beat, int32 Pins)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey: return TEXT("TURKEY");
		case EBowlingBeat::Strike: return TEXT("STRIKE");
		case EBowlingBeat::Spare: return TEXT("SPARE");
		case EBowlingBeat::SplitConvert: return TEXT("SPLIT CONVERT");
		case EBowlingBeat::Split: return TEXT("SPLIT");
		case EBowlingBeat::Gutter: return TEXT("GUTTER");
		case EBowlingBeat::Good: return FString::Printf(TEXT("%d DOWN"), Pins);
		default: return Pins == 0 ? FString(TEXT("NO PINS")) : FString::Printf(TEXT("%d DOWN"), Pins);
		}
	}

	/** The web's BowlingBeat names. */
	const TCHAR* BeatId(EBowlingBeat Beat)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey: return TEXT("turkey");
		case EBowlingBeat::Strike: return TEXT("strike");
		case EBowlingBeat::Spare: return TEXT("spare");
		case EBowlingBeat::SplitConvert: return TEXT("split-convert");
		case EBowlingBeat::Split: return TEXT("split");
		case EBowlingBeat::Gutter: return TEXT("gutter");
		case EBowlingBeat::Good: return TEXT("good");
		default: return TEXT("open");
		}
	}

	/** runtime.ts releaseGestureFromPayload: a bowling:release payload, or unset without a finite power. */
	TOptional<MovecadeKart::Bowling::FShot> ParseRelease(const FJsonObject& Data)
	{
		double Power = 0.0;
		if (!Data.TryGetNumberField(TEXT("power"), Power) || !FMath::IsFinite(Power))
		{
			return {};
		}
		auto Field = [&Data](const TCHAR* Key)
		{
			double Value = 0.0;
			return Data.TryGetNumberField(Key, Value) && FMath::IsFinite(Value) ? Value : 0.0;
		};
		MovecadeKart::Bowling::FShot Throw;
		Throw.Aim = Field(TEXT("aim"));
		Throw.Power = Power;
		Throw.Curve = Field(TEXT("curve"));
		Throw.Loft = Field(TEXT("loft"));
		return Throw;
	}
}

ABowlingGameMode::ABowlingGameMode()
{
	ArcadeId = TEXT("bowling");
	CommandLinePrefix = TEXT("Bowl");
}

void ABowlingGameMode::SpawnVenue()
{
	UWorld* World = GetWorld();
	TActorIterator<ABowlingAlley> It(World);
	Alley = It ? *It : World->SpawnActor<ABowlingAlley>();
}

void ABowlingGameMode::BuildRoomSeats(TConstArrayView<FMcSeat> Players)
{
	for (const FMcSeat& Player : Players)
	{
		FBowlerSeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EBowlerKind::Bot : EBowlerKind::Phone;
		Seat.Skill = RoomBotSkill;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
}

void ABowlingGameMode::BuildLocalSeats(const FArcadeLaunchOptions& InLaunch)
{
	for (int32 i = 0; i < InLaunch.Locals + InLaunch.Bots; ++i)
	{
		FBowlerSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < InLaunch.Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - InLaunch.Locals) % UE_ARRAY_COUNT(HouseNames)];
		// The device seat plays through the phone path (AArcadeGameModeBase::MakeDeviceControls).
		Seat.Kind = bLocal && !InLaunch.bAutoPlay ? (i == 0 && InLaunch.bDeviceSeat ? EBowlerKind::Phone : EBowlerKind::Local) : EBowlerKind::Bot;
		Seat.Skill = bLocal ? LocalAutoSkill : HouseSkillBase + HouseSkillStep * (i % HouseSkillLevels);
		Seat.Color = MovecadeKart::Art::Hex(SeatColors[i % UE_ARRAY_COUNT(SeatColors)]);
		Seat.Look = FKartLook::MakeVaried(i);
	}
}

void ABowlingGameMode::SpawnGame()
{
	Game = SpawnArcadeGame<ABowlingGame>();
	Game->Setup(Alley, Seats, GetWorld()->GetFirstPlayerController(), FBowlingDebugFlags::Parse(FCommandLine::Get()));
	Game->OnQuit = MakeQuitCallback();
	Game->OnRematch = MakeRematchCallback();
}

void ABowlingGameMode::BindRoomCallbacks(URoomHostSubsystem& Room)
{
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = &Room;
	// runtime.ts: announce the bowler up on each change, so the server lets only their phone throw.
	Game->OnTurn = [WeakRoom](const FString& PlayerId)
	{
		if (URoomHostSubsystem* BoundRoom = WeakRoom.Get())
		{
			BoundRoom->AdvanceTurn(PlayerId);
		}
	};
	TWeakObjectPtr<ABowlingGameMode> WeakMode = this;
	Game->OnRoll = [WeakMode, WeakRoom](const FString& PlayerId, int32 Pins, EBowlingBeat Beat)
	{
		ABowlingGameMode* Mode = WeakMode.Get();
		URoomHostSubsystem* BoundRoom = WeakRoom.Get();
		if (!Mode || !BoundRoom || PlayerId.IsEmpty())
		{
			return;
		}
		Mode->SendRollFeedback(*BoundRoom, PlayerId, Pins, Beat);
		Mode->PushLiveScores();
	};
	Game->OnFinished = [WeakMode]()
	{
		if (ABowlingGameMode* Mode = WeakMode.Get())
		{
			Mode->CompleteRoomMatch();
		}
	};
}

void ABowlingGameMode::SendRollFeedback(URoomHostSubsystem& Room, const FString& PlayerId, int32 Pins, EBowlingBeat Beat) const
{
	const MovecadeKart::Bowling::FShotResult* Result = Game ? Game->GetShot() : nullptr;
	const bool bBig = MovecadeKart::Bowling::IsCelebration(Beat);
	const FString Label = BeatLabel(Beat, Pins);
	const TCHAR* Tone = Beat == EBowlingBeat::Gutter || Pins == 0 ? TEXT("warning") : TEXT("success");
	const TCHAR* Haptic = bBig ? TEXT("celebration") : (Pins > 0 ? TEXT("impact") : TEXT("warning"));
	Room.SendPlayFeedback(PlayerId, Label, FString::Printf(TEXT("%d pins · %.1f m/s"), Pins, Result ? Result->Speed : 0.0), Tone, Haptic, bBig);
	Room.SendGameEvent(ReplayEvent, FString::Printf(TEXT("{\"playerId\":%s,\"pins\":%d,\"label\":%s,\"beat\":\"%s\"}"),
		*MovecadeKart::Net::QuoteJson(PlayerId), Pins, *MovecadeKart::Net::QuoteJson(Label), BeatId(Beat)));
}

void ABowlingGameMode::HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame)
{
	if (Game)
	{
		Game->FeedPhone(Seat.PlayerId, Frame);
	}
}

void ABowlingGameMode::HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data)
{
	if (!Game || !Data.IsValid() || !Event.Equals(ReleaseEvent, ESearchCase::CaseSensitive))
	{
		return;
	}
	if (const TOptional<MovecadeKart::Bowling::FShot> Throw = ParseRelease(*Data))
	{
		Game->SubmitRelease(FromPlayerId, *Throw);
	}
}

bool ABowlingGameMode::CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const
{
	if (!Game)
	{
		return false;
	}
	// In turn order, every bowler (a card with no rolls yet scores 0).
	const MovecadeKart::Bowling::FMatch& Match = Game->GetMatch();
	for (const FString& Id : Match.Order)
	{
		const MovecadeKart::Bowling::FCard* Card = Match.CardOf(Id);
		FMcScoreEntry& Entry = OutScores.AddDefaulted_GetRef();
		Entry.PlayerId = Id;
		Entry.Score = Card ? Card->Score : 0;
	}
	return true;
}

TSharedPtr<MovecadeKart::Input::IDeviceControlScheme> ABowlingGameMode::MakeDeviceControls()
{
	return MakeShared<FBowlingDeviceControls>();
}

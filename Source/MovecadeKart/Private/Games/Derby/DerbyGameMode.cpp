#include "Games/Derby/DerbyGameMode.h"

#include "Art/KartMaterials.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Derby/DerbyBallpark.h"
#include "Games/Derby/DerbyDebugFlags.h"
#include "Games/Derby/DerbyGame.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Derby;

namespace
{
	const TCHAR* const SeatColors[] = { TEXT("#ff6b5a"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	const TCHAR* const HouseNames[] = { TEXT("Dante"), TEXT("Rosa"), TEXT("Mika"), TEXT("Priya") };
	/** Local people bat well; house bots range 0.5-0.74 by seat; a room's bots are steady. */
	constexpr float LocalSkill = 0.85f;
	constexpr float HouseBaseSkill = 0.5f;
	constexpr float HouseSkillStep = 0.12f;
	constexpr int32 HouseSkillSteps = 3;
	constexpr float RoomBotSkill = 0.62f;
	/** Local seats wear varied looks from this seed on. */
	constexpr int32 LookSeedBase = 4;
	/** A room match's seed: every match number gets its own schedule, the same on any host. */
	constexpr uint32 RoomSeedBase = 0xDE4Bu;
	constexpr uint32 RoomSeedStep = 7919u;
	const TCHAR* const PitchEvent = TEXT("derby:pitch");

	FString Hex(const FLinearColor& Color)
	{
		const FColor S = Color.ToFColor(true);
		return FString::Printf(TEXT("#%02x%02x%02x"), S.R, S.G, S.B);
	}

	/** The phone's toast for a pitch (play:feedback). */
	struct FDerbyCall
	{
		FString Title;
		FString Detail;
		FString Tone;
		FString Haptic;
		bool bMajor = false;
	};

	FString CallTitle(const FOutcome& Outcome, int32 Strikes)
	{
		const bool bOut = Strikes >= StrikesPerOut;
		switch (Outcome.Result)
		{
		case EResult::Moonshot: return FString::Printf(TEXT("MOONSHOT! %d FT"), Outcome.DistanceFt);
		case EResult::HomeRun: return FString::Printf(TEXT("HOME RUN! %d FT"), Outcome.DistanceFt);
		case EResult::InPlay: return FString::Printf(TEXT("%d FT"), Outcome.DistanceFt);
		case EResult::Foul: return TEXT("FOUL BALL");
		case EResult::Whiff: return bOut ? TEXT("STRUCK OUT") : TEXT("SWING AND A MISS");
		default: return bOut ? TEXT("STRUCK OUT") : TEXT("STRIKE");
		}
	}

	FString CallDetail(const FOutcome& Outcome, int32 Strikes)
	{
		if (Outcome.Result == EResult::Take)
		{
			return FString::Printf(TEXT("%d strike%s · swing when the ball arrives"), Strikes, Strikes == 1 ? TEXT("") : TEXT("s"));
		}
		const double Ms = Outcome.TimingMs;
		const FString Timing = Outcome.Contact.Quality >= PerfectCallQuality ? FString(TEXT("perfect timing"))
			: FString::Printf(TEXT("%d ms %s"), FMath::RoundToInt32(FMath::Abs(Ms)), Ms < 0.0 ? TEXT("early") : TEXT("late"));
		return Outcome.Points > 0 ? FString::Printf(TEXT("+%d · %s"), Outcome.Points, *Timing) : Timing;
	}

	FDerbyCall MakeCall(const FOutcome& Outcome, int32 Strikes)
	{
		FDerbyCall Call;
		Call.bMajor = IsHomeRun(Outcome.Result);
		Call.Tone = Outcome.Points > 0 ? TEXT("success") : (Outcome.bStrike ? TEXT("warning") : TEXT("neutral"));
		const bool bContact = Outcome.Result == EResult::InPlay || Outcome.Result == EResult::Foul;
		Call.Haptic = IsHomeRun(Outcome.Result) ? TEXT("celebration") : (bContact ? TEXT("impact") : TEXT("warning"));
		Call.Title = CallTitle(Outcome, Strikes);
		Call.Detail = CallDetail(Outcome, Strikes);
		return Call;
	}
}

ADerbyGameMode::ADerbyGameMode()
{
	ArcadeId = TEXT("derby");
	CommandLinePrefix = TEXT("Derby");
}

void ADerbyGameMode::SpawnVenue()
{
	UWorld* World = GetWorld();
	for (TActorIterator<ADerbyBallpark> It(World); It; ++It)
	{
		Park = *It;
		break;
	}
	if (!Park)
	{
		Park = World->SpawnActor<ADerbyBallpark>();
	}
}

void ADerbyGameMode::BuildRoomSeats(TConstArrayView<FMcSeat> Players)
{
	for (const FMcSeat& Player : Players)
	{
		FDerbySeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EDerbyKind::Bot : EDerbyKind::Phone;
		Seat.Skill = RoomBotSkill;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
}

void ADerbyGameMode::BuildLocalSeats(const FArcadeLaunchOptions& InLaunch)
{
	for (int32 i = 0; i < InLaunch.Locals + InLaunch.Bots; ++i)
	{
		FDerbySeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < InLaunch.Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - InLaunch.Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !InLaunch.bAutoPlay ? EDerbyKind::Local : EDerbyKind::Bot;
		Seat.Skill = bLocal ? LocalSkill : HouseBaseSkill + HouseSkillStep * (i % HouseSkillSteps);
		Seat.Color = MovecadeKart::Art::Hex(SeatColors[i % UE_ARRAY_COUNT(SeatColors)]);
		Seat.Look = FKartLook::MakeVaried(i + LookSeedBase);
	}
}

uint32 ADerbyGameMode::MatchSeed() const
{
	if (IsRoomMatch())
	{
		if (const URoomHostSubsystem* Room = GetRoom())
		{
			return RoomSeedBase ^ (RoomSeedStep * static_cast<uint32>(Room->GetRoom().MatchNumber + 1));
		}
	}
	return FDerbyDebugFlags::Parse(FCommandLine::Get()).Seed.Get(FPlatformTime::Cycles());
}

void ADerbyGameMode::SpawnGame()
{
	Game = SpawnArcadeGame<ADerbyGame>();
	Game->Setup(Park, Seats, GetWorld()->GetFirstPlayerController(), MatchSeed());
	Game->OnQuit = MakeQuitCallback();
	Game->OnRematch = MakeRematchCallback();
}

void ADerbyGameMode::BindRoomCallbacks(URoomHostSubsystem& Room)
{
	TWeakObjectPtr<ADerbyGameMode> WeakThis = this;
	TWeakObjectPtr<URoomHostSubsystem> WeakRoom = &Room;
	Game->OnTurn = [WeakThis, WeakRoom](const FString& PlayerId)
	{
		ADerbyGameMode* Mode = WeakThis.Get();
		URoomHostSubsystem* R = WeakRoom.Get();
		if (Mode && R)
		{
			R->AdvanceTurn(PlayerId);
			Mode->PromptPhones(*R, PlayerId);
		}
	};
	Game->OnPitch = [WeakThis, WeakRoom](const FString& PlayerId, const FPitch& Pitch)
	{
		ADerbyGameMode* Mode = WeakThis.Get();
		URoomHostSubsystem* R = WeakRoom.Get();
		if (Mode && R)
		{
			Mode->SendPitch(*R, PlayerId, Pitch);
		}
	};
	Game->OnOutcome = [WeakThis, WeakRoom](const FString& PlayerId, const FOutcome& Outcome)
	{
		ADerbyGameMode* Mode = WeakThis.Get();
		URoomHostSubsystem* R = WeakRoom.Get();
		if (Mode && R && Mode->Game)
		{
			Mode->SendCall(*R, PlayerId, Outcome);
			Mode->PushLiveScores();
		}
	};
	Game->OnFinished = [WeakThis]()
	{
		if (ADerbyGameMode* Mode = WeakThis.Get())
		{
			Mode->CompleteRoomMatch();
		}
	};
}

void ADerbyGameMode::PromptPhones(URoomHostSubsystem& Room, const FString& BatterId) const
{
	if (!Game)
	{
		return;
	}
	const FDerbySeat* Up = Game->SeatOf(BatterId);
	for (const FDerbySeat& Seat : Game->GetSeats())
	{
		if (Seat.Kind != EDerbyKind::Phone)
		{
			continue;
		}
		const bool bUp = Seat.Id == BatterId;
		const FString Title = bUp ? TEXT("Swing your phone like a bat") : FString::Printf(TEXT("%s is batting"), Up ? *Up->Name : TEXT("Someone"));
		const FString Subtitle = bUp ? TEXT("Hold it by the bottom, screen to you. Swing as the ball reaches you.") : TEXT("Get loose: you're up soon");
		Room.SendPadPrompt(Seat.Id, Title, Subtitle, bUp ? TEXT("READY") : TEXT(""), TEXT(""), Hex(Seat.Color), TEXT("swing"));
	}
}

void ADerbyGameMode::SendPitch(URoomHostSubsystem& Room, const FString& BatterId, const FPitch& Pitch) const
{
	// The phones' timing hint (phone-sim.mjs swings to it): the pitch number, its type and speed, when it arrives.
	Room.SendGameEvent(PitchEvent, FString::Printf(TEXT("{\"playerId\":%s,\"pitch\":%d,\"type\":\"%s\",\"mph\":%d,\"arriveInMs\":%d}"),
		*MovecadeKart::Net::QuoteJson(BatterId), Pitch.Index + 1, *FString(PitchName(Pitch.Type)).ToLower(), FMath::RoundToInt32(Pitch.SpeedMph),
		FMath::RoundToInt32(Pitch.FlightMs)));
}

void ADerbyGameMode::SendCall(URoomHostSubsystem& Room, const FString& BatterId, const FOutcome& Outcome) const
{
	const FDerbyCall Call = MakeCall(Outcome, Game->ShownStrikes());
	Room.SendPlayFeedback(BatterId, Call.Title, Call.Detail, Call.Tone, Call.Haptic, Call.bMajor);
}

void ADerbyGameMode::HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame)
{
	if (Game)
	{
		Game->FeedPhone(Seat.PlayerId, Frame);
	}
}

void ADerbyGameMode::HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data)
{
	// The motion pad's A button: READY while the pitcher waits, a button swing while the ball is on its way.
	TCHAR Button = 0;
	if (!Game || Event != MovecadeKart::Net::PadPressEvent || !Data.IsValid() || !MovecadeKart::Net::ReadPadButton(*Data, Button))
	{
		return;
	}
	if (Button == TEXT('a'))
	{
		Game->PressButton(FromPlayerId);
	}
}

bool ADerbyGameMode::CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const
{
	if (!Game)
	{
		return false;
	}
	for (const FBatter& Batter : Game->GetMatch().Batters)
	{
		FMcScoreEntry& Entry = OutScores.AddDefaulted_GetRef();
		Entry.PlayerId = Batter.Id;
		Entry.Score = FMath::Clamp(Batter.Score, 0, MovecadeKart::Net::MaxScore);
	}
	return true;
}

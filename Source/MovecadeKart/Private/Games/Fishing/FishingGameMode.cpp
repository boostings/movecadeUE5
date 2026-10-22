#include "Games/Fishing/FishingGameMode.h"

#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Games/Fishing/FishingLake.h"
#include "Misc/CommandLine.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"

DEFINE_LOG_CATEGORY_STATIC(LogFishingMode, Log, All);

using namespace MovecadeKart::Fishing;

namespace
{
	const TCHAR* const HouseNames[] = { TEXT("Rosa"), TEXT("Mika"), TEXT("Dante"), TEXT("Priya") };

	/** House skill (0 sloppy .. 1 sharp): a local seat on autoplay fishes sharp, house bots vary by seat. */
	constexpr float AutoLocalSkill = 0.92f;
	constexpr float HouseSkillBase = 0.45f;
	constexpr float HouseSkillStep = 0.15f;
	constexpr int32 HouseSkillLevels = 3;
	constexpr float RoomBotSkill = 0.65f;
	/** FKartLook::MakeVaried's seed offset, so the house anglers don't wear the kart racers' looks. */
	constexpr int32 LookSeedOffset = 3;

	/** The phone's haptic strengths (0..1). */
	constexpr float NibbleHaptic = 0.3f;
	constexpr float BiteHaptic = 1.f;
	constexpr float RunHaptic = 0.7f;

	FString HexOf(const FLinearColor& Color)
	{
		const FColor S = Color.ToFColor(true);
		return FString::Printf(TEXT("#%02x%02x%02x"), S.R, S.G, S.B);
	}

	const TCHAR* LossTitle(ELoss Loss)
	{
		return Loss == ELoss::Snap ? TEXT("Line snapped!") : (Loss == ELoss::Escape ? TEXT("It got away!") : TEXT("Bait stolen!"));
	}

	const TCHAR* LossAdvice(ELoss Loss)
	{
		return Loss == ELoss::Snap ? TEXT("Too much tension. Ease off when it runs.") : (Loss == ELoss::Escape ? TEXT("Too slack. Keep reeling.") : TEXT("Hook faster when it bites."));
	}

	/** A catch's toast: the species (or the record), its weight, what it scored and the running total. */
	void SendCatchFeedback(URoomHostSubsystem& Room, const FEffect& Effect, const FAngler& Angler)
	{
		const FSpecies& Caught = Species()[FMath::Max(0, Effect.Species)];
		const bool bJunk = Effect.Kind == EEffectKind::Junk;
		const bool bRecord = Effect.Kind == EEffectKind::Record;
		const FString Title = bRecord ? FString(TEXT("NEW RECORD!")) : FString::Printf(TEXT("%s!"), Caught.Name);
		const FString Detail = bJunk ? FString(TEXT("Junk scores nothing, but your combo lives"))
			: FString::Printf(TEXT("%s · +%s · total %s"), *FormatWeight(Effect.WeightG), *FormatWeight(Effect.ScoreG), *FormatWeight(Angler.Score));
		Room.SendPlayFeedback(Effect.PlayerId, Title, Detail, TEXT("success"), bJunk ? TEXT("impact") : TEXT("celebration"), bRecord || Caught.Kind == EKind::Trophy);
	}

	void SendLossFeedback(URoomHostSubsystem& Room, const FEffect& Effect)
	{
		const TCHAR* const Advice = Effect.Kind == EEffectKind::Snap ? TEXT("Ease off when the fish runs")
			: (Effect.Kind == EEffectKind::TooEarly ? TEXT("Wait for the big buzz") : TEXT("Combo lost"));
		Room.SendPlayFeedback(Effect.PlayerId, Effect.Label, Advice, TEXT("warning"), TEXT("warning"), false);
	}
}

AFishingGameMode::AFishingGameMode()
{
	ArcadeId = TEXT("fishing");
	CommandLinePrefix = TEXT("Fishing");
	DefaultLocals = 1;
	DefaultBots = 2;
}

FString AFishingGameMode::PromptJson(const FAngler& A, const FLinearColor& Accent)
{
	FString Title, Subtitle, ButtonA, Mode;
	PromptFields(A, Title, Subtitle, ButtonA, Mode);
	return MovecadeKart::Net::PadPromptData(A.PlayerId, Title, Subtitle, ButtonA, FString(), HexOf(Accent), Mode);
}

void AFishingGameMode::PromptFields(const FAngler& A, FString& Title, FString& Subtitle, FString& ButtonA, FString& Mode)
{
	Mode = TEXT("flick");
	ButtonA.Reset();
	switch (A.State)
	{
	case EAnglerState::Idle:
		Title = TEXT("CAST!");
		Subtitle = TEXT("Flick the phone forward like a rod. Harder casts reach the big fish.");
		ButtonA = TEXT("HOLD TO CAST");
		break;
	case EAnglerState::Casting:
	case EAnglerState::Waiting:
		Title = TEXT("Wait for the bite...");
		Subtitle = TEXT("When the phone buzzes, jerk it up to hook. Roll your wrist to reel the lure back.");
		ButtonA = TEXT("HOOK");
		break;
	case EAnglerState::Bite:
		Title = TEXT("BITE! JERK UP!");
		Subtitle = TEXT("Snap the phone up now to set the hook.");
		ButtonA = TEXT("HOOK");
		break;
	case EAnglerState::Fighting:
		Title = TEXT("REEL IT IN!");
		Subtitle = TEXT("Roll your wrist to reel. Keep the tension in the green; ease off when it runs.");
		Mode = TEXT("swing");
		break;
	case EAnglerState::Landed:
	{
		const FSpecies& Caught = Species()[A.LastCatch.Species];
		Title = FString::Printf(TEXT("%s!"), Caught.Name);
		Subtitle = Caught.Kind == EKind::Junk ? FString(TEXT("Junk. Back in the lake it goes.")) : FString::Printf(TEXT("%s on the scale"), *FormatWeight(A.LastCatch.WeightG));
		break;
	}
	case EAnglerState::Lost:
		Title = LossTitle(A.LastLoss);
		Subtitle = LossAdvice(A.LastLoss);
		break;
	}
}

void AFishingGameMode::SpawnVenue()
{
	UWorld* World = GetWorld();
	TActorIterator<AFishingLake> Found(World);
	Lake = Found ? *Found : World->SpawnActor<AFishingLake>();
}

void AFishingGameMode::BuildRoomSeats(TConstArrayView<FMcSeat> Players)
{
	for (const FMcSeat& Player : Players)
	{
		FFishingSeat& Seat = Seats.AddDefaulted_GetRef();
		Seat.Id = Player.PlayerId;
		Seat.Name = Player.Name;
		Seat.Kind = Player.bBot ? EAnglerKind::Bot : EAnglerKind::Phone;
		Seat.Skill = RoomBotSkill;
		Seat.Color = Player.PaintColor;
		Seat.Look = FKartLook::FromRoomAvatar(Player.Vehicle, Player.Face, Player.Body, Player.SeatIndex());
	}
}

void AFishingGameMode::BuildLocalSeats(const FArcadeLaunchOptions& InLaunch)
{
	for (int32 i = 0; i < InLaunch.Locals + InLaunch.Bots; ++i)
	{
		FFishingSeat& Seat = Seats.AddDefaulted_GetRef();
		const bool bLocal = i < InLaunch.Locals;
		Seat.Id = FString::Printf(TEXT("seat-%d"), i);
		Seat.Name = bLocal ? FString::Printf(TEXT("Player %d"), i + 1) : HouseNames[(i - InLaunch.Locals) % UE_ARRAY_COUNT(HouseNames)];
		Seat.Kind = bLocal && !InLaunch.bAutoPlay ? EAnglerKind::Local : EAnglerKind::Bot;
		Seat.Skill = bLocal ? AutoLocalSkill : HouseSkillBase + HouseSkillStep * (i % HouseSkillLevels);
		Seat.Color = AFishingLake::LaneColor(i);
		Seat.Look = FKartLook::MakeVaried(i + LookSeedOffset);
	}
}

void AFishingGameMode::SpawnGame()
{
	DebugFlags = FFishingDebugFlags::Parse(FCommandLine::Get());
	const uint32 Seed = DebugFlags.Seed.Get(static_cast<uint32>(FDateTime::Now().GetTicks() & 0x7fffffff));
	Game = SpawnArcadeGame<AFishingGame>();
	Game->Setup(Lake, Seats, GetWorld()->GetFirstPlayerController(), Seed, DebugFlags);
	Game->OnQuit = MakeQuitCallback();
	Game->OnRematch = MakeRematchCallback();
}

bool AFishingGameMode::IsPhone(const FString& PlayerId) const
{
	const FFishingSeat* Seat = Game ? Game->SeatOf(PlayerId) : nullptr;
	return Seat && Seat->Kind == EAnglerKind::Phone;
}

void AFishingGameMode::SendPromptIfPhone(URoomHostSubsystem& Room, const FAngler& Angler) const
{
	if (!IsPhone(Angler.PlayerId))
	{
		return;
	}
	FString Title, Subtitle, ButtonA, Mode;
	PromptFields(Angler, Title, Subtitle, ButtonA, Mode);
	Room.SendPadPrompt(Angler.PlayerId, Title, Subtitle, ButtonA, FString(), HexOf(AFishingLake::LaneColor(Angler.LaneIndex)), Mode);
}

void AFishingGameMode::BindRoomCallbacks(URoomHostSubsystem& Room)
{
	// The game holds these callbacks: both ends are weak, so a call after either is gone does nothing.
	const TWeakObjectPtr<AFishingGameMode> WeakThis = this;
	const TWeakObjectPtr<URoomHostSubsystem> WeakRoom = &Room;
	// Everyone fishes at once: no turn holder. The phones count down with the rules' 3-2-1.
	Game->OnMatchStart = [WeakThis, WeakRoom]()
	{
		const AFishingGameMode* Mode = WeakThis.Get();
		URoomHostSubsystem* R = WeakRoom.Get();
		if (!Mode || !R || !Mode->Game)
		{
			return;
		}
		R->AdvanceTurn(FString());
		R->PublishMatchCountdown(static_cast<int32>(CountdownMs));
		for (const FAngler& A : Mode->Game->GetMatch().Anglers)
		{
			Mode->SendPromptIfPhone(*R, A);
		}
	};
	// The prompt follows the angler's moment (and is sent again when the horn goes). Casting keeps the wait prompt
	// the phone already shows: it would only flash.
	Game->OnAnglerState = [WeakThis, WeakRoom](const FAngler& A)
	{
		const AFishingGameMode* Mode = WeakThis.Get();
		URoomHostSubsystem* R = WeakRoom.Get();
		if (Mode && R && A.State != EAnglerState::Casting)
		{
			Mode->SendPromptIfPhone(*R, A);
		}
	};
	Game->OnEffect = [WeakThis, WeakRoom](const FEffect& E)
	{
		const AFishingGameMode* Mode = WeakThis.Get();
		if (URoomHostSubsystem* R = WeakRoom.Get(); Mode && R)
		{
			Mode->SendEffectFeedback(*R, E);
		}
	};
	Game->OnFinished = [WeakThis]()
	{
		if (AFishingGameMode* Mode = WeakThis.Get())
		{
			Mode->CompleteRoomMatch();
		}
	};
}

void AFishingGameMode::SendEffectFeedback(URoomHostSubsystem& Room, const FEffect& E) const
{
	// Toasts and haptics: a buzz on the nibble, a hard one on the bite, a celebration on the catch.
	const FAngler* A = Game ? Game->GetMatch().Find(E.PlayerId) : nullptr;
	if (!A || !IsPhone(E.PlayerId))
	{
		return;
	}
	const FMcSeat* Player = Room.GetRoom().FindByPlayerId(E.PlayerId);
	const int32 SeatIndex = Player ? Player->SeatIndex() : INDEX_NONE;
	switch (E.Kind)
	{
	case EEffectKind::Nibble:
		Room.SendHaptic(SeatIndex, NibbleHaptic, TEXT("nibble"));
		break;
	case EEffectKind::Bite:
		Room.SendHaptic(SeatIndex, BiteHaptic, TEXT("bite"));
		Room.SendPlayFeedback(E.PlayerId, TEXT("BITE!"), TEXT("Jerk the phone up to hook it"), TEXT("success"), TEXT("impact"), false);
		break;
	case EEffectKind::Hooked:
		Room.SendPlayFeedback(E.PlayerId, E.Label, TEXT("Roll your wrist to reel"), TEXT("success"), TEXT("impact"), false);
		break;
	case EEffectKind::Burst:
		Room.SendHaptic(SeatIndex, RunHaptic, TEXT("run"));
		break;
	case EEffectKind::Catch:
	case EEffectKind::Record:
	case EEffectKind::Junk:
		SendCatchFeedback(Room, E, *A);
		break;
	case EEffectKind::Snap:
	case EEffectKind::Escape:
	case EEffectKind::StolenBait:
	case EEffectKind::TooEarly:
		SendLossFeedback(Room, E);
		break;
	default:
		break;
	}
}

void AFishingGameMode::HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame)
{
	Game->FeedPhone(Seat.PlayerId, Frame);
}

void AFishingGameMode::HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data)
{
	// The motion pad's button: hold to charge a cast, press to hook. Fishing has no second button.
	TCHAR Button = 0;
	if (!Data.IsValid() || !MovecadeKart::Net::ReadPadButton(*Data, Button) || Button != TEXT('a'))
	{
		return;
	}
	if (Event == MovecadeKart::Net::PadPressEvent)
	{
		Game->PadPress(FromPlayerId);
	}
	else if (Event == MovecadeKart::Net::PadReleaseEvent)
	{
		Game->PadRelease(FromPlayerId);
	}
}

bool AFishingGameMode::CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const
{
	// Nothing to report during the 3-2-1: everyone is still on zero.
	if (!Game || Game->GetMatch().Phase == EMatchPhase::Countdown)
	{
		return false;
	}
	for (const FAngler& A : Game->GetMatch().Anglers)
	{
		FMcScoreEntry& Entry = OutScores.AddDefaulted_GetRef();
		Entry.PlayerId = A.PlayerId;
		// Grams: whole numbers inside the room's score range.
		Entry.Score = FMath::Clamp(A.Score, 0, MovecadeKart::Net::MaxScore);
	}
	if (bFinal)
	{
		FString Line;
		for (const FMcScoreEntry& Entry : OutScores)
		{
			Line += FString::Printf(TEXT("%s=%d;"), *Entry.PlayerId, Entry.Score);
		}
		UE_LOG(LogFishingMode, Display, TEXT("Fishing: complete-game %s"), *Line);
	}
	return true;
}

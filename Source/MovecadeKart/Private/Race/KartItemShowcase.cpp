// AKartItemPresenter's -KartFxShowcase: a cosmetic tour of the item effects on seat 0, with screenshots, or with
// =items every item mesh floating in a row ahead of the grid. It fakes item events for the presenter only; the item
// world and the sim never see them.
#include "Race/KartItemPresenter.h"

#include "FX/KartFxComponent.h"
#include "GameFramework/PlayerController.h"
#include "KartCoords.h"
#include "KartDebugFlags.h"
#include "KartPawn.h"
#include "Race/KartRaceManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartShowcase, Log, All);

using namespace MovecadeKart;

namespace
{
	const TCHAR* const ShowcaseBeats[] = { TEXT("explosion"), TEXT("lightning"), TEXT("prism"), TEXT("rocket"), TEXT("ink"), TEXT("hit"), TEXT("confetti"), TEXT("box") };
	constexpr int32 NumShowcaseBeats = UE_ARRAY_COUNT(ShowcaseBeats);
	const TCHAR* const ItemsBeat = TEXT("items");

	/** The first beat 1.5 s into the race, then one every 3 s; the item states it forces last 2.8 s. */
	constexpr double FirstBeatDelay = 1.5;
	constexpr double BeatSeconds = 3.0;
	constexpr double ForcedStateSeconds = 2.8;
	/** Each beat is captured just after it starts and once it has developed. */
	constexpr double ShotDelays[] = { 0.12, 0.45 };
	/** Frames per frame-time summary. */
	constexpr int32 FrameSampleCount = 300;
}

void AKartItemPresenter::StartShowcase(const AKartRaceManager& Race)
{
	const FKartDebugFlags& Flags = FKartDebugFlags::Get();
	bShowcase = Flags.bFxShowcase;
	ShowcaseBeat = Flags.FxShowcaseBeat;
	ShowcaseNext = -1.0;
	ShowcaseIndex = 0;
	if (bShowcase && Race.GetRacers().Num() > 0)
	{
		const AKartPawn* Kart = Race.GetRacers()[0].Kart;
		ShowcaseOrigin = Kart->GetActorLocation();
		ShowcaseForward = Kart->GetActorForwardVector();
	}
}

void AKartItemPresenter::UpdateShowcase(double Time)
{
	AKartRaceManager* Race = Manager.Get();
	if (!bShowcase || ShowcaseBeat == ItemsBeat || Race->GetPhase() != EKartRacePhase::Racing || Race->GetRacers().Num() == 0)
	{
		return;
	}
	if (ShowcaseNext < 0.0)
	{
		ShowcaseNext = Time + FirstBeatDelay;
	}
	if (Time < ShowcaseNext)
	{
		return;
	}
	ShowcaseNext = Time + BeatSeconds;
	int32 Beat = ShowcaseIndex++ % NumShowcaseBeats;
	for (int32 i = 0; i < NumShowcaseBeats; ++i)
	{
		if (ShowcaseBeat == ShowcaseBeats[i])
		{
			Beat = i;
		}
	}
	AKartPawn* Kart = Race->GetRacers()[0].Kart;
	UKartFxComponent* KartFx = Kart->FindComponentByClass<UKartFxComponent>();
	const FVector At = Kart->GetActorLocation();
	// Far enough ahead that the kart reaches the effect about half a second in.
	const FVector Ahead = At + Kart->GetActorForwardVector() * (500.0 + Kart->GetVelocity().Size() * 0.3);
	TArray<FKartItemEvent> Events;
	auto Event = [&](EKartItemEventType Type, EKartItem Item)
	{
		FKartItemEvent E;
		E.Type = Type;
		E.Seat = 0;
		E.Other = 0;
		E.Item = Item;
		E.Position = Coords::FromUE(Ahead);
		Events.Add(E);
	};
	const FString Name = ShowcaseBeats[Beat];
	UE_LOG(LogKartShowcase, Display, TEXT("KartFxShowcase: %s (2 shots)"), *Name);
	for (const double Delay : ShotDelays)
	{
		ShowcaseShots.Add(Time + Delay);
	}
	if (Name == TEXT("explosion"))
	{
		Event(EKartItemEventType::Explosion, EKartItem::Bomb);
		Events.Last().Radius = 5.0;
		Event(EKartItemEventType::Used, EKartItem::Nitro);
	}
	else if (Name == TEXT("lightning"))
	{
		Event(EKartItemEventType::Used, EKartItem::Lightning);
		for (int32 Seat = 0; Seat < Race->GetRacers().Num(); ++Seat)
		{
			Event(EKartItemEventType::Lightning, EKartItem::Lightning);
			Events.Last().Seat = Seat;
		}
	}
	else if (Name == TEXT("prism") && KartFx)
	{
		Event(EKartItemEventType::Used, EKartItem::Prism);
		Event(EKartItemEventType::Invincible, EKartItem::Prism);
		KartFx->Overrides().PrismUntil = Time + ForcedStateSeconds;
	}
	else if (Name == TEXT("rocket") && KartFx)
	{
		Event(EKartItemEventType::RocketStart, EKartItem::Rocket);
		KartFx->Overrides().RocketUntil = Time + ForcedStateSeconds;
	}
	else if (Name == TEXT("ink") && KartFx)
	{
		Event(EKartItemEventType::Used, EKartItem::Ink);
		Event(EKartItemEventType::Inked, EKartItem::Ink);
		KartFx->Overrides().InkStart = Time;
		KartFx->Overrides().InkUntil = Time + ForcedStateSeconds;
	}
	else if (Name == TEXT("hit"))
	{
		Event(EKartItemEventType::Hit, EKartItem::Seeker);
		Event(EKartItemEventType::SpinOut, EKartItem::Seeker);
		Event(EKartItemEventType::Used, EKartItem::TripleNitro);
	}
	else if (Name == TEXT("confetti"))
	{
		HandleCue(0, TEXT("kart.finish"));
	}
	else if (Name == TEXT("box"))
	{
		Event(EKartItemEventType::Pickup, EKartItem::None);
		Events.Last().Position = Coords::FromUE(At + Kart->GetActorForwardVector() * 500.0);
	}
	HandleEvents(Events, Time);
}

void AKartItemPresenter::UpdateShowcaseCapture(double Time, float DeltaSeconds)
{
	if (bShowcase)
	{
		// A rough frame-time readout for the effects-heavy showcase (the window's whole frame, render included).
		ShowcaseFrames.Add(DeltaSeconds);
		if (ShowcaseFrames.Num() >= FrameSampleCount)
		{
			ShowcaseFrames.Sort();
			double Sum = 0.0;
			for (const float F : ShowcaseFrames)
			{
				Sum += F;
			}
			UE_LOG(LogKartShowcase, Display, TEXT("KartFxShowcase: last %d frames avg %.2f ms, p95 %.2f ms"), ShowcaseFrames.Num(),
				Sum / ShowcaseFrames.Num() * 1000.0, ShowcaseFrames[FMath::FloorToInt32(ShowcaseFrames.Num() * 0.95)] * 1000.0);
			ShowcaseFrames.Reset();
		}
	}
	if (ShowcaseShots.Num() > 0 && Time >= ShowcaseShots[0])
	{
		ShowcaseShots.RemoveAt(0);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->ConsoleCommand(TEXT("Shot showui"));
		}
	}
}

void AKartItemPresenter::AddShowcaseItems(double Time)
{
	if (!bShowcase || ShowcaseBeat != ItemsBeat)
	{
		return;
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, ShowcaseForward);
	const int32 Count = static_cast<int32>(EKartItem::Count) - 1;
	for (int32 i = 1; i <= Count; ++i)
	{
		const EKartItem Item = static_cast<EKartItem>(i);
		const FVector At = ShowcaseOrigin + ShowcaseForward * 650.0 + Right * ((i - 1 - (Count - 1) * 0.5) * 115.0) + FVector(0.0, 0.0, 130.0 + 12.0 * FMath::Sin(Time * 2.0 + i));
		const double Scale = Item == EKartItem::Rocket ? 0.3 : (Item == EKartItem::Banana ? 1.8 : (Item == EKartItem::Bomb ? 0.9 : 1.3));
		const FTransform Shown(FRotator(0.0, Time * 45.0 + i * 30.0, 0.0), At, FVector(Scale));
		if (Item == EKartItem::Bomb)
		{
			BatchFor(Item).Add(Shown, 0.f); // the bomb's custom float (its glow pulse) stays aligned
		}
		else
		{
			BatchFor(Item).Add(Shown);
		}
	}
}

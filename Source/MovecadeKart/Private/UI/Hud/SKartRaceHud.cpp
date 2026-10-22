#include "UI/Hud/SKartRaceHud.h"

#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Race/KartRaceManager.h"
#include "Race/SKartRaceOverlay.h"
#include "UI/Hud/KartRaceHudShared.h"
#include "UI/KartAccessibility.h"
#include "UI/KartUIPainter.h"

#define LOCTEXT_NAMESPACE "KartHud"

using namespace KartUI;
using namespace KartRaceHud;
using namespace MovecadeKart;

namespace
{
	/** The speed readout eases towards the kart's speed at this rate (FInterpTo speed). */
	constexpr double SpeedEaseRate = 12.0;
	/** The race clock: under the lap counter in one view, on the split line otherwise; the warning chip above (or below) it. */
	namespace ClockArt
	{
		constexpr double SoloDrop = 118.0;
		/** Four views: the clock sits this fraction of the screen above the centre minimap. */
		constexpr double FourViewLift = 0.17;
		constexpr float WarnPulse = 0.06f;
		constexpr float WarnPulseSpeed = 8.f;
		constexpr double SoloWarnDrop = 24.0;
		constexpr double SplitWarnDrop = 44.0;
	}

	/** Three views: the two empty bottom corners become striped panels, the minimap left and the standings right. */
	namespace CornerArt
	{
		constexpr int32 Stripes = 7;
		/** The stripes are spread over the panel in this many steps and start this far left of it. */
		constexpr double StripeSteps = 6.0;
		constexpr double StripeLead = 60.0;
		constexpr double StripeTopWidth = 40.0;
		constexpr double StripeBottomFrom = 60.0;
		constexpr double StripeBottomTo = 100.0;
		constexpr float StripeAlpha = 0.03f;
		/** The minimap's height as a fraction of the panel's, and its box's left offset in map heights. */
		constexpr double MapHeight = 0.62;
		constexpr double MapLead = 0.6;
		constexpr double Middle = 0.75;
		constexpr float RailScale = 0.85f;
		constexpr double RailRow = 52.0;
		constexpr double RailOffset = 125.0;
		constexpr float TitleSize = 20.f;
		constexpr double TitleLift = 40.0;
	}

	/** Four views: the shared minimap's backing disc. */
	namespace DiscArt
	{
		constexpr float FillAlpha = 0.7f;
		constexpr float RimAlpha = 0.6f;
		constexpr float Rim = 3.f;
	}

	/** The "RACE OVER" banner across the whole screen. */
	namespace RaceOverArt
	{
		constexpr double Height = 190.0;
		constexpr double Centre = 0.42;
		constexpr double Hold = 1.6;
	}
}

void SKartRaceHud::Construct(const FArguments& InArgs)
{
	Manager = InArgs._Manager;
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(true);
	// Painted from live race state every frame: never cache it under global invalidation.
	ForceVolatile(true);
}

void SKartRaceHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const AKartRaceManager* Race = Manager.Get();
	if (!Race || !Race->GetTrack())
	{
		return;
	}
	const double Now = InCurrentTime;
	const int32 Phase = static_cast<int32>(Race->GetPhase());
	if (Phase != LastPhase)
	{
		if (Race->GetPhase() == EKartRacePhase::Waiting)
		{
			Seats.Reset(); // a restart: every seat animates from scratch
		}
		if (Race->GetPhase() == EKartRacePhase::Finished)
		{
			RaceFinishedAt = Now;
		}
		LastPhase = Phase;
		PhaseAt = Now;
	}
	const TArray<FKartRacer>& Racers = Race->GetRacers();
	Seats.SetNum(Racers.Num());
	const int32 Laps = Race->GetTrack()->Laps;
	for (int32 i = 0; i < Racers.Num(); ++i)
	{
		const FKartRacer& Racer = Racers[i];
		if (!Racer.Kart)
		{
			continue;
		}
		FSeatAnim& A = Seats[i];
		const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
		if (A.Place == 0)
		{
			A.Place = Racer.Place;
		}
		else if (Racer.Place != A.Place)
		{
			A.PlaceDir = Racer.Place < A.Place ? 1 : -1;
			A.Place = Racer.Place;
			A.PlaceAt = Now;
		}
		const int32 Lap = FMath::Min(S.LapsCompleted + 1, Laps);
		if (A.Lap == 0)
		{
			A.Lap = Lap;
		}
		else if (Lap != A.Lap)
		{
			A.Lap = Lap;
			A.LapAt = Now;
			if (Lap == Laps && Laps > 1 && !Racer.bFinished)
			{
				A.FinalLapAt = Now;
			}
		}
		if (Racer.bFinished && !A.bFinished)
		{
			A.FinishedAt = Now;
		}
		A.bFinished = Racer.bFinished;
		const double Kmh = FMath::Abs(S.Speed) * 3.6;
		A.ShownSpeed = FMath::FInterpTo(A.ShownSpeed, Kmh, InDeltaTime, SpeedEaseRate);
	}
}

void SKartRaceHud::GatherViews(const FVector2D& Screen) const
{
	Views.Reset();
	const AKartRaceManager* Race = Manager.Get();
	if (!Race || !KartRaceHud::IsRacingPhase(Race->GetPhase()))
	{
		return;
	}
	for (const FKartRacer& Racer : Race->GetRacers())
	{
		const APlayerController* PC = Racer.Viewer;
		const ULocalPlayer* Local = PC ? PC->GetLocalPlayer() : nullptr;
		if (!Local || PC->GetViewTarget() != Racer.Kart)
		{
			continue;
		}
		FView& View = Views.AddDefaulted_GetRef();
		View.Pos = Local->Origin * Screen;
		View.Size = Local->Size * Screen;
		View.Seat = Racer.Seat;
	}
	if (Views.Num() == 1)
	{
		Views[0].Pos = FVector2D::ZeroVector;
		Views[0].Size = Screen;
	}
}

int32 SKartRaceHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const AKartRaceManager* Race = Manager.Get();
	if (!Race || !Race->GetTrack() || Race->GetRacers().Num() == 0)
	{
		return LayerId;
	}
	int32 Layer = LayerId;
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const FVector2D Screen = P.Size();
	const double Now = FSlateApplication::Get().GetCurrentTime();
	const EKartRacePhase Phase = Race->GetPhase();

	if (Phase == EKartRacePhase::Waiting || Phase == EKartRacePhase::Intro)
	{
		PaintIntro(P, *Race, Screen, Now);
	}

	GatherViews(Screen);
	for (const FView& View : Views)
	{
		PaintView(P, *Race, View, Views.Num(), Now);
	}
	if (KartRaceHud::IsRacingPhase(Phase) && Views.Num() > 0)
	{
		PaintClock(P, *Race, Screen, Now);
		PaintSharedSpace(P, *Race, Screen);
	}

	if (Phase == EKartRacePhase::Countdown || (Phase == EKartRacePhase::Racing && Race->GetRaceMs() < GoShownMs))
	{
		PaintCountdown(P, *Race, Screen, Now);
	}
	if (Phase == EKartRacePhase::Finished && Now - RaceFinishedAt < FinishBannerSeconds)
	{
		const FVector2D Size(Screen.X, RaceOverArt::Height);
		PaintBanner(P, FVector2D(0.0, Screen.Y * RaceOverArt::Centre - Size.Y * 0.5), Size, LOCTEXT("RaceOver", "RACE OVER"), Color::Hot, Now - RaceFinishedAt,
			RaceOverArt::Hold, 1.f, true);
	}
	if (Race->IsPaused() && Race->GetPauseReason() != TEXT("local"))
	{
		PaintRoomPause(P, Screen, Now);
	}
	return Layer;
}

void SKartRaceHud::PaintClock(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const
{
	using namespace ClockArt;
	const FVector2D Margin = Screen * Metric::SafeFraction;
	const bool bSingle = Views.Num() == 1;
	const float Scale = bSingle ? 1.f : SplitScale;
	// The race clock: under the lap counter in a single view, on the split line otherwise.
	const int64 Second = FMath::FloorToInt64(FMath::Max(0.0, Race.GetRaceMs()) / 1000.0);
	if (Second != ClockSecond)
	{
		ClockSecond = Second;
		ClockText = FText::FromString(SKartRaceOverlay::FormatRaceTime(Race.GetRaceMs(), false));
	}
	const FVector2D ClockAnchor = bSingle ? FVector2D(Screen.X - Margin.X, Margin.Y + SoloDrop)
		: FVector2D(Screen.X * 0.5, Screen.Y * 0.5 - (Views.Num() >= 4 ? Screen.Y * FourViewLift : 0.0));
	const FVector2D ClockAlign = bSingle ? FVector2D(1.0, 0.0) : FVector2D(0.5, 0.5);
	KartRaceHud::Chip(P, ClockAnchor, ClockAlign, LOCTEXT("Time", "TIME"), ClockText, Color::White, Scale);

	const double Remaining = Race.GetDeadlineMs() - Race.GetRaceMs();
	if (Race.GetPhase() != EKartRacePhase::Racing || Remaining > DeadlineWarnMs)
	{
		return;
	}
	const float Pulse = 1.f + WarnPulse * FKartAccessibility::MotionScale() * FMath::Sin(static_cast<float>(Now) * WarnPulseSpeed);
	const int32 Left = FMath::CeilToInt32(Remaining / 1000.0);
	if (Left != WarnSecond)
	{
		WarnSecond = Left;
		WarnText = FText::FromString(FString::Printf(TEXT("%ds"), Left));
	}
	const FVector2D WarnAnchor(Screen.X * 0.5, bSingle ? Margin.Y + SoloWarnDrop : Screen.Y * 0.5 + SplitWarnDrop * Scale);
	KartRaceHud::Chip(P.Transformed(WarnAnchor, Pulse), WarnAnchor, FVector2D(0.5, 0.0), LOCTEXT("EndsIn", "RACE ENDS IN"), WarnText, FKartAccessibility::Bad(), Scale);
}

void SKartRaceHud::PaintSharedSpace(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen) const
{
	if (Views.Num() == 3)
	{
		using namespace CornerArt;
		// Three views fill the top half and the bottom middle, leaving a quarter-screen panel in each bottom corner.
		const FVector2D Corner(Screen.X * 0.25, Screen.Y * 0.5);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector2D Pos(Side == 0 ? 0.0 : Screen.X - Corner.X, Screen.Y * 0.5);
			P.Gradient(Pos, Corner, Color::NightSoft, Color::Night);
			for (int32 Stripe = 0; Stripe < Stripes; ++Stripe)
			{
				const double X = Pos.X + Stripe * Corner.X / StripeSteps - StripeLead;
				P.Polygon({ FVector2D(X, Pos.Y), FVector2D(X + StripeTopWidth, Pos.Y), FVector2D(X + StripeBottomTo, Screen.Y), FVector2D(X + StripeBottomFrom, Screen.Y) },
					Color::WithAlpha(Color::White, StripeAlpha));
			}
		}
		const double H = Corner.Y * MapHeight;
		PaintMinimap(P, Race, FVector2D(Corner.X * 0.5 - H * MapLead, Screen.Y * Middle - H * 0.5), static_cast<float>(H), INDEX_NONE);
		const double RailH = Race.GetStandings().Num() * RailRow * RailScale;
		P.Text(LOCTEXT("Standings", "STANDINGS"), Font(EFont::Label, TitleSize), FVector2D(Screen.X - Corner.X * 0.5, Screen.Y * Middle - RailH * 0.5 - TitleLift), Color::Sun,
			FVector2D(0.5, 0.0));
		PaintStandings(P, Race, FVector2D(Screen.X - Corner.X * 0.5 + RailOffset * RailScale, Screen.Y * Middle - RailH * 0.5), RailScale);
	}
	else if (Views.Num() >= 4)
	{
		const double H = Screen.Y * SharedMapFraction;
		const FVector2D Centre = Screen * 0.5;
		P.Circle(Centre, static_cast<float>(H * SharedMapDisc), Color::WithAlpha(Color::Night, DiscArt::FillAlpha), Color::WithAlpha(Color::White, DiscArt::RimAlpha), DiscArt::Rim);
		PaintMinimap(P, Race, Centre - FVector2D(H * 0.5, H * 0.5), static_cast<float>(H), INDEX_NONE);
	}
}

#undef LOCTEXT_NAMESPACE

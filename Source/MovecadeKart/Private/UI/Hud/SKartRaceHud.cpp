#include "UI/Hud/SKartRaceHud.h"

#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Race/KartRaceManager.h"
#include "Race/SKartRaceOverlay.h"
#include "UI/Hud/KartItemIcons.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartUISubsystem.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "KartHud"

using namespace KartUI;
using namespace MovecadeKart;

namespace
{
	/** The sim's top speed is 40 SU (m/s); the HUD shows km/h (B12). */
	constexpr double TopSpeedKmh = 144.0;
	constexpr double FinishBannerSeconds = 2.6;

	bool IsRacingPhase(EKartRacePhase Phase)
	{
		return Phase == EKartRacePhase::Countdown || Phase == EKartRacePhase::Racing;
	}

	/** What the status pill says (KartHUD's priority order), and whether it's an alert. */
	FText StatusFor(const FKartState& S, const FKartItemSlot& Slot, double ItemNow, bool& bAlert)
	{
		bAlert = true;
		if (ItemNow < Slot.SpinUntil) return LOCTEXT("SpunOut", "SPUN OUT!");
		if (S.bWrongWay) return LOCTEXT("WrongWay", "WRONG WAY");
		if (S.OffTrackSince >= 0.0) return LOCTEXT("BackToTrack", "Back to the track...");
		if (S.BoostMultiplier < 1.0)
		{
			return S.ActiveBoost == TEXT("EXPLODED") ? LOCTEXT("Boom", "BOOM!") : S.ActiveBoost == TEXT("LIGHTNING") ? LOCTEXT("Zapped", "ZAPPED!")
				: S.ActiveBoost == TEXT("SPUN_OUT") ? LOCTEXT("SpunOutSmall", "Spun out") : LOCTEXT("Bogged", "Bogged start");
		}
		bAlert = false;
		return FText::GetEmpty();
	}

	/** A smaller chip for the kart's state: drift tier, boost, off-road. */
	FText ChipFor(const FKartState& S, FLinearColor& OutColor)
	{
		if (S.DriftDirection != 0.0)
		{
			if (S.DriftPower >= 1.8) { OutColor = Color::Grape; return LOCTEXT("Ultra", "ULTRA"); }
			if (S.DriftPower >= 1.15) { OutColor = Color::SunDeep; return LOCTEXT("Super", "SUPER"); }
			if (S.DriftPower >= 0.65) { OutColor = Color::Sky; return LOCTEXT("Mini", "MINI"); }
			OutColor = Color::WithAlpha(Color::Night, 0.75f);
			return LOCTEXT("Drift", "DRIFT");
		}
		if (S.BoostMultiplier > 1.0 || S.Turbo > 0.0)
		{
			OutColor = Color::Hot;
			const FString Label = S.ActiveBoost.Replace(TEXT("_"), TEXT(" "));
			return Label.IsEmpty() ? LOCTEXT("Boost", "BOOST") : FText::FromString(Label);
		}
		if (S.bOnDirt)
		{
			OutColor = FLinearColor::FromSRGBColor(FColor(0x9a, 0x6b, 0x3c));
			return LOCTEXT("OffRoad", "OFF-ROAD");
		}
		return FText::GetEmpty();
	}

	void Chip(const FKartPainter& P, const FVector2D& Anchor, const FVector2D& Align, const FText& Label, const FText& Value,
		const FLinearColor& ValueColor, float Scale)
	{
		const FSlateFontInfo LabelFont = Font(EFont::Label, 16.f * Scale);
		const FSlateFontInfo ValueFont = Font(EFont::Display, 34.f * Scale, 2.f * Scale);
		const FVector2D LabelSize = FKartPainter::Measure(Label.ToString(), LabelFont);
		const FVector2D ValueSize = FKartPainter::Measure(Value.ToString(), ValueFont);
		const FVector2D Size(LabelSize.X + ValueSize.X + 54.0 * Scale, 56.0 * Scale);
		const FVector2D Pos = Anchor - Size * Align;
		P.Box(Pos, Size, Color::WithAlpha(Color::Night, 0.72f), static_cast<float>(Size.Y * 0.5), Color::WithAlpha(Color::White, 0.5f), 2.f);
		P.Text(Label, LabelFont, Pos + FVector2D(22.0 * Scale, Size.Y * 0.5), Color::WithAlpha(Color::White, 0.75f), FVector2D(0.0, 0.5));
		P.Text(Value, ValueFont, Pos + FVector2D(Size.X - 22.0 * Scale, Size.Y * 0.5), ValueColor, FVector2D(1.0, 0.5));
	}
}

void SKartRaceHud::Construct(const FArguments& InArgs)
{
	Manager = InArgs._Manager;
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(true);
}

bool SKartRaceHud::IsShowingFinish() const
{
	return FSlateApplication::Get().GetCurrentTime() - RaceFinishedAt < 1.4;
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
		A.ShownSpeed = FMath::FInterpTo(A.ShownSpeed, Kmh, InDeltaTime, 12.0);
	}
}

TArray<SKartRaceHud::FView> SKartRaceHud::GatherViews(const FVector2D& Screen) const
{
	TArray<FView> Views;
	const AKartRaceManager* Race = Manager.Get();
	if (!Race || !IsRacingPhase(Race->GetPhase()))
	{
		return Views;
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
	return Views;
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

	const TArray<FView> Views = GatherViews(Screen);
	for (const FView& View : Views)
	{
		PaintView(P, *Race, View, Views.Num(), Now);
	}

	if (IsRacingPhase(Phase) && Views.Num() > 0)
	{
		const FVector2D Margin = Screen * Metric::SafeFraction;
		const float Scale = Views.Num() == 1 ? 1.f : 0.8f;
		// The race clock: under the lap counter in a single view, on the split line otherwise.
		const FText Clock = FText::FromString(SKartRaceOverlay::FormatRaceTime(Race->GetRaceMs(), false));
		const double Remaining = Race->GetDeadlineMs() - Race->GetRaceMs();
		const bool bWarn = Phase == EKartRacePhase::Racing && Remaining <= 30000.0;
		FVector2D ClockAnchor = Views.Num() == 1 ? FVector2D(Screen.X - Margin.X, Margin.Y + 118.0) : FVector2D(Screen.X * 0.5, Screen.Y * 0.5 - (Views.Num() >= 4 ? Screen.Y * 0.17 : 0.0));
		const FVector2D ClockAlign = Views.Num() == 1 ? FVector2D(1.0, 0.0) : FVector2D(0.5, 0.5);
		Chip(P, ClockAnchor, ClockAlign, LOCTEXT("Time", "TIME"), Clock, Color::White, Scale);
		if (bWarn)
		{
			const float Pulse = 1.f + 0.06f * FMath::Sin(static_cast<float>(Now) * 8.f);
			const FVector2D WarnAnchor(Screen.X * 0.5, Views.Num() == 1 ? Margin.Y + 24.0 : Screen.Y * 0.5 + 44.0 * Scale);
			Chip(P.Transformed(WarnAnchor, Pulse), WarnAnchor, FVector2D(0.5, 0.0), LOCTEXT("EndsIn", "RACE ENDS IN"),
				FText::FromString(FString::Printf(TEXT("%ds"), FMath::CeilToInt32(Remaining / 1000.0))), Color::Hot, Scale);
		}
		// Shared space in 3 and 4 views: the minimap (and the standings for 3).
		if (Views.Num() == 3)
		{
			const double H = Screen.Y * 0.36;
			PaintMinimap(P, *Race, FVector2D(Screen.X * 0.125 - H * 0.6, Screen.Y * 0.75 - H * 0.5), static_cast<float>(H), INDEX_NONE);
			PaintStandings(P, *Race, FVector2D(Screen.X - Margin.X * 0.5, Screen.Y * 0.5 + Margin.Y), 0.75f);
		}
		else if (Views.Num() >= 4)
		{
			const double H = Screen.Y * 0.24;
			const FVector2D Centre = Screen * 0.5;
			P.Circle(Centre, static_cast<float>(H * 0.72), Color::WithAlpha(Color::Night, 0.7f), Color::WithAlpha(Color::White, 0.6f), 3.f);
			PaintMinimap(P, *Race, Centre - FVector2D(H * 0.5, H * 0.5), static_cast<float>(H), INDEX_NONE);
		}
	}

	if (Phase == EKartRacePhase::Countdown || (Phase == EKartRacePhase::Racing && Race->GetRaceMs() < 1200.0))
	{
		PaintCountdown(P, *Race, Screen, Now);
	}
	if (Phase == EKartRacePhase::Finished && Now - RaceFinishedAt < FinishBannerSeconds)
	{
		const FVector2D Size(Screen.X, 190.0);
		PaintBanner(P, FVector2D(0.0, Screen.Y * 0.42 - Size.Y * 0.5), Size, LOCTEXT("RaceOver", "RACE OVER"), Color::Hot, Now - RaceFinishedAt, 1.6, 1.f, true);
	}
	if (Race->IsPaused() && Race->GetPauseReason() != TEXT("local"))
	{
		PaintRoomPause(P, *Race, Screen, Now);
	}
	return Layer;
}

void SKartRaceHud::PaintView(const FKartPainter& Root, const AKartRaceManager& Race, const FView& View, int32 NumViews, double Now) const
{
	if (!Race.GetRacers().IsValidIndex(View.Seat) || !Seats.IsValidIndex(View.Seat))
	{
		return;
	}
	const FKartRacer& Racer = Race.GetRacers()[View.Seat];
	const FSeatAnim& A = Seats[View.Seat];
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	const FKartPainter P = Root;
	const float Scale = FMath::Clamp(static_cast<float>(FMath::Min(View.Size.X / Metric::CanvasW, View.Size.Y / Metric::CanvasH) * 1.25), 0.58f, 1.f);
	const FVector2D Margin(FMath::Max(View.Size.X * 0.04, 24.0), FMath::Max(View.Size.Y * 0.05, 18.0));
	const FVector2D Min = View.Pos + Margin;
	const FVector2D Max = View.Pos + View.Size - Margin;
	const int32 Laps = Race.GetTrack()->Laps;
	const double Intro = Ease::OutBack(static_cast<float>((Now - PhaseAt) / 0.5), 1.4f);
	const bool bCountdown = Race.GetPhase() == EKartRacePhase::Countdown;
	const float SlideIn = bCountdown ? static_cast<float>(Intro) : 1.f;

	// Item slot, top-left.
	PaintItemSlot(P, Race, Racer, FVector2D(Min.X - (1.0 - SlideIn) * 240.0 * Scale, Min.Y), Scale, Now);

	// Lap counter, top-right: pops on a new lap, sunny on the final lap.
	{
		const bool bFinal = A.Lap == Laps && Laps > 1;
		const float Pop = 1.f + 0.35f * (1.f - Ease::OutCubic(static_cast<float>((Now - A.LapAt) / 0.45)));
		const FVector2D Anchor(Max.X + (1.0 - SlideIn) * 240.0 * Scale, Min.Y);
		const FSlateFontInfo LapFont = Font(EFont::Display, 76.f * Scale, 6.f * Scale);
		const FString Value = FString::Printf(TEXT("%d/%d"), A.Lap, Laps);
		const FVector2D ValueSize = FKartPainter::Measure(Value, LapFont);
		const FKartPainter L = P.Transformed(Anchor + FVector2D(-ValueSize.X * 0.5, ValueSize.Y * 0.5), Pop);
		L.Text(Value, LapFont, Anchor, bFinal ? Color::Sun : Color::White, FVector2D(1.0, 0.0), 5.f * Scale);
		L.Text(LOCTEXT("Lap", "LAP"), Font(EFont::Display, 34.f * Scale, 4.f * Scale), Anchor + FVector2D(-ValueSize.X - 10.0 * Scale, ValueSize.Y * 0.62),
			Color::White, FVector2D(1.0, 1.0), 3.f * Scale);
	}

	// Standings rail in a single view, under the clock.
	if (NumViews == 1)
	{
		PaintStandings(P, Race, FVector2D(Max.X, Min.Y + 200.0), 1.f);
	}

	// Place, bottom-right.
	PaintPlace(P, Racer, FVector2D(Max.X + (1.0 - SlideIn) * 300.0 * Scale, Max.Y), Scale, Now);

	// Speed, bottom-left: the number and a capsule gauge that turns gold on a boost.
	{
		const bool bBoost = S.BoostMultiplier > 1.0 || S.Turbo > 0.0;
		const FVector2D Origin(Min.X - (1.0 - SlideIn) * 300.0 * Scale, Max.Y);
		const FString Speed = FString::FromInt(FMath::RoundToInt32(A.ShownSpeed));
		const FVector2D NumSize = P.Text(Speed, Font(EFont::Display, 78.f * Scale, 6.f * Scale), Origin - FVector2D(0.0, 30.0 * Scale),
			bBoost ? Color::Sun : Color::White, FVector2D(0.0, 1.0), 5.f * Scale);
		P.Text(LOCTEXT("Kmh", "km/h"), Font(EFont::Label, 24.f * Scale, 3.f * Scale), Origin + FVector2D(NumSize.X + 10.0 * Scale, -44.0 * Scale), Color::White, FVector2D(0.0, 1.0));
		const FVector2D BarPos = Origin - FVector2D(0.0, 22.0 * Scale);
		const FVector2D BarSize(300.0 * Scale, 22.0 * Scale);
		const float Fill = FMath::Clamp(static_cast<float>(A.ShownSpeed / TopSpeedKmh), 0.f, 1.25f);
		P.Box(BarPos, BarSize, Color::WithAlpha(Color::Night, 0.7f), static_cast<float>(BarSize.Y * 0.5), Color::White, 2.5f * Scale);
		if (Fill > 0.02f)
		{
			const FVector2D FillSize(FMath::Min(1.0f, Fill) * (BarSize.X - 6.0 * Scale), BarSize.Y - 6.0 * Scale);
			P.Gradient(BarPos + FVector2D(3.0, 3.0) * Scale, FillSize, bBoost ? Color::Sun : Color::Sky, bBoost ? Color::SunDeep : Color::Mint, false, static_cast<float>(FillSize.Y * 0.5));
		}
	}

	// Minimap: its own in one or two views; shared in three or four.
	if (NumViews <= 2)
	{
		const float H = NumViews == 1 ? 300.f : static_cast<float>(View.Size.Y * 0.4);
		const FVector2D MapPos = NumViews == 1 ? FVector2D(Min.X, Max.Y - 190.0 - H) : FVector2D(Max.X - H * 1.6, View.Pos.Y + View.Size.Y * 0.5 - H * 0.5);
		PaintMinimap(P, Race, MapPos, H, View.Seat);
	}

	// Status: an alert pill (wrong way, spun out) or a smaller state chip, low in the centre of the view.
	const FKartItemSlot& Slot = Race.GetItems().Slots()[Racer.Seat];
	bool bAlert = false;
	const FText Status = Race.GetPhase() == EKartRacePhase::Racing && !Racer.bFinished ? StatusFor(S, Slot, Race.GetItemTime(), bAlert) : FText::GetEmpty();
	const FVector2D StatusAnchor(View.Pos.X + View.Size.X * 0.5, View.Pos.Y + View.Size.Y * 0.7);
	if (!Status.IsEmpty())
	{
		const float Pulse = 1.f + 0.05f * FMath::Sin(static_cast<float>(Now) * 10.f);
		const FSlateFontInfo StatusFont = Font(EFont::Display, 54.f * Scale, 4.f * Scale);
		const FVector2D TextSize = FKartPainter::Measure(Status.ToString(), StatusFont);
		const FVector2D Size(TextSize.X + 80.0 * Scale, 84.0 * Scale);
		const FKartPainter B = P.Transformed(StatusAnchor, Pulse, -2.f);
		const FLinearColor Fill = S.bWrongWay ? Color::Hot : Color::WithAlpha(Color::Night, 0.8f);
		B.Box(StatusAnchor - Size * 0.5, Size, Fill, static_cast<float>(Size.Y * 0.5), Color::White, 4.f * Scale);
		B.Text(Status, StatusFont, StatusAnchor, S.bWrongWay ? Color::White : Color::Sun, FVector2D(0.5, 0.5), 4.f * Scale);
		if (S.bWrongWay)
		{
			// A U-turn arrow either side.
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				KartWidgets::Arrow(B, StatusAnchor + FVector2D(Side * (Size.X * 0.5 + 36.0 * Scale), 0.0), 20.f * Scale, Side > 0, Color::Hot);
			}
		}
	}
	else if (Race.GetPhase() == EKartRacePhase::Racing && !Racer.bFinished)
	{
		FLinearColor ChipColor;
		const FText Label = ChipFor(S, ChipColor);
		if (!Label.IsEmpty())
		{
			const FSlateFontInfo ChipFont = Font(EFont::Display, 30.f * Scale, 3.f * Scale);
			const FVector2D TextSize = FKartPainter::Measure(Label.ToString(), ChipFont);
			const FVector2D Size(TextSize.X + 44.0 * Scale, 50.0 * Scale);
			const FVector2D Anchor = StatusAnchor + FVector2D(0.0, 40.0 * Scale);
			P.Box(Anchor - Size * 0.5, Size, ChipColor, static_cast<float>(Size.Y * 0.5), Color::White, 3.f * Scale);
			P.Text(Label, ChipFont, Anchor, Color::White, FVector2D(0.5, 0.5));
		}
	}

	// Final lap and finish banners across this view.
	const FVector2D BannerSize(View.Size.X, 150.0 * Scale);
	const FVector2D BannerPos(View.Pos.X, View.Pos.Y + View.Size.Y * 0.3 - BannerSize.Y * 0.5);
	if (Now - A.FinishedAt < FinishBannerSeconds)
	{
		PaintBanner(P, BannerPos, BannerSize, FText::Format(LOCTEXT("FinishPlace", "FINISH!  {0}"), FText::FromString(SKartRaceOverlay::Ordinal(Racer.Place))),
			Color::ForPlace(Racer.Place) == Color::Sky ? Color::Hot : Color::SunDeep, Now - A.FinishedAt, 1.8, Scale, true);
	}
	else if (Now - A.FinalLapAt < FinishBannerSeconds)
	{
		PaintBanner(P, BannerPos, BannerSize, LOCTEXT("FinalLap", "FINAL LAP!"), Color::Hot, Now - A.FinalLapAt, 1.6, Scale, false);
	}
}

void SKartRaceHud::PaintItemSlot(const FKartPainter& P, const AKartRaceManager& Race, const FKartRacer& Racer, const FVector2D& Pos, float Scale, double Now) const
{
	const FKartItemSlot& Slot = Race.GetItems().Slots()[Racer.Seat];
	const double ItemNow = Race.GetItemTime();
	const float Size = 150.f * Scale;
	const FVector2D Centre = Pos + FVector2D(Size * 0.5, Size * 0.5);
	const float R = 34.f * Scale;
	const bool bActive = Slot.bRolling || Slot.Held != EKartItem::None;
	// Landing pop: the strip stops 1.8 s in; the item bounces in with a flash ring.
	const double Elapsed = FKartItemWorld::RouletteSeconds - (Slot.RollEnds - ItemNow);
	const double LandAge = Slot.bRolling ? Elapsed - 1.8 : 10.0;
	const float Land = (LandAge >= 0.0 && LandAge < 0.4) ? 1.f + 0.25f * (1.f - Ease::OutElastic(static_cast<float>(LandAge / 0.4))) : 1.f;
	const FKartPainter Frame = P.Transformed(Centre, Land);
	Frame.Box(Pos + FVector2D(0.0, 7.0 * Scale), FVector2D(Size, Size), Color::Shadow, R);
	Frame.Box(Pos, FVector2D(Size, Size), bActive ? Color::White : Color::WithAlpha(Color::White, 0.7f), R, Color::Night, 3.f);
	const FVector2D InnerPos = Pos + FVector2D(10.0, 10.0) * Scale;
	const FVector2D InnerSize(Size - 20.f * Scale, Size - 20.f * Scale);
	Frame.Gradient(InnerPos, InnerSize, Color::NightSoft, Color::Night, true, R - 8.f * Scale);

	if (Slot.bRolling && Elapsed < 1.8)
	{
		// The strip: 25 items/s easing to a stop over 1.8 s (RaceUI.tsx), scrolling down through the window.
		const double Spin = 25.0 * (1.8 * Elapsed - 0.5 * Elapsed * Elapsed) / 1.8;
		const int32 Kinds = static_cast<int32>(EKartItem::Count) - 1;
		const int32 Index = FMath::FloorToInt32(Spin);
		const double Frac = Spin - Index;
		Frame.PushClip(InnerPos, InnerSize);
		for (int32 k = -1; k <= 1; ++k)
		{
			const EKartItem Item = static_cast<EKartItem>(1 + ((Index + k) % Kinds + Kinds) % Kinds);
			const FVector2D At = Centre + FVector2D(0.0, (Frac - k) * InnerSize.Y);
			KartItemIcons::Draw(Frame, Item, At, InnerSize.X * 0.78f, Now);
		}
		Frame.PopClip();
		// Motion streaks so the spin reads as fast.
		Frame.Gradient(InnerPos, FVector2D(InnerSize.X, InnerSize.Y * 0.3), Color::WithAlpha(Color::Night, 0.9f), Color::WithAlpha(Color::Night, 0.f), true, R - 8.f * Scale);
		Frame.Gradient(InnerPos + FVector2D(0.0, InnerSize.Y * 0.7), FVector2D(InnerSize.X, InnerSize.Y * 0.3), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.9f), true, R - 8.f * Scale);
	}
	else if (Slot.Held != EKartItem::None)
	{
		const double Bob = FMath::Sin(Now * 3.0) * 3.0 * Scale;
		KartItemIcons::Draw(Frame, Slot.Held, Centre + FVector2D(0.0, Bob), InnerSize.X * 0.8f, Now);
		if (LandAge >= 0.0 && LandAge < 0.45)
		{
			const float T = static_cast<float>(LandAge / 0.45);
			const float Ring = Size * (0.5f + 0.5f * T);
			P.Box(Centre - FVector2D(Ring, Ring), FVector2D(Ring * 2.f, Ring * 2.f), FLinearColor::Transparent, Ring * 0.4f,
				Color::WithAlpha(KartItemIcons::Tint(Slot.Held), 1.f - T), 6.f * Scale);
		}
		// The item's name and how to use it, beside the slot.
		if (!Slot.bRolling || LandAge > 0.0)
		{
			const FVector2D Label = Pos + FVector2D(Size + 16.0 * Scale, Size * 0.5);
			P.Text(FText::FromString(ItemName(Slot.Held)), Font(EFont::Title, 30.f * Scale, 3.f * Scale), Label - FVector2D(0.0, 4.0 * Scale), Color::White, FVector2D(0.0, 1.0), 3.f * Scale);
			if (!Slot.bRolling)
			{
				const UKartUISubsystem* UI = UKartUISubsystem::Get(Race.GetWorld());
				const bool bPad = Racer.Seat > 0 || (UI && UI->IsUsingGamepad());
				float X = static_cast<float>(Label.X);
				const float GH = 34.f * Scale;
				X += bPad ? KartWidgets::PadGlyph(P, EKeys::Gamepad_FaceButton_Left, FVector2D(X, Label.Y + GH * 0.6), GH)
					: KartWidgets::KeyCap(P, TEXT("E"), FVector2D(X, Label.Y + GH * 0.6), GH);
				P.Text(LOCTEXT("UseItem", "Use"), Font(EFont::Button, 22.f * Scale, 2.f * Scale), FVector2D(X + 8.f * Scale, Label.Y + GH * 0.6), Color::White, FVector2D(0.0, 0.5));
			}
		}
	}
	else if (ItemNow - Slot.LastUsedAt < 0.9 && Slot.LastUsed != EKartItem::None)
	{
		// The used item lifts out of the slot and fades.
		const float T = static_cast<float>((ItemNow - Slot.LastUsedAt) / 0.9);
		KartItemIcons::Draw(P.Transformed(Centre, 1.f + T * 0.6f).Faded(1.f - T), Slot.LastUsed, Centre - FVector2D(0.0, T * 40.0 * Scale), InnerSize.X * 0.8f, Now);
		P.Faded(1.f - T).Text(FText::Format(LOCTEXT("Used", "{0}!"), FText::FromString(ItemName(Slot.LastUsed))), Font(EFont::Title, 30.f * Scale, 3.f * Scale),
			Pos + FVector2D(Size + 16.0 * Scale, Size * 0.5), Color::Sun, FVector2D(0.0, 0.5), 3.f * Scale);
	}
}

void SKartRaceHud::PaintPlace(const FKartPainter& P, const FKartRacer& Racer, const FVector2D& BottomRight, float Scale, double Now) const
{
	const FSeatAnim& A = Seats[Racer.Seat];
	const int32 Place = Racer.Place;
	const FLinearColor Fill = Color::ForPlace(Place);
	const FSlateFontInfo NumberFont = Font(EFont::Display, 200.f * Scale, 10.f * Scale);
	const FSlateFontInfo SuffixFont = Font(EFont::Display, 76.f * Scale, 6.f * Scale);
	const FString Number = FString::FromInt(Place);
	const FString Suffix = SKartRaceOverlay::OrdinalSuffix(Place);
	const FVector2D NumberSize = FKartPainter::Measure(Number, NumberFont);
	const FVector2D SuffixSize = FKartPainter::Measure(Suffix, SuffixFont);
	const FVector2D Total(NumberSize.X + SuffixSize.X - 6.0 * Scale, NumberSize.Y);
	// Font ascent leaves room under the glyphs; tuck the number down onto the safe line.
	const FVector2D Origin = BottomRight - FVector2D(Total.X, Total.Y * 0.86);
	const FVector2D Pivot = Origin + Total * 0.5;
	// A place change bounces the number (big, then elastic settle) and tilts it the way it moved.
	const float T = static_cast<float>((Now - A.PlaceAt) / 0.6);
	const float Bounce = T < 1.f ? 1.45f - 0.45f * Ease::OutElastic(T) : 1.f;
	const float Tilt = T < 1.f ? (1.f - T) * 10.f * (A.PlaceDir > 0 ? -1.f : 1.f) : 0.f;
	const FKartPainter N = P.Transformed(Pivot, Bounce, Tilt);
	N.Text(Number, NumberFont, Origin, Fill, FVector2D::ZeroVector, 8.f * Scale);
	N.Text(Suffix, SuffixFont, Origin + FVector2D(NumberSize.X - 6.0 * Scale, NumberSize.Y * 0.2), Fill, FVector2D::ZeroVector, 5.f * Scale);
	// The direction arrow that rises (or falls) next to it for a moment.
	if (Now - A.PlaceAt < 1.2 && A.PlaceDir != 0)
	{
		const float U = static_cast<float>((Now - A.PlaceAt) / 1.2);
		const FVector2D C = Origin + FVector2D(-40.0 * Scale, NumberSize.Y * 0.45 - A.PlaceDir * U * 40.0 * Scale);
		const FKartPainter Arr = P.Faded(1.f - U);
		const double S = 28.0 * Scale * A.PlaceDir;
		Arr.Polygon({ C + FVector2D(-S * 0.8, S * 0.5), C + FVector2D(0.0, -S * 0.7), C + FVector2D(S * 0.8, S * 0.5) }, A.PlaceDir > 0 ? Color::Mint : Color::Hot);
	}
}

void SKartRaceHud::PaintMinimap(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Pos, float Height, int32 FocusSeat) const
{
	const FTrack* Track = Race.GetTrack();
	if (!Track || !Track->MinimapBounds.bIsValid || Track->Centre.Num() < 2 || Race.GetPhase() != EKartRacePhase::Racing && Race.GetPhase() != EKartRacePhase::Countdown)
	{
		return;
	}
	// RaceUI.tsx minimap: x = (x - minX) / scaleX, y = (z - minZ) / scaleZ.
	const FVector2D Extent = Track->MinimapBounds.GetSize();
	const double Fit = Height / FMath::Max(Extent.X, Extent.Y);
	const FVector2D Area = Extent * Fit;
	const FVector2D Offset = Pos + (FVector2D(Height, Height) - Area) * 0.5 + FVector2D(Height * 0.1, 0.0);
	auto ToMap = [&](const FVector2D& W)
	{
		return Offset + (W - Track->MinimapBounds.Min) * Fit;
	};
	TArray<FVector2D> Road;
	const int32 Stride = FMath::Max(1, Track->Centre.Num() / 180);
	for (int32 i = 0; i < Track->Centre.Num(); i += Stride)
	{
		Road.Add(ToMap(Track->Centre[i]));
	}
	const float W = FMath::Max(4.f, Height * 0.035f);
	P.Lines(Road, Color::WithAlpha(Color::Night, 0.75f), W + 7.f, true);
	P.Lines(Road, Color::WithAlpha(Color::White, 0.95f), W, true);
	// Start/finish: a little chequer.
	if (Track->Checkpoints.Num() > 0)
	{
		const FVector2D F = ToMap(Track->Checkpoints[0].Position);
		const double C = W * 0.7;
		for (int32 i = 0; i < 4; ++i)
		{
			P.Box(F + FVector2D((i % 2) * C - C, (i / 2) * C - C), FVector2D(C, C), (i == 0 || i == 3) ? Color::Night : Color::White);
		}
	}
	// Karts: everyone's dot in paint, the focus kart bigger with a heading pointer, drawn last.
	TArray<const FKartRacer*> Order;
	for (const FKartRacer& Racer : Race.GetRacers())
	{
		Order.Add(&Racer);
	}
	Order.StableSort([FocusSeat](const FKartRacer& A, const FKartRacer& B) { return A.Seat != FocusSeat && (B.Seat == FocusSeat || A.Place > B.Place); });
	for (const FKartRacer* Racer : Order)
	{
		const FVector2D At = ToMap(Racer->Kart->GetKartMovement()->RenderPosition());
		const bool bFocus = Racer->Seat == FocusSeat || (FocusSeat == INDEX_NONE && Racer->Kind == EKartSeatKind::Local);
		const float R = bFocus ? W * 1.9f : W * 1.3f;
		if (bFocus)
		{
			const double Yaw = Racer->Kart->GetKartMovement()->RenderYaw();
			const FVector2D Dir(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
			const FVector2D Side(-Dir.Y, Dir.X);
			P.Polygon({ At + Dir * R * 2.2, At + Side * R * 0.9, At - Side * R * 0.9 }, Color::White);
		}
		P.Circle(At, R, Racer->Paint, bFocus ? Color::White : Color::Night, bFocus ? 3.f : 2.f);
	}
}

void SKartRaceHud::PaintStandings(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& TopRight, float Scale) const
{
	const double RowH = 46.0 * Scale;
	const double W = 250.0 * Scale;
	const int32 Count = Race.GetStandings().Num();
	for (int32 i = 0; i < Count; ++i)
	{
		const FKartRacer& Racer = Race.GetRacers()[Race.GetStandings()[i]];
		const bool bLocal = Racer.Kind != EKartSeatKind::Bot;
		const FVector2D Pos(TopRight.X - W, TopRight.Y + i * (RowH + 6.0 * Scale));
		const FLinearColor Back = bLocal ? Color::WithAlpha(Color::White, 0.95f) : Color::WithAlpha(Color::Night, 0.6f);
		P.Box(Pos, FVector2D(W, RowH), Back, static_cast<float>(RowH * 0.5), bLocal ? Racer.Paint : FLinearColor::Transparent, 3.f);
		const FVector2D Badge(Pos.X + RowH * 0.5, Pos.Y + RowH * 0.5);
		P.Circle(Badge, static_cast<float>(RowH * 0.5 - 4.0), Color::ForPlace(Racer.Place));
		P.Text(FString::FromInt(Racer.Place), Font(EFont::Display, 24.f * Scale), Badge, Color::Navy, FVector2D(0.5, 0.5));
		P.Circle(FVector2D(Pos.X + RowH + 14.0 * Scale, Badge.Y), 8.f * Scale, Racer.Paint, Color::Night, 2.f);
		P.Text(Racer.Name, Font(EFont::Button, 22.f * Scale), FVector2D(Pos.X + RowH + 32.0 * Scale, Badge.Y), bLocal ? Color::Navy : Color::White, FVector2D(0.0, 0.5));
		if (Racer.bFinished)
		{
			P.Text(TEXT("✓"), Font(EFont::Body, 22.f * Scale), FVector2D(Pos.X + W - 18.0 * Scale, Badge.Y), Color::Mint, FVector2D(1.0, 0.5));
		}
	}
}

void SKartRaceHud::PaintCountdown(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const
{
	const FVector2D C(Screen.X * 0.5, Screen.Y * 0.4);
	if (Race.GetPhase() == EKartRacePhase::Countdown)
	{
		// 5..1, each punched in big and settling, then shrinking away at the end of its second.
		const int32 Value = Race.GetCountdownValue();
		const double T = Race.GetPhaseSeconds() - FMath::FloorToDouble(Race.GetPhaseSeconds());
		const FLinearColor Colors[] = { Color::Hot, Color::SunDeep, Color::Sun, Color::Mint, Color::Sky, Color::Grape };
		const FLinearColor Fill = Colors[FMath::Clamp(Value, 0, 5)];
		const float In = Ease::OutBack(static_cast<float>(T / 0.28), 2.6f);
		const float Out = T > 0.78 ? Ease::InCubic(static_cast<float>((T - 0.78) / 0.22)) : 0.f;
		const float Scale = (0.4f + 0.6f * In) * (1.f - 0.5f * Out);
		const float Spin = (1.f - FMath::Min(In, 1.f)) * -25.f;
		// A ring bursts out on each beat.
		const float Ring = static_cast<float>(FMath::Min(1.0, T / 0.5));
		P.Faded(1.f - Ring).Box(C - FVector2D(140.0 + Ring * 160.0), FVector2D(280.0 + Ring * 320.0), FLinearColor::Transparent, 300.f, Fill, 14.f * (1.f - Ring));
		const FKartPainter N = P.Transformed(C, Scale, Spin).Faded(1.f - Out);
		N.Text(FString::FromInt(Value), Font(EFont::Display, 300.f, 16.f), C + FVector2D(0.0, 10.0), Fill, FVector2D(0.5, 0.5), 14.f);
		return;
	}
	// GO!: rays spin behind it while it punches in, then it fades over the first second of racing.
	const double T = Race.GetRaceMs() / 1000.0;
	const float In = Ease::OutBack(static_cast<float>(T / 0.3), 3.f);
	const float Fade = 1.f - Ease::InCubic(static_cast<float>(FMath::Max(0.0, T - 0.7) / 0.5));
	const FKartPainter G = P.Faded(Fade);
	for (int32 i = 0; i < 12; ++i)
	{
		const double A = FMath::DegreesToRadians(i * 30.0 + T * 60.0);
		const double A2 = A + FMath::DegreesToRadians(11.0);
		const double R = 520.0 * In;
		G.Polygon({ C, C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, C + FVector2D(FMath::Cos(A2), FMath::Sin(A2)) * R }, Color::WithAlpha(i % 2 ? Color::Sun : Color::Hot, 0.35f));
	}
	const FKartPainter Word = G.Transformed(C, 0.3f + 0.7f * In, (1.f - FMath::Min(In, 1.f)) * 15.f);
	Word.Text(LOCTEXT("Go", "GO!"), Font(EFont::Display, 360.f, 18.f), C + FVector2D(0.0, 12.0), Color::Sun, FVector2D(0.5, 0.5), 16.f);
}

void SKartRaceHud::PaintIntro(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const
{
	const FTrack* Track = Race.GetTrack();
	const FKartTrackInfo* Info = FKartTrackCatalog::Find(Track->Id);
	const FLinearColor Accent = Info ? Info->Accent : Color::Hot;
	// The card rides in during the intro (1 s waiting + 6 s of shots) and leaves at its end.
	const double T = Race.GetPhase() == EKartRacePhase::Waiting ? Race.GetPhaseSeconds() : 1.0 + Race.GetPhaseSeconds();
	const float In = Ease::OutBack(static_cast<float>((T - 0.4) / 0.55), 1.4f);
	const float Out = Ease::InCubic(static_cast<float>((T - 6.5) / 0.45));
	const double Slide = (1.0 - In) * -900.0 - Out * 1200.0;
	const FVector2D Margin = Screen * Metric::SafeFraction;
	const double Y = Screen.Y - Margin.Y - 250.0;
	P.Gradient(FVector2D(0.0, Screen.Y * 0.55), FVector2D(Screen.X, Screen.Y * 0.45), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.55f * (1.f - Out)));
	// Slanted accent band with the track name, a white strip with the tagline, chips below.
	const double BandW = 1100.0;
	P.Polygon({ FVector2D(Slide - 40.0, Y), FVector2D(Slide + BandW + 40.0, Y), FVector2D(Slide + BandW, Y + 130.0), FVector2D(Slide - 40.0, Y + 130.0) }, Accent);
	P.Polygon({ FVector2D(Slide + BandW + 52.0, Y), FVector2D(Slide + BandW + 76.0, Y), FVector2D(Slide + BandW + 36.0, Y + 130.0), FVector2D(Slide + BandW + 12.0, Y + 130.0) }, Color::White);
	const FText Name = Info ? Info->Name : FText::FromString(Track->Name);
	P.Text(Name.ToUpper(), Font(EFont::Display, 104.f, 6.f), FVector2D(Slide + Margin.X, Y + 66.0), Color::White, FVector2D(0.0, 0.5), 6.f);
	const double Slide2 = (1.0 - Ease::OutBack(static_cast<float>((T - 0.65) / 0.55), 1.4f)) * -900.0 - Out * 1200.0;
	if (Info)
	{
		P.Box(FVector2D(Slide2 - 40.0, Y + 140.0), FVector2D(BandW - 120.0, 56.0), Color::White, 0.f);
		P.Text(Info->Tagline, Font(EFont::Body, 26.f), FVector2D(Slide2 + Margin.X, Y + 168.0), Color::Navy, FVector2D(0.0, 0.5));
	}
	const FString Facts = FString::Printf(TEXT("%d LAPS  ·  %d RACERS"), Track->Laps, Race.GetRacers().Num());
	P.Text(Facts, Font(EFont::Label, 24.f, 3.f), FVector2D(Slide2 + Margin.X, Y + 222.0), Color::White, FVector2D(0.0, 0.5), 3.f);

	// Skip hint, bottom-right.
	if (Race.GetPhase() == EKartRacePhase::Intro)
	{
		const UKartUISubsystem* UI = UKartUISubsystem::Get(Race.GetWorld());
		FKartScreenFrame Frame;
		Frame.Size = Screen;
		Frame.SafeMin = Margin;
		Frame.SafeMax = Screen - Margin;
		Frame.Device = UI ? UI->GetInputDevice() : EKartInputDevice::Keyboard;
		KartWidgets::Hints(P.Faded(In * (1.f - Out)), Frame, { { Frame.Device == EKartInputDevice::Gamepad ? EKartMenuAction::Start : EKartMenuAction::Accept, LOCTEXT("Skip", "Skip") } });
	}
}

void SKartRaceHud::PaintRoomPause(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const
{
	P.Box(FVector2D::ZeroVector, Screen, Color::WithAlpha(Color::Night, 0.5f));
	const FVector2D Size(820.0, 280.0);
	const FVector2D Pos = Screen * 0.5 - Size * 0.5;
	P.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 12.f, Color::Sun, 5.f);
	P.Text(LOCTEXT("Paused", "PAUSED"), Font(EFont::Display, 70.f), Pos + FVector2D(Size.X * 0.5, 80.0), Color::Navy, FVector2D(0.5, 0.5));
	P.Text(LOCTEXT("WaitingPhone", "Waiting for a phone to reconnect..."), Font(EFont::Body, 28.f), Pos + FVector2D(Size.X * 0.5, 150.0), Color::NavyDim, FVector2D(0.5, 0.5));
	for (int32 i = 0; i < 3; ++i)
	{
		const double Bob = FMath::Sin(Now * 6.0 - i * 0.9) * 8.0;
		const FLinearColor Dots[] = { Color::Hot, Color::Sun, Color::Sky };
		P.Circle(Pos + FVector2D(Size.X * 0.5 - 40.0 + i * 40.0, 220.0 + Bob), 11.f, Dots[i]);
	}
}

void SKartRaceHud::PaintBanner(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Text, const FLinearColor& Fill,
	double Age, double Hold, float Scale, bool bChequered) const
{
	// Sweeps in from the right, holds, and leaves to the left.
	const float In = Ease::OutCubic(static_cast<float>(Age / 0.3));
	const float Out = Ease::InCubic(static_cast<float>((Age - 0.3 - Hold) / 0.35));
	if (Out >= 1.f)
	{
		return;
	}
	const double Shift = (1.0 - In) * Size.X - Out * Size.X;
	const double Skew = Size.Y * 0.25;
	const FVector2D A = Pos + FVector2D(Shift, 0.0);
	P.Polygon({ A + FVector2D(Skew, 0.0), A + FVector2D(Size.X + Skew, 0.0), A + FVector2D(Size.X, Size.Y), A + FVector2D(0.0, Size.Y) }, Fill);
	P.Box(A + FVector2D(0.0, 6.0 * Scale), FVector2D(Size.X + Skew, 6.0 * Scale), Color::WithAlpha(Color::White, 0.8f));
	P.Box(A + FVector2D(0.0, Size.Y - 12.0 * Scale), FVector2D(Size.X + Skew, 6.0 * Scale), Color::WithAlpha(Color::White, 0.8f));
	if (bChequered)
	{
		const double Cell = Size.Y / 4.0;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const double X0 = Side == 0 ? A.X + Size.X * 0.08 : A.X + Size.X * 0.92 - Cell * 3.0;
			for (int32 Row = 0; Row < 4; ++Row)
			{
				for (int32 Col = 0; Col < 3; ++Col)
				{
					if ((Row + Col) % 2 == 0)
					{
						P.Box(FVector2D(X0 + Col * Cell, A.Y + Row * Cell), FVector2D(Cell, Cell), Color::WithAlpha(Color::White, 0.9f));
					}
				}
			}
		}
	}
	const float Pop = 1.f + 0.08f * Ease::Pulse(static_cast<float>((Age - 0.25) / 0.3));
	const FVector2D C = A + Size * 0.5 + FVector2D(Skew * 0.5, 0.0);
	P.Transformed(C, Pop, -3.f).Text(Text, Font(EFont::Display, static_cast<float>(Size.Y * 0.62), 7.f * Scale), C, Color::White, FVector2D(0.5, 0.5), 7.f * Scale);
}

#undef LOCTEXT_NAMESPACE

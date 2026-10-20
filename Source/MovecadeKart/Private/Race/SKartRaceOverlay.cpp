#include "Race/SKartRaceOverlay.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Race/KartRaceManager.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FLinearColor Ink(0.02f, 0.02f, 0.035f, 0.72f);
	const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor(255, 196, 64));
	const FLinearColor Soft(1.f, 1.f, 1.f, 0.8f);

	struct FPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Out;
		int32 Layer;

		void Box(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color) const
		{
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos))),
				FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
		}

		/** Align: 0 left, 0.5 centre, 1 right. Returns the text width. */
		double Text(const FString& String, FVector2D Pos, int32 Size, const FLinearColor& Color, double Align = 0.0) const
		{
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", Size);
			const FVector2D Measured = FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(String, Font));
			Pos.X -= Measured.X * Align;
			FSlateDrawElement::MakeText(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(Measured), FSlateLayoutTransform(FVector2f(Pos + FVector2D(2.0, 2.0)))),
				String, Font, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.6f * Color.A));
			FSlateDrawElement::MakeText(Out, Layer + 2, Geometry.ToPaintGeometry(FVector2f(Measured), FSlateLayoutTransform(FVector2f(Pos))),
				String, Font, ESlateDrawEffect::None, Color);
			return Measured.X;
		}
	};

	FString StatusLine(const AKartRaceManager& Race, const FKartRacer& Racer)
	{
		const MovecadeKart::FKartState& S = Racer.Kart->GetKartMovement()->SimState();
		const MovecadeKart::FKartItemSlot& Slot = Race.GetItems().Slots()[Racer.Seat];
		// KartHud.tsx cue priority: Finished > Wrong way > Choosing item > item > drift > Boosting > Off-road > Race clean.
		if (Racer.bFinished) return TEXT("Finished");
		if (S.bWrongWay) return TEXT("Wrong way!");
		if (Slot.bRolling) return TEXT("Choosing item...");
		if (Slot.Held != MovecadeKart::EKartItem::None) return MovecadeKart::ItemName(Slot.Held);
		if (S.DriftDirection != 0.0)
		{
			return S.DriftPower >= 1.8 ? TEXT("Ultra drift") : S.DriftPower >= 1.15 ? TEXT("Super drift") : S.DriftPower >= 0.65 ? TEXT("Mini drift") : TEXT("Drifting");
		}
		if (S.BoostMultiplier > 1.0 || S.Turbo > 0.0) return TEXT("Boosting");
		if (S.bOnDirt) return TEXT("Off-road");
		return TEXT("Race clean");
	}
}

void SKartRaceOverlay::Construct(const FArguments& InArgs)
{
	Manager = InArgs._Manager;
	SetVisibility(EVisibility::HitTestInvisible);
}

FString SKartRaceOverlay::FormatRaceTime(double Ms, bool bMillis)
{
	const int64 Total = FMath::Max<int64>(0, FMath::RoundToInt64(Ms));
	const int64 Minutes = Total / 60000;
	const int64 Seconds = (Total / 1000) % 60;
	return bMillis
		? FString::Printf(TEXT("%lld:%02lld.%03lld"), Minutes, Seconds, Total % 1000)
		: FString::Printf(TEXT("%lld:%02lld"), Minutes, Seconds);
}

FString SKartRaceOverlay::Ordinal(int32 Place)
{
	const TCHAR* Suffix = (Place % 100 >= 11 && Place % 100 <= 13) ? TEXT("th")
		: Place % 10 == 1 ? TEXT("st") : Place % 10 == 2 ? TEXT("nd") : Place % 10 == 3 ? TEXT("rd") : TEXT("th");
	return FString::Printf(TEXT("%d%s"), Place, Suffix);
}

int32 SKartRaceOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const AKartRaceManager* Race = Manager.Get();
	if (!Race || !Race->GetTrack() || Race->GetRacers().Num() == 0)
	{
		return LayerId;
	}
	const FPainter P{ AllottedGeometry, OutDrawElements, LayerId };
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	const double Cx = Size.X * 0.5;
	const EKartRacePhase Phase = Race->GetPhase();
	const double T = Race->GetPhaseSeconds();
	const int32 Laps = Race->GetTrack()->Laps;

	if (Race->IsPaused())
	{
		P.Box(FVector2D(Cx - 300.0, Size.Y * 0.4), FVector2D(600.0, 110.0), Ink);
		P.Text(TEXT("PAUSED"), FVector2D(Cx, Size.Y * 0.4 + 14.0), 32, Accent, 0.5);
		P.Text(Race->GetPauseReason() == TEXT("local") ? FString(TEXT("Esc / Back: resume   ·   R: restart   ·   Q: quit"))
			: FString(TEXT("Waiting for a phone to reconnect...")), FVector2D(Cx, Size.Y * 0.4 + 64.0), 16, Soft, 0.5);
		return LayerId + 3;
	}

	if (Phase == EKartRacePhase::Waiting || Phase == EKartRacePhase::Intro)
	{
		// GameIntro (gameIntros.ts): title, objective, steps.
		const double Y = Size.Y * 0.62;
		P.Box(FVector2D(Cx - 360.0, Y - 24.0), FVector2D(720.0, 200.0), Ink);
		P.Text(Race->GetTrack()->Name.ToUpper(), FVector2D(Cx, Y - 10.0), 34, Accent, 0.5);
		P.Text(TEXT("Own the next corner."), FVector2D(Cx, Y + 40.0), 24, FLinearColor::White, 0.5);
		P.Text(TEXT("Chase the pack. Drift your way to the front."), FVector2D(Cx, Y + 78.0), 16, Soft, 0.5);
		P.Text(TEXT("Steer  ·  Drift  ·  Items"), FVector2D(Cx, Y + 108.0), 16, Soft, 0.5);
		P.Text(TEXT("Enter / Start to skip"), FVector2D(Cx, Y + 146.0), 12, FLinearColor(1.f, 1.f, 1.f, 0.55f), 0.5);
		return LayerId + 3;
	}

	if (Phase == EKartRacePhase::Countdown || Phase == EKartRacePhase::Racing)
	{
		// Countdown stage: 5..1, then "Go!" held for the first second of racing.
		if (Phase == EKartRacePhase::Countdown)
		{
			const double Frac = T - FMath::FloorToDouble(T);
			const float Alpha = static_cast<float>(1.0 - 0.6 * Frac);
			P.Text(FString::FromInt(Race->GetCountdownValue()), FVector2D(Cx, Size.Y * 0.3), 120, FLinearColor(1.f, 1.f, 1.f, Alpha), 0.5);
		}
		else if (Race->GetRaceMs() < 1000.0)
		{
			P.Text(TEXT("GO!"), FVector2D(Cx, Size.Y * 0.3), 120, Accent, 0.5);
		}

		// Top bar chips.
		double X = 16.0;
		auto Chip = [&](const FString& Label, const FString& Value, const FLinearColor& Color)
		{
			const double W = 30.0 + FMath::Max(Label.Len(), Value.Len()) * 11.0;
			P.Box(FVector2D(X, 10.0), FVector2D(W, 50.0), Ink);
			P.Text(Label, FVector2D(X + 12.0, 14.0), 10, Soft);
			P.Text(Value, FVector2D(X + 12.0, 28.0), 18, Color);
			X += W + 8.0;
		};
		Chip(TEXT("Race time"), SKartRaceOverlay::FormatRaceTime(Race->GetRaceMs(), false), FLinearColor::White);
		const double Remaining = Race->GetDeadlineMs() - Race->GetRaceMs();
		if (Phase == EKartRacePhase::Racing && Remaining <= 30000.0)
		{
			Chip(TEXT("Race ends in"), FString::Printf(TEXT("%ds"), FMath::CeilToInt32(Remaining / 1000.0)), FLinearColor(1.f, 0.35f, 0.3f));
		}
		const int32 LeadLap = Race->GetLeadLap();
		Chip(TEXT("Lead lap"), FString::Printf(TEXT("%d / %d"), LeadLap, Laps), LeadLap == Laps ? Accent : FLinearColor::White);
		Chip(TEXT("Racers"), FString::FromInt(Race->GetRacers().Num()), FLinearColor::White);

		// Player rail: place, name, lap, cue line.
		const double RailW = 250.0;
		double Y = 70.0;
		for (const int32 Seat : Race->GetStandings())
		{
			const FKartRacer& Racer = Race->GetRacers()[Seat];
			const int32 Lap = FMath::Min(Racer.Kart->GetKartMovement()->SimState().LapsCompleted + 1, Laps);
			P.Box(FVector2D(Size.X - RailW - 16.0, Y), FVector2D(RailW, 46.0), Ink);
			P.Box(FVector2D(Size.X - RailW - 16.0, Y), FVector2D(6.0, 46.0), Racer.Paint);
			P.Text(Ordinal(Racer.Place), FVector2D(Size.X - RailW, Y + 6.0), 18, FLinearColor::White);
			P.Text(Racer.Name, FVector2D(Size.X - RailW + 50.0, Y + 4.0), 14, FLinearColor::White);
			P.Text(FString::Printf(TEXT("Lap %d · %s"), Lap, *StatusLine(*Race, Racer)), FVector2D(Size.X - RailW + 50.0, Y + 24.0), 11, Soft);
			Y += 52.0;
		}
		return LayerId + 3;
	}

	// Results (GameOverScreen.tsx): winner line, then rank / name / time (m:ss.mmm or DNF) / best lap.
	const TArray<FKartResult>& Results = Race->GetResults();
	if (Results.Num() == 0)
	{
		return LayerId;
	}
	const double W = 640.0;
	const double Top = Size.Y * 0.18;
	P.Box(FVector2D(Cx - W * 0.5, Top), FVector2D(W, 130.0 + Results.Num() * 44.0), Ink);
	P.Text(TEXT("KART RACER · FINAL"), FVector2D(Cx, Top + 14.0), 12, Soft, 0.5);
	const bool bPhotoFinish = Results.Num() > 1 && Results[1].bFinished && FMath::Abs(Results[1].TimeMs - Results[0].TimeMs) < 1.0;
	P.Text(bPhotoFinish ? FString(TEXT("Photo finish!")) : Results[0].bFinished ? Results[0].Name + TEXT(" wins!") : FString(TEXT("Game over")),
		FVector2D(Cx, Top + 34.0), 32, Accent, 0.5);
	double Y = Top + 90.0;
	for (const FKartResult& Result : Results)
	{
		P.Box(FVector2D(Cx - W * 0.5 + 20.0, Y + 6.0), FVector2D(10.0, 26.0), Result.Paint);
		P.Text(Ordinal(Result.Rank), FVector2D(Cx - W * 0.5 + 44.0, Y + 6.0), 18, FLinearColor::White);
		P.Text(Result.Name, FVector2D(Cx - W * 0.5 + 110.0, Y + 6.0), 18, FLinearColor::White);
		P.Text(Result.bFinished ? FormatRaceTime(Result.TimeMs) : FString(TEXT("DNF")), FVector2D(Cx + 120.0, Y + 6.0), 18, Result.bFinished ? FLinearColor::White : Soft, 1.0);
		if (Result.BestLapMs > 0.0)
		{
			P.Text(FString::Printf(TEXT("Best lap %s"), *FormatRaceTime(Result.BestLapMs)), FVector2D(Cx + W * 0.5 - 20.0, Y + 10.0), 12, Soft, 1.0);
		}
		Y += 44.0;
	}
	P.Text(TEXT("Enter / Start: race again"), FVector2D(Cx, Y + 14.0), 14, Soft, 0.5);
	return LayerId + 3;
}

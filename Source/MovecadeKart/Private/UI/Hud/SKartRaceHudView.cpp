// One player's view of the race HUD: item slot, lap, place, speed, minimap, status and banners.

#include "UI/Hud/SKartRaceHud.h"

#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Race/KartRaceManager.h"
#include "Race/SKartRaceOverlay.h"
#include "UI/Hud/KartItemIcons.h"
#include "UI/Hud/KartRaceHudShared.h"
#include "UI/KartAccessibility.h"
#include "UI/KartUISubsystem.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "KartHud"

using namespace KartUI;
using namespace KartRaceHud;
using namespace MovecadeKart;

namespace
{
	/** How far the corner widgets slide in from off-view at the countdown (before Scale). */
	constexpr double CornerSlide = 240.0;
	constexpr double BottomSlide = 300.0;
	constexpr double SlideInSeconds = 0.5;

	/** The item slot: its size, and the roulette (RaceUI.tsx: 25 items/s easing to a stop over 1.8 s). */
	constexpr float SlotSize = 150.f;
	constexpr float SlotRadius = 34.f;
	constexpr double RouletteStopSeconds = 1.8;
	constexpr double RouletteItemsPerSecond = 25.0;
	constexpr double LandPopSeconds = 0.4;
	constexpr double LandRingSeconds = 0.45;
	constexpr double UsedLiftSeconds = 0.9;

	/** The place number: the bounce on a change and the arrow that rises (or falls) beside it. */
	constexpr double PlaceBounceSeconds = 0.6;
	constexpr double PlaceArrowSeconds = 1.2;

	constexpr double LapPopSeconds = 0.45;
	/** The speed gauge's width at Scale 1. */
	constexpr double GaugeWidth = 300.0;

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

	/** A smaller chip for the kart's state: drift tier, boost, off-road. BoostKey/BoostLabel cache the boost's name. */
	FText ChipFor(const FKartState& S, FLinearColor& OutColor, FString& BoostKey, FText& BoostLabel)
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
			if (S.ActiveBoost != BoostKey || BoostLabel.IsEmpty())
			{
				BoostKey = S.ActiveBoost;
				BoostLabel = BoostKey.IsEmpty() ? LOCTEXT("Boost", "BOOST") : FText::FromString(BoostKey.Replace(TEXT("_"), TEXT(" ")));
			}
			return BoostLabel;
		}
		if (S.bOnDirt)
		{
			static const FLinearColor Dirt = FLinearColor::FromSRGBColor(FColor(0x9a, 0x6b, 0x3c));
			OutColor = Dirt;
			return LOCTEXT("OffRoad", "OFF-ROAD");
		}
		return FText::GetEmpty();
	}

	bool IsBoosting(const FKartState& S)
	{
		return S.BoostMultiplier > 1.0 || S.Turbo > 0.0;
	}
}

void SKartRaceHud::PaintView(const FKartPainter& P, const AKartRaceManager& Race, const FView& View, int32 NumViews, double Now) const
{
	if (!Race.GetRacers().IsValidIndex(View.Seat) || !Seats.IsValidIndex(View.Seat) || !Race.GetItems().Slots().IsValidIndex(View.Seat)
		|| !Race.GetRacers()[View.Seat].Kart)
	{
		return;
	}
	const FKartRacer& Racer = Race.GetRacers()[View.Seat];
	FSeatAnim& Anim = Seats[View.Seat];
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	const FViewLayout L = LayoutView(P, Race, View, NumViews, Now);
	const double Slide = 1.0 - L.SlideIn;

	// The corners: item slot top-left, lap top-right, place bottom-right, speed bottom-left.
	PaintItemSlot(P, Race, Racer, FVector2D(L.Min.X - Slide * CornerSlide * L.Scale, L.Min.Y), L.Scale, Now);
	PaintLap(P, Anim, Race.GetTrack()->Laps, FVector2D(L.Max.X + Slide * CornerSlide * L.Scale, L.Min.Y), L.Scale, Now);
	if (NumViews == 1)
	{
		// The standings rail sits under the clock in a single view.
		PaintStandings(P, Race, FVector2D(L.Max.X, L.Min.Y + 200.0), 1.f);
	}
	PaintPlace(P, Racer, FVector2D(L.Max.X + Slide * BottomSlide * L.Scale, L.Max.Y), L.Scale, Now);
	PaintSpeed(P, Anim, S, FVector2D(L.Min.X - Slide * BottomSlide * L.Scale, L.Max.Y), L.Scale);

	// Minimap: its own in one or two views; shared in three or four.
	if (NumViews <= 2)
	{
		const float H = NumViews == 1 ? 300.f : static_cast<float>(View.Size.Y * 0.4);
		const FVector2D MapPos = NumViews == 1 ? FVector2D(L.Min.X, L.Max.Y - 190.0 - H) : FVector2D(L.Max.X - H * 1.6, View.Pos.Y + View.Size.Y * 0.5 - H * 0.5);
		PaintMinimap(P, Race, MapPos, H, View.Seat);
	}
	PaintStatus(P, Race, Racer, Anim, View, L.Scale, Now);
	PaintViewBanners(P, Racer, Anim, View, L.Scale, Now);
}

SKartRaceHud::FViewLayout SKartRaceHud::LayoutView(const FKartPainter& P, const AKartRaceManager& Race, const FView& View, int32 NumViews, double Now) const
{
	FViewLayout L;
	L.Scale = FMath::Clamp(static_cast<float>(FMath::Min(View.Size.X / Metric::CanvasW, View.Size.Y / Metric::CanvasH) * 1.25), 0.58f, 1.f);
	const FVector2D Margin(FMath::Max(View.Size.X * 0.04, 24.0), FMath::Max(View.Size.Y * 0.05, 18.0));
	L.Min = View.Pos + Margin;
	L.Max = View.Pos + View.Size - Margin;
	if (NumViews >= 4)
	{
		// Keep the corner each view shares with the centre minimap clear: push the inner side's elements out.
		const FVector2D Screen = P.Size();
		const double Clear = Screen.Y * SharedMapFraction * SharedMapDisc + 8.0;
		if (View.Pos.X + View.Size.X * 0.5 < Screen.X * 0.5)
		{
			L.Max.X -= Clear;
		}
		else
		{
			L.Min.X += Clear;
		}
	}
	if (Race.GetPhase() == EKartRacePhase::Countdown)
	{
		L.SlideIn = Ease::OutBack(static_cast<float>((Now - PhaseAt) / SlideInSeconds), 1.4f);
	}
	return L;
}

void SKartRaceHud::PaintLap(const FKartPainter& P, FSeatAnim& Anim, int32 Laps, const FVector2D& Anchor, float Scale, double Now) const
{
	// Pops on a new lap; sunny on the final lap.
	const bool bFinal = Anim.Lap == Laps && Laps > 1;
	const float Pop = 1.f + 0.35f * FKartAccessibility::MotionScale() * (1.f - Ease::OutCubic(static_cast<float>((Now - Anim.LapAt) / LapPopSeconds)));
	const FSlateFontInfo LapFont = Font(EFont::Display, 76.f * Scale, 6.f * Scale);
	FSeatText& Text = Anim.Text;
	if (Text.Lap != Anim.Lap || Text.Laps != Laps)
	{
		Text.Lap = Anim.Lap;
		Text.Laps = Laps;
		Text.LapText = FString::Printf(TEXT("%d/%d"), Anim.Lap, Laps);
	}
	const FVector2D ValueSize = FKartPainter::Measure(Text.LapText, LapFont);
	const FKartPainter L = P.Transformed(Anchor + FVector2D(-ValueSize.X * 0.5, ValueSize.Y * 0.5), Pop);
	L.Text(Text.LapText, LapFont, Anchor, bFinal ? Color::Sun : Color::White, FVector2D(1.0, 0.0), 5.f * Scale);
	L.Text(LOCTEXT("Lap", "LAP"), Font(EFont::Display, 34.f * Scale, 4.f * Scale), Anchor + FVector2D(-ValueSize.X - 10.0 * Scale, ValueSize.Y * 0.62),
		Color::White, FVector2D(1.0, 1.0), 3.f * Scale);
}

void SKartRaceHud::PaintSpeed(const FKartPainter& P, FSeatAnim& Anim, const FKartState& S, const FVector2D& Origin, float Scale) const
{
	// The number and a capsule gauge that turns gold on a boost.
	const bool bBoost = IsBoosting(S);
	FSeatText& Text = Anim.Text;
	const int32 Kmh = FMath::RoundToInt32(Anim.ShownSpeed);
	if (Kmh != Text.Speed)
	{
		Text.Speed = Kmh;
		Text.SpeedText = FString::FromInt(Kmh);
	}
	const FVector2D NumSize = P.Text(Text.SpeedText, Font(EFont::Display, 78.f * Scale, 6.f * Scale), Origin - FVector2D(0.0, 30.0 * Scale),
		bBoost ? Color::Sun : Color::White, FVector2D(0.0, 1.0), 5.f * Scale);
	P.Text(LOCTEXT("Kmh", "km/h"), Font(EFont::Label, 24.f * Scale, 3.f * Scale), Origin + FVector2D(NumSize.X + 10.0 * Scale, -44.0 * Scale), Color::White, FVector2D(0.0, 1.0));
	const FVector2D BarPos = Origin - FVector2D(0.0, 22.0 * Scale);
	const FVector2D BarSize(GaugeWidth * Scale, 22.0 * Scale);
	const float Fill = FMath::Clamp(static_cast<float>(Anim.ShownSpeed / TopSpeedKmh), 0.f, 1.25f);
	P.Box(BarPos, BarSize, Color::WithAlpha(Color::Night, 0.7f), static_cast<float>(BarSize.Y * 0.5), Color::White, 2.5f * Scale);
	if (Fill > 0.02f)
	{
		const FVector2D FillSize(FMath::Min(1.0f, Fill) * (BarSize.X - 6.0 * Scale), BarSize.Y - 6.0 * Scale);
		P.Gradient(BarPos + FVector2D(3.0, 3.0) * Scale, FillSize, bBoost ? Color::Sun : Color::Sky, bBoost ? Color::SunDeep : Color::Mint, false, static_cast<float>(FillSize.Y * 0.5));
	}
}

void SKartRaceHud::PaintStatus(const FKartPainter& P, const AKartRaceManager& Race, const FKartRacer& Racer, FSeatAnim& Anim, const FView& View, float Scale, double Now) const
{
	// An alert pill (wrong way, spun out) or a smaller state chip, low in the centre of the view.
	if (Race.GetPhase() != EKartRacePhase::Racing || Racer.bFinished)
	{
		return;
	}
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	const FKartItemSlot& Slot = Race.GetItems().Slots()[Racer.Seat];
	bool bAlert = false;
	const FText Status = StatusFor(S, Slot, Race.GetItemTime(), bAlert);
	const FVector2D Anchor(View.Pos.X + View.Size.X * 0.5, View.Pos.Y + View.Size.Y * 0.7);
	if (!Status.IsEmpty())
	{
		const float Pulse = 1.f + 0.05f * FKartAccessibility::MotionScale() * FMath::Sin(static_cast<float>(Now) * 10.f);
		const FSlateFontInfo StatusFont = Font(EFont::Display, 54.f * Scale, 4.f * Scale);
		const FVector2D TextSize = FKartPainter::Measure(Status.ToString(), StatusFont);
		const FVector2D Size(TextSize.X + 80.0 * Scale, 84.0 * Scale);
		const FKartPainter B = P.Transformed(Anchor, Pulse, -2.f);
		const FLinearColor Fill = S.bWrongWay ? FKartAccessibility::Bad() : Color::WithAlpha(Color::Night, 0.8f);
		B.Box(Anchor - Size * 0.5, Size, Fill, static_cast<float>(Size.Y * 0.5), Color::White, 4.f * Scale);
		B.Text(Status, StatusFont, Anchor, S.bWrongWay ? Color::White : Color::Sun, FVector2D(0.5, 0.5), 4.f * Scale);
		if (S.bWrongWay)
		{
			// A U-turn arrow either side.
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				KartWidgets::Arrow(B, Anchor + FVector2D(Side * (Size.X * 0.5 + 36.0 * Scale), 0.0), 20.f * Scale, Side > 0, FKartAccessibility::Bad());
			}
		}
		return;
	}
	FLinearColor ChipColor;
	const FText Label = ChipFor(S, ChipColor, Anim.Text.BoostKey, Anim.Text.BoostLabel);
	if (Label.IsEmpty())
	{
		return;
	}
	const FSlateFontInfo ChipFont = Font(EFont::Display, 30.f * Scale, 3.f * Scale);
	const FVector2D TextSize = FKartPainter::Measure(Label.ToString(), ChipFont);
	const FVector2D Size(TextSize.X + 44.0 * Scale, 50.0 * Scale);
	const FVector2D ChipAnchor = Anchor + FVector2D(0.0, 40.0 * Scale);
	P.Box(ChipAnchor - Size * 0.5, Size, ChipColor, static_cast<float>(Size.Y * 0.5), Color::White, 3.f * Scale);
	P.Text(Label, ChipFont, ChipAnchor, Color::White, FVector2D(0.5, 0.5));
}

void SKartRaceHud::PaintViewBanners(const FKartPainter& P, const FKartRacer& Racer, FSeatAnim& Anim, const FView& View, float Scale, double Now) const
{
	// Final lap and finish banners across this view.
	const FVector2D BannerSize(View.Size.X, 150.0 * Scale);
	const FVector2D BannerPos(View.Pos.X, View.Pos.Y + View.Size.Y * 0.3 - BannerSize.Y * 0.5);
	if (Now - Anim.FinishedAt < FinishBannerSeconds)
	{
		if (Anim.Text.FinishText.IsEmpty())
		{
			Anim.Text.FinishText = FText::Format(LOCTEXT("FinishPlace", "FINISH!  {0}"), FText::FromString(SKartRaceOverlay::Ordinal(Racer.Place)));
		}
		PaintBanner(P, BannerPos, BannerSize, Anim.Text.FinishText,
			Color::ForPlace(Racer.Place) == Color::Sky ? Color::Hot : Color::SunDeep, Now - Anim.FinishedAt, 1.8, Scale, true);
	}
	else if (Now - Anim.FinalLapAt < FinishBannerSeconds)
	{
		PaintBanner(P, BannerPos, BannerSize, LOCTEXT("FinalLap", "FINAL LAP!"), FKartAccessibility::Bad(), Now - Anim.FinalLapAt, 1.6, Scale, false);
	}
}

void SKartRaceHud::PaintItemSlot(const FKartPainter& P, const AKartRaceManager& Race, const FKartRacer& Racer, const FVector2D& Pos, float Scale, double Now) const
{
	if (!Race.GetItems().Slots().IsValidIndex(Racer.Seat))
	{
		return;
	}
	const FKartItemSlot& Slot = Race.GetItems().Slots()[Racer.Seat];
	const double ItemNow = Race.GetItemTime();
	const float Size = SlotSize * Scale;
	const FVector2D Centre = Pos + FVector2D(Size * 0.5, Size * 0.5);
	const float R = SlotRadius * Scale;
	const bool bActive = Slot.bRolling || Slot.Held != EKartItem::None;
	// Landing pop: the strip stops RouletteStopSeconds in; the item bounces in with a flash ring.
	const double Elapsed = FKartItemWorld::RouletteSeconds - (Slot.RollEnds - ItemNow);
	const double LandAge = Slot.bRolling ? Elapsed - RouletteStopSeconds : 10.0;
	const float Land = (LandAge >= 0.0 && LandAge < LandPopSeconds)
		? 1.f + 0.25f * FKartAccessibility::MotionScale() * (1.f - Ease::OutElastic(static_cast<float>(LandAge / LandPopSeconds))) : 1.f;
	const FKartPainter Frame = P.Transformed(Centre, Land);
	Frame.Box(Pos + FVector2D(0.0, 7.0 * Scale), FVector2D(Size, Size), Color::Shadow, R);
	Frame.Box(Pos, FVector2D(Size, Size), bActive ? Color::White : Color::WithAlpha(Color::White, 0.7f), R, Color::Night, 3.f);
	const FVector2D InnerPos = Pos + FVector2D(10.0, 10.0) * Scale;
	const FVector2D InnerSize(Size - 20.f * Scale, Size - 20.f * Scale);
	Frame.Gradient(InnerPos, InnerSize, Color::NightSoft, Color::Night, true, R - 8.f * Scale);

	if (Slot.bRolling && Elapsed < RouletteStopSeconds)
	{
		// The strip scrolls down through the window, easing to a stop.
		const double Spin = RouletteItemsPerSecond * (RouletteStopSeconds * Elapsed - 0.5 * Elapsed * Elapsed) / RouletteStopSeconds;
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
		return;
	}
	if (Slot.Held != EKartItem::None)
	{
		const double Bob = FMath::Sin(Now * 3.0) * 3.0 * Scale;
		KartItemIcons::Draw(Frame, Slot.Held, Centre + FVector2D(0.0, Bob), InnerSize.X * 0.8f, Now);
		if (LandAge >= 0.0 && LandAge < LandRingSeconds && !FKartAccessibility::ReduceMotion())
		{
			const float T = static_cast<float>(LandAge / LandRingSeconds);
			const float Ring = Size * (0.5f + 0.5f * T);
			P.Box(Centre - FVector2D(Ring, Ring), FVector2D(Ring * 2.f, Ring * 2.f), FLinearColor::Transparent, Ring * 0.4f,
				Color::WithAlpha(KartItemIcons::Tint(Slot.Held), 1.f - T), 6.f * Scale);
		}
		// The item's name and how to use it, beside the slot.
		if (Slot.bRolling && LandAge <= 0.0)
		{
			return;
		}
		const FVector2D Label = Pos + FVector2D(Size + 16.0 * Scale, Size * 0.5);
		P.Text(ItemText(Slot.Held, false), Font(EFont::Title, 30.f * Scale, 3.f * Scale), Label - FVector2D(0.0, 4.0 * Scale), Color::White, FVector2D(0.0, 1.0), 3.f * Scale);
		if (!Slot.bRolling)
		{
			const UKartUISubsystem* UI = UKartUISubsystem::Get(Race.GetWorld());
			const bool bPad = Racer.Seat > 0 || (UI && UI->IsUsingGamepad());
			float X = static_cast<float>(Label.X);
			const float GlyphH = 34.f * Scale;
			X += bPad ? KartWidgets::PadGlyph(P, EKeys::Gamepad_FaceButton_Left, FVector2D(X, Label.Y + GlyphH * 0.6), GlyphH)
				: KartWidgets::KeyCap(P, TEXT("E"), FVector2D(X, Label.Y + GlyphH * 0.6), GlyphH);
			P.Text(LOCTEXT("UseItem", "Use"), Font(EFont::Button, 22.f * Scale, 2.f * Scale), FVector2D(X + 8.f * Scale, Label.Y + GlyphH * 0.6), Color::White, FVector2D(0.0, 0.5));
		}
		return;
	}
	if (ItemNow - Slot.LastUsedAt < UsedLiftSeconds && Slot.LastUsed != EKartItem::None)
	{
		// The used item lifts out of the slot and fades.
		const float T = static_cast<float>((ItemNow - Slot.LastUsedAt) / UsedLiftSeconds);
		KartItemIcons::Draw(P.Transformed(Centre, 1.f + T * 0.6f).Faded(1.f - T), Slot.LastUsed, Centre - FVector2D(0.0, T * 40.0 * Scale), InnerSize.X * 0.8f, Now);
		P.Faded(1.f - T).Text(ItemText(Slot.LastUsed, true), Font(EFont::Title, 30.f * Scale, 3.f * Scale),
			Pos + FVector2D(Size + 16.0 * Scale, Size * 0.5), Color::Sun, FVector2D(0.0, 0.5), 3.f * Scale);
	}
}

const FText& SKartRaceHud::ItemText(EKartItem Item, bool bUsed) const
{
	// A deliberate process-wide cache: the item names never change while the game runs.
	constexpr int32 Num = static_cast<int32>(EKartItem::Count) + 1;
	static FText Names[Num];
	static FText Used[Num];
	const int32 Index = FMath::Clamp(static_cast<int32>(Item), 0, Num - 1);
	FText& Out = bUsed ? Used[Index] : Names[Index];
	if (Out.IsEmpty())
	{
		const FText Name = FText::FromString(ItemName(Item));
		Out = bUsed ? FText::Format(LOCTEXT("Used", "{0}!"), Name) : Name;
	}
	return Out;
}

void SKartRaceHud::PaintPlace(const FKartPainter& P, const FKartRacer& Racer, const FVector2D& BottomRight, float Scale, double Now) const
{
	FSeatAnim& Anim = Seats[Racer.Seat];
	const int32 Place = Racer.Place;
	const FLinearColor Fill = Color::ForPlace(Place);
	const FSlateFontInfo NumberFont = Font(EFont::Display, 200.f * Scale, 10.f * Scale);
	const FSlateFontInfo SuffixFont = Font(EFont::Display, 76.f * Scale, 6.f * Scale);
	FSeatText& Text = Anim.Text;
	if (Text.Place != Place)
	{
		Text.Place = Place;
		Text.PlaceNumber = FString::FromInt(Place);
		Text.PlaceSuffix = SKartRaceOverlay::OrdinalSuffix(Place);
	}
	const FVector2D NumberSize = FKartPainter::Measure(Text.PlaceNumber, NumberFont);
	const FVector2D SuffixSize = FKartPainter::Measure(Text.PlaceSuffix, SuffixFont);
	const FVector2D Total(NumberSize.X + SuffixSize.X - 6.0 * Scale, NumberSize.Y);
	// Font ascent leaves room under the glyphs; tuck the number down onto the safe line.
	const FVector2D Origin = BottomRight - FVector2D(Total.X, Total.Y * 0.86);
	const FVector2D Pivot = Origin + Total * 0.5;
	// A place change bounces the number (big, then elastic settle) and tilts it the way it moved.
	const float T = static_cast<float>((Now - Anim.PlaceAt) / PlaceBounceSeconds);
	const float Motion = FKartAccessibility::MotionScale();
	const float Bounce = T < 1.f ? 1.f + (0.45f - 0.45f * Ease::OutElastic(T)) * Motion : 1.f;
	const float Tilt = T < 1.f ? (1.f - T) * 10.f * Motion * (Anim.PlaceDir > 0 ? -1.f : 1.f) : 0.f;
	const FKartPainter N = P.Transformed(Pivot, Bounce, Tilt);
	N.Text(Text.PlaceNumber, NumberFont, Origin, Fill, FVector2D::ZeroVector, 8.f * Scale);
	N.Text(Text.PlaceSuffix, SuffixFont, Origin + FVector2D(NumberSize.X - 6.0 * Scale, NumberSize.Y * 0.2), Fill, FVector2D::ZeroVector, 5.f * Scale);
	// The direction arrow that rises (or falls) next to it for a moment.
	if (Now - Anim.PlaceAt < PlaceArrowSeconds && Anim.PlaceDir != 0)
	{
		const float U = static_cast<float>((Now - Anim.PlaceAt) / PlaceArrowSeconds);
		const FVector2D C = Origin + FVector2D(-40.0 * Scale, NumberSize.Y * 0.45 - Anim.PlaceDir * U * 40.0 * Scale);
		const FKartPainter Arrow = P.Faded(1.f - U);
		const double S = 28.0 * Scale * Anim.PlaceDir;
		Arrow.Polygon({ C + FVector2D(-S * 0.8, S * 0.5), C + FVector2D(0.0, -S * 0.7), C + FVector2D(S * 0.8, S * 0.5) },
			Anim.PlaceDir > 0 ? FKartAccessibility::Good() : FKartAccessibility::Bad());
	}
}

#undef LOCTEXT_NAMESPACE

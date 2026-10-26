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

// Each widget's proportions are a block of named constants (Slate units at Scale 1 unless noted); the paint
// functions below only lay them out.

namespace
{
	/** A view's frame: how its widgets scale with the view and how far in from its edges they sit. */
	namespace ViewArt
	{
		/** Widgets scale with the view (a quarter of 1080p reads at 0.625), within these bounds. */
		constexpr double ScaleBoost = 1.25;
		constexpr float MinScale = 0.58f;
		constexpr double MarginX = 0.04;
		constexpr double MinMarginX = 24.0;
		constexpr double MarginY = 0.05;
		constexpr double MinMarginY = 18.0;
		/** Four views: extra room kept beside the centre minimap's disc. */
		constexpr double ClearPad = 8.0;
		/** How far the corner widgets slide in from off-view at the countdown (before Scale). */
		constexpr double CornerSlide = 240.0;
		constexpr double BottomSlide = 300.0;
		constexpr double SlideInSeconds = 0.5;
		constexpr float SlideOvershoot = 1.4f;
		/** One view: the standings rail sits under the clock. */
		constexpr double StandingsDrop = 200.0;
		/** The view's own minimap: fixed height in one view (above the speed), a share of the view in two. */
		constexpr float SoloMapHeight = 300.f;
		constexpr double SoloMapLift = 190.0;
		/** A phone: the speed and the place sit this far above the touch controls, the minimap is smaller. */
		constexpr double TouchClearance = 12.0;
		constexpr float PhoneMapHeight = 200.f;
		constexpr double PhoneMapLift = 150.0;
		/** A phone's place digits: smaller, so they clear the standings rail above them. */
		constexpr float PhonePlaceScale = 0.78f;
		constexpr double SplitMapHeight = 0.4;
		constexpr double SplitMapWidth = 1.6;
	}

	/** The lap counter, top right: pops on a new lap, sunny on the final lap. */
	namespace LapArt
	{
		constexpr double PopSeconds = 0.45;
		constexpr float Pop = 0.35f;
		constexpr float ValueSize = 76.f;
		constexpr float ValueOutline = 6.f;
		constexpr float ValueShadow = 5.f;
		constexpr float LabelSize = 34.f;
		constexpr float LabelOutline = 4.f;
		constexpr float LabelShadow = 3.f;
		constexpr double LabelGap = 10.0;
		/** The "LAP" label's baseline, as a fraction of the value's height. */
		constexpr double LabelBaseline = 0.62;
	}

	/** The speed readout, bottom left: the number and a capsule gauge that turns gold on a boost. */
	namespace SpeedArt
	{
		constexpr float NumberSize = 78.f;
		constexpr float NumberOutline = 6.f;
		constexpr float NumberShadow = 5.f;
		constexpr double NumberLift = 30.0;
		constexpr float UnitSize = 24.f;
		constexpr float UnitOutline = 3.f;
		constexpr double UnitGap = 10.0;
		constexpr double UnitLift = 44.0;
		constexpr double GaugeWidth = 300.0;
		constexpr double GaugeHeight = 22.0;
		constexpr float GaugeAlpha = 0.7f;
		constexpr float GaugeRim = 2.5f;
		/** The fill sits inside the rim and may read past full on a boost. */
		constexpr double FillInset = 3.0;
		constexpr float MaxFill = 1.25f;
		constexpr float MinFill = 0.02f;
	}

	/** The status pill (wrong way, spun out) or the smaller state chip, low in the middle of the view. */
	namespace StatusArt
	{
		constexpr double Height = 0.7;
		constexpr float PulseAmount = 0.05f;
		constexpr float PulseSpeed = 10.f;
		constexpr float TiltDegrees = -2.f;
		constexpr float PillTextSize = 54.f;
		constexpr float PillOutline = 4.f;
		constexpr double PillPadding = 80.0;
		constexpr double PillHeight = 84.0;
		constexpr float PillAlpha = 0.8f;
		constexpr double UTurnGap = 36.0;
		constexpr float UTurnSize = 20.f;
		constexpr float ChipTextSize = 30.f;
		constexpr float ChipOutline = 3.f;
		constexpr double ChipPadding = 44.0;
		constexpr double ChipHeight = 50.0;
		constexpr double ChipDrop = 40.0;
		constexpr float ChipRim = 3.f;
	}

	/** The finish and final-lap banners across the view. */
	namespace ViewBannerArt
	{
		constexpr double Height = 150.0;
		constexpr double Centre = 0.3;
		constexpr double FinishHold = 1.8;
		constexpr double FinalLapHold = 1.6;
	}

	/** The item slot, top left, and its roulette (RaceUI.tsx: 25 items/s easing to a stop over 1.8 s). */
	namespace SlotArt
	{
		constexpr float Size = 150.f;
		constexpr float Radius = 34.f;
		constexpr double ShadowDrop = 7.0;
		constexpr float IdleAlpha = 0.7f;
		constexpr float Rim = 3.f;
		/** The dark window inside the frame. */
		constexpr double WindowInset = 10.0;
		constexpr float WindowRound = 8.f;
		constexpr double RouletteStopSeconds = 1.8;
		constexpr double RouletteItemsPerSecond = 25.0;
		constexpr float StripIcon = 0.78f;
		/** Motion streaks over the top and bottom of the window while it spins. */
		constexpr double StreakHeight = 0.3;
		/** Where the bottom streak starts, as a fraction of the window's height. */
		constexpr double StreakBottom = 0.7;
		constexpr float StreakAlpha = 0.9f;
		/** The landing: a pop and a flash ring in the item's colour. */
		constexpr double LandPopSeconds = 0.4;
		constexpr float LandPop = 0.25f;
		constexpr double LandRingSeconds = 0.45;
		constexpr float RingRound = 0.4f;
		constexpr float RingWidth = 6.f;
		constexpr float HeldIcon = 0.8f;
		constexpr double BobSpeed = 3.0;
		constexpr double BobHeight = 3.0;
		/** The item's name and how to use it, beside the slot. */
		constexpr double LabelGap = 16.0;
		constexpr float NameSize = 30.f;
		constexpr float NameOutline = 3.f;
		constexpr double NameLift = 4.0;
		constexpr float GlyphHeight = 34.f;
		constexpr float GlyphDrop = 0.6f;
		constexpr float UseSize = 22.f;
		constexpr float UseOutline = 2.f;
		constexpr float UseGap = 8.f;
		/** A used item lifts out of the slot and fades. */
		constexpr double UsedLiftSeconds = 0.9;
		constexpr float UsedGrow = 0.6f;
		constexpr double UsedRise = 40.0;
	}

	/** The place, bottom right: a big number, its suffix, and an arrow on a change. */
	namespace PlaceArt
	{
		constexpr float NumberSize = 200.f;
		constexpr float NumberOutline = 10.f;
		constexpr float NumberShadow = 8.f;
		constexpr float SuffixSize = 76.f;
		constexpr float SuffixOutline = 6.f;
		constexpr float SuffixShadow = 5.f;
		/** The suffix tucks into the number by this much, and sits this far down its height. */
		constexpr double SuffixTuck = 6.0;
		constexpr double SuffixDrop = 0.2;
		/** Font ascent leaves room under the glyphs; the number is tucked down onto the safe line. */
		constexpr double Baseline = 0.86;
		constexpr double BounceSeconds = 0.6;
		constexpr float Bounce = 0.45f;
		constexpr float TiltDegrees = 10.f;
		constexpr double ArrowSeconds = 1.2;
		constexpr double ArrowX = -40.0;
		constexpr double ArrowY = 0.45;
		constexpr double ArrowRise = 40.0;
		constexpr double ArrowSize = 28.0;
		constexpr double ArrowHalfWidth = 0.8;
		constexpr double ArrowBase = 0.5;
		constexpr double ArrowTip = 0.7;
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

	/** Drift tiers (the sim's drift power thresholds for each boost). */
	constexpr double UltraDrift = 1.8;
	constexpr double SuperDrift = 1.15;
	constexpr double MiniDrift = 0.65;

	/** A smaller chip for the kart's state: drift tier, boost, off-road. BoostKey/BoostLabel cache the boost's name. */
	FText ChipFor(const FKartState& S, FLinearColor& OutColor, FString& BoostKey, FText& BoostLabel)
	{
		if (S.DriftDirection != 0.0)
		{
			if (S.DriftPower >= UltraDrift) { OutColor = Color::Grape; return LOCTEXT("Ultra", "ULTRA"); }
			if (S.DriftPower >= SuperDrift) { OutColor = Color::SunDeep; return LOCTEXT("Super", "SUPER"); }
			if (S.DriftPower >= MiniDrift) { OutColor = Color::Sky; return LOCTEXT("Mini", "MINI"); }
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
	using namespace ViewArt;
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
		PaintStandings(P, Race, FVector2D(L.Max.X, L.Min.Y + StandingsDrop), 1.f);
	}
	PaintPlace(P, Racer, FVector2D(L.Max.X + Slide * BottomSlide * L.Scale, L.BottomRight), Safe.HasTouchZones() ? L.Scale * PhonePlaceScale : L.Scale, Now);
	PaintSpeed(P, Anim, S, FVector2D(L.Min.X - Slide * BottomSlide * L.Scale, L.BottomLeft), L.Scale);

	// Minimap: its own in one or two views; shared in three or four.
	if (NumViews <= 2)
	{
		const float H = NumViews == 1 ? L.MapHeight : static_cast<float>(View.Size.Y * SplitMapHeight);
		const double Lift = Safe.HasTouchZones() ? PhoneMapLift * L.Scale : SoloMapLift;
		const FVector2D MapPos = NumViews == 1 ? FVector2D(L.Min.X, L.BottomLeft - Lift - H)
			: FVector2D(L.Max.X - H * SplitMapWidth, View.Pos.Y + View.Size.Y * 0.5 - H * 0.5);
		PaintMinimap(P, Race, MapPos, H, View.Seat);
	}
	PaintStatus(P, Race, Racer, Anim, View, L.Scale, Now);
	PaintViewBanners(P, Racer, Anim, View, L.Scale, Now);
}

SKartRaceHud::FViewLayout SKartRaceHud::LayoutView(const FKartPainter& P, const AKartRaceManager& Race, const FView& View, int32 NumViews, double Now) const
{
	using namespace ViewArt;
	FViewLayout L;
	L.Scale = FMath::Clamp(static_cast<float>(FMath::Min(View.Size.X / Metric::CanvasW, View.Size.Y / Metric::CanvasH) * ScaleBoost), MinScale, 1.f);
	const FVector2D Margin(FMath::Max(View.Size.X * MarginX, MinMarginX), FMath::Max(View.Size.Y * MarginY, MinMarginY));
	L.Min = View.Pos + Margin;
	L.Max = View.Pos + View.Size - Margin;
	L.MapHeight = SoloMapHeight;
	if (NumViews == 1 && Safe.bPhone)
	{
		// A phone: inside the notch and the home bar, bigger for a hand-held screen, and the bottom corners' widgets
		// lifted above the touch controls.
		L.Min = Safe.SafeMin;
		L.Max = Safe.SafeMax;
		L.Scale = static_cast<float>(View.Size.Y / Metric::CanvasH) * KartSafeArea::PhoneHudBoost;
		L.MapHeight = PhoneMapHeight * L.Scale;
	}
	L.BottomLeft = Safe.TouchLeft.bIsValid && NumViews == 1 ? Safe.TouchLeft.Min.Y - TouchClearance * L.Scale : L.Max.Y;
	L.BottomRight = Safe.TouchRight.bIsValid && NumViews == 1 ? Safe.TouchRight.Min.Y - TouchClearance * L.Scale : L.Max.Y;
	if (NumViews >= 4)
	{
		// Keep the corner each view shares with the centre minimap clear: push the inner side's elements out.
		const FVector2D Screen = P.Size();
		const double Clear = Screen.Y * SharedMapFraction * SharedMapDisc + ClearPad;
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
		L.SlideIn = Ease::OutBack(static_cast<float>((Now - PhaseAt) / SlideInSeconds), SlideOvershoot);
	}
	return L;
}

void SKartRaceHud::PaintLap(const FKartPainter& P, FSeatAnim& Anim, int32 Laps, const FVector2D& Anchor, float Scale, double Now) const
{
	using namespace LapArt;
	const bool bFinal = Anim.Lap == Laps && Laps > 1;
	const float LapPop = 1.f + Pop * FKartAccessibility::MotionScale() * (1.f - Ease::OutCubic(static_cast<float>((Now - Anim.LapAt) / PopSeconds)));
	const FSlateFontInfo LapFont = Font(EFont::Display, ValueSize * Scale, ValueOutline * Scale);
	FSeatText& Text = Anim.Text;
	if (Text.Lap != Anim.Lap || Text.Laps != Laps)
	{
		Text.Lap = Anim.Lap;
		Text.Laps = Laps;
		Text.LapText = FString::Printf(TEXT("%d/%d"), Anim.Lap, Laps);
	}
	const FVector2D ValueExtent = FKartPainter::Measure(Text.LapText, LapFont);
	const FKartPainter L = P.Transformed(Anchor + FVector2D(-ValueExtent.X * 0.5, ValueExtent.Y * 0.5), LapPop);
	L.Text(Text.LapText, LapFont, Anchor, bFinal ? Color::Sun : Color::White, FVector2D(1.0, 0.0), ValueShadow * Scale);
	L.Text(LOCTEXT("Lap", "LAP"), Font(EFont::Display, LabelSize * Scale, LabelOutline * Scale), Anchor + FVector2D(-ValueExtent.X - LabelGap * Scale, ValueExtent.Y * LabelBaseline),
		Color::White, FVector2D(1.0, 1.0), LabelShadow * Scale);
}

void SKartRaceHud::PaintSpeed(const FKartPainter& P, FSeatAnim& Anim, const FKartState& S, const FVector2D& Origin, float Scale) const
{
	using namespace SpeedArt;
	const bool bBoost = IsBoosting(S);
	FSeatText& Text = Anim.Text;
	const int32 Kmh = FMath::RoundToInt32(Anim.ShownSpeed);
	if (Kmh != Text.Speed)
	{
		Text.Speed = Kmh;
		Text.SpeedText = FString::FromInt(Kmh);
	}
	const FVector2D NumExtent = P.Text(Text.SpeedText, Font(EFont::Display, NumberSize * Scale, NumberOutline * Scale), Origin - FVector2D(0.0, NumberLift * Scale),
		bBoost ? Color::Sun : Color::White, FVector2D(0.0, 1.0), NumberShadow * Scale);
	P.Text(LOCTEXT("Kmh", "km/h"), Font(EFont::Label, UnitSize * Scale, UnitOutline * Scale), Origin + FVector2D(NumExtent.X + UnitGap * Scale, -UnitLift * Scale), Color::White,
		FVector2D(0.0, 1.0));
	const FVector2D BarPos = Origin - FVector2D(0.0, GaugeHeight * Scale);
	const FVector2D BarSize(GaugeWidth * Scale, GaugeHeight * Scale);
	const float Fill = FMath::Clamp(static_cast<float>(Anim.ShownSpeed / TopSpeedKmh), 0.f, MaxFill);
	P.Box(BarPos, BarSize, Color::WithAlpha(Color::Night, GaugeAlpha), static_cast<float>(BarSize.Y * 0.5), Color::White, GaugeRim * Scale);
	if (Fill > MinFill)
	{
		const FVector2D FillSize(FMath::Min(1.0f, Fill) * (BarSize.X - 2.0 * FillInset * Scale), BarSize.Y - 2.0 * FillInset * Scale);
		P.Gradient(BarPos + FVector2D(FillInset, FillInset) * Scale, FillSize, bBoost ? Color::Sun : Color::Sky, bBoost ? Color::SunDeep : Color::Mint, false,
			static_cast<float>(FillSize.Y * 0.5));
	}
}

void SKartRaceHud::PaintStatus(const FKartPainter& P, const AKartRaceManager& Race, const FKartRacer& Racer, FSeatAnim& Anim, const FView& View, float Scale, double Now) const
{
	using namespace StatusArt;
	if (Race.GetPhase() != EKartRacePhase::Racing || Racer.bFinished)
	{
		return;
	}
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	const FKartItemSlot& Slot = Race.GetItems().Slots()[Racer.Seat];
	bool bAlert = false;
	const FText Status = StatusFor(S, Slot, Race.GetItemTime(), bAlert);
	const FVector2D Anchor(View.Pos.X + View.Size.X * 0.5, View.Pos.Y + View.Size.Y * Height);
	if (!Status.IsEmpty())
	{
		const float Pulse = 1.f + PulseAmount * FKartAccessibility::MotionScale() * FMath::Sin(static_cast<float>(Now) * PulseSpeed);
		const FSlateFontInfo StatusFont = Font(EFont::Display, PillTextSize * Scale, PillOutline * Scale);
		const FVector2D TextExtent = FKartPainter::Measure(Status.ToString(), StatusFont);
		const FVector2D Size(TextExtent.X + PillPadding * Scale, PillHeight * Scale);
		const FKartPainter B = P.Transformed(Anchor, Pulse, TiltDegrees);
		const FLinearColor Fill = S.bWrongWay ? FKartAccessibility::Bad() : Color::WithAlpha(Color::Night, PillAlpha);
		B.Box(Anchor - Size * 0.5, Size, Fill, static_cast<float>(Size.Y * 0.5), Color::White, PillOutline * Scale);
		B.Text(Status, StatusFont, Anchor, S.bWrongWay ? Color::White : Color::Sun, FVector2D(0.5, 0.5), PillOutline * Scale);
		if (S.bWrongWay)
		{
			// A U-turn arrow either side.
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				KartWidgets::Arrow(B, Anchor + FVector2D(Side * (Size.X * 0.5 + UTurnGap * Scale), 0.0), UTurnSize * Scale, Side > 0, FKartAccessibility::Bad());
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
	const FSlateFontInfo ChipFont = Font(EFont::Display, ChipTextSize * Scale, ChipOutline * Scale);
	const FVector2D TextExtent = FKartPainter::Measure(Label.ToString(), ChipFont);
	const FVector2D Size(TextExtent.X + ChipPadding * Scale, ChipHeight * Scale);
	const FVector2D ChipAnchor = Anchor + FVector2D(0.0, ChipDrop * Scale);
	P.Box(ChipAnchor - Size * 0.5, Size, ChipColor, static_cast<float>(Size.Y * 0.5), Color::White, ChipRim * Scale);
	P.Text(Label, ChipFont, ChipAnchor, Color::White, FVector2D(0.5, 0.5));
}

void SKartRaceHud::PaintViewBanners(const FKartPainter& P, const FKartRacer& Racer, FSeatAnim& Anim, const FView& View, float Scale, double Now) const
{
	using namespace ViewBannerArt;
	const FVector2D BannerSize(View.Size.X, Height * Scale);
	const FVector2D BannerPos(View.Pos.X, View.Pos.Y + View.Size.Y * Centre - BannerSize.Y * 0.5);
	if (Now - Anim.FinishedAt < FinishBannerSeconds)
	{
		if (Anim.Text.FinishText.IsEmpty())
		{
			Anim.Text.FinishText = FText::Format(LOCTEXT("FinishPlace", "FINISH!  {0}"), FText::FromString(SKartRaceOverlay::Ordinal(Racer.Place)));
		}
		PaintBanner(P, BannerPos, BannerSize, Anim.Text.FinishText,
			Color::ForPlace(Racer.Place) == Color::Sky ? Color::Hot : Color::SunDeep, Now - Anim.FinishedAt, FinishHold, Scale, true);
	}
	else if (Now - Anim.FinalLapAt < FinishBannerSeconds)
	{
		PaintBanner(P, BannerPos, BannerSize, LOCTEXT("FinalLap", "FINAL LAP!"), FKartAccessibility::Bad(), Now - Anim.FinalLapAt, FinalLapHold, Scale, false);
	}
}

void SKartRaceHud::PaintItemSlot(const FKartPainter& P, const AKartRaceManager& Race, const FKartRacer& Racer, const FVector2D& Pos, float Scale, double Now) const
{
	using namespace SlotArt;
	if (!Race.GetItems().Slots().IsValidIndex(Racer.Seat))
	{
		return;
	}
	const FKartItemSlot& Slot = Race.GetItems().Slots()[Racer.Seat];
	const double ItemNow = Race.GetItemTime();
	const float SlotSize = Size * Scale;
	const FVector2D Centre = Pos + FVector2D(SlotSize * 0.5, SlotSize * 0.5);
	const float R = Radius * Scale;
	const bool bActive = Slot.bRolling || Slot.Held != EKartItem::None;
	// Landing pop: the strip stops RouletteStopSeconds in; the item bounces in with a flash ring.
	const double Elapsed = FKartItemWorld::RouletteSeconds - (Slot.RollEnds - ItemNow);
	const double LandAge = Slot.bRolling ? Elapsed - RouletteStopSeconds : 10.0;
	const float Land = (LandAge >= 0.0 && LandAge < LandPopSeconds)
		? 1.f + LandPop * FKartAccessibility::MotionScale() * (1.f - Ease::OutElastic(static_cast<float>(LandAge / LandPopSeconds))) : 1.f;
	const FKartPainter Frame = P.Transformed(Centre, Land);
	Frame.Box(Pos + FVector2D(0.0, ShadowDrop * Scale), FVector2D(SlotSize, SlotSize), Color::Shadow, R);
	Frame.Box(Pos, FVector2D(SlotSize, SlotSize), bActive ? Color::White : Color::WithAlpha(Color::White, IdleAlpha), R, Color::Night, Rim);
	const FVector2D InnerPos = Pos + FVector2D(WindowInset, WindowInset) * Scale;
	const FVector2D InnerSize(SlotSize - 2.f * static_cast<float>(WindowInset) * Scale, SlotSize - 2.f * static_cast<float>(WindowInset) * Scale);
	const float InnerRound = R - WindowRound * Scale;
	Frame.Gradient(InnerPos, InnerSize, Color::NightSoft, Color::Night, true, InnerRound);

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
			KartItemIcons::Draw(Frame, Item, At, InnerSize.X * StripIcon, Now);
		}
		Frame.PopClip();
		// Motion streaks so the spin reads as fast.
		Frame.Gradient(InnerPos, FVector2D(InnerSize.X, InnerSize.Y * StreakHeight), Color::WithAlpha(Color::Night, StreakAlpha), Color::WithAlpha(Color::Night, 0.f), true, InnerRound);
		Frame.Gradient(InnerPos + FVector2D(0.0, InnerSize.Y * StreakBottom), FVector2D(InnerSize.X, InnerSize.Y * StreakHeight), Color::WithAlpha(Color::Night, 0.f),
			Color::WithAlpha(Color::Night, StreakAlpha), true, InnerRound);
		return;
	}
	if (Slot.Held != EKartItem::None)
	{
		const double Bob = FMath::Sin(Now * BobSpeed) * BobHeight * Scale;
		KartItemIcons::Draw(Frame, Slot.Held, Centre + FVector2D(0.0, Bob), InnerSize.X * HeldIcon, Now);
		if (LandAge >= 0.0 && LandAge < LandRingSeconds && !FKartAccessibility::ReduceMotion())
		{
			const float T = static_cast<float>(LandAge / LandRingSeconds);
			const float Ring = SlotSize * (0.5f + 0.5f * T);
			P.Box(Centre - FVector2D(Ring, Ring), FVector2D(Ring * 2.f, Ring * 2.f), FLinearColor::Transparent, Ring * RingRound,
				Color::WithAlpha(KartItemIcons::Tint(Slot.Held), 1.f - T), RingWidth * Scale);
		}
		// The item's name and how to use it, beside the slot.
		if (Slot.bRolling && LandAge <= 0.0)
		{
			return;
		}
		const FVector2D Label = Pos + FVector2D(SlotSize + LabelGap * Scale, SlotSize * 0.5);
		P.Text(ItemText(Slot.Held, false), Font(EFont::Title, NameSize * Scale, NameOutline * Scale), Label - FVector2D(0.0, NameLift * Scale), Color::White,
			FVector2D(0.0, 1.0), NameOutline * Scale);
		if (!Slot.bRolling)
		{
			const UKartUISubsystem* UI = UKartUISubsystem::Get(Race.GetWorld());
			const bool bPad = Racer.Seat > 0 || (UI && UI->IsUsingGamepad());
			float X = static_cast<float>(Label.X);
			const float GlyphH = GlyphHeight * Scale;
			X += bPad ? KartWidgets::PadGlyph(P, EKeys::Gamepad_FaceButton_Left, FVector2D(X, Label.Y + GlyphH * GlyphDrop), GlyphH)
				: KartWidgets::KeyCap(P, TEXT("E"), FVector2D(X, Label.Y + GlyphH * GlyphDrop), GlyphH);
			P.Text(LOCTEXT("UseItem", "Use"), Font(EFont::Button, UseSize * Scale, UseOutline * Scale), FVector2D(X + UseGap * Scale, Label.Y + GlyphH * GlyphDrop), Color::White,
				FVector2D(0.0, 0.5));
		}
		return;
	}
	if (ItemNow - Slot.LastUsedAt < UsedLiftSeconds && Slot.LastUsed != EKartItem::None)
	{
		// The used item lifts out of the slot and fades.
		const float T = static_cast<float>((ItemNow - Slot.LastUsedAt) / UsedLiftSeconds);
		KartItemIcons::Draw(P.Transformed(Centre, 1.f + T * UsedGrow).Faded(1.f - T), Slot.LastUsed, Centre - FVector2D(0.0, T * UsedRise * Scale), InnerSize.X * HeldIcon, Now);
		P.Faded(1.f - T).Text(ItemText(Slot.LastUsed, true), Font(EFont::Title, NameSize * Scale, NameOutline * Scale),
			Pos + FVector2D(SlotSize + LabelGap * Scale, SlotSize * 0.5), Color::Sun, FVector2D(0.0, 0.5), NameOutline * Scale);
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
	using namespace PlaceArt;
	FSeatAnim& Anim = Seats[Racer.Seat];
	const int32 Place = Racer.Place;
	const FLinearColor Fill = Color::ForPlace(Place);
	const FSlateFontInfo NumberFont = Font(EFont::Display, NumberSize * Scale, NumberOutline * Scale);
	const FSlateFontInfo SuffixFont = Font(EFont::Display, SuffixSize * Scale, SuffixOutline * Scale);
	FSeatText& Text = Anim.Text;
	if (Text.Place != Place)
	{
		Text.Place = Place;
		Text.PlaceNumber = FString::FromInt(Place);
		Text.PlaceSuffix = SKartRaceOverlay::OrdinalSuffix(Place);
	}
	const FVector2D NumberExtent = FKartPainter::Measure(Text.PlaceNumber, NumberFont);
	const FVector2D SuffixExtent = FKartPainter::Measure(Text.PlaceSuffix, SuffixFont);
	const FVector2D Total(NumberExtent.X + SuffixExtent.X - SuffixTuck * Scale, NumberExtent.Y);
	const FVector2D Origin = BottomRight - FVector2D(Total.X, Total.Y * Baseline);
	const FVector2D Pivot = Origin + Total * 0.5;
	// A place change bounces the number (big, then elastic settle) and tilts it the way it moved.
	const float T = static_cast<float>((Now - Anim.PlaceAt) / BounceSeconds);
	const float Motion = FKartAccessibility::MotionScale();
	const float BounceScale = T < 1.f ? 1.f + (Bounce - Bounce * Ease::OutElastic(T)) * Motion : 1.f;
	const float Tilt = T < 1.f ? (1.f - T) * TiltDegrees * Motion * (Anim.PlaceDir > 0 ? -1.f : 1.f) : 0.f;
	const FKartPainter N = P.Transformed(Pivot, BounceScale, Tilt);
	N.Text(Text.PlaceNumber, NumberFont, Origin, Fill, FVector2D::ZeroVector, NumberShadow * Scale);
	N.Text(Text.PlaceSuffix, SuffixFont, Origin + FVector2D(NumberExtent.X - SuffixTuck * Scale, NumberExtent.Y * SuffixDrop), Fill, FVector2D::ZeroVector, SuffixShadow * Scale);
	// The direction arrow that rises (or falls) next to it for a moment.
	if (Now - Anim.PlaceAt < ArrowSeconds && Anim.PlaceDir != 0)
	{
		const float U = static_cast<float>((Now - Anim.PlaceAt) / ArrowSeconds);
		const FVector2D C = Origin + FVector2D(ArrowX * Scale, NumberExtent.Y * ArrowY - Anim.PlaceDir * U * ArrowRise * Scale);
		const FKartPainter ArrowPainter = P.Faded(1.f - U);
		const double S = ArrowSize * Scale * Anim.PlaceDir;
		ArrowPainter.Polygon({ C + FVector2D(-S * ArrowHalfWidth, S * ArrowBase), C + FVector2D(0.0, -S * ArrowTip), C + FVector2D(S * ArrowHalfWidth, S * ArrowBase) },
			Anim.PlaceDir > 0 ? FKartAccessibility::Good() : FKartAccessibility::Bad());
	}
}

#undef LOCTEXT_NAMESPACE

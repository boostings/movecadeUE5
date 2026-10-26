// The race HUD's panels and full-screen layers: minimap, standings, countdown and GO, intro card, banners.

#include "UI/Hud/SKartRaceHud.h"

#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Race/KartRaceManager.h"
#include "UI/Hud/KartRaceHudShared.h"
#include "UI/KartAccessibility.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartUISubsystem.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "KartHud"

using namespace KartUI;
using namespace MovecadeKart;

// Each panel's proportions are a block of named constants (Slate units at Scale 1 unless noted); the paint
// functions below only lay them out.

namespace
{
	/** The "TIME 1:23" chips. */
	namespace ChipArt
	{
		constexpr float LabelSize = 16.f;
		constexpr float ValueSize = 34.f;
		constexpr float ValueOutline = 2.f;
		constexpr double Padding = 54.0;
		constexpr double Height = 56.0;
		constexpr double Inset = 22.0;
		constexpr float FillAlpha = 0.72f;
		constexpr float RimAlpha = 0.5f;
		constexpr float Rim = 2.f;
		constexpr float LabelAlpha = 0.75f;
	}

	/** The minimap: the road, the start chequer and the kart dots, in fractions of its height or road width. */
	namespace MapArt
	{
		/** The centreline is sampled down to about this many points. */
		constexpr int32 RoadPoints = 180;
		/** The map is nudged right of its box by this fraction of its height. */
		constexpr double Nudge = 0.1;
		constexpr float MinRoadWidth = 4.f;
		constexpr float RoadWidth = 0.035f;
		constexpr float KerbExtra = 7.f;
		constexpr float KerbAlpha = 0.75f;
		constexpr float RoadAlpha = 0.95f;
		constexpr double ChequerCell = 0.7;
		constexpr float FocusDot = 1.9f;
		constexpr float Dot = 1.3f;
		constexpr float FocusOutline = 3.f;
		constexpr float Outline = 2.f;
		/** The focus kart's heading pointer, in dot radii. */
		constexpr double PointerReach = 2.2;
		constexpr double PointerHalfWidth = 0.9;
		constexpr float TagSize = 2.2f;
		constexpr float MinTagSize = 12.f;
		constexpr float TagOutline = 2.f;
		constexpr double TagLift = 2.0;
	}

	/** The standings rail. */
	namespace StandingsArt
	{
		constexpr double RowHeight = 46.0;
		constexpr double RowGap = 6.0;
		constexpr double Width = 250.0;
		constexpr float LocalAlpha = 0.95f;
		constexpr float BotAlpha = 0.6f;
		constexpr float LocalOutline = 3.f;
		constexpr double BadgeInset = 4.0;
		constexpr float NumberSize = 24.f;
		constexpr double SwatchX = 14.0;
		constexpr float SwatchRadius = 8.f;
		constexpr float SwatchOutline = 2.f;
		constexpr double NameX = 32.0;
		constexpr float NameSize = 22.f;
		constexpr double TickInset = 18.0;
		constexpr float TickSize = 22.f;
		constexpr float LocalTickShade = 0.8f;
	}

	/** Countdown digits: punch in, hold, then shrink away at the end of each second, a ring bursting on each beat. */
	namespace CountdownArt
	{
		constexpr double Height = 0.4;
		constexpr double PunchSeconds = 0.28;
		constexpr float PunchOvershoot = 2.6f;
		constexpr double LeaveAt = 0.78;
		constexpr double LeaveSeconds = 0.22;
		/** The digit punches in from MinScale, growing by PunchGrowth to full size. */
		constexpr float MinScale = 0.4f;
		constexpr float PunchGrowth = 0.6f;
		constexpr float LeaveShrink = 0.5f;
		constexpr float SpinDegrees = -25.f;
		constexpr double RingSeconds = 0.5;
		constexpr double RingStart = 140.0;
		constexpr double RingGrow = 160.0;
		constexpr float RingRound = 300.f;
		constexpr float RingWidth = 14.f;
		constexpr float DigitSize = 300.f;
		constexpr float DigitOutline = 16.f;
		constexpr double DigitDrop = 10.0;
		constexpr float DigitShadow = 14.f;
	}

	/** GO!: it punches in over spinning rays, then fades over the first second of racing. */
	namespace GoArt
	{
		constexpr double PunchSeconds = 0.3;
		constexpr float PunchOvershoot = 3.f;
		constexpr double FadeAt = 0.7;
		constexpr double FadeSeconds = 0.5;
		constexpr int32 Rays = 12;
		constexpr double RayStepDegrees = 30.0;
		constexpr double RayWidthDegrees = 11.0;
		constexpr double RaySpin = 60.0;
		constexpr double RayLength = 520.0;
		constexpr float RayAlpha = 0.35f;
		constexpr float MinScale = 0.3f;
		constexpr float PunchGrowth = 0.7f;
		constexpr float SpinDegrees = 15.f;
		constexpr float WordSize = 360.f;
		constexpr float WordOutline = 18.f;
		constexpr double WordDrop = 12.0;
		constexpr float WordShadow = 16.f;
	}

	/** The intro card: it rides in during the intro (1 s waiting + 6 s of shots) and leaves at its end. */
	namespace IntroArt
	{
		constexpr double WaitSeconds = 1.0;
		constexpr double InAt = 0.4;
		constexpr double TaglineInAt = 0.65;
		constexpr double InSeconds = 0.55;
		constexpr float Overshoot = 1.4f;
		constexpr double OutAt = 6.5;
		constexpr double OutSeconds = 0.45;
		constexpr double SlideIn = 900.0;
		constexpr double SlideOut = 1200.0;
		constexpr double Lift = 250.0;
		/** The shade behind the card covers the screen's lower part, from ShadeTop for ShadeHeight. */
		constexpr double ShadeTop = 0.55;
		constexpr double ShadeHeight = 0.45;
		constexpr float ShadeAlpha = 0.55f;
		/** The accent band: slanted, with a white sliver after it. */
		constexpr double BandWidth = 1100.0;
		constexpr double BandHeight = 130.0;
		constexpr double BandLead = 40.0;
		constexpr double SliverTopFrom = 52.0, SliverTopTo = 76.0;
		constexpr double SliverBottomFrom = 12.0, SliverBottomTo = 36.0;
		constexpr float NameSize = 104.f;
		constexpr float NameOutline = 6.f;
		constexpr double NameY = 66.0;
		/** The white tagline strip under the band. */
		constexpr double StripY = 140.0;
		constexpr double StripTrim = 120.0;
		constexpr double StripHeight = 56.0;
		constexpr float TaglineSize = 26.f;
		constexpr double TaglineY = 168.0;
		constexpr float FactsSize = 24.f;
		constexpr float FactsOutline = 3.f;
		constexpr double FactsY = 222.0;
	}

	/** The room pause card (a phone dropped out). */
	namespace RoomPauseArt
	{
		constexpr float ShadeAlpha = 0.5f;
		const FVector2D Size(820.0, 280.0);
		constexpr float Shadow = 12.f;
		constexpr float Rim = 5.f;
		constexpr float TitleSize = 70.f;
		constexpr double TitleY = 80.0;
		constexpr float BodySize = 28.f;
		constexpr double BodyY = 150.0;
		constexpr double DotsY = 220.0;
		constexpr double DotSpacing = 40.0;
		constexpr float DotRadius = 11.f;
		constexpr double BobSpeed = 6.0;
		constexpr double BobPhase = 0.9;
		constexpr double BobHeight = 8.0;
	}

	/** Banners sweep in from the right, hold, and leave to the left. */
	namespace BannerArt
	{
		constexpr double InSeconds = 0.3;
		constexpr double OutSeconds = 0.35;
		/** The slant, as a fraction of the banner's height. */
		constexpr double Skew = 0.25;
		/** The white rules along the top and bottom edges. */
		constexpr double RuleInset = 6.0;
		constexpr double RuleHeight = 6.0;
		constexpr float RuleAlpha = 0.8f;
		/** The chequer blocks near each end. */
		constexpr int32 ChequerRows = 4;
		constexpr int32 ChequerCols = 3;
		constexpr double ChequerLeft = 0.08;
		constexpr double ChequerRight = 0.92;
		constexpr float ChequerAlpha = 0.9f;
		constexpr float PopAmount = 0.08f;
		constexpr double PopAt = 0.25;
		constexpr double PopSeconds = 0.3;
		constexpr float TiltDegrees = -3.f;
		constexpr double FontFraction = 0.62;
		constexpr float TextOutline = 7.f;
	}
}

namespace KartRaceHud
{
	void Chip(const FKartPainter& P, const FVector2D& Anchor, const FVector2D& Align, const FText& Label, const FText& Value,
		const FLinearColor& ValueColor, float Scale)
	{
		using namespace ChipArt;
		const FSlateFontInfo LabelFont = Font(EFont::Label, LabelSize * Scale);
		const FSlateFontInfo ValueFont = Font(EFont::Display, ValueSize * Scale, ValueOutline * Scale);
		const FVector2D LabelExtent = FKartPainter::Measure(Label.ToString(), LabelFont);
		const FVector2D ValueExtent = FKartPainter::Measure(Value.ToString(), ValueFont);
		const FVector2D Size(LabelExtent.X + ValueExtent.X + Padding * Scale, Height * Scale);
		const FVector2D Pos = Anchor - Size * Align;
		P.Box(Pos, Size, Color::WithAlpha(Color::Night, FillAlpha), static_cast<float>(Size.Y * 0.5), Color::WithAlpha(Color::White, RimAlpha), Rim);
		P.Text(Label, LabelFont, Pos + FVector2D(Inset * Scale, Size.Y * 0.5), Color::WithAlpha(Color::White, LabelAlpha), FVector2D(0.0, 0.5));
		P.Text(Value, ValueFont, Pos + FVector2D(Size.X - Inset * Scale, Size.Y * 0.5), ValueColor, FVector2D(1.0, 0.5));
	}
}

void SKartRaceHud::PaintMinimap(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Pos, float Height, int32 FocusSeat) const
{
	using namespace MapArt;
	const FTrack* Track = Race.GetTrack();
	if (!Track || !Track->MinimapBounds.bIsValid || Track->Centre.Num() < 2 || !KartRaceHud::IsRacingPhase(Race.GetPhase()))
	{
		return;
	}
	// RaceUI.tsx minimap: x = (x - minX) / scaleX, y = (z - minZ) / scaleZ.
	const FVector2D Extent = Track->MinimapBounds.GetSize();
	const double Fit = Height / Track->MinimapSpan(); // never zero, even for a degenerate box
	const FVector2D Area = Extent * Fit;
	const FVector2D Offset = Pos + (FVector2D(Height, Height) - Area) * 0.5 + FVector2D(Height * Nudge, 0.0);
	auto ToMap = [&](const FVector2D& W)
	{
		return Offset + (W - Track->MinimapBounds.Min) * Fit;
	};
	// The centreline is sampled once per track; each paint only scales and offsets it into a reused array.
	if (MinimapTrack != Track)
	{
		MinimapTrack = Track;
		MinimapRoad.Reset();
		const int32 Stride = FMath::Max(1, Track->Centre.Num() / RoadPoints);
		MinimapRoad.Reserve(Track->Centre.Num() / Stride + 1);
		for (int32 i = 0; i < Track->Centre.Num(); i += Stride)
		{
			MinimapRoad.Add(Track->Centre[i] - Track->MinimapBounds.Min);
		}
	}
	MinimapScratch.Reset(MinimapRoad.Num());
	for (const FVector2D& Pt : MinimapRoad)
	{
		MinimapScratch.Add(Offset + Pt * Fit);
	}
	const float W = FMath::Max(MinRoadWidth, Height * RoadWidth);
	P.Lines(MinimapScratch, Color::WithAlpha(Color::Night, KerbAlpha), W + KerbExtra, true);
	P.Lines(MinimapScratch, Color::WithAlpha(Color::White, RoadAlpha), W, true);
	// Start/finish: a little chequer.
	if (Track->Checkpoints.Num() > 0)
	{
		const FVector2D F = ToMap(Track->Checkpoints[0].Position);
		const double C = W * ChequerCell;
		for (int32 i = 0; i < 4; ++i)
		{
			P.Box(F + FVector2D((i % 2) * C - C, (i / 2) * C - C), FVector2D(C, C), (i == 0 || i == 3) ? Color::Night : Color::White);
		}
	}
	// Karts: everyone's dot in paint, the focus kart bigger with a heading pointer, drawn last.
	TArray<const FKartRacer*, TInlineAllocator<16>> Order;
	for (const FKartRacer& Racer : Race.GetRacers())
	{
		if (Racer.Kart)
		{
			Order.Add(&Racer);
		}
	}
	// Back to front: the leaders over the pack, the focus kart over everyone (seats are unique, so a plain sort is stable enough).
	Order.Sort([FocusSeat](const FKartRacer& A, const FKartRacer& B)
	{
		const bool bAFocus = A.Seat == FocusSeat, bBFocus = B.Seat == FocusSeat;
		return bAFocus != bBFocus ? bBFocus : A.Place != B.Place ? A.Place > B.Place : A.Seat < B.Seat;
	});
	const bool bLabels = FKartAccessibility::ColorVision() != EKartColorVision::Standard;
	for (const FKartRacer* Racer : Order)
	{
		const FVector2D At = ToMap(Racer->Kart->GetKartMovement()->RenderPosition());
		const bool bFocus = Racer->Seat == FocusSeat || (FocusSeat == INDEX_NONE && Racer->Kind == EKartSeatKind::Local);
		const float R = bFocus ? W * FocusDot : W * Dot;
		if (bFocus)
		{
			const double Yaw = Racer->Kart->GetKartMovement()->RenderYaw();
			const FVector2D Dir(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
			const FVector2D Side(-Dir.Y, Dir.X);
			P.Polygon({ At + Dir * R * PointerReach, At + Side * R * PointerHalfWidth, At - Side * R * PointerHalfWidth }, Color::White);
		}
		P.Circle(At, R, Racer->Paint, bFocus ? Color::White : Color::Night, bFocus ? FocusOutline : Outline);
		if (bLabels && Racer->Kind != EKartSeatKind::Bot)
		{
			// Colour-vision modes tag the players' dots, so the map never relies on paint colour alone.
			P.Text(KartWidgets::SeatTag(Racer->Seat), Font(EFont::Label, FMath::Max(MinTagSize, W * TagSize), TagOutline), At + FVector2D(0.0, -R - TagLift),
				Color::White, FVector2D(0.5, 1.0));
		}
	}
}

void SKartRaceHud::PaintStandings(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& TopRight, float Scale) const
{
	using namespace StandingsArt;
	static const FString Digits[] = { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9"), TEXT("10"), TEXT("11"), TEXT("12") };
	static const FString Tick(TEXT("✓"));
	const double RowH = RowHeight * Scale;
	const double W = Width * Scale;
	const int32 Count = Race.GetStandings().Num();
	const FKartPaintGroup Group(P);
	for (int32 i = 0; i < Count; ++i)
	{
		Group.Next();
		if (!Race.GetRacers().IsValidIndex(Race.GetStandings()[i]))
		{
			continue;
		}
		const FKartRacer& Racer = Race.GetRacers()[Race.GetStandings()[i]];
		const bool bLocal = Racer.Kind != EKartSeatKind::Bot;
		const FVector2D Pos(TopRight.X - W, TopRight.Y + i * (RowH + RowGap * Scale));
		const FLinearColor Back = bLocal ? Color::WithAlpha(Color::White, LocalAlpha) : Color::WithAlpha(Color::Night, BotAlpha);
		P.Box(Pos, FVector2D(W, RowH), Back, static_cast<float>(RowH * 0.5), bLocal ? Racer.Paint : FLinearColor::Transparent, LocalOutline);
		const FVector2D Badge(Pos.X + RowH * 0.5, Pos.Y + RowH * 0.5);
		P.Circle(Badge, static_cast<float>(RowH * 0.5 - BadgeInset), Color::ForPlace(Racer.Place));
		P.Text(Digits[FMath::Clamp(Racer.Place, 0, static_cast<int32>(UE_ARRAY_COUNT(Digits)) - 1)], Font(EFont::Display, NumberSize * Scale), Badge, Color::Navy, FVector2D(0.5, 0.5));
		P.Circle(FVector2D(Pos.X + RowH + SwatchX * Scale, Badge.Y), SwatchRadius * Scale, Racer.Paint, Color::Night, SwatchOutline);
		P.Text(Racer.Name, Font(EFont::Button, NameSize * Scale), FVector2D(Pos.X + RowH + NameX * Scale, Badge.Y), bLocal ? Color::Navy : Color::White, FVector2D(0.0, 0.5));
		if (Racer.bFinished)
		{
			P.Text(Tick, Font(EFont::Body, TickSize * Scale), FVector2D(Pos.X + W - TickInset * Scale, Badge.Y),
				bLocal ? FKartAccessibility::Good() * LocalTickShade : FKartAccessibility::Good(), FVector2D(1.0, 0.5));
		}
	}
}

void SKartRaceHud::PaintCountdown(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const
{
	const FVector2D C(Screen.X * 0.5, Screen.Y * CountdownArt::Height);
	const float Motion = FKartAccessibility::MotionScale();
	if (Race.GetPhase() == EKartRacePhase::Countdown)
	{
		using namespace CountdownArt;
		// 5..1, each punched in big and settling, then shrinking away at the end of its second.
		static const FLinearColor Colors[] = { Color::Hot, Color::SunDeep, Color::Sun, Color::Mint, Color::Sky, Color::Grape };
		const int32 Value = Race.GetCountdownValue();
		const double T = Race.GetPhaseSeconds() - FMath::FloorToDouble(Race.GetPhaseSeconds());
		const FLinearColor Fill = Colors[FMath::Clamp(Value, 0, static_cast<int32>(UE_ARRAY_COUNT(Colors)) - 1)];
		const float In = Ease::OutBack(static_cast<float>(T / PunchSeconds), PunchOvershoot);
		const float Out = T > LeaveAt ? Ease::InCubic(static_cast<float>((T - LeaveAt) / LeaveSeconds)) : 0.f;
		const float Scale = (MinScale + PunchGrowth * In) * (1.f - LeaveShrink * Out);
		const float Spin = (1.f - FMath::Min(In, 1.f)) * SpinDegrees * Motion;
		// A ring bursts out on each beat.
		const float Ring = static_cast<float>(FMath::Min(1.0, T / RingSeconds));
		P.Faded((1.f - Ring) * Motion).Box(C - FVector2D(RingStart + Ring * RingGrow), FVector2D(2.0 * RingStart + Ring * (2.0 * RingGrow)), FLinearColor::Transparent,
			RingRound, Fill, RingWidth * (1.f - Ring));
		const FKartPainter N = P.Transformed(C, Scale, Spin).Faded(1.f - Out);
		static const FString Digits[] = { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5") };
		const FSlateFontInfo DigitFont = Font(EFont::Display, DigitSize, DigitOutline);
		if (Value >= 0 && Value < UE_ARRAY_COUNT(Digits))
		{
			N.Text(Digits[Value], DigitFont, C + FVector2D(0.0, DigitDrop), Fill, FVector2D(0.5, 0.5), DigitShadow);
		}
		else
		{
			N.Text(FString::FromInt(Value), DigitFont, C + FVector2D(0.0, DigitDrop), Fill, FVector2D(0.5, 0.5), DigitShadow);
		}
		return;
	}
	using namespace GoArt;
	// GO!: rays spin behind it while it punches in, then it fades over the first second of racing.
	const double T = Race.GetRaceMs() / 1000.0;
	const float In = Ease::OutBack(static_cast<float>(T / PunchSeconds), PunchOvershoot);
	const float Fade = 1.f - Ease::InCubic(static_cast<float>(FMath::Max(0.0, T - FadeAt) / FadeSeconds));
	const FKartPainter G = P.Faded(Fade);
	const FKartPainter RayPainter = G.Faded(Motion);
	for (int32 i = 0; i < Rays; ++i)
	{
		const double A = FMath::DegreesToRadians(i * RayStepDegrees + T * RaySpin * Motion);
		const double A2 = A + FMath::DegreesToRadians(RayWidthDegrees);
		const double R = RayLength * In;
		RayPainter.Polygon({ C, C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, C + FVector2D(FMath::Cos(A2), FMath::Sin(A2)) * R },
			Color::WithAlpha(i % 2 ? Color::Sun : Color::Hot, RayAlpha));
	}
	const FKartPainter Word = G.Transformed(C, MinScale + PunchGrowth * In, (1.f - FMath::Min(In, 1.f)) * SpinDegrees * Motion);
	Word.Text(LOCTEXT("Go", "GO!"), Font(EFont::Display, WordSize, WordOutline), C + FVector2D(0.0, WordDrop), Color::Sun, FVector2D(0.5, 0.5), WordShadow);
}

void SKartRaceHud::PaintIntro(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const
{
	using namespace IntroArt;
	const FTrack* Track = Race.GetTrack();
	const FKartTrackInfo* Info = FKartTrackCatalog::Find(Track->Id);
	const FLinearColor Accent = Info ? Info->Accent : Color::Hot;
	const double T = Race.GetPhase() == EKartRacePhase::Waiting ? Race.GetPhaseSeconds() : WaitSeconds + Race.GetPhaseSeconds();
	const float In = Ease::OutBack(static_cast<float>((T - InAt) / InSeconds), Overshoot);
	const float Out = Ease::InCubic(static_cast<float>((T - OutAt) / OutSeconds));
	const double Slide = (1.0 - In) * -SlideIn - Out * SlideOut;
	const FVector2D Margin(Safe.SafeMin.X, Screen.Y - Safe.SafeMax.Y);
	const double Y = Screen.Y - Margin.Y - Lift;
	P.Gradient(FVector2D(0.0, Screen.Y * ShadeTop), FVector2D(Screen.X, Screen.Y * ShadeHeight), Color::WithAlpha(Color::Night, 0.f),
		Color::WithAlpha(Color::Night, ShadeAlpha * (1.f - Out)));
	// Slanted accent band with the track name, a white strip with the tagline, chips below.
	const double BandW = BandWidth;
	const double BandH = BandHeight;
	P.Polygon({ FVector2D(Slide - BandLead, Y), FVector2D(Slide + BandW + BandLead, Y), FVector2D(Slide + BandW, Y + BandH), FVector2D(Slide - BandLead, Y + BandH) }, Accent);
	P.Polygon({ FVector2D(Slide + BandW + SliverTopFrom, Y), FVector2D(Slide + BandW + SliverTopTo, Y), FVector2D(Slide + BandW + SliverBottomTo, Y + BandH),
		FVector2D(Slide + BandW + SliverBottomFrom, Y + BandH) }, Color::White);
	if (IntroNameTrack != Track)
	{
		IntroNameTrack = Track;
		IntroName = (Info ? Info->Name : FText::FromString(Track->Name)).ToUpper();
	}
	P.Text(IntroName, Font(EFont::Display, NameSize, NameOutline), FVector2D(Slide + Margin.X, Y + NameY), Color::White, FVector2D(0.0, 0.5), NameOutline);
	const double Slide2 = (1.0 - Ease::OutBack(static_cast<float>((T - TaglineInAt) / InSeconds), Overshoot)) * -SlideIn - Out * SlideOut;
	if (Info)
	{
		P.Box(FVector2D(Slide2 - BandLead, Y + StripY), FVector2D(BandW - StripTrim, StripHeight), Color::White, 0.f);
		P.Text(Info->Tagline, Font(EFont::Body, TaglineSize), FVector2D(Slide2 + Margin.X, Y + TaglineY), Color::Navy, FVector2D(0.0, 0.5));
	}
	const int32 FactsKey = Track->Laps * 1000 + Race.GetRacers().Num();
	if (FactsKey != IntroFactsKey)
	{
		IntroFactsKey = FactsKey;
		IntroFacts = FString::Printf(TEXT("%d LAPS  ·  %d RACERS"), Track->Laps, Race.GetRacers().Num());
	}
	P.Text(IntroFacts, Font(EFont::Label, FactsSize, FactsOutline), FVector2D(Slide2 + Margin.X, Y + FactsY), Color::White, FVector2D(0.0, 0.5), FactsOutline);

	// Skip hint, bottom-right.
	if (Race.GetPhase() == EKartRacePhase::Intro)
	{
		const UKartUISubsystem* UI = UKartUISubsystem::Get(Race.GetWorld());
		FKartScreenFrame Frame;
		Frame.Size = Screen;
		Frame.SafeMin = Safe.SafeMin;
		Frame.SafeMax = Safe.SafeMax;
		Frame.Device = UI ? UI->GetInputDevice() : EKartInputDevice::Keyboard;
		KartWidgets::Hints(P.Faded(In * (1.f - Out)), Frame, { { Frame.Device == EKartInputDevice::Gamepad ? EKartMenuAction::Start : EKartMenuAction::Accept, LOCTEXT("Skip", "Skip") } });
	}
}

void SKartRaceHud::PaintRoomPause(const FKartPainter& P, const FVector2D& Screen, double Now) const
{
	using namespace RoomPauseArt;
	static const FLinearColor Dots[] = { Color::Hot, Color::Sun, Color::Sky };
	P.Box(FVector2D::ZeroVector, Screen, Color::WithAlpha(Color::Night, ShadeAlpha));
	const FVector2D Pos = Screen * 0.5 - Size * 0.5;
	P.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, Shadow, Color::Sun, Rim);
	P.Text(LOCTEXT("Paused", "PAUSED"), Font(EFont::Display, TitleSize), Pos + FVector2D(Size.X * 0.5, TitleY), Color::Navy, FVector2D(0.5, 0.5));
	P.Text(LOCTEXT("WaitingPhone", "Waiting for a phone to reconnect..."), Font(EFont::Body, BodySize), Pos + FVector2D(Size.X * 0.5, BodyY), Color::NavyDim, FVector2D(0.5, 0.5));
	for (int32 i = 0; i < UE_ARRAY_COUNT(Dots); ++i)
	{
		const double Bob = FMath::Sin(Now * BobSpeed - i * BobPhase) * BobHeight;
		P.Circle(Pos + FVector2D(Size.X * 0.5 - DotSpacing + i * DotSpacing, DotsY + Bob), DotRadius, Dots[i]);
	}
}

void SKartRaceHud::PaintBanner(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Text, const FLinearColor& Fill,
	double Age, double Hold, float Scale, bool bChequered) const
{
	using namespace BannerArt;
	const float In = Ease::OutCubic(static_cast<float>(Age / InSeconds));
	const float Out = Ease::InCubic(static_cast<float>((Age - InSeconds - Hold) / OutSeconds));
	if (Out >= 1.f)
	{
		return;
	}
	const double Shift = (1.0 - In) * Size.X - Out * Size.X;
	const double Slant = Size.Y * Skew;
	const FVector2D A = Pos + FVector2D(Shift, 0.0);
	P.Polygon({ A + FVector2D(Slant, 0.0), A + FVector2D(Size.X + Slant, 0.0), A + FVector2D(Size.X, Size.Y), A + FVector2D(0.0, Size.Y) }, Fill);
	P.Box(A + FVector2D(0.0, RuleInset * Scale), FVector2D(Size.X + Slant, RuleHeight * Scale), Color::WithAlpha(Color::White, RuleAlpha));
	P.Box(A + FVector2D(0.0, Size.Y - (RuleInset + RuleHeight) * Scale), FVector2D(Size.X + Slant, RuleHeight * Scale), Color::WithAlpha(Color::White, RuleAlpha));
	if (bChequered)
	{
		const double Cell = Size.Y / ChequerRows;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const double X0 = Side == 0 ? A.X + Size.X * ChequerLeft : A.X + Size.X * ChequerRight - Cell * ChequerCols;
			for (int32 Row = 0; Row < ChequerRows; ++Row)
			{
				for (int32 Col = 0; Col < ChequerCols; ++Col)
				{
					if ((Row + Col) % 2 == 0)
					{
						P.Box(FVector2D(X0 + Col * Cell, A.Y + Row * Cell), FVector2D(Cell, Cell), Color::WithAlpha(Color::White, ChequerAlpha));
					}
				}
			}
		}
	}
	const float Pop = 1.f + PopAmount * FKartAccessibility::MotionScale() * Ease::Pulse(static_cast<float>((Age - PopAt) / PopSeconds));
	const FVector2D C = A + Size * 0.5 + FVector2D(Slant * 0.5, 0.0);
	P.Transformed(C, Pop, TiltDegrees).Text(Text, Font(EFont::Display, static_cast<float>(Size.Y * FontFraction), TextOutline * Scale), C, Color::White,
		FVector2D(0.5, 0.5), TextOutline * Scale);
}

#undef LOCTEXT_NAMESPACE

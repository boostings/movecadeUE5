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

namespace
{
	/** The minimap samples the centreline down to about this many points. */
	constexpr int32 MinimapPoints = 180;

	/** The standings rail at Scale 1. */
	constexpr double StandingsRowHeight = 46.0;
	constexpr double StandingsRowGap = 6.0;
	constexpr double StandingsWidth = 250.0;

	/** Countdown digits: punch in, hold, then shrink away at the end of each second. */
	constexpr double DigitPunchSeconds = 0.28;
	constexpr double DigitLeaveAt = 0.78;
	constexpr double DigitLeaveSeconds = 0.22;
	constexpr double RingBurstSeconds = 0.5;
	/** GO!: punch in, then fade over the first second of racing behind its spinning rays. */
	constexpr double GoPunchSeconds = 0.3;
	constexpr double GoFadeAt = 0.7;
	constexpr double GoFadeSeconds = 0.5;
	constexpr int32 GoRays = 12;
	constexpr double GoRayLength = 520.0;

	/** The intro card: it rides in during the intro (1 s waiting + 6 s of shots) and leaves at its end. */
	constexpr double IntroWaitSeconds = 1.0;
	constexpr double IntroInAt = 0.4;
	constexpr double IntroTaglineInAt = 0.65;
	constexpr double IntroInSeconds = 0.55;
	constexpr double IntroOutAt = 6.5;
	constexpr double IntroOutSeconds = 0.45;
	constexpr double IntroBandWidth = 1100.0;
	constexpr double IntroBandHeight = 130.0;

	/** Banners sweep in from the right, hold, and leave to the left. */
	constexpr double BannerInSeconds = 0.3;
	constexpr double BannerOutSeconds = 0.35;
}

namespace KartRaceHud
{
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

void SKartRaceHud::PaintMinimap(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Pos, float Height, int32 FocusSeat) const
{
	const FTrack* Track = Race.GetTrack();
	if (!Track || !Track->MinimapBounds.bIsValid || Track->Centre.Num() < 2 || !KartRaceHud::IsRacingPhase(Race.GetPhase()))
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
	// The centreline is sampled once per track; each paint only scales and offsets it into a reused array.
	if (MinimapTrack != Track)
	{
		MinimapTrack = Track;
		MinimapRoad.Reset();
		const int32 Stride = FMath::Max(1, Track->Centre.Num() / MinimapPoints);
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
	const float W = FMath::Max(4.f, Height * 0.035f);
	P.Lines(MinimapScratch, Color::WithAlpha(Color::Night, 0.75f), W + 7.f, true);
	P.Lines(MinimapScratch, Color::WithAlpha(Color::White, 0.95f), W, true);
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
		const float R = bFocus ? W * 1.9f : W * 1.3f;
		if (bFocus)
		{
			const double Yaw = Racer->Kart->GetKartMovement()->RenderYaw();
			const FVector2D Dir(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
			const FVector2D Side(-Dir.Y, Dir.X);
			P.Polygon({ At + Dir * R * 2.2, At + Side * R * 0.9, At - Side * R * 0.9 }, Color::White);
		}
		P.Circle(At, R, Racer->Paint, bFocus ? Color::White : Color::Night, bFocus ? 3.f : 2.f);
		if (bLabels && Racer->Kind != EKartSeatKind::Bot)
		{
			// Colour-vision modes tag the players' dots, so the map never relies on paint colour alone.
			P.Text(KartWidgets::SeatTag(Racer->Seat), Font(EFont::Label, FMath::Max(12.f, W * 2.2f), 2.f), At + FVector2D(0.0, -R - 2.0), Color::White, FVector2D(0.5, 1.0));
		}
	}
}

void SKartRaceHud::PaintStandings(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& TopRight, float Scale) const
{
	static const FString Digits[] = { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9"), TEXT("10"), TEXT("11"), TEXT("12") };
	static const FString Tick(TEXT("✓"));
	const double RowH = StandingsRowHeight * Scale;
	const double W = StandingsWidth * Scale;
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
		const FVector2D Pos(TopRight.X - W, TopRight.Y + i * (RowH + StandingsRowGap * Scale));
		const FLinearColor Back = bLocal ? Color::WithAlpha(Color::White, 0.95f) : Color::WithAlpha(Color::Night, 0.6f);
		P.Box(Pos, FVector2D(W, RowH), Back, static_cast<float>(RowH * 0.5), bLocal ? Racer.Paint : FLinearColor::Transparent, 3.f);
		const FVector2D Badge(Pos.X + RowH * 0.5, Pos.Y + RowH * 0.5);
		P.Circle(Badge, static_cast<float>(RowH * 0.5 - 4.0), Color::ForPlace(Racer.Place));
		P.Text(Digits[FMath::Clamp(Racer.Place, 0, static_cast<int32>(UE_ARRAY_COUNT(Digits)) - 1)], Font(EFont::Display, 24.f * Scale), Badge, Color::Navy, FVector2D(0.5, 0.5));
		P.Circle(FVector2D(Pos.X + RowH + 14.0 * Scale, Badge.Y), 8.f * Scale, Racer.Paint, Color::Night, 2.f);
		P.Text(Racer.Name, Font(EFont::Button, 22.f * Scale), FVector2D(Pos.X + RowH + 32.0 * Scale, Badge.Y), bLocal ? Color::Navy : Color::White, FVector2D(0.0, 0.5));
		if (Racer.bFinished)
		{
			P.Text(Tick, Font(EFont::Body, 22.f * Scale), FVector2D(Pos.X + W - 18.0 * Scale, Badge.Y), bLocal ? FKartAccessibility::Good() * 0.8f : FKartAccessibility::Good(), FVector2D(1.0, 0.5));
		}
	}
}

void SKartRaceHud::PaintCountdown(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const
{
	const FVector2D C(Screen.X * 0.5, Screen.Y * 0.4);
	const float Motion = FKartAccessibility::MotionScale();
	if (Race.GetPhase() == EKartRacePhase::Countdown)
	{
		// 5..1, each punched in big and settling, then shrinking away at the end of its second.
		static const FLinearColor Colors[] = { Color::Hot, Color::SunDeep, Color::Sun, Color::Mint, Color::Sky, Color::Grape };
		const int32 Value = Race.GetCountdownValue();
		const double T = Race.GetPhaseSeconds() - FMath::FloorToDouble(Race.GetPhaseSeconds());
		const FLinearColor Fill = Colors[FMath::Clamp(Value, 0, 5)];
		const float In = Ease::OutBack(static_cast<float>(T / DigitPunchSeconds), 2.6f);
		const float Out = T > DigitLeaveAt ? Ease::InCubic(static_cast<float>((T - DigitLeaveAt) / DigitLeaveSeconds)) : 0.f;
		const float Scale = (0.4f + 0.6f * In) * (1.f - 0.5f * Out);
		const float Spin = (1.f - FMath::Min(In, 1.f)) * -25.f * Motion;
		// A ring bursts out on each beat.
		const float Ring = static_cast<float>(FMath::Min(1.0, T / RingBurstSeconds));
		P.Faded((1.f - Ring) * Motion).Box(C - FVector2D(140.0 + Ring * 160.0), FVector2D(280.0 + Ring * 320.0), FLinearColor::Transparent, 300.f, Fill, 14.f * (1.f - Ring));
		const FKartPainter N = P.Transformed(C, Scale, Spin).Faded(1.f - Out);
		N.Text(FString::FromInt(Value), Font(EFont::Display, 300.f, 16.f), C + FVector2D(0.0, 10.0), Fill, FVector2D(0.5, 0.5), 14.f);
		return;
	}
	// GO!: rays spin behind it while it punches in, then it fades over the first second of racing.
	const double T = Race.GetRaceMs() / 1000.0;
	const float In = Ease::OutBack(static_cast<float>(T / GoPunchSeconds), 3.f);
	const float Fade = 1.f - Ease::InCubic(static_cast<float>(FMath::Max(0.0, T - GoFadeAt) / GoFadeSeconds));
	const FKartPainter G = P.Faded(Fade);
	const FKartPainter Rays = G.Faded(Motion);
	for (int32 i = 0; i < GoRays; ++i)
	{
		const double A = FMath::DegreesToRadians(i * 30.0 + T * 60.0 * Motion);
		const double A2 = A + FMath::DegreesToRadians(11.0);
		const double R = GoRayLength * In;
		Rays.Polygon({ C, C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, C + FVector2D(FMath::Cos(A2), FMath::Sin(A2)) * R }, Color::WithAlpha(i % 2 ? Color::Sun : Color::Hot, 0.35f));
	}
	const FKartPainter Word = G.Transformed(C, 0.3f + 0.7f * In, (1.f - FMath::Min(In, 1.f)) * 15.f * Motion);
	Word.Text(LOCTEXT("Go", "GO!"), Font(EFont::Display, 360.f, 18.f), C + FVector2D(0.0, 12.0), Color::Sun, FVector2D(0.5, 0.5), 16.f);
}

void SKartRaceHud::PaintIntro(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const
{
	const FTrack* Track = Race.GetTrack();
	const FKartTrackInfo* Info = FKartTrackCatalog::Find(Track->Id);
	const FLinearColor Accent = Info ? Info->Accent : Color::Hot;
	const double T = Race.GetPhase() == EKartRacePhase::Waiting ? Race.GetPhaseSeconds() : IntroWaitSeconds + Race.GetPhaseSeconds();
	const float In = Ease::OutBack(static_cast<float>((T - IntroInAt) / IntroInSeconds), 1.4f);
	const float Out = Ease::InCubic(static_cast<float>((T - IntroOutAt) / IntroOutSeconds));
	const double Slide = (1.0 - In) * -900.0 - Out * 1200.0;
	const FVector2D Margin = Screen * Metric::SafeFraction;
	const double Y = Screen.Y - Margin.Y - 250.0;
	P.Gradient(FVector2D(0.0, Screen.Y * 0.55), FVector2D(Screen.X, Screen.Y * 0.45), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.55f * (1.f - Out)));
	// Slanted accent band with the track name, a white strip with the tagline, chips below.
	const double BandW = IntroBandWidth;
	const double BandH = IntroBandHeight;
	P.Polygon({ FVector2D(Slide - 40.0, Y), FVector2D(Slide + BandW + 40.0, Y), FVector2D(Slide + BandW, Y + BandH), FVector2D(Slide - 40.0, Y + BandH) }, Accent);
	P.Polygon({ FVector2D(Slide + BandW + 52.0, Y), FVector2D(Slide + BandW + 76.0, Y), FVector2D(Slide + BandW + 36.0, Y + BandH), FVector2D(Slide + BandW + 12.0, Y + BandH) }, Color::White);
	const FText Name = Info ? Info->Name : FText::FromString(Track->Name);
	P.Text(Name.ToUpper(), Font(EFont::Display, 104.f, 6.f), FVector2D(Slide + Margin.X, Y + 66.0), Color::White, FVector2D(0.0, 0.5), 6.f);
	const double Slide2 = (1.0 - Ease::OutBack(static_cast<float>((T - IntroTaglineInAt) / IntroInSeconds), 1.4f)) * -900.0 - Out * 1200.0;
	if (Info)
	{
		P.Box(FVector2D(Slide2 - 40.0, Y + 140.0), FVector2D(BandW - 120.0, 56.0), Color::White, 0.f);
		P.Text(Info->Tagline, Font(EFont::Body, 26.f), FVector2D(Slide2 + Margin.X, Y + 168.0), Color::Navy, FVector2D(0.0, 0.5));
	}
	const int32 FactsKey = Track->Laps * 1000 + Race.GetRacers().Num();
	if (FactsKey != IntroFactsKey)
	{
		IntroFactsKey = FactsKey;
		IntroFacts = FString::Printf(TEXT("%d LAPS  ·  %d RACERS"), Track->Laps, Race.GetRacers().Num());
	}
	P.Text(IntroFacts, Font(EFont::Label, 24.f, 3.f), FVector2D(Slide2 + Margin.X, Y + 222.0), Color::White, FVector2D(0.0, 0.5), 3.f);

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

void SKartRaceHud::PaintRoomPause(const FKartPainter& P, const FVector2D& Screen, double Now) const
{
	static const FLinearColor Dots[] = { Color::Hot, Color::Sun, Color::Sky };
	P.Box(FVector2D::ZeroVector, Screen, Color::WithAlpha(Color::Night, 0.5f));
	const FVector2D Size(820.0, 280.0);
	const FVector2D Pos = Screen * 0.5 - Size * 0.5;
	P.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 12.f, Color::Sun, 5.f);
	P.Text(LOCTEXT("Paused", "PAUSED"), Font(EFont::Display, 70.f), Pos + FVector2D(Size.X * 0.5, 80.0), Color::Navy, FVector2D(0.5, 0.5));
	P.Text(LOCTEXT("WaitingPhone", "Waiting for a phone to reconnect..."), Font(EFont::Body, 28.f), Pos + FVector2D(Size.X * 0.5, 150.0), Color::NavyDim, FVector2D(0.5, 0.5));
	for (int32 i = 0; i < UE_ARRAY_COUNT(Dots); ++i)
	{
		const double Bob = FMath::Sin(Now * 6.0 - i * 0.9) * 8.0;
		P.Circle(Pos + FVector2D(Size.X * 0.5 - 40.0 + i * 40.0, 220.0 + Bob), 11.f, Dots[i]);
	}
}

void SKartRaceHud::PaintBanner(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Text, const FLinearColor& Fill,
	double Age, double Hold, float Scale, bool bChequered) const
{
	// Sweeps in from the right, holds, and leaves to the left.
	const float In = Ease::OutCubic(static_cast<float>(Age / BannerInSeconds));
	const float Out = Ease::InCubic(static_cast<float>((Age - BannerInSeconds - Hold) / BannerOutSeconds));
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
	const float Pop = 1.f + 0.08f * FKartAccessibility::MotionScale() * Ease::Pulse(static_cast<float>((Age - 0.25) / 0.3));
	const FVector2D C = A + Size * 0.5 + FVector2D(Skew * 0.5, 0.0);
	P.Transformed(C, Pop, -3.f).Text(Text, Font(EFont::Display, static_cast<float>(Size.Y * 0.62), 7.f * Scale), C, Color::White, FVector2D(0.5, 0.5), 7.f * Scale);
}

#undef LOCTEXT_NAMESPACE

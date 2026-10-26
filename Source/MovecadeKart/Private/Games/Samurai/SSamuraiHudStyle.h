#pragma once

#include "CoreMinimal.h"
#include "Art/KartMaterials.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

/**
 * SSamuraiHud's styles and layout, shared by its two paint files (SSamuraiHud.cpp: the round's panels;
 * SSamuraiHudCards.cpp: the intro, the practice coach mark and the standings). Canvas units are the 1080-line design
 * canvas (times S on screen); timings are seconds; alphas 0..1.
 */
namespace MovecadeKart::Samurai::HudStyle
{
	namespace KC = KartUI::Color;
	using KartUI::EFont;

	inline const FLinearColor Vermilion = MovecadeKart::Art::Hex(TEXT("#d8343c"));
	inline const FLinearColor Gilt = MovecadeKart::Art::Hex(TEXT("#ffd477"));
	inline const FLinearColor Jade = MovecadeKart::Art::Hex(TEXT("#7cf0c4"));

	/** A line of text's face, size, outline and drop shadow, in 1080-line canvas units. */
	struct FTextStyle
	{
		EFont Face;
		float Size;
		float Outline;
		float Shadow;
	};

	/** A card's corner radius, drop shadow and outline width, in canvas units. */
	struct FCardStyle
	{
		float Radius;
		float Shadow;
		float OutlineWidth;
	};

	inline FSlateFontInfo Font(const FTextStyle& Style, float S)
	{
		return KartUI::Font(Style.Face, Style.Size * S, Style.Outline * S, KC::Night);
	}

	/** An entrance: it pops in over Seconds with an OutBack overshoot of Back. */
	struct FPop
	{
		double Seconds;
		float Back;
	};

	// ---- Layout and style, in canvas units (times S on screen); seconds for timings; alphas 0..1 ----
	namespace Clock
	{
		inline const FVector2D CardSize(220.f, 112.f);
		constexpr FCardStyle Card{ 26.f, 6.f, 3.f };
		constexpr float CardAlpha = 0.82f;
		constexpr FTextStyle Digits{ EFont::Display, 64.f, 3.f, 4.f };
		constexpr FTextStyle Wave{ EFont::Label, 18.f, 0.f, 0.f };
		constexpr float DigitsY = 46.f;
		constexpr float WaveY = 94.f;
		/** The last seconds pulse red. */
		constexpr int32 LateSeconds = 10;
		constexpr float LatePulse = 0.06f;
		constexpr float LatePulseRate = 6.f;
	}
	namespace Lane
	{
		constexpr float MaxWidth = 520.f;
		/** The panels share this much of the screen's width, with a gap between. */
		constexpr float ScreenShare = 0.9f;
		constexpr float Gap = 24.f;
		constexpr float Height = 118.f;
		/** The panel follows its swordsman across the screen, but stays this far in from either edge (0..1). */
		constexpr double MinX01 = 0.12;
		constexpr double MaxX01 = 0.88;
		inline const FVector2D FallbackAt01(0.5, 0.9);
		constexpr FCardStyle Card{ 22.f, 6.f, 2.5f };
		constexpr float FocusOutlineWidth = 4.f;
		constexpr float CardAlpha = 0.86f;
		constexpr float EliminatedAlpha = 0.5f;
		/** Focus: a gold glow pulsing around the panel. */
		constexpr float FocusGlowBase = 0.6f;
		constexpr float FocusGlowPulse = 0.4f;
		constexpr float FocusGlowRate = 5.f;
		constexpr float FocusGlowAlpha = 0.35f;
		constexpr float FocusHaloPad = 6.f;
		constexpr float FocusHaloRadius = 28.f;
		/** The seat's colour band down the left edge. */
		constexpr float BandInset = 18.f;
		constexpr float BandWidth = 6.f;
		constexpr float BandRadius = 3.f;
		constexpr FTextStyle Name{ EFont::Title, 26.f, 1.5f, 2.f };
		inline const FVector2D NameAt(26.f, 32.f);
		constexpr FTextStyle Score{ EFont::Display, 50.f, 2.5f, 3.f };
		/** The score's right edge, in from the panel's right. */
		inline const FVector2D ScoreAt(22.f, 40.f);
		constexpr float LivesX = 34.f;
		constexpr float LivesGap = 30.f;
		constexpr float LivesY = 84.f;
		constexpr float HeartRadius = 10.f;
		constexpr float LostHeartAlpha = 0.18f;
		constexpr FTextStyle Combo{ EFont::Display, 32.f, 2.f, 3.f };
		constexpr float ComboX = 122.f;
		/** From this combo the counter turns gold. */
		constexpr int32 GoldCombo = 6;
		constexpr FTextStyle Out{ EFont::Display, 44.f, 3.f, 4.f };
		constexpr float OutY = 59.f;
	}
	/** A phone held upright: the lane panels in a grid under the clock, the banner lower, wide cards shrunk to fit. */
	namespace Portrait
	{
		constexpr int32 LaneColumns = 2;
		constexpr float LanesBelowClock = 18.f;
		constexpr float BannerY01 = 0.42f;
		/** The intro's widest line (its ink stroke and the seat chips). */
		constexpr float IntroWidth = 1240.f;
	}
	namespace Focus
	{
		constexpr float FromRight = 186.f;
		constexpr float Y = 74.f;
		inline const FVector2D Size(166.f, 22.f);
		constexpr float TrackAlpha = 0.15f;
		constexpr float BarRadius = 11.f;
		constexpr FTextStyle BarLabel{ EFont::Label, 15.f, 2.f, 0.f };
		constexpr FTextStyle ChargeLabel{ EFont::Label, 14.f, 0.f, 0.f };
		constexpr float ChargeLabelAlpha = 0.65f;
		constexpr float PipX = 96.f;
		constexpr float PipGap = 28.f;
		constexpr float PipRadius = 9.f;
		constexpr float PipOutlineAlpha = 0.75f;
		constexpr float PipOutlineWidth = 2.f;
	}
	namespace Callout
	{
		/** Where a lane's callout rises from when it struck nothing (web metres). */
		constexpr double RestY = 2.7;
		constexpr double Lift = 0.35;
		constexpr double RisePerSecond = 90.0;
		constexpr FTextStyle Text{ EFont::Display, 46.f, 4.5f, 6.f };
		constexpr float BigScale = 1.2f;
		constexpr float SmallScale = 0.92f;
		/** Safe-area margin for the pop's overshoot. */
		constexpr float Overshoot = 1.1f;
		constexpr float BigTilt = -5.f;
		constexpr FPop Pop{ 0.2, 2.4f };
		constexpr double FadeSeconds = 0.25;
		constexpr float MissAlpha = 0.85f;
	}
	namespace NoteText
	{
		constexpr double RisePerSecond = 60.0;
		constexpr FTextStyle Text{ EFont::Display, 40.f, 4.f, 5.f };
		constexpr FPop Pop{ 0.18, 2.f };
		constexpr double FadeSeconds = 0.25;
	}
	namespace Banner
	{
		constexpr double Life = 1.5;
		constexpr float Y01 = 0.3f;
		constexpr FTextStyle Text{ EFont::Display, 110.f, 6.f, 8.f };
		constexpr FPop Pop{ 0.28, 2.2f };
		constexpr double FadeSeconds = 0.35;
		/** The ink stroke behind the words, swept in from the left, and its coloured underline. */
		constexpr double SweepSeconds = 0.22;
		constexpr float BandWidth = 900.f;
		constexpr float BandHeight = 124.f;
		constexpr float BandAlpha = 0.72f;
		constexpr float UnderlineY = 116.f;
		constexpr float UnderlineHeight = 8.f;
		constexpr float Tilt = -3.f;
	}
	namespace FocusFrame
	{
		constexpr float MinMood = 0.01f;
		constexpr float Alpha = 0.32f;
		constexpr float PulseBase = 0.8f;
		constexpr float Pulse = 0.2f;
		constexpr float PulseRate = 5.f;
		constexpr float Edge = 140.f;
	}
	namespace Countdown
	{
		constexpr float Y01 = 0.36f;
		constexpr float PopSeconds = 0.35f;
		constexpr float DiscRadius = 150.f;
		constexpr float DiscAlpha = 0.85f;
		constexpr float DiscRimAlpha = 0.6f;
		constexpr float DiscRimWidth = 6.f;
		constexpr FTextStyle Digits{ EFont::Display, 200.f, 9.f, 10.f };
		/** "SLICE!" holds this long into the round, fading over the last part. */
		constexpr double GoMs = 800.0;
		constexpr double GoFadeMs = 300.0;
		constexpr double GoPopMs = 200.0;
		constexpr float GoPopBack = 2.f;
		constexpr float GoTilt = -4.f;
		constexpr FTextStyle Go{ EFont::Display, 160.f, 8.f, 9.f };
	}
	namespace Controls
	{
		/** The controls hint shows for the round's first seconds, fading over the last part. */
		constexpr double ShowMs = 9000.0;
		constexpr double FadeMs = 600.0;
		constexpr float Y01 = 0.7f;
		inline const FVector2D CardSize(1060.f, 80.f);
		constexpr FCardStyle Card{ 22.f, 6.f, 2.f };
		constexpr float CardAlpha = 0.8f;
		constexpr FTextStyle Text{ EFont::Body, 24.f, 0.f, 0.f };
	}
	namespace Intro
	{
		constexpr double FadeInSeconds = 0.45;
		constexpr double FadeOutSeconds = 0.4;
		/** The card clears this much earlier when the practice follows it. */
		constexpr double PracticeLead = 0.3;
		/** Letterbox bars. */
		constexpr double BarsInSeconds = 0.5;
		constexpr double BarsLinger = 0.2;
		constexpr float BarHeight = 96.f;
		constexpr float BarAlpha = 0.92f;
		constexpr float CentreY01 = 0.4f;
		/** The rising sun: when it starts, how long it takes, its overshoot, size, travel and alpha. */
		constexpr double SunFrom = 0.1;
		constexpr FPop SunPop{ 0.6, 1.4f };
		constexpr float SunRadius = 230.f;
		constexpr float SunRise = 60.f;
		constexpr float SunY = 10.f;
		constexpr float SunAlpha = 0.88f;
		/** The ink stroke under the rules line. */
		constexpr double StrokeFrom = 0.25;
		constexpr double StrokeSeconds = 0.35;
		constexpr float StrokeWidth = 1120.f;
		constexpr float StrokeHeight = 70.f;
		constexpr float StrokeY = 92.f;
		constexpr float StrokeTilt = -1.5f;
		constexpr float StrokeAlpha = 0.8f;
		constexpr double TitleFrom = 0.15;
		constexpr FPop TitlePop{ 0.45, 1.8f };
		constexpr FTextStyle Title{ EFont::Display, 150.f, 8.f, 10.f };
		constexpr float TitleY = 20.f;
		constexpr FTextStyle Rules{ EFont::Body, 26.f, 1.f, 2.f };
		constexpr float RulesY = 127.f;
		/** One chip per swordsman, popping in one by one. */
		constexpr float ChipWidth = 280.f;
		constexpr float ChipHeight = 56.f;
		constexpr float ChipGap = 20.f;
		constexpr float ChipsY = 190.f;
		constexpr double ChipsFrom = 0.6;
		constexpr double ChipStagger = 0.12;
		constexpr FPop ChipPop{ 0.35, 2.f };
		constexpr float ChipAlpha = 0.86f;
		constexpr float ChipShadow = 4.f;
		constexpr float ChipOutlineWidth = 3.f;
		constexpr float DotX = 30.f;
		constexpr float DotRadius = 11.f;
		constexpr FTextStyle ChipName{ EFont::Title, 28.f, 0.f, 0.f };
		constexpr float NameX = 52.f;
		constexpr FTextStyle Cpu{ EFont::Label, 14.f, 0.f, 0.f };
		constexpr float CpuFromRight = 18.f;
		constexpr float CpuAlpha = 0.55f;
	}
	namespace Practice
	{
		/** The swipe to make: an arc above the apple, from -150 to -30 degrees, and its bead's run. */
		constexpr double SwipeFrom = 0.3;
		constexpr double SwipeFadeSeconds = 0.3;
		constexpr float ArcRadius = 150.f;
		constexpr float ArcSquash = 0.55f;
		constexpr int32 ArcSteps = 16;
		constexpr float ArcFrom = -150.f;
		constexpr float ArcTo = -30.f;
		constexpr float ArcAlpha = 0.55f;
		constexpr float LineWidth = 5.f;
		constexpr float ArrowLength = 22.f;
		constexpr float ArrowWidth = 14.f;
		constexpr float ArrowAlpha = 0.8f;
		constexpr float BeadRadius = 13.f;
		constexpr float BeadPeriod = 1.1f;
		constexpr float BeadRun = 0.7f;
		constexpr float BeadRimAlpha = 0.6f;
		constexpr float BeadRimWidth = 2.f;
		/** The coach mark. */
		constexpr float CardY01 = 0.76f;
		inline const FVector2D CardSize(1120.f, 140.f);
		constexpr FCardStyle Card{ 26.f, 6.f, 3.f };
		constexpr float CardAlpha = 0.88f;
		constexpr FPop CardPop{ 0.35, 1.8f };
		inline const FVector2D ChipOffset(-530.f, -88.f);
		inline const FVector2D ChipSize(170.f, 36.f);
		inline const FVector2D ChipLabel(-445.f, -70.f);
		constexpr FTextStyle ChipText{ EFont::Label, 18.f, 0.f, 0.f };
		constexpr FTextStyle Line{ EFont::Title, 36.f, 0.f, 0.f };
		constexpr float LineY = -12.f;
		constexpr FTextStyle Sub{ EFont::Body, 22.f, 0.f, 0.f };
		constexpr float SubY = 34.f;
		constexpr float SubAlpha = 0.8f;
		inline const FVector2D SkipOffset(540.f, 92.f);
		constexpr FTextStyle Skip{ EFont::Label, 16.f, 0.f, 2.f };
		constexpr float SkipAlpha = 0.7f;
		constexpr double SkipFadeSeconds = 0.35;
	}
	namespace Standings
	{
		constexpr double FadeSeconds = 0.5;
		constexpr float TitleY01 = 0.2f;
		constexpr float SunRadius = 120.f;
		constexpr float SunAlpha = 0.8f;
		constexpr FTextStyle Title{ EFont::Display, 96.f, 5.f, 7.f };
		constexpr float RowsY01 = 0.3f;
		constexpr float RowPitch = 104.f;
		inline const FVector2D RowSize(800.f, 88.f);
		constexpr FCardStyle Card{ 22.f, 6.f, 3.f };
		constexpr float RowAlpha = 0.85f;
		constexpr double RowsFrom = 0.3;
		constexpr double RowStagger = 0.18;
		constexpr double RowPopSeconds = 0.4;
		constexpr FTextStyle Place{ EFont::Display, 52.f, 0.f, 3.f };
		constexpr float PlaceX = 48.f;
		constexpr FTextStyle Name{ EFont::Title, 36.f, 0.f, 0.f };
		inline const FVector2D NameAt(100.f, 30.f);
		constexpr FTextStyle Detail{ EFont::Body, 18.f, 0.f, 0.f };
		inline const FVector2D DetailAt(100.f, 64.f);
		constexpr float DetailAlpha = 0.75f;
		constexpr FTextStyle Score{ EFont::Display, 56.f, 0.f, 0.f };
		/** The score's right edge, in from the row's right. */
		constexpr float ScoreFromRight = 30.f;
	}

	inline const FVector2D Centred(0.5, 0.5);
	inline const FVector2D LeftMiddle(0.0, 0.5);
	inline const FVector2D RightMiddle(1.0, 0.5);

	inline void Heart(const FKartPainter& P, const FVector2D& C, float R, const FLinearColor& Fill, TArray<FVector2D>& Scratch)
	{
		// Two lobes and a point, in heart radii.
		P.Circle(C + FVector2D(-R * 0.5f, -R * 0.2f), R * 0.58f, Fill);
		P.Circle(C + FVector2D(R * 0.5f, -R * 0.2f), R * 0.58f, Fill);
		Scratch.Reset();
		Scratch.Add(C + FVector2D(-R * 1.05f, -R * 0.05f));
		Scratch.Add(C + FVector2D(R * 1.05f, -R * 0.05f));
		Scratch.Add(C + FVector2D(0.f, R * 1.05f));
		P.Polygon(Scratch, Fill);
	}

	/**
	 * Keeps a centred line of text (drawn at Scale, which may overshoot while it pops in) inside the safe area:
	 * shifts its centre in from either edge by what it would hang over.
	 */
	inline FVector2D InsideSafe(const FVector2D& At, const FString& Text, const FSlateFontInfo& Font, float Scale, const FVector2D& Size)
	{
		const float Half = static_cast<float>(FKartPainter::Measure(Text, Font).X) * Scale * 0.5f;
		const float Margin = static_cast<float>(Size.X) * KartUI::Metric::SafeFraction;
		const float Lo = Margin + Half, Hi = static_cast<float>(Size.X) - Margin - Half;
		return FVector2D(Lo > Hi ? Size.X * 0.5f : FMath::Clamp(static_cast<float>(At.X), Lo, Hi), At.Y);
	}

	inline float Clamp01(double X)
	{
		return FMath::Clamp(static_cast<float>(X), 0.f, 1.f);
	}

	/** Scales below this collapse the transform: pops start from it instead of zero. */
	inline float PopScale(float Scale)
	{
		return FMath::Max(0.01f, Scale);
	}

	/** An entrance's scale Age seconds in. */
	inline float Popped(const FPop& Pop, double Age)
	{
		return KartUI::Ease::OutBack(Clamp01(Age / Pop.Seconds), Pop.Back);
	}

	inline FString FormatInt(int32 Value)
	{
		return FString::FromInt(Value);
	}
}

#include "UI/KartWidgets.h"

#include "UI/KartAccessibility.h"

using namespace KartUI;

// Each control's proportions live in its own block of named constants below, so the drawing code reads as
// layout data plus painting. Glyph and icon proportions are fractions of the glyph's height or radius.

namespace
{
	/** The focus pop every focusable control uses. */
	namespace FocusArt
	{
		constexpr float DefaultPop = 0.05f;
		constexpr double PopSeconds = 0.22;
		constexpr float Overshoot = 3.f;
		/** Reduce motion halves the pop and drops the overshoot. */
		constexpr float ReducedFraction = 0.5f;
	}

	/** Chunky arrows, in units of their size. */
	namespace ArrowArt
	{
		constexpr double Back = 0.45;
		constexpr double Tip = 0.6;
		constexpr double HalfHeight = 0.6;
	}

	/** Stars: ten points alternating outer and inner, the first pointing up. */
	namespace StarArt
	{
		constexpr int32 Points = 10;
		constexpr double StepDegrees = 36.0;
		constexpr double UpDegrees = -90.0;
	}

	/** Keyboard key caps, in fractions of the cap's height. */
	namespace KeyCapArt
	{
		constexpr float FontSize = 0.42f;
		constexpr float LabelPad = 0.55f;
		constexpr double ShadowDrop = 0.1;
		constexpr float Round = 0.24f;
		constexpr float Outline = 2.f;
	}

	/** Gamepad glyphs, in fractions of the glyph's radius (or height for fonts and pill widths). */
	namespace PadArt
	{
		constexpr float Outline = 2.f;
		constexpr float PillRound = 0.8f;
		constexpr float FontSize = 0.42f;
		constexpr float ShoulderWidth = 1.5f;
		constexpr float TriggerWidth = 1.6f;
		/** Face buttons: four dots in a diamond, the pressed one big. */
		constexpr double FaceSpread = 0.48;
		constexpr float FaceOn = 0.3f;
		constexpr float FaceOff = 0.17f;
		/** Menu (three lines) and view (two windows) buttons. */
		constexpr double MenuLineX = -0.42;
		constexpr double MenuLineStep = 0.3;
		constexpr double MenuLineHalf = 0.07;
		constexpr double MenuLineWidth = 0.84;
		constexpr double MenuLineHeight = 0.14;
		constexpr float MenuLineRound = 0.07f;
		constexpr double ViewBackX = -0.45;
		constexpr double ViewBackY = -0.38;
		constexpr double ViewFrontX = -0.1;
		constexpr double ViewFrontY = -0.12;
		constexpr double ViewWidth = 0.55;
		constexpr double ViewHeight = 0.5;
		constexpr float ViewRound = 0.08f;
		/** Sticks: a ring inside the disc and the stick's letter. */
		constexpr float StickRing = 0.62f;
		constexpr float StickRingOutline = 1.5f;
		constexpr float StickFontSize = 0.36f;
	}

	/** The d-pad: a white plus on a dark disc, in fractions of the disc's radius. */
	namespace DPadArt
	{
		constexpr float Outline = 2.f;
		constexpr double ArmWidth = 0.42;
		constexpr double ArmReach = 0.66;
		/** Arm corners, as a fraction of the arm's width. */
		constexpr double ArmRound = 0.25;
		/** The lit tip: inset from the arm's edges, and how far it stops short of the centre. */
		constexpr double LitInset = 0.18;
		constexpr double LitShort = 0.62;
		constexpr float LitRound = 2.f;
	}

	/** Arrow-key glyphs: an arrow inside an empty key cap. */
	namespace ArrowKeyArt
	{
		constexpr float ArrowSize = 0.26f;
		constexpr double Wide = 0.6;
		constexpr double Short = 0.45;
	}

	/** The hint bar along the bottom right. */
	namespace HintsArt
	{
		constexpr float GlyphHeight = 40.f;
		constexpr float FontSize = 24.f;
		constexpr float FontOutline = 2.f;
		/** "Change" on a keyboard shows two caps this far apart. */
		constexpr float PairGap = 4.f;
		constexpr float GlyphGap = 10.f;
		/** A glyph without a label (the Q of "Q E Tabs") sits this close to the next one. */
		constexpr float BareGap = 8.f;
		constexpr float HintGap = 30.f;
		constexpr double PillLeft = 22.0;
		constexpr double PillPadY = 10.0;
		constexpr double PillExtra = 30.0;
		constexpr float PillAlpha = 0.55f;
	}

	/** Screen headers: the slanted accent tag and the title. */
	namespace HeaderArt
	{
		constexpr float InSeconds = 0.45f;
		constexpr double Slide = 80.0;
		/** The tag's corners, relative to the header's top-left. */
		constexpr double TagTop = 6.0;
		constexpr double TagBottom = 70.0;
		constexpr double TagTopLeft = 0.0;
		constexpr double TagTopRight = 34.0;
		constexpr double TagBottomRight = 22.0;
		constexpr double TagBottomLeft = -12.0;
		constexpr float TitleSize = 66.f;
		constexpr float TitleOutline = 4.f;
		constexpr float TitleShadow = 5.f;
		constexpr double TitleX = 44.0;
		constexpr double TitleY = -4.0;
		constexpr float SubtitleSize = 24.f;
		constexpr float SubtitleOutline = 2.f;
		constexpr double SubtitleX = 46.0;
		/** The subtitle tucks this far up under the title's line height. */
		constexpr double SubtitleTuck = 6.0;
	}

	/** The "1 Racers  2 Rules  3 Track" pills at the top right. */
	namespace StepsArt
	{
		constexpr float FontSize = 22.f;
		constexpr float NumberSize = 17.f;
		constexpr float Top = 30.f;
		constexpr float LabelPad = 64.f;
		constexpr double Height = 44.0;
		constexpr float Round = 22.f;
		constexpr float CurrentOutline = 3.f;
		constexpr double BadgeX = 22.0;
		constexpr float BadgeRadius = 14.f;
		constexpr double LabelX = 42.0;
		constexpr float Gap = 12.f;
		/** The tick on a done step: three points relative to the pill's left edge and centre line. */
		constexpr double TickStartX = 15.0, TickMidX = 20.0, TickEndX = 29.0;
		constexpr double TickDip = 5.0;
		constexpr float TickWidth = 3.f;
	}

	/** Menu buttons. */
	namespace ButtonArt
	{
		constexpr double GlowPad = 8.0;
		constexpr float GlowAlpha = 0.35f;
		constexpr float GlowPulse = 0.15f;
		constexpr float GlowSpeed = 5.f;
		constexpr double ShadowDrop = 7.0;
		constexpr float GradientMix = 0.55f;
		constexpr float RimWidth = 4.f;
		constexpr float CardShadow = 6.f;
		/** The chevron (focused) or accent dot (not) sits this far in from the left. */
		constexpr double MarkerX = 34.0;
		constexpr double ChevronBob = 4.0;
		constexpr double ChevronSpeed = 9.0;
		constexpr float ChevronSize = 13.f;
		constexpr float DotRadius = 9.f;
		constexpr double MaxFontSize = 40.0;
		constexpr double FontFraction = 0.46;
		constexpr double LabelX = 64.0;
		/** With a detail line, the label sits just above the centre and the detail just below. */
		constexpr double DetailX = 66.0;
		constexpr double DetailSplit = 2.0;
		constexpr float DetailSize = 19.f;
	}

	/** Option and slider rows: the pill and its label. */
	namespace RowArt
	{
		constexpr float FocusPop = 0.025f;
		constexpr double GlowPad = 6.0;
		constexpr float GlowAlpha = 0.4f;
		constexpr float FocusShadow = 6.f;
		constexpr float FocusRim = 4.f;
		constexpr float Shadow = 5.f;
		constexpr float Alpha = 0.94f;
		constexpr float DisabledAlpha = 0.7f;
		constexpr float LabelSize = 28.f;
		constexpr double LabelX = 34.0;
	}

	/** Option rows: the value between two arrows on the right. */
	namespace OptionArt
	{
		constexpr double MaxValueWidth = 380.0;
		constexpr double ValueFraction = 0.46;
		constexpr double RightPad = 24.0;
		constexpr double NudgeSeconds = 0.15;
		constexpr double NudgeDistance = 7.0;
		constexpr float ArrowAlpha = 0.6f;
		constexpr double ArrowInset = 14.0;
		constexpr float ArrowSize = 12.f;
		/** The arrows' click targets. */
		constexpr double LeftHitX = -6.0;
		constexpr double RightHitX = -38.0;
		constexpr double HitWidth = 44.0;
		constexpr double PopSeconds = 0.2;
		constexpr float Pop = 0.12f;
		/** Room kept clear of the arrows, plus the swatch when there is one. */
		constexpr double ArrowRoom = 64.0;
		constexpr double SwatchRoom = 36.0;
		constexpr float ValueSize = 28.f;
		constexpr float MinValueSize = 18.f;
		constexpr double SwatchGap = 8.0;
		constexpr double SwatchShift = 6.0;
		constexpr float SwatchRadius = 15.f;
		constexpr float SwatchOutline = 2.5f;
		constexpr double SwatchTextShift = 18.0;
	}

	/** Slider rows: chunky bars that grow left to right, and the percentage. */
	namespace SliderArt
	{
		constexpr double RightPad = 136.0;
		constexpr double MaxWidth = 380.0;
		constexpr double WidthFraction = 0.46;
		constexpr double WidthTrim = 90.0;
		constexpr double Gap = 6.0;
		constexpr double MinBar = 16.0;
		constexpr double BarGrowth = 18.0;
		/** The bars stand on a line this far below the row's centre. */
		constexpr double Baseline = 17.0;
		constexpr float OffAlpha = 0.25f;
		constexpr double PopSeconds = 0.2;
		constexpr float PopFrom = 1.25f;
		constexpr float BarRound = 4.f;
		constexpr float PercentSize = 26.f;
		constexpr double PercentPad = 30.0;
		constexpr double HitPad = 10.0;
	}

	/** The big primary pill ("START RACE"), in fractions of its height where noted. */
	namespace PrimaryArt
	{
		constexpr float FontSize = 0.5f;
		constexpr float FontOutline = 3.f;
		constexpr float GlyphHeight = 0.52f;
		constexpr float Padding = 90.f;
		constexpr float Breath = 0.03f;
		constexpr float BreathSpeed = 4.f;
		constexpr float FocusPop = 0.06f;
		constexpr double ShadowDrop = 8.0;
		constexpr float GradientMix = 0.35f;
		constexpr float FocusRim = 5.f;
		constexpr float Rim = 3.f;
		constexpr float GlyphX = 34.f;
		constexpr float GlyphGap = 22.f;
		constexpr float TextShadow = 3.f;
	}

	/** Track outlines. */
	namespace OutlineArt
	{
		constexpr double MinExtent = 1e-3;
		constexpr float KerbExtra = 6.f;
		constexpr float KerbAlpha = 0.55f;
		constexpr float MarkerScale = 1.3f;
		constexpr float MarkerOutline = 2.f;
	}

	float FocusScale(const KartWidgets::FItemState& State, float Amount = FocusArt::DefaultPop)
	{
		if (!State.bFocused)
		{
			return 1.f;
		}
		const float T = static_cast<float>(State.FocusAge / FocusArt::PopSeconds);
		return FKartAccessibility::ReduceMotion() ? 1.f + Amount * FocusArt::ReducedFraction * Ease::OutCubic(T) : 1.f + Amount * Ease::OutBack(T, FocusArt::Overshoot);
	}

	const TCHAR* KeyboardLabel(EKartMenuAction Action)
	{
		switch (Action)
		{
		case EKartMenuAction::Accept: return TEXT("Enter");
		case EKartMenuAction::Back: return TEXT("Esc");
		case EKartMenuAction::Start: return TEXT("Enter");
		case EKartMenuAction::PrevTab: return TEXT("Q");
		case EKartMenuAction::NextTab: return TEXT("E");
		case EKartMenuAction::Alt: return TEXT("R");
		case EKartMenuAction::Pause: return TEXT("P");
		default: return TEXT("");
		}
	}

	FKey PadKey(EKartMenuAction Action)
	{
		switch (Action)
		{
		case EKartMenuAction::Accept: return EKeys::Gamepad_FaceButton_Bottom;
		case EKartMenuAction::Back: return EKeys::Gamepad_FaceButton_Right;
		case EKartMenuAction::Alt: return EKeys::Gamepad_FaceButton_Top;
		case EKartMenuAction::Start: return EKeys::Gamepad_Special_Right;
		case EKartMenuAction::Pause: return EKeys::Gamepad_Special_Left;
		case EKartMenuAction::PrevTab: return EKeys::Gamepad_LeftShoulder;
		case EKartMenuAction::NextTab: return EKeys::Gamepad_RightShoulder;
		case EKartMenuAction::Up: return EKeys::Gamepad_DPad_Up;
		case EKartMenuAction::Down: return EKeys::Gamepad_DPad_Down;
		case EKartMenuAction::Left: return EKeys::Gamepad_DPad_Left;
		default: return EKeys::Gamepad_DPad_Right;
		}
	}

	/** A d-pad on a dark disc: a white plus whose lit arms are sunny. */
	void DPad(const FKartPainter& P, const FVector2D& C, float R, bool bUp, bool bDown, bool bLeft, bool bRight)
	{
		using namespace DPadArt;
		P.Circle(C, R, Color::Night, Color::White, Outline);
		const double Arm = R * ArmWidth, Len = R * ArmReach;
		P.Box(C - FVector2D(Arm * 0.5, Len), FVector2D(Arm, Len * 2.0), Color::White, static_cast<float>(Arm * ArmRound));
		P.Box(C - FVector2D(Len, Arm * 0.5), FVector2D(Len * 2.0, Arm), Color::White, static_cast<float>(Arm * ArmRound));
		const double In = Arm * LitInset, Tip = Len - Arm * LitShort;
		const FVector2D Cell(Arm - In * 2.0, Arm - In * 2.0);
		if (bUp) P.Box(C + FVector2D(-Cell.X * 0.5, -Len + In), FVector2D(Cell.X, Tip), Color::SunDeep, LitRound);
		if (bDown) P.Box(C + FVector2D(-Cell.X * 0.5, Len - In - Tip), FVector2D(Cell.X, Tip), Color::SunDeep, LitRound);
		if (bLeft) P.Box(C + FVector2D(-Len + In, -Cell.Y * 0.5), FVector2D(Tip, Cell.Y), Color::SunDeep, LitRound);
		if (bRight) P.Box(C + FVector2D(Len - In - Tip, -Cell.Y * 0.5), FVector2D(Tip, Cell.Y), Color::SunDeep, LitRound);
	}

	/** "0%" .. "100%", built once. */
	const FString& PercentLabel(int32 Percent)
	{
		static const TArray<FString> Labels = []()
		{
			TArray<FString> Out;
			for (int32 i = 0; i <= 100; ++i)
			{
				Out.Add(FString::Printf(TEXT("%d%%"), i));
			}
			return Out;
		}();
		return Labels[FMath::Clamp(Percent, 0, 100)];
	}

	bool IsArrowAction(EKartMenuAction Action)
	{
		return FKartMenuInputProcessor::IsDirectional(Action);
	}

	/** An option or slider row's pill and label; returns the painter (popped when focused) the row's value uses. */
	FKartPainter RowBack(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, const KartWidgets::FItemState& State)
	{
		using namespace RowArt;
		const FVector2D Centre = Pos + Size * 0.5;
		const FKartPainter B = State.bFocused ? P.Transformed(Centre, FocusScale(State, FocusPop)) : P;
		const float R = static_cast<float>(Size.Y * 0.5);
		if (State.bFocused)
		{
			B.Box(Pos - FVector2D(GlowPad, GlowPad), Size + FVector2D(2.0 * GlowPad, 2.0 * GlowPad), Color::WithAlpha(Color::Sun, GlowAlpha), R + static_cast<float>(GlowPad));
			B.Card(Pos, Size, Color::White, R, FocusShadow, Color::Sun, FocusRim);
		}
		else
		{
			B.Card(Pos, Size, Color::WithAlpha(Color::Paper, State.bEnabled ? Alpha : DisabledAlpha), R, Shadow);
		}
		B.Text(Label, Font(EFont::Button, LabelSize), FVector2D(Pos.X + LabelX, Centre.Y), State.bEnabled ? Color::Navy : Color::NavyDim, FVector2D(0.0, 0.5));
		return B;
	}
}

namespace KartWidgets
{
	float Entrance(const FKartScreenFrame& Frame, float Delay, float Duration)
	{
		return Ease::OutCubic(static_cast<float>((Frame.Age - Delay) / Duration));
	}

	float SlideX(const FKartScreenFrame& Frame, float Distance)
	{
		return (1.f - Ease::OutCubic(Frame.Presence)) * Distance * Frame.Direction;
	}

	const FString& SeatTag(int32 Seat)
	{
		static const FString Tags[] = { TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4"), TEXT("P5"), TEXT("P6"), TEXT("P7"), TEXT("P8") };
		return Tags[FMath::Clamp(Seat, 0, static_cast<int32>(UE_ARRAY_COUNT(Tags)) - 1)];
	}

	const FString& CpuTag()
	{
		static const FString Cpu(TEXT("CPU"));
		return Cpu;
	}

	void Arrow(const FKartPainter& P, const FVector2D& C, float Size, bool bRight, const FLinearColor& Color)
	{
		using namespace ArrowArt;
		const double S = bRight ? 1.0 : -1.0;
		P.Polygon({ C + FVector2D(-Back * Size * S, -HalfHeight * Size), C + FVector2D(Tip * Size * S, 0.0), C + FVector2D(-Back * Size * S, HalfHeight * Size) }, Color);
	}

	void Star(const FKartPainter& P, const FVector2D& C, float Outer, float Inner, const FLinearColor& Color, float Degrees)
	{
		using namespace StarArt;
		TArray<FVector2D, TInlineAllocator<Points>> Corners;
		for (int32 i = 0; i < Points; ++i)
		{
			const double A = FMath::DegreesToRadians(Degrees + UpDegrees + i * StepDegrees);
			const double R = (i % 2 == 0) ? Outer : Inner;
			Corners.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		P.Polygon(Corners, Color);
	}

	float KeyCap(const FKartPainter& P, const FString& Label, const FVector2D& LeftCentre, float Height)
	{
		using namespace KeyCapArt;
		const FSlateFontInfo CapFont = Font(EFont::Label, Height * FontSize);
		const float W = FMath::Max(Height, static_cast<float>(FKartPainter::Measure(Label, CapFont).X) + Height * LabelPad);
		const FVector2D Pos(LeftCentre.X, LeftCentre.Y - Height * 0.5);
		P.Box(Pos + FVector2D(0.0, Height * ShadowDrop), FVector2D(W, Height), Color::NavyDim, Height * Round);
		P.Box(Pos, FVector2D(W, Height), Color::White, Height * Round, Color::Navy, Outline);
		P.Text(Label, CapFont, Pos + FVector2D(W * 0.5, Height * 0.5), Color::Navy, FVector2D(0.5, 0.5));
		return W;
	}

	float PadGlyph(const FKartPainter& P, const FKey& Key, const FVector2D& LeftCentre, float Height)
	{
		using namespace PadArt;
		const float R = Height * 0.5f;
		const FVector2D C(LeftCentre.X + R, LeftCentre.Y);
		auto Pill = [&](const TCHAR* Label, float Width)
		{
			P.Box(FVector2D(LeftCentre.X, LeftCentre.Y - R), FVector2D(Width, Height), Color::Night, R * PillRound, Color::White, Outline);
			P.Text(Label, Font(EFont::Label, Height * FontSize), FVector2D(LeftCentre.X + Width * 0.5, LeftCentre.Y), Color::White, FVector2D(0.5, 0.5));
			return Width;
		};
		// Face buttons by position: a diamond of four dots, the pressed one big and white.
		static const FKey Faces[] = { EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Left };
		static const FVector2D Dirs[] = { FVector2D(0, -1), FVector2D(1, 0), FVector2D(0, 1), FVector2D(-1, 0) };
		for (int32 Face = 0; Face < 4; ++Face)
		{
			if (Key != Faces[Face])
			{
				continue;
			}
			P.Circle(C, R, Color::Night, Color::White, Outline);
			for (int32 i = 0; i < 4; ++i)
			{
				const bool bOn = i == Face;
				P.Circle(C + Dirs[i] * R * FaceSpread, bOn ? R * FaceOn : R * FaceOff, bOn ? Color::White : Color::NavyDim);
			}
			return Height;
		}
		if (Key == EKeys::Gamepad_LeftShoulder) return Pill(TEXT("L"), Height * ShoulderWidth);
		if (Key == EKeys::Gamepad_RightShoulder) return Pill(TEXT("R"), Height * ShoulderWidth);
		if (Key == EKeys::Gamepad_LeftTrigger || Key == EKeys::Gamepad_LeftTriggerAxis) return Pill(TEXT("LT"), Height * TriggerWidth);
		if (Key == EKeys::Gamepad_RightTrigger || Key == EKeys::Gamepad_RightTriggerAxis) return Pill(TEXT("RT"), Height * TriggerWidth);
		if (Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_Special_Left)
		{
			P.Circle(C, R, Color::Night, Color::White, Outline);
			if (Key == EKeys::Gamepad_Special_Right)
			{
				for (int32 i = -1; i <= 1; ++i) // menu: three lines
				{
					P.Box(C + FVector2D(R * MenuLineX, i * R * MenuLineStep - R * MenuLineHalf), FVector2D(R * MenuLineWidth, R * MenuLineHeight), Color::White, R * MenuLineRound);
				}
			}
			else
			{
				// view: two overlapping windows
				P.Box(C + FVector2D(R * ViewBackX, R * ViewBackY), FVector2D(R * ViewWidth, R * ViewHeight), FLinearColor::Transparent, R * ViewRound, Color::White, Outline);
				P.Box(C + FVector2D(R * ViewFrontX, R * ViewFrontY), FVector2D(R * ViewWidth, R * ViewHeight), Color::White, R * ViewRound);
			}
			return Height;
		}
		if (Key == EKeys::Gamepad_LeftThumbstick || Key == EKeys::Gamepad_RightThumbstick || Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_RightX)
		{
			const bool bLeft = Key == EKeys::Gamepad_LeftThumbstick || Key == EKeys::Gamepad_LeftX;
			P.Circle(C, R, Color::Night, Color::White, Outline);
			P.Circle(C, R * StickRing, Color::NightSoft, Color::White, StickRingOutline);
			P.Text(bLeft ? TEXT("L") : TEXT("R"), Font(EFont::Label, Height * StickFontSize), C, Color::White, FVector2D(0.5, 0.5));
			return Height;
		}
		// D-pad: a plus with the pressed arm lit (or all arms for the whole pad).
		const bool bUp = Key == EKeys::Gamepad_DPad_Up, bDown = Key == EKeys::Gamepad_DPad_Down;
		const bool bLeftArm = Key == EKeys::Gamepad_DPad_Left, bRightArm = Key == EKeys::Gamepad_DPad_Right;
		const bool bAll = !(bUp || bDown || bLeftArm || bRightArm);
		DPad(P, C, R, bUp || bAll, bDown || bAll, bLeftArm || bAll, bRightArm || bAll);
		return Height;
	}

	float GlyphWidth(EKartMenuAction Action, EKartInputDevice Device, float Height)
	{
		if (Device == EKartInputDevice::Gamepad)
		{
			return Action == EKartMenuAction::PrevTab || Action == EKartMenuAction::NextTab ? Height * PadArt::ShoulderWidth : Height;
		}
		if (IsArrowAction(Action))
		{
			return Height;
		}
		const FSlateFontInfo CapFont = Font(EFont::Label, Height * KeyCapArt::FontSize);
		return FMath::Max(Height, static_cast<float>(FKartPainter::Measure(KeyboardLabel(Action), CapFont).X) + Height * KeyCapArt::LabelPad);
	}

	float Glyph(const FKartPainter& P, EKartMenuAction Action, EKartInputDevice Device, const FVector2D& LeftCentre, float Height)
	{
		if (Device == EKartInputDevice::Gamepad)
		{
			return PadGlyph(P, PadKey(Action), LeftCentre, Height);
		}
		if (!IsArrowAction(Action))
		{
			return KeyCap(P, KeyboardLabel(Action), LeftCentre, Height);
		}
		using namespace ArrowKeyArt;
		const float W = KeyCap(P, TEXT(""), LeftCentre, Height);
		const FVector2D C(LeftCentre.X + W * 0.5, LeftCentre.Y);
		const float S = Height * ArrowSize;
		switch (Action)
		{
		case EKartMenuAction::Left: Arrow(P, C, S, false, Color::Navy); break;
		case EKartMenuAction::Right: Arrow(P, C, S, true, Color::Navy); break;
		case EKartMenuAction::Up: P.Polygon({ C + FVector2D(-Wide * S, Short * S), C + FVector2D(0.0, -Wide * S), C + FVector2D(Wide * S, Short * S) }, Color::Navy); break;
		default: P.Polygon({ C + FVector2D(-Wide * S, -Short * S), C + FVector2D(Wide * S, -Short * S), C + FVector2D(0.0, Wide * S) }, Color::Navy); break;
		}
		return W;
	}

	void Hints(const FKartPainter& P, const FKartScreenFrame& Frame, const TArray<FKartHint>& List)
	{
		if (List.Num() == 0)
		{
			return;
		}
		using namespace HintsArt;
		const float H = GlyphHeight;
		const FSlateFontInfo LabelFont = Font(EFont::Button, FontSize, FontOutline);
		// Lay out right to left from the safe area's bottom-right corner, on a soft pill.
		float Total = 0.f;
		auto Width = [&](EKartMenuAction Action)
		{
			// "Change" hints show both directions: two key caps, or a d-pad lit left and right.
			return Action == EKartMenuAction::Left && Frame.Device != EKartInputDevice::Gamepad ? GlyphWidth(Action, Frame.Device, H) * 2.f + PairGap : GlyphWidth(Action, Frame.Device, H);
		};
		for (const FKartHint& Hint : List)
		{
			Total += Hint.Label.IsEmpty() ? Width(Hint.Action) + BareGap
				: Width(Hint.Action) + GlyphGap + static_cast<float>(FKartPainter::Measure(Hint.Label.ToString(), LabelFont).X) + HintGap;
		}
		const FVector2D Origin(Frame.SafeMax.X - Total, Frame.SafeMax.Y - H * 0.5);
		P.Box(Origin - FVector2D(PillLeft, H * 0.5 + PillPadY), FVector2D(Total + PillExtra, H + 2.0 * PillPadY), Color::WithAlpha(Color::Night, PillAlpha),
			(H + 2.f * static_cast<float>(PillPadY)) * 0.5f);
		float X = Origin.X;
		const FKartPaintGroup Group(P);
		for (const FKartHint& Hint : List)
		{
			Group.Next();
			if (Hint.Action == EKartMenuAction::Left && Frame.Device == EKartInputDevice::Gamepad)
			{
				DPad(P, FVector2D(X + H * 0.5, Origin.Y), H * 0.5f, false, false, true, true);
				X += H + GlyphGap;
			}
			else if (Hint.Action == EKartMenuAction::Left)
			{
				X += Glyph(P, EKartMenuAction::Left, Frame.Device, FVector2D(X, Origin.Y), H) + PairGap;
				X += Glyph(P, EKartMenuAction::Right, Frame.Device, FVector2D(X, Origin.Y), H) + GlyphGap;
			}
			else if (Hint.Label.IsEmpty())
			{
				X += Glyph(P, Hint.Action, Frame.Device, FVector2D(X, Origin.Y), H) + BareGap;
				continue;
			}
			else
			{
				X += Glyph(P, Hint.Action, Frame.Device, FVector2D(X, Origin.Y), H) + GlyphGap;
			}
			X += static_cast<float>(P.Text(Hint.Label, LabelFont, FVector2D(X, Origin.Y), Color::White, FVector2D(0.0, 0.5)).X) + HintGap;
		}
	}

	double HintBarRise()
	{
		// The pill is centred half a glyph above the safe bottom and pads PillPadY around the glyphs.
		return HintsArt::GlyphHeight + HintsArt::PillPadY;
	}

	void Header(const FKartPainter& P, const FKartScreenFrame& Frame, const FText& Title, const FText& Subtitle, const FLinearColor& Accent)
	{
		using namespace HeaderArt;
		const float In = Entrance(Frame, 0.f, InSeconds);
		const FVector2D Pos(Frame.SafeMin.X - Slide * (1.0 - In), Frame.SafeMin.Y);
		const FKartPainter Faded = P.Faded(In);
		// A slanted accent tag, then the title in the display face with a heavy outline.
		Faded.Polygon({ Pos + FVector2D(TagTopLeft, TagTop), Pos + FVector2D(TagTopRight, TagTop), Pos + FVector2D(TagBottomRight, TagBottom),
			Pos + FVector2D(TagBottomLeft, TagBottom) }, Accent);
		const FVector2D TitleExtent = Faded.Text(Title, Font(EFont::Display, TitleSize, TitleOutline), Pos + FVector2D(TitleX, TitleY), Color::White, FVector2D::ZeroVector, TitleShadow);
		if (!Subtitle.IsEmpty())
		{
			Faded.Text(Subtitle, Font(EFont::Body, SubtitleSize, SubtitleOutline), Pos + FVector2D(SubtitleX, TitleExtent.Y - SubtitleTuck), Color::WithAlpha(Color::White, 0.92f));
		}
	}

	void Steps(const FKartPainter& P, const FKartScreenFrame& Frame, const TArray<FText>& Names, int32 Current)
	{
		using namespace StepsArt;
		static const FString Numbers[] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9") };
		const FSlateFontInfo StepFont = Font(EFont::Button, FontSize);
		float X = Frame.SafeMax.X;
		const float Y = Frame.SafeMin.Y + Top;
		const FKartPaintGroup Group(P);
		for (int32 i = Names.Num() - 1; i >= 0; --i)
		{
			Group.Next();
			const bool bOn = i == Current;
			const bool bDone = i < Current;
			const FString& Label = Names[i].ToString();
			const float W = static_cast<float>(FKartPainter::Measure(Label, StepFont).X) + LabelPad;
			X -= W;
			// Done steps get a tick as well as the colour, so the state never rests on colour alone.
			const FLinearColor Fill = bOn ? Color::Sun : bDone ? FKartAccessibility::Good() : Color::WithAlpha(Color::Night, 0.6f);
			P.Box(FVector2D(X, Y - Height * 0.5), FVector2D(W, Height), Fill, Round, Color::White, bOn ? CurrentOutline : 0.f);
			P.Circle(FVector2D(X + BadgeX, Y), BadgeRadius, bOn || bDone ? Color::Navy : Color::White);
			if (bDone)
			{
				P.Lines({ FVector2D(X + TickStartX, Y), FVector2D(X + TickMidX, Y + TickDip), FVector2D(X + TickEndX, Y - TickDip) }, Color::White, TickWidth);
			}
			else
			{
				P.Text(Numbers[FMath::Clamp(i, 0, static_cast<int32>(UE_ARRAY_COUNT(Numbers)) - 1)], Font(EFont::Label, NumberSize), FVector2D(X + BadgeX, Y),
					bOn ? Color::White : Color::Navy, FVector2D(0.5, 0.5));
			}
			P.Text(Label, StepFont, FVector2D(X + LabelX, Y), bOn || bDone ? Color::Navy : Color::White, FVector2D(0.0, 0.5));
			X -= Gap;
		}
	}

	void Button(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, const FItemState& State,
		const FLinearColor& Accent, int32 HitId, const FText& Detail)
	{
		using namespace ButtonArt;
		P.Hit(HitId, Pos, Size);
		const FVector2D Centre = Pos + Size * 0.5;
		const FKartPainter B = State.bFocused ? P.Transformed(Centre, FocusScale(State)) : P;
		const float R = static_cast<float>(Size.Y * 0.5);
		if (State.bFocused)
		{
			// Glow, the sunny fill, a white rim and a bouncing chevron.
			const float Glow = GlowAlpha + GlowPulse * FKartAccessibility::MotionScale() * FMath::Sin(static_cast<float>(State.FocusAge) * GlowSpeed);
			B.Box(Pos - FVector2D(GlowPad, GlowPad), Size + FVector2D(2.0 * GlowPad, 2.0 * GlowPad), Color::WithAlpha(Color::Sun, Glow), R + static_cast<float>(GlowPad));
			B.Box(Pos + FVector2D(0.0, ShadowDrop), Size, Color::WithAlpha(Color::SunDeep, 1.f), R);
			B.Gradient(Pos, Size, Color::Sun, FLinearColor::LerpUsingHSV(Color::Sun, Color::SunDeep, GradientMix), true, R);
			B.Box(Pos, Size, FLinearColor::Transparent, R, Color::White, RimWidth);
			const double Bob = FMath::Sin(State.FocusAge * ChevronSpeed) * ChevronBob * FKartAccessibility::MotionScale();
			Arrow(B, FVector2D(Pos.X + MarkerX + Bob, Centre.Y), ChevronSize, true, Color::Navy);
		}
		else
		{
			B.Card(Pos, Size, State.bEnabled ? Color::Paper : Color::PaperShade, R, CardShadow);
			B.Circle(FVector2D(Pos.X + MarkerX, Centre.Y), DotRadius, Accent);
		}
		const FLinearColor Ink = State.bEnabled ? Color::Navy : Color::NavyDim;
		const FSlateFontInfo LabelFont = Font(EFont::Button, static_cast<float>(FMath::Min(MaxFontSize, Size.Y * FontFraction)));
		if (Detail.IsEmpty())
		{
			B.Text(Label, LabelFont, FVector2D(Pos.X + LabelX, Centre.Y), Ink, FVector2D(0.0, 0.5));
		}
		else
		{
			B.Text(Label, LabelFont, FVector2D(Pos.X + LabelX, Centre.Y + DetailSplit), Ink, FVector2D(0.0, 1.0));
			B.Text(Detail, Font(EFont::Body, DetailSize), FVector2D(Pos.X + DetailX, Centre.Y + DetailSplit), State.bFocused ? Color::Navy : Color::NavyDim, FVector2D(0.0, 0.0));
		}
	}

	void OptionRow(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, const FText& Value,
		const FItemState& State, int32 HitId, const FLinearColor& Swatch)
	{
		using namespace OptionArt;
		P.Hit(HitId, Pos, Size);
		const FKartPainter B = RowBack(P, Pos, Size, Label, State);
		const FVector2D Centre = Pos + Size * 0.5;
		// The value sits between two arrows on the right half; the arrow nudges the way it changed.
		const double ValueW = FMath::Min(MaxValueWidth, Size.X * ValueFraction);
		const double Right = Pos.X + Size.X - RightPad;
		const double Left = Right - ValueW;
		const double Nudge = State.ChangeAge < NudgeSeconds ? (1.0 - State.ChangeAge / NudgeSeconds) * NudgeDistance : 0.0;
		const FLinearColor ArrowColor = State.bFocused ? Color::SunDeep : Color::WithAlpha(Color::NavyDim, ArrowAlpha);
		Arrow(B, FVector2D(Left + ArrowInset - (State.ChangeDir < 0 ? Nudge : 0.0), Centre.Y), ArrowSize, false, ArrowColor);
		Arrow(B, FVector2D(Right - ArrowInset + (State.ChangeDir > 0 ? Nudge : 0.0), Centre.Y), ArrowSize, true, ArrowColor);
		P.Hit(HitId | LeftArrowFlag, FVector2D(Left + LeftHitX, Pos.Y), FVector2D(HitWidth, Size.Y));
		P.Hit(HitId | RightArrowFlag, FVector2D(Right + RightHitX, Pos.Y), FVector2D(HitWidth, Size.Y));
		// The value pops a little when it changes.
		const float ValuePop = State.ChangeAge < PopSeconds ? 1.f + Pop * (1.f - Ease::OutCubic(static_cast<float>(State.ChangeAge / PopSeconds))) : 1.f;
		const FVector2D ValueCentre((Left + Right) * 0.5, Centre.Y);
		const FKartPainter V = B.Transformed(ValueCentre, ValuePop);
		// Long values ("1 on this screen") shrink to fit between the arrows rather than run over them.
		const bool bSwatch = Swatch.A > 0.f;
		const double Room = ValueW - ArrowRoom - (bSwatch ? SwatchRoom : 0.0);
		FSlateFontInfo ValueFont = Font(EFont::Button, ValueSize);
		FVector2D ValueExtent = FKartPainter::Measure(Value.ToString(), ValueFont);
		if (ValueExtent.X > Room)
		{
			ValueFont = Font(EFont::Button, FMath::Max(MinValueSize, FMath::FloorToFloat(ValueSize * static_cast<float>(Room / ValueExtent.X))));
			ValueExtent = FKartPainter::Measure(Value.ToString(), ValueFont);
		}
		double TextX = ValueCentre.X;
		if (bSwatch)
		{
			const double SwatchX = ValueCentre.X - ValueExtent.X * 0.5 - SwatchGap;
			V.Circle(FVector2D(SwatchX - SwatchShift, Centre.Y), SwatchRadius, Swatch, Color::Navy, SwatchOutline);
			TextX += SwatchTextShift;
		}
		V.Text(Value, ValueFont, FVector2D(TextX, Centre.Y), State.bEnabled ? Color::Navy : Color::NavyDim, FVector2D(0.5, 0.5));
	}

	void SliderRow(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, float Value,
		const FItemState& State, int32 HitId, int32 Segments)
	{
		using namespace SliderArt;
		P.Hit(HitId, Pos, Size);
		const FKartPainter B = RowBack(P, Pos, Size, Label, State);
		const double Right = Pos.X + Size.X - RightPad;
		const double Width = FMath::Min(MaxWidth, Size.X * WidthFraction) - WidthTrim;
		const double Left = Right - Width;
		const double SegW = (Width - Gap * (Segments - 1)) / Segments;
		const int32 Lit = FMath::RoundToInt32(FMath::Clamp(Value, 0.f, 1.f) * Segments);
		for (int32 i = 0; i < Segments; ++i)
		{
			const double H = MinBar + BarGrowth * (i + 1) / Segments;
			const FVector2D SegPos(Left + i * (SegW + Gap), Pos.Y + Size.Y * 0.5 + Baseline - H);
			const bool bOn = i < Lit;
			const FLinearColor Fill = bOn ? (State.bFocused ? Color::SunDeep : Color::Sky) : Color::WithAlpha(Color::NavyDim, OffAlpha);
			const float Pop = (bOn && i == Lit - 1 && State.ChangeAge < PopSeconds) ? PopFrom - (PopFrom - 1.f) * Ease::OutCubic(static_cast<float>(State.ChangeAge / PopSeconds)) : 1.f;
			B.Transformed(SegPos + FVector2D(SegW * 0.5, H), Pop).Box(SegPos, FVector2D(SegW, H), Fill, BarRound);
		}
		B.Text(PercentLabel(FMath::RoundToInt32(Value * 100.f)), Font(EFont::Button, PercentSize),
			FVector2D(Pos.X + Size.X - PercentPad, Pos.Y + Size.Y * 0.5), Color::Navy, FVector2D(1.0, 0.5));
		P.Hit(HitId | LeftArrowFlag, FVector2D(Left - HitPad, Pos.Y), FVector2D(Width * 0.5 + HitPad, Size.Y));
		P.Hit(HitId | RightArrowFlag, FVector2D(Left + Width * 0.5, Pos.Y), FVector2D(Width * 0.5 + HitPad, Size.Y));
	}

	void PrimaryButton(const FKartPainter& P, const FVector2D& Centre, const FText& Label, EKartMenuAction Action,
		EKartInputDevice Device, const FItemState& State, int32 HitId, float Height)
	{
		using namespace PrimaryArt;
		const FSlateFontInfo LabelFont = Font(EFont::Display, Height * FontSize, FontOutline);
		const float GlyphH = Height * GlyphHeight;
		const float TextW = static_cast<float>(FKartPainter::Measure(Label.ToString(), LabelFont).X);
		const float W = TextW + GlyphWidth(Action, Device, GlyphH) + Padding;
		const FVector2D Pos = Centre - FVector2D(W, Height) * 0.5;
		P.Hit(HitId, Pos, FVector2D(W, Height));
		const float BreathScale = State.bFocused ? 1.f + Breath * FKartAccessibility::MotionScale() * FMath::Sin(static_cast<float>(State.FocusAge) * BreathSpeed) : 1.f;
		const FKartPainter B = P.Transformed(Centre, FocusScale(State, FocusPop) * BreathScale);
		const float R = Height * 0.5f;
		B.Box(Pos + FVector2D(0.0, ShadowDrop), FVector2D(W, Height), Color::Shadow, R);
		B.Gradient(Pos, FVector2D(W, Height), Color::Hot, FLinearColor::LerpUsingHSV(Color::Hot, Color::Grape, GradientMix), true, R);
		B.Box(Pos, FVector2D(W, Height), FLinearColor::Transparent, R, Color::White, State.bFocused ? FocusRim : Rim);
		float X = static_cast<float>(Pos.X) + GlyphX;
		X += Glyph(B, Action, Device, FVector2D(X, Centre.Y), GlyphH) + GlyphGap;
		B.Text(Label, LabelFont, FVector2D(X, Centre.Y), Color::White, FVector2D(0.0, 0.5), TextShadow);
	}

	void TrackOutline(const FKartPainter& P, const TArray<FVector2D>& Outline, const FVector2D& Pos, const FVector2D& Size,
		const FLinearColor& Road, float Thickness, double MarkerPhase, const FLinearColor& Marker)
	{
		using namespace OutlineArt;
		if (Outline.Num() < 3)
		{
			return;
		}
		FVector2D Max(0.0, 0.0);
		for (const FVector2D& Pt : Outline)
		{
			Max.X = FMath::Max(Max.X, Pt.X);
			Max.Y = FMath::Max(Max.Y, Pt.Y);
		}
		const double Fit = FMath::Min(Size.X / FMath::Max(Max.X, MinExtent), Size.Y / FMath::Max(Max.Y, MinExtent));
		const FVector2D Offset = Pos + (Size - Max * Fit) * 0.5;
		thread_local TArray<FVector2D> Points; // reused: the outline is redrawn every frame
		Points.Reset(Outline.Num());
		for (const FVector2D& Pt : Outline)
		{
			Points.Add(Offset + Pt * Fit);
		}
		P.Lines(Points, Color::WithAlpha(Color::Night, KerbAlpha * Road.A), Thickness + KerbExtra, true);
		P.Lines(Points, Road, Thickness, true);
		if (MarkerPhase >= 0.0)
		{
			const double F = FMath::Fmod(MarkerPhase, 1.0) * Points.Num();
			const int32 I = FMath::FloorToInt32(F) % Points.Num();
			const FVector2D At = FMath::Lerp(Points[I], Points[(I + 1) % Points.Num()], F - FMath::FloorToDouble(F));
			P.Circle(At, Thickness * MarkerScale, Marker, Color::Night, MarkerOutline);
		}
	}
}

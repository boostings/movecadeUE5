#include "UI/KartWidgets.h"

#include "UI/KartAccessibility.h"

using namespace KartUI;

namespace
{
	/** The focus pop: 1 -> 1.05 with a little overshoot. */
	float FocusScale(const KartWidgets::FItemState& State, float Amount = 0.05f)
	{
		if (!State.bFocused)
		{
			return 1.f;
		}
		// Reduce motion: a smaller pop with no overshoot.
		const float T = static_cast<float>(State.FocusAge / 0.22);
		return FKartAccessibility::ReduceMotion() ? 1.f + Amount * 0.5f * Ease::OutCubic(T) : 1.f + Amount * Ease::OutBack(T, 3.f);
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
		P.Circle(C, R, Color::Night, Color::White, 2.f);
		const double Arm = R * 0.42, Len = R * 0.66;
		P.Box(C - FVector2D(Arm * 0.5, Len), FVector2D(Arm, Len * 2.0), Color::White, static_cast<float>(Arm * 0.25));
		P.Box(C - FVector2D(Len, Arm * 0.5), FVector2D(Len * 2.0, Arm), Color::White, static_cast<float>(Arm * 0.25));
		const double In = Arm * 0.18, Tip = Len - Arm * 0.62;
		const FVector2D Cell(Arm - In * 2.0, Arm - In * 2.0);
		if (bUp) P.Box(C + FVector2D(-Cell.X * 0.5, -Len + In), FVector2D(Cell.X, Tip), Color::SunDeep, 2.f);
		if (bDown) P.Box(C + FVector2D(-Cell.X * 0.5, Len - In - Tip), FVector2D(Cell.X, Tip), Color::SunDeep, 2.f);
		if (bLeft) P.Box(C + FVector2D(-Len + In, -Cell.Y * 0.5), FVector2D(Tip, Cell.Y), Color::SunDeep, 2.f);
		if (bRight) P.Box(C + FVector2D(Len - In - Tip, -Cell.Y * 0.5), FVector2D(Tip, Cell.Y), Color::SunDeep, 2.f);
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
		const double S = bRight ? 1.0 : -1.0;
		P.Polygon({ C + FVector2D(-0.45 * Size * S, -0.6 * Size), C + FVector2D(0.6 * Size * S, 0.0), C + FVector2D(-0.45 * Size * S, 0.6 * Size) }, Color);
	}

	void Star(const FKartPainter& P, const FVector2D& C, float Outer, float Inner, const FLinearColor& Color, float Degrees)
	{
		TArray<FVector2D, TInlineAllocator<10>> Points;
		for (int32 i = 0; i < 10; ++i)
		{
			const double A = FMath::DegreesToRadians(Degrees - 90.0 + i * 36.0);
			const double R = (i % 2 == 0) ? Outer : Inner;
			Points.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		P.Polygon(Points, Color);
	}

	float KeyCap(const FKartPainter& P, const FString& Label, const FVector2D& LeftCentre, float Height)
	{
		const FSlateFontInfo CapFont = Font(EFont::Label, Height * 0.42f);
		const float W = FMath::Max(Height, static_cast<float>(FKartPainter::Measure(Label, CapFont).X) + Height * 0.55f);
		const FVector2D Pos(LeftCentre.X, LeftCentre.Y - Height * 0.5);
		P.Box(Pos + FVector2D(0.0, Height * 0.1), FVector2D(W, Height), Color::NavyDim, Height * 0.24f);
		P.Box(Pos, FVector2D(W, Height), Color::White, Height * 0.24f, Color::Navy, 2.f);
		P.Text(Label, CapFont, Pos + FVector2D(W * 0.5, Height * 0.5), Color::Navy, FVector2D(0.5, 0.5));
		return W;
	}

	float PadGlyph(const FKartPainter& P, const FKey& Key, const FVector2D& LeftCentre, float Height)
	{
		const float R = Height * 0.5f;
		const FVector2D C(LeftCentre.X + R, LeftCentre.Y);
		auto Pill = [&](const TCHAR* Label, float Width)
		{
			P.Box(FVector2D(LeftCentre.X, LeftCentre.Y - R), FVector2D(Width, Height), Color::Night, R * 0.8f, Color::White, 2.f);
			P.Text(Label, Font(EFont::Label, Height * 0.42f), FVector2D(LeftCentre.X + Width * 0.5, LeftCentre.Y), Color::White, FVector2D(0.5, 0.5));
			return Width;
		};
		// Face buttons by position: a diamond of four dots, the pressed one big and white.
		const FKey Faces[] = { EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Left };
		for (int32 Face = 0; Face < 4; ++Face)
		{
			if (Key != Faces[Face])
			{
				continue;
			}
			P.Circle(C, R, Color::Night, Color::White, 2.f);
			const FVector2D Dirs[] = { FVector2D(0, -1), FVector2D(1, 0), FVector2D(0, 1), FVector2D(-1, 0) };
			for (int32 i = 0; i < 4; ++i)
			{
				const bool bOn = i == Face;
				P.Circle(C + Dirs[i] * R * 0.48, bOn ? R * 0.3f : R * 0.17f, bOn ? Color::White : Color::NavyDim);
			}
			return Height;
		}
		if (Key == EKeys::Gamepad_LeftShoulder) return Pill(TEXT("L"), Height * 1.5f);
		if (Key == EKeys::Gamepad_RightShoulder) return Pill(TEXT("R"), Height * 1.5f);
		if (Key == EKeys::Gamepad_LeftTrigger || Key == EKeys::Gamepad_LeftTriggerAxis) return Pill(TEXT("LT"), Height * 1.6f);
		if (Key == EKeys::Gamepad_RightTrigger || Key == EKeys::Gamepad_RightTriggerAxis) return Pill(TEXT("RT"), Height * 1.6f);
		if (Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_Special_Left)
		{
			P.Circle(C, R, Color::Night, Color::White, 2.f);
			if (Key == EKeys::Gamepad_Special_Right)
			{
				for (int32 i = -1; i <= 1; ++i) // menu: three lines
				{
					P.Box(C + FVector2D(-R * 0.42, i * R * 0.3 - R * 0.07), FVector2D(R * 0.84, R * 0.14), Color::White, R * 0.07f);
				}
			}
			else
			{
				P.Box(C + FVector2D(-R * 0.45, -R * 0.38), FVector2D(R * 0.55, R * 0.5), FLinearColor::Transparent, R * 0.08f, Color::White, 2.f);
				P.Box(C + FVector2D(-R * 0.1, -R * 0.12), FVector2D(R * 0.55, R * 0.5), Color::White, R * 0.08f);
			}
			return Height;
		}
		if (Key == EKeys::Gamepad_LeftThumbstick || Key == EKeys::Gamepad_RightThumbstick || Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_RightX)
		{
			const bool bLeft = Key == EKeys::Gamepad_LeftThumbstick || Key == EKeys::Gamepad_LeftX;
			P.Circle(C, R, Color::Night, Color::White, 2.f);
			P.Circle(C, R * 0.62f, Color::NightSoft, Color::White, 1.5f);
			P.Text(bLeft ? TEXT("L") : TEXT("R"), Font(EFont::Label, Height * 0.36f), C, Color::White, FVector2D(0.5, 0.5));
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
			return Action == EKartMenuAction::PrevTab || Action == EKartMenuAction::NextTab ? Height * 1.5f : Height;
		}
		if (IsArrowAction(Action))
		{
			return Height;
		}
		const FSlateFontInfo CapFont = Font(EFont::Label, Height * 0.42f);
		return FMath::Max(Height, static_cast<float>(FKartPainter::Measure(KeyboardLabel(Action), CapFont).X) + Height * 0.55f);
	}

	float Glyph(const FKartPainter& P, EKartMenuAction Action, EKartInputDevice Device, const FVector2D& LeftCentre, float Height)
	{
		if (Device == EKartInputDevice::Gamepad)
		{
			return PadGlyph(P, PadKey(Action), LeftCentre, Height);
		}
		if (IsArrowAction(Action))
		{
			const float W = KeyCap(P, TEXT(""), LeftCentre, Height);
			const FVector2D C(LeftCentre.X + W * 0.5, LeftCentre.Y);
			const float S = Height * 0.26f;
			switch (Action)
			{
			case EKartMenuAction::Left: Arrow(P, C, S, false, Color::Navy); break;
			case EKartMenuAction::Right: Arrow(P, C, S, true, Color::Navy); break;
			case EKartMenuAction::Up: P.Polygon({ C + FVector2D(-0.6 * S, 0.45 * S), C + FVector2D(0.0, -0.6 * S), C + FVector2D(0.6 * S, 0.45 * S) }, Color::Navy); break;
			default: P.Polygon({ C + FVector2D(-0.6 * S, -0.45 * S), C + FVector2D(0.6 * S, -0.45 * S), C + FVector2D(0.0, 0.6 * S) }, Color::Navy); break;
			}
			return W;
		}
		return KeyCap(P, KeyboardLabel(Action), LeftCentre, Height);
	}

	void Hints(const FKartPainter& P, const FKartScreenFrame& Frame, const TArray<FKartHint>& List)
	{
		if (List.Num() == 0)
		{
			return;
		}
		const float H = 40.f;
		const FSlateFontInfo LabelFont = Font(EFont::Button, 24.f, 2.f);
		// Lay out right to left from the safe area's bottom-right corner, on a soft pill.
		float Total = 0.f;
		auto Width = [&](EKartMenuAction Action)
		{
			// "Change" hints show both directions: two key caps, or a d-pad lit left and right.
			return Action == EKartMenuAction::Left && Frame.Device != EKartInputDevice::Gamepad ? GlyphWidth(Action, Frame.Device, H) * 2.f + 4.f : GlyphWidth(Action, Frame.Device, H);
		};
		for (const FKartHint& Hint : List)
		{
			// A glyph without a label (the Q of "Q E Tabs") sits right next to the following one.
			Total += Hint.Label.IsEmpty() ? Width(Hint.Action) + 8.f
				: Width(Hint.Action) + 10.f + static_cast<float>(FKartPainter::Measure(Hint.Label.ToString(), LabelFont).X) + 30.f;
		}
		const FVector2D Origin(Frame.SafeMax.X - Total, Frame.SafeMax.Y - H * 0.5);
		P.Box(Origin - FVector2D(22.0, H * 0.5 + 10.0), FVector2D(Total + 30.0, H + 20.0), Color::WithAlpha(Color::Night, 0.55f), (H + 20.f) * 0.5f);
		float X = Origin.X;
		const FKartPaintGroup Group(P);
		for (const FKartHint& Hint : List)
		{
			Group.Next();
			if (Hint.Action == EKartMenuAction::Left && Frame.Device == EKartInputDevice::Gamepad)
			{
				DPad(P, FVector2D(X + H * 0.5, Origin.Y), H * 0.5f, false, false, true, true);
				X += H + 10.f;
			}
			else if (Hint.Action == EKartMenuAction::Left)
			{
				X += Glyph(P, EKartMenuAction::Left, Frame.Device, FVector2D(X, Origin.Y), H) + 4.f;
				X += Glyph(P, EKartMenuAction::Right, Frame.Device, FVector2D(X, Origin.Y), H) + 10.f;
			}
			else if (Hint.Label.IsEmpty())
			{
				X += Glyph(P, Hint.Action, Frame.Device, FVector2D(X, Origin.Y), H) + 8.f;
				continue;
			}
			else
			{
				X += Glyph(P, Hint.Action, Frame.Device, FVector2D(X, Origin.Y), H) + 10.f;
			}
			X += static_cast<float>(P.Text(Hint.Label, LabelFont, FVector2D(X, Origin.Y), Color::White, FVector2D(0.0, 0.5)).X) + 30.f;
		}
	}

	void Header(const FKartPainter& P, const FKartScreenFrame& Frame, const FText& Title, const FText& Subtitle, const FLinearColor& Accent)
	{
		const float In = Entrance(Frame, 0.f, 0.45f);
		const FVector2D Pos(Frame.SafeMin.X - 80.0 * (1.0 - In), Frame.SafeMin.Y);
		const FKartPainter Faded = P.Faded(In);
		// A slanted accent tag, then the title in the display face with a heavy outline.
		Faded.Polygon({ Pos + FVector2D(0, 6), Pos + FVector2D(34, 6), Pos + FVector2D(22, 70), Pos + FVector2D(-12, 70) }, Accent);
		const FVector2D TitleSize = Faded.Text(Title, Font(EFont::Display, 66.f, 4.f), Pos + FVector2D(44.0, -4.0), Color::White, FVector2D::ZeroVector, 5.f);
		if (!Subtitle.IsEmpty())
		{
			Faded.Text(Subtitle, Font(EFont::Body, 24.f, 2.f), Pos + FVector2D(46.0, TitleSize.Y - 6.0), Color::WithAlpha(Color::White, 0.92f));
		}
	}

	void Steps(const FKartPainter& P, const FKartScreenFrame& Frame, const TArray<FText>& Names, int32 Current)
	{
		const FSlateFontInfo StepFont = Font(EFont::Button, 22.f);
		float X = Frame.SafeMax.X;
		const float Y = Frame.SafeMin.Y + 30.f;
		static const FString Numbers[] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9") };
		const FKartPaintGroup Group(P);
		for (int32 i = Names.Num() - 1; i >= 0; --i)
		{
			Group.Next();
			const bool bOn = i == Current;
			const bool bDone = i < Current;
			const FString& Label = Names[i].ToString();
			const float W = static_cast<float>(FKartPainter::Measure(Label, StepFont).X) + 64.f;
			X -= W;
			// Done steps get a tick as well as the colour, so the state never rests on colour alone.
			const FLinearColor Fill = bOn ? Color::Sun : bDone ? FKartAccessibility::Good() : Color::WithAlpha(Color::Night, 0.6f);
			P.Box(FVector2D(X, Y - 22.0), FVector2D(W, 44.0), Fill, 22.f, Color::White, bOn ? 3.f : 0.f);
			P.Circle(FVector2D(X + 22.0, Y), 14.f, bOn || bDone ? Color::Navy : Color::White);
			if (bDone)
			{
				P.Lines({ FVector2D(X + 15.0, Y), FVector2D(X + 20.0, Y + 5.0), FVector2D(X + 29.0, Y - 5.0) }, Color::White, 3.f);
			}
			else
			{
				P.Text(Numbers[FMath::Clamp(i, 0, 8)], Font(EFont::Label, 17.f), FVector2D(X + 22.0, Y), bOn ? Color::White : Color::Navy, FVector2D(0.5, 0.5));
			}
			P.Text(Label, StepFont, FVector2D(X + 42.0, Y), bOn || bDone ? Color::Navy : Color::White, FVector2D(0.0, 0.5));
			X -= 12.f;
		}
	}

	void Button(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, const FItemState& State,
		const FLinearColor& Accent, int32 HitId, const FText& Detail)
	{
		P.Hit(HitId, Pos, Size);
		const FVector2D Centre = Pos + Size * 0.5;
		const FKartPainter B = State.bFocused ? P.Transformed(Centre, FocusScale(State)) : P;
		const float R = static_cast<float>(Size.Y * 0.5);
		if (State.bFocused)
		{
			// Glow, the sunny fill, a white rim and a bouncing chevron.
			const float Glow = 0.35f + 0.15f * FKartAccessibility::MotionScale() * FMath::Sin(static_cast<float>(State.FocusAge) * 5.f);
			B.Box(Pos - FVector2D(8.0, 8.0), Size + FVector2D(16.0, 16.0), Color::WithAlpha(Color::Sun, Glow), R + 8.f);
			B.Box(Pos + FVector2D(0.0, 7.0), Size, Color::WithAlpha(Color::SunDeep, 1.f), R);
			B.Gradient(Pos, Size, Color::Sun, FLinearColor::LerpUsingHSV(Color::Sun, Color::SunDeep, 0.55f), true, R);
			B.Box(Pos, Size, FLinearColor::Transparent, R, Color::White, 4.f);
			const double Bob = FMath::Sin(State.FocusAge * 9.0) * 4.0 * FKartAccessibility::MotionScale();
			Arrow(B, FVector2D(Pos.X + 34.0 + Bob, Centre.Y), 13.f, true, Color::Navy);
		}
		else
		{
			B.Card(Pos, Size, State.bEnabled ? Color::Paper : Color::PaperShade, R, 6.f);
			B.Circle(FVector2D(Pos.X + 34.0, Centre.Y), 9.f, Accent);
		}
		const FLinearColor Ink = State.bEnabled ? Color::Navy : Color::NavyDim;
		const FSlateFontInfo LabelFont = Font(EFont::Button, static_cast<float>(FMath::Min(40.0, Size.Y * 0.46)));
		if (Detail.IsEmpty())
		{
			B.Text(Label, LabelFont, FVector2D(Pos.X + 64.0, Centre.Y), Ink, FVector2D(0.0, 0.5));
		}
		else
		{
			B.Text(Label, LabelFont, FVector2D(Pos.X + 64.0, Centre.Y + 2.0), Ink, FVector2D(0.0, 1.0));
			B.Text(Detail, Font(EFont::Body, 19.f), FVector2D(Pos.X + 66.0, Centre.Y + 2.0), State.bFocused ? Color::Navy : Color::NavyDim, FVector2D(0.0, 0.0));
		}
	}

	namespace
	{
		void RowBack(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, const FItemState& State, const FKartPainter*& OutPainter, FKartPainter& Storage)
		{
			const FVector2D Centre = Pos + Size * 0.5;
			Storage = State.bFocused ? P.Transformed(Centre, FocusScale(State, 0.025f)) : P;
			OutPainter = &Storage;
			const float R = static_cast<float>(Size.Y * 0.5);
			if (State.bFocused)
			{
				Storage.Box(Pos - FVector2D(6.0, 6.0), Size + FVector2D(12.0, 12.0), Color::WithAlpha(Color::Sun, 0.4f), R + 6.f);
				Storage.Card(Pos, Size, Color::White, R, 6.f, Color::Sun, 4.f);
			}
			else
			{
				Storage.Card(Pos, Size, Color::WithAlpha(Color::Paper, State.bEnabled ? 0.94f : 0.7f), R, 5.f);
			}
			Storage.Text(Label, Font(EFont::Button, 28.f), FVector2D(Pos.X + 34.0, Centre.Y), State.bEnabled ? Color::Navy : Color::NavyDim, FVector2D(0.0, 0.5));
		}
	}

	void OptionRow(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, const FText& Value,
		const FItemState& State, int32 HitId, const FLinearColor& Swatch)
	{
		P.Hit(HitId, Pos, Size);
		const FKartPainter* B = nullptr;
		FKartPainter Storage = P;
		RowBack(P, Pos, Size, Label, State, B, Storage);
		const FVector2D Centre = Pos + Size * 0.5;
		// The value sits between two arrows on the right half; the arrow nudges the way it changed.
		const double ValueW = FMath::Min(380.0, Size.X * 0.46);
		const double Right = Pos.X + Size.X - 24.0;
		const double Left = Right - ValueW;
		const double Nudge = State.ChangeAge < 0.15 ? (1.0 - State.ChangeAge / 0.15) * 7.0 : 0.0;
		const FLinearColor ArrowColor = State.bFocused ? Color::SunDeep : Color::WithAlpha(Color::NavyDim, 0.6f);
		Arrow(*B, FVector2D(Left + 14.0 - (State.ChangeDir < 0 ? Nudge : 0.0), Centre.Y), 12.f, false, ArrowColor);
		Arrow(*B, FVector2D(Right - 14.0 + (State.ChangeDir > 0 ? Nudge : 0.0), Centre.Y), 12.f, true, ArrowColor);
		P.Hit(HitId | LeftArrowFlag, FVector2D(Left - 6.0, Pos.Y), FVector2D(44.0, Size.Y));
		P.Hit(HitId | RightArrowFlag, FVector2D(Right - 38.0, Pos.Y), FVector2D(44.0, Size.Y));
		// The value pops a little when it changes.
		const float Pop = State.ChangeAge < 0.2 ? 1.f + 0.12f * (1.f - Ease::OutCubic(static_cast<float>(State.ChangeAge / 0.2))) : 1.f;
		const FVector2D ValueCentre((Left + Right) * 0.5, Centre.Y);
		const FKartPainter V = B->Transformed(ValueCentre, Pop);
		// Long values ("1 on this screen") shrink to fit between the arrows rather than run over them.
		const double Room = ValueW - 64.0 - (Swatch.A > 0.f ? 36.0 : 0.0);
		FSlateFontInfo ValueFont = Font(EFont::Button, 28.f);
		FVector2D ValueSize = FKartPainter::Measure(Value.ToString(), ValueFont);
		if (ValueSize.X > Room)
		{
			ValueFont = Font(EFont::Button, FMath::Max(18.f, FMath::FloorToFloat(28.f * static_cast<float>(Room / ValueSize.X))));
			ValueSize = FKartPainter::Measure(Value.ToString(), ValueFont);
		}
		double TextX = ValueCentre.X;
		if (Swatch.A > 0.f)
		{
			const double SwatchX = ValueCentre.X - ValueSize.X * 0.5 - 8.0;
			V.Circle(FVector2D(SwatchX - 6.0, Centre.Y), 15.f, Swatch, Color::Navy, 2.5f);
			TextX += 18.0;
		}
		V.Text(Value, ValueFont, FVector2D(TextX, Centre.Y), State.bEnabled ? Color::Navy : Color::NavyDim, FVector2D(0.5, 0.5));
	}

	void SliderRow(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, float Value,
		const FItemState& State, int32 HitId, int32 Segments)
	{
		P.Hit(HitId, Pos, Size);
		const FKartPainter* B = nullptr;
		FKartPainter Storage = P;
		RowBack(P, Pos, Size, Label, State, B, Storage);
		const double Right = Pos.X + Size.X - 136.0;
		const double Width = FMath::Min(380.0, Size.X * 0.46) - 90.0;
		const double Left = Right - Width;
		const double Gap = 6.0;
		const double SegW = (Width - Gap * (Segments - 1)) / Segments;
		const int32 Lit = FMath::RoundToInt32(FMath::Clamp(Value, 0.f, 1.f) * Segments);
		for (int32 i = 0; i < Segments; ++i)
		{
			const double H = 16.0 + 18.0 * (i + 1) / Segments;
			const FVector2D SegPos(Left + i * (SegW + Gap), Pos.Y + Size.Y * 0.5 + 17.0 - H);
			const bool bOn = i < Lit;
			const FLinearColor Fill = bOn ? (State.bFocused ? Color::SunDeep : Color::Sky) : Color::WithAlpha(Color::NavyDim, 0.25f);
			const float Pop = (bOn && i == Lit - 1 && State.ChangeAge < 0.2) ? 1.25f - 0.25f * Ease::OutCubic(static_cast<float>(State.ChangeAge / 0.2)) : 1.f;
			B->Transformed(SegPos + FVector2D(SegW * 0.5, H), Pop).Box(SegPos, FVector2D(SegW, H), Fill, 4.f);
		}
		B->Text(PercentLabel(FMath::RoundToInt32(Value * 100.f)), Font(EFont::Button, 26.f),
			FVector2D(Pos.X + Size.X - 30.0, Pos.Y + Size.Y * 0.5), Color::Navy, FVector2D(1.0, 0.5));
		P.Hit(HitId | LeftArrowFlag, FVector2D(Left - 10.0, Pos.Y), FVector2D(Width * 0.5 + 10.0, Size.Y));
		P.Hit(HitId | RightArrowFlag, FVector2D(Left + Width * 0.5, Pos.Y), FVector2D(Width * 0.5 + 10.0, Size.Y));
	}

	void PrimaryButton(const FKartPainter& P, const FVector2D& Centre, const FText& Label, EKartMenuAction Action,
		EKartInputDevice Device, const FItemState& State, int32 HitId, float Height)
	{
		const FSlateFontInfo LabelFont = Font(EFont::Display, Height * 0.5f, 3.f);
		const float GlyphH = Height * 0.52f;
		const float TextW = static_cast<float>(FKartPainter::Measure(Label.ToString(), LabelFont).X);
		const float W = TextW + GlyphWidth(Action, Device, GlyphH) + 90.f;
		const FVector2D Pos = Centre - FVector2D(W, Height) * 0.5;
		P.Hit(HitId, Pos, FVector2D(W, Height));
		const float Breath = State.bFocused ? 1.f + 0.03f * FKartAccessibility::MotionScale() * FMath::Sin(static_cast<float>(State.FocusAge) * 4.f) : 1.f;
		const FKartPainter B = P.Transformed(Centre, FocusScale(State, 0.06f) * Breath);
		const float R = Height * 0.5f;
		B.Box(Pos + FVector2D(0.0, 8.0), FVector2D(W, Height), Color::Shadow, R);
		B.Gradient(Pos, FVector2D(W, Height), Color::Hot, FLinearColor::LerpUsingHSV(Color::Hot, Color::Grape, 0.35f), true, R);
		B.Box(Pos, FVector2D(W, Height), FLinearColor::Transparent, R, Color::White, State.bFocused ? 5.f : 3.f);
		float X = static_cast<float>(Pos.X) + 34.f;
		X += Glyph(B, Action, Device, FVector2D(X, Centre.Y), GlyphH) + 22.f;
		B.Text(Label, LabelFont, FVector2D(X, Centre.Y), Color::White, FVector2D(0.0, 0.5), 3.f);
	}

	void TrackOutline(const FKartPainter& P, const TArray<FVector2D>& Outline, const FVector2D& Pos, const FVector2D& Size,
		const FLinearColor& Road, float Thickness, double MarkerPhase, const FLinearColor& Marker)
	{
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
		const double Fit = FMath::Min(Size.X / FMath::Max(Max.X, 1e-3), Size.Y / FMath::Max(Max.Y, 1e-3));
		const FVector2D Offset = Pos + (Size - Max * Fit) * 0.5;
		thread_local TArray<FVector2D> Points; // reused: the outline is redrawn every frame
		Points.Reset(Outline.Num());
		for (const FVector2D& Pt : Outline)
		{
			Points.Add(Offset + Pt * Fit);
		}
		P.Lines(Points, Color::WithAlpha(Color::Night, 0.55f * Road.A), Thickness + 6.f, true);
		P.Lines(Points, Road, Thickness, true);
		if (MarkerPhase >= 0.0)
		{
			const double F = FMath::Fmod(MarkerPhase, 1.0) * Points.Num();
			const int32 I = FMath::FloorToInt32(F) % Points.Num();
			const FVector2D At = FMath::Lerp(Points[I], Points[(I + 1) % Points.Num()], F - FMath::FloorToDouble(F));
			P.Circle(At, Thickness * 1.3f, Marker, Color::Night, 2.f);
		}
	}
}

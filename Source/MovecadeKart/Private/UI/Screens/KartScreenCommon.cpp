#include "UI/Screens/KartScreenCommon.h"

#include "Framework/Application/SlateApplication.h"
#include "UI/KartAccessibility.h"
#include "UI/KartSessionSettings.h"

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;

namespace
{
	/** The left panel: how far its slanted edge leans, the accent stripe's width, and its slide-in. */
	constexpr double PanelLean = 120.0;
	constexpr double PanelStripe = 12.0;
	constexpr float PanelInSeconds = 0.4f;

	/** The logo's letters: their size at Scale 1, outlines, how tightly they pack, and the bob and tilt. */
	constexpr float LetterSize = 150.f;
	constexpr float LetterOutline = 9.f;
	constexpr float LetterRim = 15.f;
	/** The dark rim copy sits this far below each letter. */
	constexpr double LetterRimDrop = 9.0;
	constexpr double LetterOverlap = 14.0;
	constexpr double LetterFirstDrop = 0.15;
	constexpr double LetterStagger = 0.06;
	constexpr double LetterDropSeconds = 0.5;
	constexpr double LetterDropHeight = 120.0;
	constexpr double LetterBob = 7.0;
	constexpr float LetterTilt = 3.f;

	/** The ribbon under the letters: when it pops in, where and how big it is. */
	constexpr double RibbonAt = 0.75;
	constexpr double RibbonPopSeconds = 0.45;
	constexpr double RibbonDrop = 112.0;
	constexpr double RibbonWidth = 620.0;
	constexpr double RibbonHeight = 84.0;
	constexpr float RibbonTilt = -3.f;
}

namespace KartScreens::Shared
{
	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	UKartSessionSettings* SessionOf(const IKartScreenHost* Host)
	{
		return Host ? UKartSessionSettings::Get(Host->GetWorld()) : nullptr;
	}

	// FListFocus --------------------------------------------------------------------------------------

	KartWidgets::FItemState FListFocus::State(int32 Row, double Now, bool bEnabled) const
	{
		KartWidgets::FItemState S;
		S.bFocused = Row == Cursor.Index;
		S.bEnabled = bEnabled;
		S.FocusAge = Now - FocusAt;
		if (Row == ChangedRow)
		{
			S.ChangeAge = Now - ChangeAt;
			S.ChangeDir = ChangeDir;
		}
		return S;
	}

	bool FListFocus::Move(int32 Delta, double Now)
	{
		if (!Cursor.Move(Delta))
		{
			return false;
		}
		FocusAt = Now;
		return true;
	}

	bool FListFocus::SetFocus(int32 Row, double Now)
	{
		if (!Cursor.Set(Row))
		{
			return false;
		}
		FocusAt = Now;
		return true;
	}

	void FListFocus::Changed(int32 Row, int32 Dir, double Now)
	{
		ChangedRow = Row;
		ChangeDir = Dir;
		ChangeAt = Now;
	}

	void FListFocus::Step(int32 Delta, const IKartScreenHost& Host)
	{
		if (Move(Delta, NowSeconds()))
		{
			Host.PlaySound(EKartUISound::Move);
		}
	}

	void FListFocus::HoverTo(int32 Row, const IKartScreenHost& Host)
	{
		if (SetFocus(Row, NowSeconds()))
		{
			Host.PlaySound(EKartUISound::Move);
		}
	}

	// Front-end art ----------------------------------------------------------------------------------

	void LeftPanel(const FKartPainter& P, const FKartScreenFrame& Frame, float Width, float Alpha)
	{
		const float In = KartWidgets::Entrance(Frame, 0.f, PanelInSeconds);
		const double W = Width * In;
		P.Polygon({ FVector2D(0, 0), FVector2D(W + PanelLean, 0), FVector2D(W, Frame.Size.Y), FVector2D(0, Frame.Size.Y) }, Color::WithAlpha(Color::Night, Alpha));
		P.Polygon({ FVector2D(W + PanelLean, 0), FVector2D(W + (PanelLean + PanelStripe), 0), FVector2D(W + PanelStripe, Frame.Size.Y), FVector2D(W, Frame.Size.Y) },
			Color::WithAlpha(Color::Sun, 0.9f));
	}

	void Logo(const FKartPainter& P, const FVector2D& Centre, float Scale, double Age, double Now)
	{
		static const FLinearColor Letters[] = { Color::Hot, Color::Sun, Color::Sky, Color::Mint, Color::Grape, Color::SunDeep, Color::Hot, Color::Sky };
		// One string per letter, built once: the logo is painted every frame.
		static const TArray<FString> Word = []()
		{
			TArray<FString> Out;
			for (const TCHAR Ch : FString(TEXT("MOVECADE")))
			{
				Out.Add(FString::Chr(Ch));
			}
			return Out;
		}();
		const FSlateFontInfo LetterFont = Font(EFont::Display, LetterSize * Scale, LetterOutline * Scale, Color::White);
		const FSlateFontInfo RimFont = Font(EFont::Display, LetterSize * Scale, LetterRim * Scale, Color::Night);
		TArray<double, TInlineAllocator<8>> Widths;
		double Total = 0.0;
		for (const FString& Ch : Word)
		{
			Widths.Add(FKartPainter::Measure(Ch, LetterFont).X - LetterOverlap * Scale);
			Total += Widths.Last();
		}
		double X = Centre.X - Total * 0.5;
		const float Motion = FKartAccessibility::MotionScale();
		for (int32 i = 0; i < Word.Num(); ++i)
		{
			// Each letter drops in on its own beat, then bobs on a wave.
			const float In = Ease::OutBack(static_cast<float>((Age - LetterFirstDrop - i * LetterStagger) / LetterDropSeconds), 2.2f);
			if (In <= 0.f)
			{
				X += Widths[i];
				continue;
			}
			const double Bob = FMath::Sin(Now * 2.6 - i * 0.55) * LetterBob * Scale * Motion;
			const float Tilt = FMath::Sin(static_cast<float>(Now) * 1.8f - i * 0.7f) * LetterTilt * Motion + (i % 2 == 0 ? -LetterTilt : LetterTilt);
			const FVector2D C(X + Widths[i] * 0.5, Centre.Y + Bob - (1.f - FMath::Min(In, 1.f)) * LetterDropHeight * Scale);
			const FKartPainter L = P.Transformed(C, FMath::Max(In, 0.01f), Tilt).Faded(FMath::Min(1.f, In * 2.f));
			const FString& Ch = Word[i];
			L.Text(Ch, RimFont, C + FVector2D(0.0, LetterRimDrop * Scale), Color::Night, FVector2D(0.5, 0.5));
			L.Text(Ch, LetterFont, C, Letters[i % UE_ARRAY_COUNT(Letters)], FVector2D(0.5, 0.5));
			X += Widths[i];
		}

		// The ribbon: a slanted pink banner with a checker strip at each end.
		const float RibbonIn = Ease::OutBack(static_cast<float>((Age - RibbonAt) / RibbonPopSeconds), 1.6f);
		if (RibbonIn <= 0.f)
		{
			return;
		}
		const FVector2D RC(Centre.X, Centre.Y + RibbonDrop * Scale);
		const FKartPainter R = P.Transformed(RC, RibbonIn, RibbonTilt);
		const double W = RibbonWidth * Scale, H = RibbonHeight * Scale;
		R.Polygon({ RC + FVector2D(-W * 0.5 - 20 * Scale, -H * 0.5 + 8 * Scale), RC + FVector2D(W * 0.5 + 20 * Scale, -H * 0.5 + 8 * Scale),
			RC + FVector2D(W * 0.5, H * 0.5 + 8 * Scale), RC + FVector2D(-W * 0.5 - 40 * Scale, H * 0.5 + 8 * Scale) }, Color::Night);
		R.Polygon({ RC + FVector2D(-W * 0.5, -H * 0.5), RC + FVector2D(W * 0.5 + 30 * Scale, -H * 0.5),
			RC + FVector2D(W * 0.5 + 10 * Scale, H * 0.5), RC + FVector2D(-W * 0.5 - 20 * Scale, H * 0.5) }, Color::Hot);
		const double Cell = H / 3.0;
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Col = 0; Col < 3; ++Col)
				{
					if ((Row + Col) % 2 == 0)
					{
						const FVector2D CellPos(RC.X + Side * (W * 0.5 - 30.0 * Scale) - Cell * 1.5 + Col * Cell + Side * 4.0, RC.Y - H * 0.5 + Row * Cell);
						R.Box(CellPos, FVector2D(Cell, Cell), Color::White);
					}
				}
			}
		}
		R.Text(TEXT("KART RACER"), Font(EFont::Display, 64.f * Scale, 4.f * Scale), RC + FVector2D(0.0, -2.0), Color::White, FVector2D(0.5, 0.5), 4.f * Scale);
	}

	const TArray<FText>& RaceSetupSteps()
	{
		static const TArray<FText> Steps = { LOCTEXT("StepRacers", "Racers"), LOCTEXT("StepRules", "Rules"), LOCTEXT("StepTrack", "Track") };
		return Steps;
	}
}

#undef LOCTEXT_NAMESPACE

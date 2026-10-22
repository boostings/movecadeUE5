#include "UI/Screens/KartScreens.h"

#include "Engine/World.h"
#include "UI/KartAccessibility.h"
#include "UI/KartFrontEnd.h"
#include "UI/KartRoster.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartWidgets.h"

#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeSession.h"
#include "Store/KartCosmetics.h" // store cosmetics

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;
using KartWidgets::FItemState;

namespace
{
	/** Shared by screens with a vertical list: focus moves and the pop timing. */
	struct FFocus
	{
		FKartMenuCursor Cursor;
		double FocusAt = 0.0;
		double ChangeAt = -10.0;
		int32 ChangeDir = 0;
		int32 ChangedRow = INDEX_NONE;

		FItemState State(int32 Row, double Now, bool bEnabled = true) const
		{
			FItemState S;
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

		bool Move(int32 Delta, double Now)
		{
			if (Cursor.Move(Delta))
			{
				FocusAt = Now;
				return true;
			}
			return false;
		}

		bool SetFocus(int32 Row, double Now)
		{
			if (Cursor.Set(Row))
			{
				FocusAt = Now;
				return true;
			}
			return false;
		}

		void Changed(int32 Row, int32 Dir, double Now)
		{
			ChangedRow = Row;
			ChangeDir = Dir;
			ChangeAt = Now;
		}
	};

	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	UKartSessionSettings* Session(const IKartScreenHost* Host)
	{
		return Host ? UKartSessionSettings::Get(Host->GetWorld()) : nullptr;
	}

	/** A dark, slanted panel down the left side so menus read over any backdrop. */
	void LeftPanel(const FKartPainter& P, const FKartScreenFrame& Frame, float Width, float Alpha = 0.8f)
	{
		const float In = KartWidgets::Entrance(Frame, 0.f, 0.4f);
		const double W = Width * In;
		P.Polygon({ FVector2D(0, 0), FVector2D(W + 120.0, 0), FVector2D(W, Frame.Size.Y), FVector2D(0, Frame.Size.Y) }, Color::WithAlpha(Color::Night, Alpha));
		P.Polygon({ FVector2D(W + 120.0, 0), FVector2D(W + 132.0, 0), FVector2D(W + 12.0, Frame.Size.Y), FVector2D(W, Frame.Size.Y) }, Color::WithAlpha(Color::Sun, 0.9f));
	}

	/** The "MOVECADE / KART RACER" logo, built from type: bouncing multi-colour letters on a ribbon. */
	void Logo(const FKartPainter& P, const FVector2D& Centre, float Scale, double Age, double Now)
	{
		static const FLinearColor Letters[] = { Color::Hot, Color::Sun, Color::Sky, Color::Mint, Color::Grape, Color::SunDeep, Color::Hot, Color::Sky };
		const FString Word = TEXT("MOVECADE");
		const FSlateFontInfo LetterFont = Font(EFont::Display, 150.f * Scale, 9.f * Scale, Color::White);
		const FSlateFontInfo RimFont = Font(EFont::Display, 150.f * Scale, 15.f * Scale, Color::Night);
		TArray<double> Widths;
		double Total = 0.0;
		for (TCHAR Ch : Word)
		{
			Widths.Add(FKartPainter::Measure(FString::Chr(Ch), LetterFont).X - 14.0 * Scale);
			Total += Widths.Last();
		}
		double X = Centre.X - Total * 0.5;
		for (int32 i = 0; i < Word.Len(); ++i)
		{
			// Each letter drops in on its own beat, then bobs on a wave.
			const float In = Ease::OutBack(static_cast<float>((Age - 0.15 - i * 0.06) / 0.5), 2.2f);
			if (In <= 0.f)
			{
				X += Widths[i];
				continue;
			}
			const float Motion = FKartAccessibility::MotionScale();
			const double Bob = FMath::Sin(Now * 2.6 - i * 0.55) * 7.0 * Scale * Motion;
			const float Tilt = FMath::Sin(static_cast<float>(Now) * 1.8f - i * 0.7f) * 3.f * Motion + (i % 2 == 0 ? -3.f : 3.f);
			const FVector2D C(X + Widths[i] * 0.5, Centre.Y + Bob - (1.f - FMath::Min(In, 1.f)) * 120.0 * Scale);
			const FKartPainter L = P.Transformed(C, FMath::Max(In, 0.01f), Tilt).Faded(FMath::Min(1.f, In * 2.f));
			const FString Ch = FString::Chr(Word[i]);
			L.Text(Ch, RimFont, C + FVector2D(0.0, 9.0 * Scale), Color::Night, FVector2D(0.5, 0.5));
			L.Text(Ch, LetterFont, C, Letters[i % UE_ARRAY_COUNT(Letters)], FVector2D(0.5, 0.5));
			X += Widths[i];
		}
		// The ribbon: a slanted pink banner with a checker strip at each end.
		const float RibbonIn = Ease::OutBack(static_cast<float>((Age - 0.75) / 0.45), 1.6f);
		if (RibbonIn > 0.f)
		{
			const FVector2D RC(Centre.X, Centre.Y + 112.0 * Scale);
			const FKartPainter R = P.Transformed(RC, RibbonIn, -3.f);
			const double W = 620.0 * Scale, H = 84.0 * Scale;
			R.Polygon({ RC + FVector2D(-W * 0.5 - 20 * Scale, -H * 0.5 + 8 * Scale), RC + FVector2D(W * 0.5 + 20 * Scale, -H * 0.5 + 8 * Scale),
				RC + FVector2D(W * 0.5, H * 0.5 + 8 * Scale), RC + FVector2D(-W * 0.5 - 40 * Scale, H * 0.5 + 8 * Scale) }, Color::Night);
			R.Polygon({ RC + FVector2D(-W * 0.5, -H * 0.5), RC + FVector2D(W * 0.5 + 30 * Scale, -H * 0.5),
				RC + FVector2D(W * 0.5 + 10 * Scale, H * 0.5), RC + FVector2D(-W * 0.5 - 20 * Scale, H * 0.5) }, Color::Hot);
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				for (int32 Row = 0; Row < 3; ++Row)
				{
					for (int32 Col = 0; Col < 3; ++Col)
					{
						if ((Row + Col) % 2 == 0)
						{
							const double S = H / 3.0;
							const FVector2D Cell(RC.X + Side * (W * 0.5 - 30.0 * Scale) - S * 1.5 + Col * S + Side * 4.0, RC.Y - H * 0.5 + Row * S);
							R.Box(Cell, FVector2D(S, S), Color::White);
						}
					}
				}
			}
			R.Text(TEXT("KART RACER"), Font(EFont::Display, 64.f * Scale, 4.f * Scale), RC + FVector2D(0.0, -2.0), Color::White, FVector2D(0.5, 0.5), 4.f * Scale);
		}
	}

	// ------------------------------------------------------------------------------------------------- Title

	class FTitleScreen final : public FKartScreen
	{
	public:
		explicit FTitleScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd) {}

		virtual FName GetId() const override { return TEXT("title"); }
		virtual bool CanGoBack() const override { return false; }
		virtual TArray<FKartHint> GetHints() const override { return {}; }

		virtual void OnShown() override
		{
			PressedAt = -1.0;
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Flyover);
				FE->SetShowroom({});
			}
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (Event.Action == EKartMenuAction::Accept || Event.Action == EKartMenuAction::Start)
			{
				Press();
				return true;
			}
			if (Event.Action == EKartMenuAction::Back)
			{
				Host->PlaySound(EKartUISound::Back);
				Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("QuitTitle", "Quit the game?"), LOCTEXT("QuitBody", "See you on the track soon."),
					LOCTEXT("QuitYes", "Quit"), LOCTEXT("QuitNo", "Stay"), [WeakFE = FrontEnd]() { if (WeakFE.IsValid()) { WeakFE->QuitGame(); } }));
				return true;
			}
			return false;
		}

		virtual void OnClick(int32 HitId) override { Press(); }

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const double Now = Frame.Now;
			// Soft vignettes keep the type readable over the flyover.
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, Frame.Size.Y * 0.55), Color::WithAlpha(Color::Night, 0.55f), Color::WithAlpha(Color::Night, 0.f));
			P.Gradient(FVector2D(0.0, Frame.Size.Y * 0.62), FVector2D(Frame.Size.X, Frame.Size.Y * 0.38), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.75f));
			P.Hit(0, FVector2D::ZeroVector, Frame.Size);

			Logo(P, FVector2D(Frame.Size.X * 0.5, Frame.Size.Y * 0.3), 1.f, Frame.Age, Now);

			// "Press Start": a pulsing pill with the right glyph; it flashes white when pressed.
			const float In = KartWidgets::Entrance(Frame, 1.1f, 0.5f);
			if (In > 0.f)
			{
				const FVector2D C(Frame.Size.X * 0.5, Frame.Size.Y * 0.74);
				const bool bPressed = PressedAt >= 0.0;
				const float Motion = FKartAccessibility::MotionScale();
				const float Pulse = bPressed ? 1.f + 0.15f * Motion * Ease::Pulse(static_cast<float>((Now - PressedAt) / 0.3)) : 1.f + 0.035f * Motion * FMath::Sin(static_cast<float>(Now) * 3.4f);
				const FKartPainter B = P.Transformed(C, Pulse * In).Faded(In);
				const FText Label = Frame.Device == EKartInputDevice::Gamepad ? LOCTEXT("PressStart", "PRESS START") : LOCTEXT("PressEnter", "PRESS ENTER");
				const FSlateFontInfo LabelFont = Font(EFont::Display, 52.f, 4.f);
				const float GlyphH = 50.f;
				const EKartMenuAction Action = Frame.Device == EKartInputDevice::Gamepad ? EKartMenuAction::Start : EKartMenuAction::Accept;
				const float W = static_cast<float>(FKartPainter::Measure(Label.ToString(), LabelFont).X) + KartWidgets::GlyphWidth(Action, Frame.Device, GlyphH) + 100.f;
				const FVector2D Pos = C - FVector2D(W * 0.5, 46.0);
				B.Box(Pos + FVector2D(0.0, 8.0), FVector2D(W, 92.0), Color::Shadow, 46.f);
				B.Box(Pos, FVector2D(W, 92.0), bPressed ? Color::White : Color::WithAlpha(Color::Night, 0.82f), 46.f, Color::Sun, 5.f);
				float X = static_cast<float>(Pos.X) + 36.f;
				X += KartWidgets::Glyph(B, Action, Frame.Device, FVector2D(X, C.Y), GlyphH) + 24.f;
				B.Text(Label, LabelFont, FVector2D(X, C.Y), bPressed ? Color::Navy : Color::Sun, FVector2D(0.0, 0.5), bPressed ? 0.f : 4.f);
			}
			const FKartPainter Foot = P.Faded(KartWidgets::Entrance(Frame, 1.4f, 0.6f) * 0.8f);
			Foot.Text(LOCTEXT("Footer", "A Movecade game  ·  Unreal Engine 5"), Font(EFont::Body, 20.f, 1.f), FVector2D(Frame.SafeMin.X, Frame.SafeMax.Y), Color::White, FVector2D(0.0, 1.0));
			Foot.Text(LOCTEXT("PhonesHint", "Phones can be controllers: try Party Mode"), Font(EFont::Body, 20.f, 1.f), FVector2D(Frame.SafeMax.X, Frame.SafeMax.Y), Color::White, FVector2D(1.0, 1.0));

			if (PressedAt >= 0.0 && Now - PressedAt > 0.32 && !bAdvanced)
			{
				bAdvanced = true;
				Host->PushScreen(KartScreens::ShouldShowWelcome() ? KartScreens::MakeWelcome(FrontEnd.Get()) : KartScreens::MakeMainMenu(FrontEnd.Get()));
			}
		}

	private:
		void Press()
		{
			if (PressedAt < 0.0)
			{
				PressedAt = NowSeconds();
				bAdvanced = false;
				Host->PlaySound(EKartUISound::Start);
			}
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		double PressedAt = -1.0;
		bool bAdvanced = false;
	};

	// --------------------------------------------------------------------------------------------- Main menu

	/** A big flat-shaded icon for each main menu entry, drawn from primitives. */
	void MenuIcon(const FKartPainter& P, int32 Item, const FVector2D& C, float S, double Now)
	{
		switch (Item)
		{
		case 0: // Race: a waving chequered flag on a pole.
		{
			P.Box(C + FVector2D(-0.62 * S, -0.7 * S), FVector2D(0.08 * S, 1.5 * S), Color::Navy, 0.04f * S);
			const int32 Cols = 6, Rows = 4;
			const double Cell = 0.2 * S;
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				for (int32 Col = 0; Col < Cols; ++Col)
				{
					const double Wave = FMath::Sin(Now * 4.0 - Col * 0.8) * 0.06 * S * (Col + 1) / Cols;
					const FVector2D Pos(C.X - 0.54 * S + Col * Cell, C.Y - 0.66 * S + Row * Cell + Wave);
					P.Box(Pos, FVector2D(Cell + 0.5, Cell + 0.5), (Row + Col) % 2 == 0 ? Color::Navy : Color::White);
				}
			}
			break;
		}
		case 1: // Party: two phones with a signal arc.
			for (int32 i = 0; i < 2; ++i)
			{
				const FVector2D Pos = C + FVector2D(-0.62 * S + i * 0.66 * S, -0.5 * S + i * 0.18 * S);
				P.Box(Pos, FVector2D(0.56 * S, 0.98 * S), Color::Navy, 0.1f * S);
				P.Box(Pos + FVector2D(0.06 * S, 0.1 * S), FVector2D(0.44 * S, 0.7 * S), i == 0 ? Color::Sky : Color::Hot, 0.05f * S);
				P.Circle(Pos + FVector2D(0.28 * S, 0.88 * S), 0.04f * S, Color::White);
			}
			for (int32 Ring = 0; Ring < 3; ++Ring)
			{
				const float A = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 4.f - Ring);
				P.Box(C + FVector2D(-0.1 * S - Ring * 0.12 * S, -0.92 * S - Ring * 0.12 * S), FVector2D(0.2 * S + Ring * 0.24 * S, 0.2 * S + Ring * 0.24 * S),
					FLinearColor::Transparent, 0.2f * S, Color::WithAlpha(Color::Navy, A), 0.045f * S);
			}
			break;
		case 2: // Settings: a turning gear.
		{
			TArray<FVector2D> Gear;
			const int32 Teeth = 8;
			const double Spin = Now * 0.6;
			for (int32 i = 0; i < Teeth * 4; ++i)
			{
				const double A = Spin + i * UE_DOUBLE_PI * 2.0 / (Teeth * 4);
				const double R = (i % 4 < 2) ? 0.8 * S : 0.6 * S;
				Gear.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
			}
			P.Polygon(Gear, Color::Navy);
			P.Circle(C, 0.3f * S, Color::Paper);
			break;
		}
		case 3: // Credits: a spinning star.
			KartWidgets::Star(P, C, 0.85f * S, 0.38f * S, Color::Navy, static_cast<float>(FMath::Sin(Now * 1.5) * 12.0));
			KartWidgets::Star(P, C, 0.5f * S, 0.22f * S, Color::Sun, static_cast<float>(FMath::Sin(Now * 1.5) * 12.0));
			break;
		case 5: // Arcade: a ball rolling into three pins.
		{
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2D Pin = C + FVector2D(0.18 * S + i * 0.26 * S, -0.1 * S - (i == 1 ? 0.12 * S : 0.0));
				P.Circle(Pin + FVector2D(0.0, -0.42 * S), 0.09f * S, Color::White, Color::Navy, 0.03f * S);
				P.Box(Pin + FVector2D(-0.1 * S, -0.36 * S), FVector2D(0.2 * S, 0.66 * S), Color::White, 0.09f * S, Color::Navy, 0.03f * S);
				P.Box(Pin + FVector2D(-0.1 * S, -0.26 * S), FVector2D(0.2 * S, 0.06 * S), Color::Hot);
			}
			const double Roll = FMath::Fmod(Now * 0.8, 1.0);
			const FVector2D Ball = C + FVector2D(-0.9 * S + Roll * 0.5 * S, 0.28 * S);
			P.Circle(Ball, 0.34f * S, Color::Navy);
			for (int32 Hole = 0; Hole < 3; ++Hole)
			{
				const double A = Now * 4.0 + Hole * 0.6;
				P.Circle(Ball + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 0.16 * S, 0.045f * S, Color::Sun);
			}
			P.Box(C + FVector2D(-0.95 * S, 0.64 * S), FVector2D(1.9 * S, 0.06 * S), Color::Navy, 0.03f * S);
			break;
		}
		case 6: // Store: a shopping bag (KartStoreScreen.cpp).
			KartScreens::PaintStoreIcon(P, C, S, Now);
			break;
		default: // Quit: a door with an arrow.
			P.Box(C + FVector2D(-0.5 * S, -0.75 * S), FVector2D(0.7 * S, 1.5 * S), Color::Navy, 0.08f * S);
			P.Box(C + FVector2D(-0.42 * S, -0.66 * S), FVector2D(0.54 * S, 1.32 * S), Color::PaperShade, 0.05f * S);
			KartWidgets::Arrow(P, C + FVector2D(0.42 * S + FMath::Sin(Now * 5.0) * 0.06 * S, 0.0), 0.32f * S, true, Color::Hot);
			P.Box(C + FVector2D(-0.05 * S, -0.08 * S), FVector2D(0.35 * S, 0.16 * S), Color::Hot);
			break;
		}
	}

	class FMainMenuScreen final : public FKartScreen
	{
	public:
		explicit FMainMenuScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd)
		{
			Focus.Cursor.Count = 7; // store: +1 entry
		}

		virtual FName GetId() const override { return TEXT("menu"); }

		virtual void OnShown() override
		{
			Focus.FocusAt = NowSeconds();
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				if (Focus.Move(Event.Action == EKartMenuAction::Up ? -1 : 1, NowSeconds()))
				{
					Host->PlaySound(EKartUISound::Move);
				}
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Activate(Focus.Cursor.Index);
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (Focus.SetFocus(HitId, NowSeconds()))
			{
				Host->PlaySound(EKartUISound::Move);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			Focus.SetFocus(HitId, NowSeconds());
			Activate(HitId);
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			LeftPanel(P, Frame, 720.f);
			const FText Names[] = { LOCTEXT("Race", "Race"), LOCTEXT("Arcade", "Arcade"), LOCTEXT("Party", "Party Mode"), LOCTEXT("Settings", "Settings"), LOCTEXT("Credits", "Credits"), LOCTEXT("Store", "Store"), LOCTEXT("Quit", "Quit") };
			const FText Details[] = {
				LOCTEXT("RaceDetail", "Up to four players on one screen, with bots."),
				LOCTEXT("ArcadeDetail", "Bowling, darts, Samurai Slice and Sky Stacker, with friends or the house."),
				LOCTEXT("PartyDetail", "Phones are the controllers. Scan, tilt, race."),
				LOCTEXT("SettingsDetail", "Graphics, sound and controls."),
				LOCTEXT("CreditsDetail", "Who made this, and with what."),
				LOCTEXT("StoreDetail", "Kart paints, driver looks and party-game skins. Cosmetic only, plus free unlocks."),
				LOCTEXT("QuitDetail", "Back to the desktop."),
			};
			const FLinearColor Accents[] = { Color::Hot, Color::Sun, Color::Sky, Color::Grape, Color::Mint, Color::Gold, Color::NavyDim };
			// The icon each entry shows on the card (MenuIcon's cases).
			const int32 IconOf[] = { 0, 5, 1, 2, 3, 6, 4 };

			// The small logo at the top of the panel.
			Logo(P.Transformed(FVector2D(Frame.SafeMin.X + 300.0, Frame.SafeMin.Y + 70.0), 1.f), FVector2D(Frame.SafeMin.X + 300.0, Frame.SafeMin.Y + 70.0), 0.42f, 10.0, Frame.Now);

			const FVector2D ButtonSize(560.0, 88.0);
			const double Gap = 18.0;
			const double Top = Frame.Size.Y * 0.5 - (7 * ButtonSize.Y + 6 * Gap) * 0.5 + 50.0;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < 7; ++i)
			{
				Group.Next();
				const float In = KartWidgets::Entrance(Frame, 0.05f + i * 0.05f, 0.4f);
				const FVector2D Pos(Frame.SafeMin.X - (1.0 - In) * 260.0 + KartWidgets::SlideX(Frame), Top + i * (ButtonSize.Y + Gap));
				KartWidgets::Button(P.Faded(In), Pos, ButtonSize, Names[i], Focus.State(i, Frame.Now), Accents[i], i);
			}

			// The focused entry's card on the right: a big icon over its accent colour.
			const int32 Item = Focus.Cursor.Index;
			const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - Focus.FocusAt) / 0.35), 2.f);
			const FVector2D CardSize(520.0, 480.0);
			const FVector2D CardPos(Frame.SafeMax.X - CardSize.X, Frame.Size.Y * 0.5 - CardSize.Y * 0.5 + 30.0);
			const FKartPainter Card = P.Transformed(CardPos + CardSize * 0.5, 0.9f + 0.1f * Pop, (1.f - Pop) * -4.f).Faded(KartWidgets::Entrance(Frame, 0.2f));
			Card.Card(CardPos, CardSize, Color::Paper, Metric::RadiusLarge, 10.f, Color::White, 5.f);
			Card.Box(CardPos + FVector2D(18.0, 18.0), FVector2D(CardSize.X - 36.0, 270.0), Accents[Item], Metric::Radius);
			for (int32 Stripe = 0; Stripe < 6; ++Stripe)
			{
				const double X = CardPos.X + 18.0 + FMath::Fmod(Frame.Now * 40.0 + Stripe * 90.0, CardSize.X + 90.0) - 90.0;
				const double X0 = FMath::Clamp(X, CardPos.X + 18.0, CardPos.X + CardSize.X - 18.0);
				const double X1 = FMath::Clamp(X + 40.0, CardPos.X + 18.0, CardPos.X + CardSize.X - 18.0);
				if (X1 > X0 + 1.0)
				{
					Card.Box(FVector2D(X0, CardPos.Y + 18.0), FVector2D(X1 - X0, 270.0), Color::WithAlpha(Color::White, 0.12f));
				}
			}
			MenuIcon(Card, IconOf[Item], CardPos + FVector2D(CardSize.X * 0.5, 153.0), 110.f, Frame.Now);
			Card.Text(Names[Item], Font(EFont::Title, 46.f), CardPos + FVector2D(36.0, 306.0), Color::Navy);
			Card.TextWrapped(Details[Item], Font(EFont::Body, 22.f), CardPos + FVector2D(38.0, 380.0), CardSize.X - 76.0, Color::NavyDim);
		}

	private:
		void Activate(int32 Item)
		{
			AKartFrontEnd* FE = FrontEnd.Get();
			switch (Item)
			{
			case 0:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeRacers(FE));
				break;
			case 1:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeArcade(FE));
				break;
			case 2:
				Host->PlaySound(EKartUISound::Go);
				if (FE)
				{
					FE->StartParty();
				}
				break;
			case 3:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeSettings(0));
				break;
			case 4:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeCredits());
				break;
			case 5: // store
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeStore(FE));
				break;
			default:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("QuitTitle", "Quit the game?"), LOCTEXT("QuitBody", "See you on the track soon."),
					LOCTEXT("QuitYes", "Quit"), LOCTEXT("QuitNo", "Stay"), [WeakFE = FrontEnd]() { if (WeakFE.IsValid()) { WeakFE->QuitGame(); } }));
				break;
			}
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FFocus Focus;
	};

	// ---------------------------------------------------------------------------------------------- Arcade

	/** The party games (Docs/Port/05-games-framework.md); Map is null until a game is ported. */
	struct FArcadeGame
	{
		FName Id;
		FText Name;
		FText Tagline;
		const TCHAR* Map;
		FLinearColor Accent;
		int32 MaxPlayers;
		/** Players on this screen: turn-based games share one set of controls; simultaneous ones need a pair of hands each. */
		int32 MaxLocal;
	};

	const TArray<FArcadeGame>& ArcadeGames()
	{
		// The shared catalog (Games/ArcadeCatalog) is the one list; the picker keeps its own view of it.
		static const TArray<FArcadeGame> Games = []()
		{
			TArray<FArcadeGame> Out;
			for (const MovecadeKart::Arcade::FGameInfo& G : MovecadeKart::Arcade::Games())
			{
				Out.Add({ FName(*G.Id), G.Name, G.Tagline, *G.Map, G.Accent, G.MaxPlayers, G.MaxLocal });
			}
			return Out;
		}();
		return Games;
	}

	/** The picker's choices, kept for the session so coming back lands where you left. */
	struct FArcadePick
	{
		int32 Game = 0;
		int32 Players = 1;
		int32 House = 1;
	};

	FArcadePick& ArcadePick()
	{
		static FArcadePick Pick;
		return Pick;
	}

	/** Each game's card art, drawn from primitives like the main menu's. */
	void GameIcon(const FKartPainter& P, int32 Game, const FVector2D& C, float S, double Now)
	{
		if (ArcadeGames().IsValidIndex(Game) && ArcadeGames()[Game].Id == FName(TEXT("derby")))
		{
			// A bat swinging through a ball that streaks away.
			const double Swing = FMath::Sin(Now * 2.4) * 0.25;
			const FVector2D Ball = C + FVector2D(0.42 * S, -0.38 * S);
			for (int32 i = 1; i <= 3; ++i)
			{
				P.Circle(Ball - FVector2D(0.16 * S * i, -0.13 * S * i), (0.15f - i * 0.03f) * S, Color::WithAlpha(Color::Sun, 0.8f - i * 0.2f));
			}
			P.Circle(Ball, 0.2f * S, Color::White, Color::Navy, 0.04f * S);
			P.Lines({ Ball + FVector2D(-0.1 * S, -0.08 * S), Ball + FVector2D(0.02 * S, 0.12 * S) }, Color::Hot, 0.03f * S);
			const FVector2D Knob = C + FVector2D(-0.55 * S, 0.62 * S);
			const double A = -0.95 + Swing;
			const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
			const FVector2D Side(-Dir.Y, Dir.X);
			P.Polygon({ Knob + Side * 0.04 * S, Knob + Dir * 1.15 * S + Side * 0.12 * S, Knob + Dir * 1.22 * S, Knob + Dir * 1.15 * S - Side * 0.12 * S, Knob - Side * 0.04 * S }, Color::Gold);
			P.Circle(Knob, 0.07f * S, Color::Navy);
			return;
		}
		if (ArcadeGames().IsValidIndex(Game) && ArcadeGames()[Game].Id == FName(TEXT("fishing")))
		{
			// A bobber dipping on the ripples, its line up out of frame.
			const double Dip = FMath::Max(0.0, FMath::Sin(Now * 2.4)) * 0.12 * S;
			for (int32 i = 0; i < 3; ++i)
			{
				const double R = (0.45 + 0.22 * i + 0.1 * FMath::Frac(Now * 0.6)) * S;
				P.Lines({ C + FVector2D(-R, 0.45 * S), C + FVector2D(R, 0.45 * S) }, Color::WithAlpha(Color::White, 0.7f - 0.2f * i), 0.05f * S);
			}
			P.Lines({ C + FVector2D(0.0, -0.95 * S), C + FVector2D(0.0, -0.3 * S + Dip) }, Color::White, 0.03f * S);
			P.Circle(C + FVector2D(0.0, 0.05 * S + Dip), 0.42f * S, Color::White, Color::Navy, 0.05f * S);
			P.Polygon({ C + FVector2D(-0.42 * S, 0.05 * S + Dip), C + FVector2D(-0.3 * S, -0.27 * S + Dip), C + FVector2D(0.0, -0.37 * S + Dip), C + FVector2D(0.3 * S, -0.27 * S + Dip), C + FVector2D(0.42 * S, 0.05 * S + Dip) }, Color::Hot);
			P.Box(C + FVector2D(-0.04 * S, -0.62 * S + Dip), FVector2D(0.08 * S, 0.3 * S), Color::Navy, 0.03f * S);
			return;
		}
		switch (Game)
		{
		case 0:
			MenuIcon(P, 5, C, S, Now);
			break;
		case 1: // A dartboard with a dart in it.
		{
			const FLinearColor Rings[] = { Color::Navy, Color::White, Color::Hot, Color::White, Color::Mint, Color::Hot };
			for (int32 i = 0; i < UE_ARRAY_COUNT(Rings); ++i)
			{
				P.Circle(C, (0.9f - i * 0.15f) * S, Rings[i]);
			}
			const double Wobble = FMath::Sin(Now * 9.0) * 0.03 * S;
			P.Box(C + FVector2D(0.05 * S, -0.08 * S + Wobble), FVector2D(0.7 * S, 0.07 * S), Color::Navy, 0.03f * S);
			P.Polygon({ C + FVector2D(0.62 * S, -0.2 * S + Wobble), C + FVector2D(0.85 * S, -0.2 * S + Wobble), C + FVector2D(0.85 * S, 0.05 * S + Wobble), C + FVector2D(0.62 * S, 0.05 * S + Wobble) }, Color::Sun);
			break;
		}
		case 2: // A fruit falling apart under a blade stroke.
		{
			const double Part = 0.08 * S * (0.5 + 0.5 * FMath::Sin(Now * 3.0));
			P.Circle(C + FVector2D(-0.1 * S - Part, 0.05 * S), 0.5f * S, Color::Hot, Color::Navy, 0.04f * S);
			P.Circle(C + FVector2D(0.1 * S + Part, -0.05 * S), 0.5f * S, Color::Sun, Color::Navy, 0.04f * S);
			P.Polygon({ C + FVector2D(-0.9 * S, 0.6 * S), C + FVector2D(0.8 * S, -0.7 * S), C + FVector2D(0.9 * S, -0.62 * S), C + FVector2D(-0.82 * S, 0.68 * S) }, Color::White);
			break;
		}
		case 4: // Two bumper pucks colliding on a ring, sparks between them.
		{
			const double Bump = FMath::Abs(FMath::Sin(Now * 2.4)) * 0.12 * S;
			P.Circle(C + FVector2D(0.0, 0.18 * S), 0.92f * S, Color::WithAlpha(Color::Navy, 0.85f), Color::Sky, 0.05f * S);
			P.Circle(C + FVector2D(0.0, 0.18 * S), 0.5f * S, FLinearColor::Transparent, Color::WithAlpha(Color::Sky, 0.5f), 0.03f * S);
			P.Circle(C + FVector2D(-0.36 * S - Bump, 0.05 * S), 0.33f * S, Color::Hot, Color::Navy, 0.05f * S);
			P.Circle(C + FVector2D(0.36 * S + Bump, 0.05 * S), 0.33f * S, Color::Sky, Color::Navy, 0.05f * S);
			P.Circle(C + FVector2D(-0.36 * S - Bump, -0.05 * S), 0.12f * S, Color::White);
			P.Circle(C + FVector2D(0.36 * S + Bump, -0.05 * S), 0.12f * S, Color::White);
			for (int32 i = 0; i < 4; ++i)
			{
				const double A = -0.9 + i * 0.6;
				const FVector2D D(FMath::Sin(A), -FMath::Cos(A));
				P.Lines({ C + D * (0.12 * S + Bump), C + D * (0.34 * S + Bump * 2.0) }, Color::Sun, 0.05f * S);
			}
			break;
		}
		default: // A swaying tower of blocks.
		{
			const FLinearColor Fills[] = { Color::Navy, Color::Sky, Color::Sun, Color::Hot, Color::Mint };
			for (int32 i = 0; i < 5; ++i)
			{
				const double Sway = FMath::Sin(Now * 1.6) * 0.03 * S * i;
				const double W = (1.2 - i * 0.12) * S;
				P.Box(C + FVector2D(-W * 0.5 + Sway + i * 0.03 * S, 0.62 * S - (i + 1) * 0.26 * S), FVector2D(W, 0.24 * S), Fills[i], 0.04f * S, Color::Navy, 0.03f * S);
			}
			break;
		}
		}
	}

	class FArcadeScreen final : public FKartScreen
	{
	public:
		/** Rows: the games, then Players and House players. */
		const int32 PlayersRow = ArcadeGames().Num();
		const int32 HouseRow = PlayersRow + 1;

		explicit FArcadeScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd)
		{
			Focus.Cursor.Count = HouseRow + 1;
		}

		virtual FName GetId() const override { return TEXT("arcade"); }

		virtual void OnShown() override
		{
			// Back from a game: land on the one just played (next game may have moved on from the pick).
			if (const UArcadeSession* Session = UArcadeSession::Get(FrontEnd.Get()); Session && !Session->GetGameId().IsEmpty())
			{
				const int32 Played = ArcadeGames().IndexOfByPredicate([Session](const FArcadeGame& G) { return G.Id == FName(*Session->GetGameId()); });
				if (Played != INDEX_NONE)
				{
					ArcadePick().Game = Played;
				}
			}
			Focus.Cursor.Set(ArcadePick().Game);
			Focus.FocusAt = NowSeconds();
			bStarting = false;
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
		}

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Change", "Change") }, { EKartMenuAction::Accept, LOCTEXT("Play", "Play") }, { EKartMenuAction::Back, LOCTEXT("Back", "Back") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (bStarting)
			{
				return true;
			}
			switch (Event.Action)
			{
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				MoveTo(Focus.Cursor.Index + (Event.Action == EKartMenuAction::Up ? -1 : 1));
				return true;
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Change(Focus.Cursor.Index, Event.Action == EKartMenuAction::Left ? -1 : 1);
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Play();
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (HitId != PlayHit)
			{
				MoveTo(KartWidgets::HitBase(HitId));
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId == PlayHit)
			{
				Play();
				return;
			}
			const int32 Row = KartWidgets::HitBase(HitId);
			MoveTo(Row);
			if (Row < PlayersRow)
			{
				Play();
			}
			else
			{
				Change(Row, KartWidgets::HitArrow(HitId) < 0 ? -1 : 1);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const TArray<FArcadeGame>& Games = ArcadeGames();
			const FArcadePick& Pick = ArcadePick();
			const FArcadeGame& Game = Games[Pick.Game];
			LeftPanel(P, Frame, 760.f, 0.76f);
			KartWidgets::Header(P, Frame, LOCTEXT("ArcadeTitle", "Arcade"), LOCTEXT("ArcadeSub", "Pick a game, then who's playing."), Color::Sun);

			const double Top = Frame.SafeMin.Y + 190.0;
			const FVector2D ButtonSize(600.0, 84.0);
			{
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Games.Num(); ++i)
			{
				Group.Next();
				const float In = KartWidgets::Entrance(Frame, 0.05f + i * 0.05f, 0.4f);
				const FVector2D Pos(Frame.SafeMin.X - (1.0 - In) * 260.0 + KartWidgets::SlideX(Frame), Top + i * (ButtonSize.Y + 16.0));
				KartWidgets::Button(P.Faded(In), Pos, ButtonSize, Games[i].Name, Focus.State(i, Frame.Now, Games[i].Map != nullptr), Games[i].Accent, i);
				if (!Games[i].Map)
				{
					// Not ported yet: a chip at the right end rather than a second line.
					const FVector2D ChipSize(116.0, 40.0);
					const FVector2D ChipPos(Pos.X + ButtonSize.X - ChipSize.X - 24.0, Pos.Y + (ButtonSize.Y - ChipSize.Y) * 0.5);
					P.Faded(In).Box(ChipPos, ChipSize, Color::WithAlpha(Games[i].Accent, 0.9f), 20.f);
					P.Faded(In).Text(LOCTEXT("Soon", "SOON"), Font(EFont::Label, 19.f), ChipPos + ChipSize * 0.5, Color::TextOn(Games[i].Accent), FVector2D(0.5, 0.5));
				}
			}
			}
			static const FText Labels[] = { LOCTEXT("ArcadePlayers", "Players"), LOCTEXT("ArcadeHouse", "House players") };
			if (Pick.Players != ValuesPlayers || Pick.House != ValuesHouse)
			{
				ValuesPlayers = Pick.Players;
				ValuesHouse = Pick.House;
				Values[0] = FText::Format(LOCTEXT("PlayersN", "{0} on this screen"), Pick.Players);
				Values[1] = Pick.House == 0 ? LOCTEXT("NoHouse", "None") : FText::Format(LOCTEXT("HouseN", "{0} {0}|plural(one=bot,other=bots)"), Pick.House);
			}
			const FVector2D RowSize(600.0, 76.0);
			const double OptTop = Top + Games.Num() * (ButtonSize.Y + 16.0) + 24.0;
			const FKartPaintGroup Rows(P);
			for (int32 i = 0; i < 2; ++i)
			{
				Rows.Next();
				const int32 Row = PlayersRow + i;
				const float In = KartWidgets::Entrance(Frame, 0.25f + i * 0.05f);
				const FVector2D Pos(Frame.SafeMin.X + KartWidgets::SlideX(Frame) - (1.0 - In) * 200.0, OptTop + i * (RowSize.Y + 14.0));
				KartWidgets::OptionRow(P.Faded(In), Pos, RowSize, Labels[i], Values[i], Focus.State(Row, Frame.Now), Row);
			}

			// The picked game's card: its icon, pitch and the line-up as dots.
			const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - PickedAt) / 0.35), 2.f);
			const FVector2D CardSize(620.0, 560.0);
			const FVector2D CardPos(Frame.SafeMax.X - CardSize.X, Top);
			const FKartPainter Card = P.Transformed(CardPos + CardSize * 0.5, 0.92f + 0.08f * Pop, (1.f - Pop) * -3.f * FKartAccessibility::MotionScale()).Faded(KartWidgets::Entrance(Frame, 0.2f));
			Card.Card(CardPos, CardSize, Color::Paper, Metric::RadiusLarge, 10.f, Color::White, 5.f);
			Card.Gradient(CardPos + FVector2D(18.0, 18.0), FVector2D(CardSize.X - 36.0, 250.0), Game.Accent, FMath::Lerp(Game.Accent, Color::Night, 0.35f), true, Metric::Radius);
			GameIcon(Card, Pick.Game, CardPos + FVector2D(CardSize.X * 0.5, 143.0), 100.f, Frame.Now);
			Card.Text(Game.Name, Font(EFont::Display, 54.f), CardPos + FVector2D(36.0, 290.0), Color::Navy);
			Card.TextWrapped(Game.Tagline, Font(EFont::Body, 22.f), CardPos + FVector2D(38.0, 372.0), CardSize.X - 76.0, Color::NavyDim);
			const int32 Seats = Pick.Players + Pick.House;
			static const FString SeatTags[] = { TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4"), TEXT("P5"), TEXT("P6"), TEXT("P7"), TEXT("P8") };
			static const FString Cpu(TEXT("CPU"));
			for (int32 i = 0; i < Game.MaxPlayers; ++i)
			{
				const FVector2D C(CardPos.X + 70.0 + i * 76.0, CardPos.Y + CardSize.Y - 66.0);
				if (i < Seats)
				{
					const bool bLocal = i < Pick.Players;
					const FLinearColor Fill = bLocal ? Game.Accent : Color::NavyDim;
					Card.Circle(C, 28.f, Fill, Color::Navy, 3.f);
					Card.Text(bLocal ? SeatTags[FMath::Min(i, 7)] : Cpu, Font(EFont::Label, bLocal ? 18.f : 14.f), C, Color::TextOn(Fill), FVector2D(0.5, 0.5));
				}
				else
				{
					Card.Circle(C, 28.f, FLinearColor::Transparent, Color::WithAlpha(Color::NavyDim, 0.4f), 3.f);
				}
			}

			FItemState Start;
			Start.bFocused = true;
			Start.bEnabled = Game.Map != nullptr;
			Start.FocusAge = Frame.Now - Focus.FocusAt;
			KartWidgets::PrimaryButton(P.Faded(KartWidgets::Entrance(Frame, 0.35f)), FVector2D(CardPos.X + CardSize.X * 0.5, CardPos.Y + CardSize.Y + 70.0),
				Game.Map ? LOCTEXT("PlayButton", "PLAY") : LOCTEXT("SoonButton", "COMING SOON"), EKartMenuAction::Accept, Frame.Device, Start, PlayHit);
		}

	private:
		static constexpr int32 PlayHit = 99;

		void MoveTo(int32 Row)
		{
			if (Focus.SetFocus(FMath::Clamp(Row, 0, Focus.Cursor.Count - 1), NowSeconds()))
			{
				Host->PlaySound(EKartUISound::Move);
				if (Focus.Cursor.Index < PlayersRow && ArcadePick().Game != Focus.Cursor.Index)
				{
					FArcadePick& Pick = ArcadePick();
					Pick.Game = Focus.Cursor.Index;
					// A simultaneous game seats one player here; the rest of the line-up becomes the house.
					const FArcadeGame& G = ArcadeGames()[Pick.Game];
					Pick.Players = FMath::Min(Pick.Players, G.MaxLocal);
					Pick.House = FMath::Min(Pick.House, G.MaxPlayers - Pick.Players);
					PickedAt = NowSeconds();
				}
			}
		}

		void Change(int32 Row, int32 Dir)
		{
			if (Row < PlayersRow)
			{
				return;
			}
			FArcadePick& Pick = ArcadePick();
			const FArcadePick Before = Pick;
			const int32 Max = ArcadeGames()[Pick.Game].MaxPlayers;
			if (Row == PlayersRow)
			{
				Pick.Players = FMath::Clamp(Pick.Players + Dir, 1, ArcadeGames()[Pick.Game].MaxLocal);
				Pick.House = FMath::Min(Pick.House, Max - Pick.Players);
			}
			else
			{
				Pick.House = FMath::Clamp(Pick.House + Dir, 0, Max - Pick.Players);
			}
			const bool bChanged = Pick.Players != Before.Players || Pick.House != Before.House;
			if (bChanged)
			{
				Focus.Changed(Row, Dir, NowSeconds());
			}
			Host->PlaySound(bChanged ? EKartUISound::Tick : EKartUISound::Error);
		}

		void Play()
		{
			const FArcadePick& Pick = ArcadePick();
			const FArcadeGame& Game = ArcadeGames()[Pick.Game];
			AKartFrontEnd* FE = FrontEnd.Get();
			if (bStarting || !Game.Map || !FE)
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			bStarting = true;
			Host->PlaySound(EKartUISound::Go);
			if (UArcadeSession* Session = UArcadeSession::Get(FE))
			{
				Session->NoteLaunch(Game.Id.ToString(), Pick.Players, Pick.House);
			}
			FE->StartGame(Game.Map, Pick.Players, Pick.House);
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FFocus Focus;
		double PickedAt = -10.0;
		bool bStarting = false;
		/** The option rows' values, rebuilt when the pick changes. */
		int32 ValuesPlayers = -1;
		int32 ValuesHouse = -1;
		FText Values[2];
	};


	const TArray<FText>& SetupSteps()
	{
		static const TArray<FText> Steps = { LOCTEXT("StepRacers", "Racers"), LOCTEXT("StepRules", "Rules"), LOCTEXT("StepTrack", "Track") };
		return Steps;
	}

	// ---------------------------------------------------------------------------------------------- Racers

	class FRacersScreen final : public FKartScreen
	{
	public:
		explicit FRacersScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd) {}

		virtual FName GetId() const override { return TEXT("racers"); }

		virtual void OnShown() override
		{
			const double Now = NowSeconds();
			for (FSlot& Slot : Slots)
			{
				Slot.Focus.Cursor.Count = NumRows;
				Slot.Focus.FocusAt = Now;
				Slot.bReady = false;
			}
			if (UKartSessionSettings* S = Session(Host))
			{
				S->MutableSetup().Sanitize();
				for (int32 i = 0; i < 4; ++i)
				{
					Slots[i].bJoined = i < S->GetSetup().LocalPlayers;
				}
			}
			Slots[0].bJoined = true;
			bAdvancing = false;
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Showroom);
			}
			SyncShowroom();
		}

		virtual TArray<FKartHint> GetHints() const override
		{
			return {
				{ EKartMenuAction::Left, LOCTEXT("Change", "Change") },
				{ EKartMenuAction::Alt, LOCTEXT("Random", "Random") },
				{ EKartMenuAction::Accept, LOCTEXT("Ready", "Ready") },
				{ EKartMenuAction::Back, LOCTEXT("Back", "Back") },
			};
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			const int32 Player = FMath::Clamp(Event.UserIndex, 0, 3);
			FSlot& Slot = Slots[Player];
			const double Now = NowSeconds();
			if (!Slot.bJoined)
			{
				// Players 2..4 join with Start (or A), in pad order: seat N is driven by gamepad N.
				if ((Event.Action == EKartMenuAction::Start || Event.Action == EKartMenuAction::Accept) && Player != NumJoined())
				{
					Host->PlaySound(EKartUISound::Error);
				}
				else if (Event.Action == EKartMenuAction::Start || Event.Action == EKartMenuAction::Accept)
				{
					Slot.bJoined = true;
					Slot.bReady = false;
					Slot.JoinedAt = Now;
					Slot.Focus.Cursor.Count = NumRows;
					Host->PlaySound(EKartUISound::Start);
					SyncShowroom();
				}
				return Event.Action != EKartMenuAction::None;
			}
			if (Slot.bReady)
			{
				if (Event.Action == EKartMenuAction::Back)
				{
					Slot.bReady = false;
					Host->PlaySound(EKartUISound::Back);
					return true;
				}
				return Event.Action != EKartMenuAction::None;
			}
			switch (Event.Action)
			{
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				if (Slot.Focus.Move(Event.Action == EKartMenuAction::Up ? -1 : 1, Now))
				{
					Host->PlaySound(EKartUISound::Move);
				}
				return true;
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Change(Player, Slot.Focus.Cursor.Index, Event.Action == EKartMenuAction::Left ? -1 : 1);
				return true;
			case EKartMenuAction::Alt:
				Randomise(Player);
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Slot.bReady = true;
				Slot.ReadyAt = Now;
				Host->PlaySound(EKartUISound::Accept);
				return true;
			case EKartMenuAction::Back:
				if (Player > 0)
				{
					if (Player != NumJoined() - 1)
					{
						Host->PlaySound(EKartUISound::Error); // the last seat leaves first
						return true;
					}
					Slot.bJoined = false;
					Host->PlaySound(EKartUISound::Back);
					SyncShowroom();
					return true;
				}
				SaveJoined();
				return false; // P1 backs out to the menu
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			const int32 Row = KartWidgets::HitBase(HitId);
			if (Row < NumRows && !Slots[0].bReady && Slots[0].Focus.SetFocus(Row, NowSeconds()))
			{
				Host->PlaySound(EKartUISound::Move);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			const int32 Row = KartWidgets::HitBase(HitId);
			if (Row == ReadyHit)
			{
				Slots[0].bReady = !Slots[0].bReady;
				Slots[0].ReadyAt = NowSeconds();
				Host->PlaySound(Slots[0].bReady ? EKartUISound::Accept : EKartUISound::Back);
				return;
			}
			if (Row < NumRows && !Slots[0].bReady)
			{
				Slots[0].Focus.SetFocus(Row, NowSeconds());
				Change(0, Row, KartWidgets::HitArrow(HitId) < 0 ? -1 : 1);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const double Now = Frame.Now;
			P.Gradient(FVector2D(0.0, Frame.Size.Y * 0.45), FVector2D(Frame.Size.X, Frame.Size.Y * 0.55), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.7f));
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, 260.0), Color::WithAlpha(Color::Night, 0.6f), Color::WithAlpha(Color::Night, 0.f));
			KartWidgets::Header(P, Frame, LOCTEXT("RacersTitle", "Choose your racer"),
				LOCTEXT("RacersSub", "Players 2 to 4: press Start on your controller to join."), Color::Hot);
			KartWidgets::Steps(P, Frame, SetupSteps(), 0);

			const int32 Joined = NumJoined();
			// Joined players' cards, plus one "press start" card for the next free seat.
			const int32 Cards = FMath::Min(4, Joined + 1);
			const double Gap = 24.0;
			const double CardW = FMath::Min(470.0, (Frame.SafeSize().X - Gap * 3) / 4.0);
			const double CardH = 486.0;
			const double Total = Cards * CardW + (Cards - 1) * Gap;
			const double Left = Frame.Size.X * 0.5 - Total * 0.5;
			const double Top = Frame.SafeMax.Y - CardH - 64.0;
			int32 Card = 0;
			for (int32 i = 0; i < 4 && Card < Cards; ++i)
			{
				const FVector2D Pos(Left + Card * (CardW + Gap) + KartWidgets::SlideX(Frame), Top);
				if (Slots[i].bJoined)
				{
					PaintCard(P, Frame, i, Pos, FVector2D(CardW, CardH));
					++Card;
				}
				else if (Card == Joined)
				{
					PaintJoinCard(P, Frame, i, Pos, FVector2D(CardW, CardH));
					++Card;
				}
			}

			if (!bAdvancing && AllReady())
			{
				bAdvancing = true;
				AdvanceAt = Now + 0.45;
			}
			if (bAdvancing && Now >= AdvanceAt)
			{
				bAdvancing = false;
				SaveJoined();
				Host->PlaySound(EKartUISound::Whoosh);
				Host->PushScreen(KartScreens::MakeRules(FrontEnd.Get()));
			}
		}

	private:
		static constexpr int32 NumRows = 4;
		static constexpr int32 ReadyHit = 100;

		struct FSlot
		{
			bool bJoined = false;
			bool bReady = false;
			double JoinedAt = -10.0;
			double ReadyAt = -10.0;
			FFocus Focus;
		};

		int32 NumJoined() const
		{
			int32 N = 0;
			for (const FSlot& Slot : Slots)
			{
				N += Slot.bJoined ? 1 : 0;
			}
			return N;
		}

		bool AllReady() const
		{
			for (const FSlot& Slot : Slots)
			{
				if (Slot.bJoined && !Slot.bReady)
				{
					return false;
				}
			}
			return true;
		}

		FKartRacerChoice& Choice(int32 Player)
		{
			UKartSessionSettings* S = Session(Host);
			check(S);
			FKartRaceSetup& Setup = S->MutableSetup();
			Setup.Sanitize();
			return Setup.Racers[Player];
		}

		void Change(int32 Player, int32 Row, int32 Dir)
		{
			if (!Session(Host))
			{
				return;
			}
			FKartRacerChoice& C = Choice(Player);
			switch (Row)
			{
			case 0: C.Character = FKartMenuCursor::Cycle(C.Character, Dir, FKartRoster::Characters().Num()); break;
			case 1: C.Outfit = static_cast<EKartOutfit>(FKartMenuCursor::Cycle(static_cast<int32>(C.Outfit), Dir, FKartRoster::NumOutfits())); break;
			case 2: C.Vehicle = static_cast<EKartVehicle>(FKartMenuCursor::Cycle(static_cast<int32>(C.Vehicle), Dir, FKartRoster::NumVehicles())); break;
			default: C.Paint = FKartMenuCursor::Cycle(C.Paint, Dir, FKartRoster::Paints().Num()); break;
			}
			Slots[Player].Focus.Changed(Row, Dir, NowSeconds());
			Host->PlaySound(EKartUISound::Tick);
			SyncShowroom();
		}

		void Randomise(int32 Player)
		{
			if (!Session(Host))
			{
				return;
			}
			FKartRacerChoice& C = Choice(Player);
			C.Character = FMath::RandRange(0, FKartRoster::Characters().Num() - 1);
			C.Outfit = static_cast<EKartOutfit>(FMath::RandRange(0, FKartRoster::NumOutfits() - 1));
			C.Vehicle = static_cast<EKartVehicle>(FMath::RandRange(0, FKartRoster::NumVehicles() - 1));
			C.Paint = FMath::RandRange(0, FKartRoster::Paints().Num() - 1);
			Slots[Player].Focus.Changed(Slots[Player].Focus.Cursor.Index, 1, NowSeconds());
			Host->PlaySound(EKartUISound::Accept);
			SyncShowroom();
		}

		void SaveJoined()
		{
			if (UKartSessionSettings* S = Session(Host))
			{
				// Seats join in pad order, so the joined slots are always 0..N-1.
				FKartRaceSetup& Setup = S->MutableSetup();
				Setup.LocalPlayers = FMath::Max(1, NumJoined());
				Setup.Sanitize();
			}
		}

		void SyncShowroom()
		{
			AKartFrontEnd* FE = FrontEnd.Get();
			UKartSessionSettings* S = Session(Host);
			if (!FE || !S)
			{
				return;
			}
			TArray<FKartLook> Looks;
			for (int32 i = 0; i < 4; ++i)
			{
				if (Slots[i].bJoined)
				{
					Looks.Add(FKartRoster::MakeLook(Choice(i), FString::Printf(TEXT("P%d"), i + 1)));
					FKartCosmetics::ApplyToLook(Looks.Last(), i); // store cosmetics
				}
			}
			FE->SetShowroom(Looks);
		}

		void PaintCard(const FKartPainter& P, const FKartScreenFrame& Frame, int32 Player, const FVector2D& Pos, const FVector2D& Size)
		{
			const FSlot& Slot = Slots[Player];
			const UKartSessionSettings* Settings = Session(Host);
			const FKartRacerChoice C = FKartRoster::Sanitize(Settings && Settings->GetSetup().Racers.IsValidIndex(Player) ? Settings->GetSetup().Racers[Player] : FKartRoster::DefaultChoice(Player));
			const FLinearColor Paint = FKartCosmetics::PaintColor(Player, FKartRoster::PaintColor(C.Paint)); // store cosmetics
			const float Join = Ease::OutBack(static_cast<float>((Frame.Now - Slot.JoinedAt) / 0.4), 2.f);
			const float In = FMath::Min(KartWidgets::Entrance(Frame, 0.1f + Player * 0.06f), FMath::Max(Join, 0.f));
			const FKartPainter Card = P.Transformed(Pos + Size * 0.5, 0.85f + 0.15f * In).Faded(In);
			Card.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 10.f, Slot.bReady ? FKartAccessibility::Good() : Color::White, 5.f);
			// Header band in the kart's paint with the player tag and the driver's name.
			Card.Box(Pos + FVector2D(14.0, 14.0), FVector2D(Size.X - 28.0, 92.0), Paint, Metric::Radius);
			Card.Circle(Pos + FVector2D(66.0, 60.0), 34.f, Color::White, Color::Navy, 3.f);
			static const FString Tags[] = { TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4") };
			Card.Text(Tags[FMath::Clamp(Player, 0, 3)], Font(EFont::Display, 30.f), Pos + FVector2D(66.0, 60.0), Color::Navy, FVector2D(0.5, 0.5));
			Card.Text(FKartRoster::Characters()[C.Character].Name, Font(EFont::Display, 46.f, 3.f), Pos + FVector2D(116.0, 60.0), Color::White, FVector2D(0.0, 0.5), 3.f);

			static const FText Labels[NumRows] = { LOCTEXT("Driver", "Driver"), LOCTEXT("Outfit", "Outfit"), LOCTEXT("Vehicle", "Vehicle"), LOCTEXT("Paint", "Paint") };
			const FText Values[NumRows] = { FKartRoster::Characters()[C.Character].Name, FKartRoster::OutfitName(C.Outfit), FKartRoster::VehicleName(C.Vehicle), FKartCosmetics::PaintName(Player, FKartRoster::Paints()[C.Paint].Name) }; // store cosmetics: paint name
			const double RowH = 58.0;
			{
			const FKartPaintGroup Group(Card);
			for (int32 Row = 0; Row < NumRows; ++Row)
			{
				Group.Next();
				FItemState State = Slot.Focus.State(Row, Frame.Now);
				State.bFocused = State.bFocused && !Slot.bReady;
				const FVector2D RowPos(Pos.X + 16.0, Pos.Y + 122.0 + Row * (RowH + 10.0));
				// Only P1's card is mouse-driven (the keyboard and mouse belong to player 1).
				const FKartPainter RowPainter = Player == 0 ? Card : Card.Faded(0.999f);
				CompactRow(RowPainter, RowPos, FVector2D(Size.X - 32.0, RowH), Labels[Row], Values[Row], State, Player == 0 ? Row : -1,
					Row == 3 ? Paint : FLinearColor::Transparent);
			}
			}
			// Ready bar / stamp.
			const FVector2D ReadyPos(Pos.X + 16.0, Pos.Y + Size.Y - 70.0);
			const FVector2D ReadySize(Size.X - 32.0, 54.0);
			if (Player == 0)
			{
				Card.Hit(ReadyHit, ReadyPos, ReadySize);
			}
			if (Slot.bReady)
			{
				const float Stamp = Ease::OutBack(static_cast<float>((Frame.Now - Slot.ReadyAt) / 0.3), 3.f);
				const float Motion = FKartAccessibility::MotionScale();
				const FKartPainter S = Card.Transformed(ReadyPos + ReadySize * 0.5, 1.f + (0.6f - 0.6f * Stamp) * Motion, -4.f * Motion);
				S.Box(ReadyPos, ReadySize, FKartAccessibility::Good(), 27.f, Color::White, 3.f);
				S.Text(LOCTEXT("ReadyStamp", "READY!"), Font(EFont::Display, 36.f, 2.f), ReadyPos + ReadySize * 0.5, Color::White, FVector2D(0.5, 0.5), 3.f);
			}
			else
			{
				Card.Box(ReadyPos, ReadySize, Color::PaperShade, 27.f);
				const EKartInputDevice Device = Player == 0 ? Frame.Device : EKartInputDevice::Gamepad;
				const float GW = KartWidgets::GlyphWidth(EKartMenuAction::Accept, Device, 36.f);
				const FSlateFontInfo F = Font(EFont::Button, 24.f);
				const FText Label = LOCTEXT("PressReady", "when ready");
				const float W = GW + 10.f + static_cast<float>(FKartPainter::Measure(Label.ToString(), F).X);
				const double X = ReadyPos.X + ReadySize.X * 0.5 - W * 0.5;
				KartWidgets::Glyph(Card, EKartMenuAction::Accept, Device, FVector2D(X, ReadyPos.Y + 27.0), 36.f);
				Card.Text(Label, F, FVector2D(X + GW + 10.0, ReadyPos.Y + 27.0), Color::NavyDim, FVector2D(0.0, 0.5));
			}
		}

		/** An option row sized for the player cards. */
		static void CompactRow(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, const FText& Value,
			const FItemState& State, int32 HitId, const FLinearColor& Swatch)
		{
			if (HitId >= 0)
			{
				P.Hit(HitId, Pos, Size);
				P.Hit(HitId | KartWidgets::LeftArrowFlag, FVector2D(Pos.X + Size.X * 0.3, Pos.Y), FVector2D(50.0, Size.Y));
				P.Hit(HitId | KartWidgets::RightArrowFlag, FVector2D(Pos.X + Size.X - 50.0, Pos.Y), FVector2D(50.0, Size.Y));
			}
			const float R = static_cast<float>(Size.Y * 0.5);
			if (State.bFocused)
			{
				P.Box(Pos - FVector2D(4.0, 4.0), Size + FVector2D(8.0, 8.0), Color::WithAlpha(Color::Sun, 0.45f), R + 4.f);
				P.Box(Pos, Size, Color::White, R, Color::SunDeep, 3.f);
			}
			else
			{
				P.Box(Pos, Size, Color::WithAlpha(Color::PaperShade, 0.8f), R);
			}
			P.Text(Label, Font(EFont::Label, 17.f), FVector2D(Pos.X + 22.0, Pos.Y + Size.Y * 0.5), Color::NavyDim, FVector2D(0.0, 0.5));
			const double L = Pos.X + Size.X * 0.3 + 18.0;
			const double Rt = Pos.X + Size.X - 22.0;
			const double Nudge = State.ChangeAge < 0.15 ? (1.0 - State.ChangeAge / 0.15) * 6.0 : 0.0;
			const FLinearColor ArrowColor = State.bFocused ? Color::SunDeep : Color::WithAlpha(Color::NavyDim, 0.35f);
			KartWidgets::Arrow(P, FVector2D(L - (State.ChangeDir < 0 ? Nudge : 0.0), Pos.Y + Size.Y * 0.5), 10.f, false, ArrowColor);
			KartWidgets::Arrow(P, FVector2D(Rt + (State.ChangeDir > 0 ? Nudge : 0.0), Pos.Y + Size.Y * 0.5), 10.f, true, ArrowColor);
			const FVector2D VC((L + Rt) * 0.5, Pos.Y + Size.Y * 0.5);
			const float Pop = State.ChangeAge < 0.2 ? 1.f + 0.14f * (1.f - Ease::OutCubic(static_cast<float>(State.ChangeAge / 0.2))) : 1.f;
			const FKartPainter V = P.Transformed(VC, Pop);
			double TextX = VC.X;
			if (Swatch.A > 0.f)
			{
				V.Circle(FVector2D(L + 30.0, VC.Y), 13.f, Swatch, Color::Navy, 2.f);
				TextX += 14.0;
			}
			// Long names ("Classic Jersey") shrink to stay between the arrows.
			const double Room = (Rt - L) - 36.0 - (Swatch.A > 0.f ? 28.0 : 0.0);
			FSlateFontInfo ValueFont = Font(EFont::Button, 23.f);
			const double Width = FKartPainter::Measure(Value.ToString(), ValueFont).X;
			if (Width > Room)
			{
				ValueFont = Font(EFont::Button, FMath::Max(16.f, FMath::FloorToFloat(23.f * static_cast<float>(Room / Width))));
			}
			V.Text(Value, ValueFont, FVector2D(TextX, VC.Y), Color::Navy, FVector2D(0.5, 0.5));
		}

		void PaintJoinCard(const FKartPainter& P, const FKartScreenFrame& Frame, int32 Player, const FVector2D& Pos, const FVector2D& Size)
		{
			const float In = KartWidgets::Entrance(Frame, 0.3f);
			const FKartPainter Card = P.Faded(In * (0.75f + 0.25f * FMath::Sin(static_cast<float>(Frame.Now) * 3.f)));
			Card.Box(Pos, Size, Color::WithAlpha(Color::Night, 0.55f), Metric::RadiusLarge, Color::WithAlpha(Color::White, 0.8f), 3.f);
			static const FString JoinTags[] = { TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4") };
			Card.Text(JoinTags[FMath::Clamp(Player, 0, 3)], Font(EFont::Display, 64.f, 3.f), Pos + FVector2D(Size.X * 0.5, Size.Y * 0.36), Color::WithAlpha(Color::White, 0.9f), FVector2D(0.5, 0.5));
			const float GW = KartWidgets::GlyphWidth(EKartMenuAction::Start, EKartInputDevice::Gamepad, 44.f);
			KartWidgets::Glyph(Card, EKartMenuAction::Start, EKartInputDevice::Gamepad, Pos + FVector2D(Size.X * 0.5 - GW * 0.5, Size.Y * 0.56), 44.f);
			Card.Text(LOCTEXT("Join", "Press Start to join"), Font(EFont::Button, 26.f, 2.f), Pos + FVector2D(Size.X * 0.5, Size.Y * 0.7), Color::White, FVector2D(0.5, 0.5));
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FSlot Slots[4];
		bool bAdvancing = false;
		double AdvanceAt = 0.0;
	};

	// ----------------------------------------------------------------------------------------------- Rules

	class FRulesScreen final : public FKartScreen
	{
	public:
		explicit FRulesScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd)
		{
			Focus.Cursor.Count = 5;
			Focus.Cursor.bWrap = true;
		}

		virtual FName GetId() const override { return TEXT("rules"); }

		virtual void OnShown() override
		{
			Focus.FocusAt = NowSeconds();
			if (UKartSessionSettings* S = Session(Host))
			{
				S->MutableSetup().Sanitize(Grid());
			}
			ValuesKey = -1;
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
		}

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Change", "Change") }, { EKartMenuAction::Accept, LOCTEXT("Next", "Next") }, { EKartMenuAction::Back, LOCTEXT("Back", "Back") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			const double Now = NowSeconds();
			switch (Event.Action)
			{
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				if (Focus.Move(Event.Action == EKartMenuAction::Up ? -1 : 1, Now))
				{
					Host->PlaySound(EKartUISound::Move);
				}
				return true;
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Change(Focus.Cursor.Index, Event.Action == EKartMenuAction::Left ? -1 : 1);
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Next();
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (Focus.SetFocus(KartWidgets::HitBase(HitId), NowSeconds()))
			{
				Host->PlaySound(EKartUISound::Move);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			const int32 Row = KartWidgets::HitBase(HitId);
			Focus.SetFocus(Row, NowSeconds());
			if (Row == 4)
			{
				Next();
			}
			else
			{
				Change(Row, KartWidgets::HitArrow(HitId) < 0 ? -1 : 1);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const UKartSessionSettings* S = Session(Host);
			if (!S)
			{
				return;
			}
			const FKartRaceSetup& Setup = S->GetSetup();
			LeftPanel(P, Frame, 1040.f, 0.72f);
			KartWidgets::Header(P, Frame, LOCTEXT("RulesTitle", "Race rules"), LOCTEXT("RulesSub", "Set up the grid."), Color::Sky);
			KartWidgets::Steps(P, Frame, SetupSteps(), 1);

			static const FText Skill[] = { LOCTEXT("Easy", "Easy"), LOCTEXT("Normal", "Normal"), LOCTEXT("Hard", "Hard") };
			static const FText Labels[] = { LOCTEXT("Opponents", "Opponents"), LOCTEXT("BotSkill", "Bot skill"), LOCTEXT("Laps", "Laps"), LOCTEXT("Screens", "Screens") };
			const int32 Key = ((Setup.Bots * 8 + FMath::Clamp(Setup.BotSkill, 0, 2)) * 64 + Setup.Laps) * 2 + (Setup.bViewEveryKart ? 1 : 0) + Setup.LocalPlayers * 100000;
			if (Key != ValuesKey)
			{
				ValuesKey = Key;
				Values[0] = Setup.Bots == 0 ? LOCTEXT("NoBots", "None") : FText::Format(LOCTEXT("BotsN", "{0} {0}|plural(one=bot,other=bots)"), Setup.Bots);
				Values[1] = Skill[FMath::Clamp(Setup.BotSkill, 0, 2)];
				Values[2] = FText::AsNumber(Setup.Laps);
				Values[3] = Setup.bViewEveryKart ? LOCTEXT("EveryKart", "Every kart (TV)") : LOCTEXT("PerPlayer", "One per player");
				RacersText = FText::Format(LOCTEXT("RacersCount", "{0} racers"), Setup.LocalPlayers + Setup.Bots);
				SummaryText = FText::Format(LOCTEXT("Summary", "{0} {0}|plural(one=lap,other=laps)  ·  {1} bots"), Setup.Laps, Skill[FMath::Clamp(Setup.BotSkill, 0, 2)]);
			}
			// The rows give way to the summary card on narrow canvases (a large UI scale).
			const FVector2D RowSize(FMath::Min(880.0, Frame.SafeSize().X - 640.0), 84.0);
			const double Top = Frame.SafeMin.Y + 200.0;
			{
			const FKartPaintGroup Group(P);
			for (int32 Row = 0; Row < 4; ++Row)
			{
				Group.Next();
				const float In = KartWidgets::Entrance(Frame, 0.05f + Row * 0.05f);
				const FVector2D Pos(Frame.SafeMin.X + KartWidgets::SlideX(Frame) - (1.0 - In) * 200.0, Top + Row * (RowSize.Y + 18.0));
				KartWidgets::OptionRow(P.Faded(In), Pos, RowSize, Labels[Row], Values[Row], Focus.State(Row, Frame.Now, Row != 1 || Setup.Bots > 0), Row);
			}
			}
			const float In = KartWidgets::Entrance(Frame, 0.3f);
			KartWidgets::Button(P.Faded(In), FVector2D(Frame.SafeMin.X + KartWidgets::SlideX(Frame), Top + 4 * (RowSize.Y + 18.0) + 20.0),
				FVector2D(480.0, 92.0), LOCTEXT("ChooseTrack", "Choose track"), Focus.State(4, Frame.Now), Color::Hot, 4);

			// Summary card: the grid as a row of paint dots.
			const int32 Racers = Setup.LocalPlayers + Setup.Bots;
			const FVector2D CardPos(Frame.SafeMax.X - 600.0, Top);
			const FVector2D CardSize(600.0, 262.0);
			const FKartPainter Card = P.Faded(KartWidgets::Entrance(Frame, 0.2f));
			Card.Card(CardPos, CardSize, Color::Paper, Metric::RadiusLarge, 10.f, Color::White, 4.f);
			Card.Text(RacersText, Font(EFont::Display, 56.f), CardPos + FVector2D(36.0, 26.0), Color::Navy);
			Card.Text(SummaryText, Font(EFont::Body, 26.f), CardPos + FVector2D(38.0, 104.0), Color::NavyDim);
			static const FString DotTags[] = { TEXT("P1"), TEXT("P2"), TEXT("P3"), TEXT("P4") };
			static const FString Cpu(TEXT("CPU"));
			for (int32 i = 0; i < 8; ++i)
			{
				const FVector2D C(CardPos.X + 70.0 + i * 66.0, CardPos.Y + 210.0);
				if (i < Racers)
				{
					const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - Focus.ChangeAt) / 0.3), 3.f);
					const bool bNew = Focus.ChangedRow == 0 && i == Racers - 1 && Focus.ChangeDir > 0;
					const FKartPainter Dot = bNew ? Card.Transformed(C, Pop) : Card;
					const FLinearColor Paint = i < Setup.LocalPlayers ? FKartRoster::PaintColor(Setup.Racers[i].Paint) : Color::NavyDim;
					Dot.Circle(C, 26.f, Paint, Color::Navy, 3.f);
					Dot.Text(i < Setup.LocalPlayers ? DotTags[FMath::Clamp(i, 0, 3)] : Cpu, Font(EFont::Label, i < Setup.LocalPlayers ? 17.f : 13.f),
						C, Color::White, FVector2D(0.5, 0.5));
				}
				else
				{
					Card.Circle(C, 26.f, FLinearColor::Transparent, Color::WithAlpha(Color::NavyDim, 0.4f), 3.f);
				}
			}
		}

	private:
		int32 Grid() const
		{
			const AKartFrontEnd* FE = FrontEnd.Get();
			return FE && FE->GetTrack() ? FE->GetTrack()->Grid.Num() : 8;
		}

		void Change(int32 Row, int32 Dir)
		{
			UKartSessionSettings* S = Session(Host);
			if (!S || Row > 3)
			{
				return;
			}
			FKartRaceSetup& Setup = S->MutableSetup();
			Setup.Sanitize(Grid());
			const FKartRaceSetup Before = Setup;
			switch (Row)
			{
			case 0: Setup.Bots = FMath::Clamp(Setup.Bots + Dir, 0, Grid() - Setup.LocalPlayers); break;
			case 1: Setup.BotSkill = FMath::Clamp(Setup.BotSkill + Dir, 0, 2); break;
			case 2: Setup.Laps = FMath::Clamp(Setup.Laps + Dir, 1, FKartRaceSetup::MaxLaps); break;
			default: Setup.bViewEveryKart = !Setup.bViewEveryKart; break;
			}
			Setup.Sanitize(Grid());
			const bool bChanged = Setup.Bots != Before.Bots || Setup.BotSkill != Before.BotSkill || Setup.Laps != Before.Laps || Setup.bViewEveryKart != Before.bViewEveryKart;
			if (bChanged)
			{
				Focus.Changed(Row, Dir, NowSeconds());
			}
			Host->PlaySound(bChanged ? EKartUISound::Tick : EKartUISound::Error);
		}

		void Next()
		{
			Host->PlaySound(EKartUISound::Accept);
			Host->PushScreen(KartScreens::MakeTracks(FrontEnd.Get()));
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FFocus Focus;
		/** The rows' values and the summary card, rebuilt when the setup changes. */
		int32 ValuesKey = -1;
		FText Values[4];
		FText RacersText;
		FText SummaryText;
	};

	// ---------------------------------------------------------------------------------------------- Tracks

	class FTracksScreen final : public FKartScreen
	{
	public:
		explicit FTracksScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd)
		{
			Focus.Cursor.Count = FKartTrackCatalog::All().Num();
			Focus.Cursor.bWrap = false;
		}

		virtual FName GetId() const override { return TEXT("tracks"); }

		virtual void OnShown() override
		{
			if (const UKartSessionSettings* S = Session(Host))
			{
				Focus.Cursor.Set(FMath::Max(0, FKartTrackCatalog::IndexOf(S->GetSetup().TrackId)));
			}
			Focus.FocusAt = NowSeconds();
			bStarting = false;
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
		}

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Choose", "Choose") }, { EKartMenuAction::Accept, LOCTEXT("StartRace", "Start race") }, { EKartMenuAction::Back, LOCTEXT("Back", "Back") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (bStarting)
			{
				return true;
			}
			switch (Event.Action)
			{
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Select(Focus.Cursor.Index + (Event.Action == EKartMenuAction::Left ? -1 : 1));
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Start();
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (HitId < Focus.Cursor.Count)
			{
				Select(HitId);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId < Focus.Cursor.Count)
			{
				Select(HitId);
			}
			Start();
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, 300.0), Color::WithAlpha(Color::Night, 0.65f), Color::WithAlpha(Color::Night, 0.f));
			P.Gradient(FVector2D(0.0, Frame.Size.Y * 0.4), FVector2D(Frame.Size.X, Frame.Size.Y * 0.6), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.75f));
			KartWidgets::Header(P, Frame, LOCTEXT("TracksTitle", "Choose a track"), FText::GetEmpty(), Color::Mint);
			KartWidgets::Steps(P, Frame, SetupSteps(), 2);

			const TArray<FKartTrackInfo>& Tracks = FKartTrackCatalog::All();
			const FKartRaceSetup* Setup = Session(Host) ? &Session(Host)->GetSetup() : nullptr;
			const FVector2D CardSize(620.0, 590.0);
			const double Gap = 48.0;
			const double Total = Tracks.Num() * CardSize.X + (Tracks.Num() - 1) * Gap;
			const double Left = Frame.Size.X * 0.5 - Total * 0.5;
			const double Top = Frame.Size.Y * 0.5 - CardSize.Y * 0.5 - 30.0;
			{
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Tracks.Num(); ++i)
			{
				Group.Next();
				const FKartTrackInfo& Track = Tracks[i];
				const bool bFocused = i == Focus.Cursor.Index;
				const float In = KartWidgets::Entrance(Frame, 0.05f + i * 0.08f, 0.45f);
				const FVector2D Pos(Left + i * (CardSize.X + Gap) + KartWidgets::SlideX(Frame), Top + (1.0 - In) * 120.0);
				P.Hit(i, Pos, CardSize);
				const float Pop = bFocused ? 1.f + 0.04f * Ease::OutBack(static_cast<float>((Frame.Now - Focus.FocusAt) / 0.25), FKartAccessibility::ReduceMotion() ? 0.f : 3.f) : 0.95f;
				const FKartPainter Card = P.Transformed(Pos + CardSize * 0.5, Pop, bFocused ? -1.f : 0.f).Faded(In);
				if (bFocused)
				{
					Card.Box(Pos - FVector2D(10.0, 10.0), CardSize + FVector2D(20.0, 20.0), Color::WithAlpha(Track.Accent, 0.5f), Metric::RadiusLarge + 10.f);
				}
				Card.Card(Pos, CardSize, Color::Paper, Metric::RadiusLarge, 12.f, bFocused ? Color::White : FLinearColor::Transparent, 6.f);
				// The outline panel: the track drawn from its centreline, with a dot running a lap.
				const FVector2D MapPos = Pos + FVector2D(18.0, 18.0);
				const FVector2D MapSize(CardSize.X - 36.0, 330.0);
				Card.Gradient(MapPos, MapSize, Track.Accent, FMath::Lerp(Track.Accent, Color::Night, 0.4f), true, Metric::Radius);
				const TSharedPtr<const FKartTrackPreview> Preview = FKartTrackCatalog::GetPreview(Track.Id);
				if (Preview.IsValid())
				{
					KartWidgets::TrackOutline(Card, Preview->Outline, MapPos + FVector2D(40.0, 30.0), MapSize - FVector2D(80.0, 60.0), Color::White, 9.f,
						bFocused ? Frame.Now * 0.09 : -1.0, Color::Sun);
				}
				else
				{
					Card.Text(LOCTEXT("Loading", "..."), Font(EFont::Display, 60.f), MapPos + MapSize * 0.5, Color::White, FVector2D(0.5, 0.5));
				}
				Card.Text(Track.Name, Font(EFont::Display, 56.f), Pos + FVector2D(34.0, 364.0), Color::Navy);
				Card.TextWrapped(Track.Tagline, Font(EFont::Body, 21.f), Pos + FVector2D(36.0, 446.0), CardSize.X - 72.0, Color::NavyDim);
				// Stat chips.
				double ChipX = Pos.X + 34.0;
				auto Chip = [&](const FString& Text, const FLinearColor& Fill)
				{
					const FSlateFontInfo ChipFont = Font(EFont::Label, 19.f);
					const double W = FKartPainter::Measure(Text, ChipFont).X + 32.0;
					Card.Box(FVector2D(ChipX, Pos.Y + CardSize.Y - 68.0), FVector2D(W, 40.0), Fill, 20.f);
					Card.Text(Text, ChipFont, FVector2D(ChipX + W * 0.5, Pos.Y + CardSize.Y - 48.0), Color::TextOn(Fill), FVector2D(0.5, 0.5));
					ChipX += W + 12.0;
				};
				if (Preview.IsValid())
				{
					if (!LapChips.Contains(Track.Id))
					{
						LapChips.Add(Track.Id, FString::Printf(TEXT("%.2f km lap"), Preview->LapMetres / 1000.0));
					}
					Chip(LapChips[Track.Id], Color::PaperShade);
				}
				if (Setup)
				{
					if (Setup->Laps != LapsChipValue)
					{
						LapsChipValue = Setup->Laps;
						LapsChip = FString::Printf(TEXT("%d %s"), Setup->Laps, Setup->Laps == 1 ? TEXT("LAP") : TEXT("LAPS"));
					}
					Chip(LapsChip, Track.Accent);
				}
				if (!bFocused)
				{
					// The card not picked steps back: dimmed rather than see-through.
					Card.Box(Pos, CardSize, Color::WithAlpha(Color::Night, 0.28f), Metric::RadiusLarge);
				}
			}
			}
			// Arrows either side when there is more to pick.
			const FVector2D Mid(Frame.Size.X * 0.5, Top + CardSize.Y * 0.5);
			const double Bob = FMath::Sin(Frame.Now * 6.0) * 5.0;
			if (Focus.Cursor.Index > 0)
			{
				KartWidgets::Arrow(P, FVector2D(Left - 50.0 - Bob, Mid.Y), 24.f, false, Color::White);
			}
			if (Focus.Cursor.Index < Tracks.Num() - 1)
			{
				KartWidgets::Arrow(P, FVector2D(Left + Total + 50.0 + Bob, Mid.Y), 24.f, true, Color::White);
			}
			FItemState Start;
			Start.bFocused = true;
			Start.FocusAge = Frame.Now - Focus.FocusAt;
			KartWidgets::PrimaryButton(P.Faded(KartWidgets::Entrance(Frame, 0.35f)), FVector2D(Frame.Size.X * 0.5, Top + CardSize.Y + 66.0),
				LOCTEXT("StartRaceButton", "START RACE"), EKartMenuAction::Accept, Frame.Device, Start, 99);
		}

	private:
		void Select(int32 Index)
		{
			if (Focus.SetFocus(Index, NowSeconds()))
			{
				Host->PlaySound(EKartUISound::Move);
				if (UKartSessionSettings* S = Session(Host))
				{
					S->MutableSetup().TrackId = FKartTrackCatalog::All()[Focus.Cursor.Index].Id;
				}
			}
		}

		void Start()
		{
			if (bStarting)
			{
				return;
			}
			bStarting = true;
			if (UKartSessionSettings* S = Session(Host))
			{
				S->MutableSetup().TrackId = FKartTrackCatalog::All()[Focus.Cursor.Index].Id;
			}
			Host->PlaySound(EKartUISound::Go);
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->StartRace();
			}
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FFocus Focus;
		bool bStarting = false;
		/** Chip labels, built once per track / laps value. */
		TMap<FString, FString> LapChips;
		int32 LapsChipValue = -1;
		FString LapsChip;
	};

	// --------------------------------------------------------------------------------------------- Credits

	class FCreditsScreen final : public FKartScreen
	{
	public:
		virtual FName GetId() const override { return TEXT("credits"); }
		virtual TArray<FKartHint> GetHints() const override { return { { EKartMenuAction::Back, LOCTEXT("Back", "Back") } }; }
		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (Event.Action == EKartMenuAction::Accept)
			{
				Host->PlaySound(EKartUISound::Back);
				Host->PopScreen();
				return true;
			}
			return false;
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			LeftPanel(P, Frame, 1100.f, 0.82f);
			KartWidgets::Header(P, Frame, LOCTEXT("CreditsTitle", "Credits"), LOCTEXT("CreditsSub", "Movecade Kart Racer"), Color::Mint);
			struct FLine { FText Heading; FText Body; };
			const FLine Lines[] = {
				{ LOCTEXT("C1H", "Game"), LOCTEXT("C1B", "Movecade: the kart racer from the Movecade party arcade, rebuilt in Unreal Engine 5.") },
				{ LOCTEXT("C2H", "Engine"), LOCTEXT("C2B", "Unreal Engine 5.8 by Epic Games: Lumen, Nanite, Slate.") },
				{ LOCTEXT("C3H", "Type"), LOCTEXT("C3B", "Fredoka, Nunito and Lilita One, under the SIL Open Font License 1.1.") },
				{ LOCTEXT("C4H", "Sound"), LOCTEXT("C4B", "Kart sounds from the Movecade web game. Menu sounds and music synthesised in-house.") },
				{ LOCTEXT("C5H", "Art"), LOCTEXT("C5B", "Karts, drivers and tracks from the Movecade web game, rebuilt for Unreal.") },
				{ LOCTEXT("C6H", "Thanks"), LOCTEXT("C6B", "Everyone who raced the web build with a phone in hand.") },
			};
			double Y = Frame.SafeMin.Y + 190.0;
			for (int32 i = 0; i < UE_ARRAY_COUNT(Lines); ++i)
			{
				const float In = KartWidgets::Entrance(Frame, 0.08f + i * 0.07f);
				const FKartPainter L = P.Faded(In);
				const double X = Frame.SafeMin.X + KartWidgets::SlideX(Frame) - (1.0 - In) * 120.0;
				L.Text(Lines[i].Heading, Font(EFont::Label, 22.f), FVector2D(X, Y), Color::Sun);
				Y += 30.0 + L.TextWrapped(Lines[i].Body, Font(EFont::Body, 27.f), FVector2D(X, Y + 30.0), 940.0, Color::White).Y + 26.0;
			}
			const FVector2D StarAt(Frame.SafeMax.X - 260.0, Frame.Size.Y * 0.5);
			KartWidgets::Star(P, StarAt + FVector2D(0.0, 10.0), 160.f, 70.f, Color::Shadow, static_cast<float>(FMath::Sin(Frame.Now) * 8.0));
			KartWidgets::Star(P, StarAt, 160.f, 70.f, Color::Sun, static_cast<float>(FMath::Sin(Frame.Now) * 8.0));
			KartWidgets::Star(P, StarAt, 92.f, 40.f, Color::SunDeep, static_cast<float>(FMath::Sin(Frame.Now) * 8.0));
		}
	};

	// --------------------------------------------------------------------------------------------- Confirm

	class FConfirmScreen final : public FKartScreen
	{
	public:
		FConfirmScreen(const FText& InTitle, const FText& InBody, const FText& InYes, const FText& InNo, TFunction<void()> InOnYes, TFunction<void()> InOnNo)
			: Title(InTitle), Body(InBody), Yes(InYes), No(InNo), OnYes(MoveTemp(InOnYes)), OnNo(MoveTemp(InOnNo))
		{
			Focus.Cursor.Count = 2;
			Focus.Cursor.Index = 1;
		}

		virtual FName GetId() const override { return TEXT("confirm"); }
		virtual bool IsModal() const override { return true; }
		virtual void OnShown() override { Focus.FocusAt = NowSeconds(); }

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				if (Focus.Move(1, NowSeconds()))
				{
					Host->PlaySound(EKartUISound::Move);
				}
				return true;
			case EKartMenuAction::Accept:
				Choose(Focus.Cursor.Index == 0);
				return true;
			case EKartMenuAction::Back:
				Choose(false);
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (Focus.SetFocus(HitId, NowSeconds()))
			{
				Host->PlaySound(EKartUISound::Move);
			}
		}

		virtual void OnClick(int32 HitId) override { Choose(HitId == 0); }

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const float In = FKartAccessibility::ReduceMotion() ? Ease::OutCubic(static_cast<float>(Frame.Age / 0.2)) : Ease::OutBack(static_cast<float>(Frame.Age / 0.3), 1.8f);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.6f * FMath::Min(1.f, static_cast<float>(Frame.Age / 0.2))));
			const FVector2D Size(760.0, 360.0);
			const FVector2D Pos = Frame.Centre() - Size * 0.5;
			const FKartPainter D = P.Transformed(Frame.Centre(), 0.7f + 0.3f * In);
			D.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 12.f, Color::White, 5.f);
			D.Box(Pos + FVector2D(Size.X * 0.5 - 60.0, 14.0), FVector2D(120.0, 10.0), Color::Hot, 5.f);
			D.Text(Title, Font(EFont::Display, 54.f), Pos + FVector2D(Size.X * 0.5, 84.0), Color::Navy, FVector2D(0.5, 0.5));
			D.Text(Body, Font(EFont::Body, 26.f), Pos + FVector2D(Size.X * 0.5, 150.0), Color::NavyDim, FVector2D(0.5, 0.5));
			const FVector2D ButtonSize(310.0, 84.0);
			{
				const FKartPaintGroup Group(D);
				KartWidgets::Button(D, Pos + FVector2D(40.0, Size.Y - 124.0), ButtonSize, Yes, Focus.State(0, Frame.Now), Color::Hot, 0);
				Group.Next();
				KartWidgets::Button(D, Pos + FVector2D(Size.X - 40.0 - ButtonSize.X, Size.Y - 124.0), ButtonSize, No, Focus.State(1, Frame.Now), Color::Mint, 1);
			}
			// Hit regions from a transformed painter are skipped; register the settled ones.
			P.Hit(0, Pos + FVector2D(40.0, Size.Y - 124.0), ButtonSize);
			P.Hit(1, Pos + FVector2D(Size.X - 40.0 - ButtonSize.X, Size.Y - 124.0), ButtonSize);
		}

	private:
		void Choose(bool bYes)
		{
			Host->PlaySound(bYes ? EKartUISound::Accept : EKartUISound::Back);
			TFunction<void()> Callback = bYes ? OnYes : OnNo;
			Host->PopScreen();
			if (Callback)
			{
				Callback();
			}
		}

		FText Title, Body, Yes, No;
		TFunction<void()> OnYes;
		TFunction<void()> OnNo;
		FFocus Focus;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeTitle(AKartFrontEnd* FrontEnd) { return MakeShared<FTitleScreen>(FrontEnd); }
	TSharedRef<FKartScreen> MakeMainMenu(AKartFrontEnd* FrontEnd) { return MakeShared<FMainMenuScreen>(FrontEnd); }
	TSharedRef<FKartScreen> MakeArcade(AKartFrontEnd* FrontEnd) { return MakeShared<FArcadeScreen>(FrontEnd); }
	TSharedRef<FKartScreen> MakeRacers(AKartFrontEnd* FrontEnd) { return MakeShared<FRacersScreen>(FrontEnd); }
	TSharedRef<FKartScreen> MakeRules(AKartFrontEnd* FrontEnd) { return MakeShared<FRulesScreen>(FrontEnd); }
	TSharedRef<FKartScreen> MakeTracks(AKartFrontEnd* FrontEnd) { return MakeShared<FTracksScreen>(FrontEnd); }
	TSharedRef<FKartScreen> MakeCredits() { return MakeShared<FCreditsScreen>(); }
	TSharedRef<FKartScreen> MakeConfirm(const FText& Title, const FText& Body, const FText& Yes, const FText& No, TFunction<void()> OnYes, TFunction<void()> OnNo)
	{
		return MakeShared<FConfirmScreen>(Title, Body, Yes, No, MoveTemp(OnYes), MoveTemp(OnNo));
	}
}

#undef LOCTEXT_NAMESPACE

#include "UI/Screens/KartScreens.h"

#include "Framework/Application/SlateApplication.h"
#include "UI/KartAccessibility.h"
#include "UI/KartFrontEnd.h"
#include "UI/KartPlayerPrefs.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "KartWelcome"

using namespace KartUI;

namespace
{
	/** The three cards: their spacing, largest size, where they start, and the room kept under them for the button. */
	constexpr double CardGap = 36.0;
	constexpr double MaxCardSize = 520.0;
	constexpr double CardsTop = 170.0;
	constexpr double CardRoomBelow = 380.0;

	/** A phone with a QR code on its screen, tilting like a steering wheel. */
	void PhoneIcon(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		const float Tilt = FMath::Sin(static_cast<float>(Now) * 1.6f) * 14.f * FKartAccessibility::MotionScale();
		const FKartPainter T = P.Transformed(C, 1.f, Tilt);
		const FVector2D Size(0.62 * S, 1.1 * S);
		const FVector2D Pos = C - Size * 0.5;
		T.Box(Pos + FVector2D(0.0, 0.06 * S), Size, Color::Shadow, 0.12f * S);
		T.Box(Pos, Size, Color::Navy, 0.12f * S);
		const FVector2D ScreenPos = Pos + FVector2D(0.05 * S, 0.12 * S);
		const FVector2D ScreenSize(Size.X - 0.1 * S, Size.Y - 0.26 * S);
		T.Box(ScreenPos, ScreenSize, Color::White, 0.05f * S);
		// A 5 x 5 code with the three finder squares.
		static const uint32 Bits = 0x1D5A6B3u;
		const double Cell = ScreenSize.X * 0.7 / 5.0;
		const FVector2D Grid = ScreenPos + FVector2D(ScreenSize.X * 0.15, ScreenSize.Y * 0.5 - Cell * 2.5);
		for (int32 Row = 0; Row < 5; ++Row)
		{
			for (int32 Col = 0; Col < 5; ++Col)
			{
				const bool bFinder = (Row < 2 && Col < 2) || (Row < 2 && Col > 2) || (Row > 2 && Col < 2);
				const bool bOn = bFinder ? !((Row == 1 || Row == 3) && (Col == 1 || Col == 3)) : ((Bits >> (Row * 5 + Col)) & 1u) != 0;
				if (bOn)
				{
					T.Box(Grid + FVector2D(Col * Cell, Row * Cell), FVector2D(Cell, Cell), Color::Navy);
				}
			}
		}
		T.Circle(FVector2D(C.X, Pos.Y + Size.Y - 0.07 * S), 0.035f * S, Color::White);
	}

	/** A chunky gamepad with a lit d-pad and four face buttons. */
	void PadIcon(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		const double Bob = FMath::Sin(Now * 2.2) * 0.03 * S * FKartAccessibility::MotionScale();
		const FVector2D Body(1.3 * S, 0.62 * S);
		const FVector2D Pos = C - Body * 0.5 + FVector2D(0.0, Bob);
		P.Circle(Pos + FVector2D(0.28 * S, 0.5 * S), 0.34f * S, Color::Navy);
		P.Circle(Pos + FVector2D(Body.X - 0.28 * S, 0.5 * S), 0.34f * S, Color::Navy);
		P.Box(Pos, Body, Color::Navy, 0.3f * S);
		const FVector2D Pad = Pos + FVector2D(0.32 * S, 0.3 * S);
		P.Box(Pad - FVector2D(0.05 * S, 0.15 * S), FVector2D(0.1 * S, 0.3 * S), Color::White, 0.02f * S);
		P.Box(Pad - FVector2D(0.15 * S, 0.05 * S), FVector2D(0.3 * S, 0.1 * S), Color::White, 0.02f * S);
		const FVector2D Face = Pos + FVector2D(Body.X - 0.32 * S, 0.3 * S);
		const FLinearColor Buttons[] = { Color::Sun, Color::Hot, Color::Mint, Color::Sky };
		const FVector2D Dirs[] = { FVector2D(0, -1), FVector2D(1, 0), FVector2D(0, 1), FVector2D(-1, 0) };
		const int32 Lit = static_cast<int32>(FMath::Fmod(Now * 1.5, 4.0));
		for (int32 i = 0; i < 4; ++i)
		{
			P.Circle(Face + Dirs[i] * 0.12 * S, (i == Lit ? 0.07f : 0.055f) * S, Buttons[i]);
		}
	}

	/** A friendly house bot: a screen-faced head with an antenna. */
	void BotIcon(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		const FVector2D Head(1.0 * S, 0.78 * S);
		const FVector2D Pos = C - Head * 0.5 + FVector2D(0.0, 0.1 * S);
		P.Box(FVector2D(C.X - 0.03 * S, Pos.Y - 0.26 * S), FVector2D(0.06 * S, 0.26 * S), Color::Navy);
		const float Blink = FMath::Sin(static_cast<float>(Now) * 4.f) > 0.f ? 1.f : 0.6f;
		P.Circle(FVector2D(C.X, Pos.Y - 0.3 * S), 0.08f * S, Color::WithAlpha(Color::Hot, Blink));
		P.Box(Pos, Head, Color::Navy, 0.2f * S);
		P.Box(Pos + FVector2D(0.1 * S, 0.1 * S), Head - FVector2D(0.2 * S, 0.2 * S), Color::Sky, 0.12f * S);
		const bool bWink = FMath::Fmod(Now, 3.0) < 0.18;
		P.Circle(Pos + FVector2D(0.34 * S, 0.34 * S), 0.08f * S, Color::White);
		if (bWink)
		{
			P.Box(Pos + FVector2D(0.58 * S, 0.32 * S), FVector2D(0.16 * S, 0.04 * S), Color::White, 0.02f * S);
		}
		else
		{
			P.Circle(Pos + FVector2D(0.66 * S, 0.34 * S), 0.08f * S, Color::White);
		}
		P.Box(Pos + FVector2D(0.3 * S, 0.52 * S), FVector2D(0.4 * S, 0.06 * S), Color::White, 0.03f * S);
	}

	/** The one-time "how it works" card between the title and the main menu. */
	class FWelcomeScreen final : public FKartScreen
	{
	public:
		explicit FWelcomeScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd) {}

		virtual FName GetId() const override { return TEXT("welcome"); }
		virtual void OnShown() override { bDone = false; }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Accept, LOCTEXT("Continue", "Let's play") }, { EKartMenuAction::Back, LOCTEXT("Back", "Back") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (Event.Action == EKartMenuAction::Accept || Event.Action == EKartMenuAction::Start)
			{
				Continue();
				return true;
			}
			return false;
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId == PlayHit)
			{
				Continue();
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.62f * KartWidgets::Entrance(Frame, 0.f, 0.3f)));
			KartWidgets::Header(P, Frame, LOCTEXT("Title", "How Movecade works"), LOCTEXT("Sub", "Three ways to play. You can mix them in the same night."), Color::Sun);

			struct FPanel { FText Title; FText Body; FLinearColor Accent; };
			static const FPanel Panels[] = {
				{ LOCTEXT("PhonesTitle", "Phones are controllers"),
					LOCTEXT("PhonesBody", "Pick Party Mode and scan the code on the TV with any phone. Tilt to steer, swipe and tap to play. Nothing to install."), Color::Sky },
				{ LOCTEXT("PadsTitle", "Or grab a pad"),
					LOCTEXT("PadsBody", "Race and Arcade run on one screen with a keyboard, a mouse and up to four gamepads. Press Start to join."), Color::Hot },
				{ LOCTEXT("BotsTitle", "House players fill in"),
					LOCTEXT("BotsBody", "Short on players? Bots take the empty seats in every race and party game, from easy to hard."), Color::Mint },
			};
			const double Gap = CardGap;
			const double CardW = FMath::Min(MaxCardSize, (Frame.SafeSize().X - Gap * 2.0) / 3.0);
			const double CardH = FMath::Min(MaxCardSize, Frame.SafeSize().Y - CardRoomBelow);
			const double Total = CardW * 3.0 + Gap * 2.0;
			const double Left = Frame.Size.X * 0.5 - Total * 0.5;
			const double Top = Frame.SafeMin.Y + CardsTop;
			const FSlateFontInfo TitleFont = Font(EFont::Title, 36.f);
			const FSlateFontInfo BodyFont = Font(EFont::Body, 23.f);
			for (int32 i = 0; i < UE_ARRAY_COUNT(Panels); ++i)
			{
				const float In = KartWidgets::Entrance(Frame, 0.12f + i * 0.1f, 0.45f);
				const FVector2D Pos(Left + i * (CardW + Gap) + KartWidgets::SlideX(Frame), Top + (1.0 - In) * 80.0);
				const FVector2D Size(CardW, CardH);
				const FKartPainter Card = P.Transformed(Pos + Size * 0.5, 0.9f + 0.1f * In).Faded(In);
				Card.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 10.f, Color::White, 5.f);
				const FVector2D ArtPos = Pos + FVector2D(16.0, 16.0);
				const FVector2D ArtSize(CardW - 32.0, CardH * 0.42);
				Card.Gradient(ArtPos, ArtSize, Panels[i].Accent, FMath::Lerp(Panels[i].Accent, Color::Night, 0.35f), true, Metric::Radius);
				// The step number on the art.
				Card.Circle(ArtPos + FVector2D(34.0, 34.0), 22.f, Color::White, Color::Navy, 3.f);
				static const FString StepNumbers[] = { TEXT("1"), TEXT("2"), TEXT("3") };
				Card.Text(StepNumbers[i], Font(EFont::Display, 26.f), ArtPos + FVector2D(34.0, 35.0), Color::Navy, FVector2D(0.5, 0.5));
				const FVector2D Art = ArtPos + ArtSize * 0.5;
				const float S = static_cast<float>(FMath::Min(ArtSize.Y * 0.62, ArtSize.X * 0.4));
				switch (i)
				{
				case 0: PhoneIcon(Card, Art, S, Frame.Now); break;
				case 1: PadIcon(Card, Art, S, Frame.Now); break;
				default: BotIcon(Card, Art, S, Frame.Now); break;
				}
				const double TextX = Pos.X + 30.0;
				const double TitleY = ArtPos.Y + ArtSize.Y + 20.0;
				// The body follows the title, which may wrap on narrow cards.
				const FVector2D TitleSize = Card.TextWrapped(Panels[i].Title, TitleFont, FVector2D(TextX, TitleY), CardW - 60.0, Color::Navy);
				Card.TextWrapped(Panels[i].Body, BodyFont, FVector2D(TextX, TitleY + TitleSize.Y + 10.0), CardW - 60.0, Color::NavyDim);
			}

			KartWidgets::FItemState Play;
			Play.bFocused = true;
			Play.FocusAge = Frame.Age;
			KartWidgets::PrimaryButton(P.Faded(KartWidgets::Entrance(Frame, 0.5f)), FVector2D(Frame.Size.X * 0.5, Top + CardH + 80.0),
				LOCTEXT("PlayButton", "LET'S PLAY"), EKartMenuAction::Accept, Frame.Device, Play, PlayHit);
		}

	private:
		static constexpr int32 PlayHit = 1;

		void Continue()
		{
			if (bDone)
			{
				return;
			}
			bDone = true;
			UKartPlayerPrefs* Prefs = GetMutableDefault<UKartPlayerPrefs>();
			Prefs->bSeenHowItWorks = true;
			Prefs->Save();
			Host->PlaySound(EKartUISound::Accept);
			// Replace, so Back from the menu returns to the title rather than to this card.
			Host->ReplaceScreen(KartScreens::MakeMainMenu(FrontEnd.Get()));
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		bool bDone = false;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeWelcome(AKartFrontEnd* FrontEnd) { return MakeShared<FWelcomeScreen>(FrontEnd); }

	bool ShouldShowWelcome()
	{
		return !GetDefault<UKartPlayerPrefs>()->bSeenHowItWorks;
	}
}

#undef LOCTEXT_NAMESPACE

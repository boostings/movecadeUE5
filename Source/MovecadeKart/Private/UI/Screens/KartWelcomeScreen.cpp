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
	constexpr float DimSeconds = 0.3f;
	constexpr float DimAlpha = 0.62f;
	constexpr float PlayInAt = 0.5f;
	constexpr double PlayButtonDrop = 80.0;

	/** Inside a panel: the art band with its step badge and icon, then the title and body text. */
	namespace PanelArt
	{
		constexpr float InAt = 0.12f;
		constexpr float Stagger = 0.1f;
		constexpr float InSeconds = 0.45f;
		constexpr double Rise = 80.0;
		constexpr float PopFrom = 0.9f;
		constexpr float PopGrowth = 0.1f;
		constexpr float Shadow = 10.f;
		constexpr float Rim = 5.f;
		constexpr double ArtInset = 16.0;
		/** The art band takes this fraction of the card's height. */
		constexpr double ArtHeight = 0.42;
		constexpr float ArtShade = 0.35f;
		const FVector2D StepBadge(34.0, 34.0);
		/** The number sits a unit lower than the badge's centre, for the display face's metrics. */
		const FVector2D StepNumberPos(34.0, 35.0);
		constexpr float StepRadius = 22.f;
		constexpr float StepOutline = 3.f;
		constexpr float StepSize = 26.f;
		/** The icon's size: the smaller of these fractions of the band's height and width. */
		constexpr double IconHeight = 0.62;
		constexpr double IconWidth = 0.4;
		constexpr double TextInset = 30.0;
		constexpr double TitleGap = 20.0;
		constexpr double BodyGap = 10.0;
		constexpr float TitleSize = 36.f;
		constexpr float BodySize = 23.f;
	}

	// The panel icons are drawn in icon units: multiples of S around C, each from its own table below.

	/** Phones: a phone with a 5 x 5 code on its screen, tilting like a steering wheel. */
	namespace PhoneArt
	{
		constexpr float TiltSpeed = 1.6f;
		constexpr float TiltDegrees = 14.f;
		constexpr double Width = 0.62;
		constexpr double Height = 1.1;
		constexpr float Round = 0.12f;
		constexpr double ShadowDrop = 0.06;
		constexpr double ScreenInsetX = 0.05;
		constexpr double ScreenInsetTop = 0.12;
		/** The screen is the body less this much across and down (the bezel and the home button). */
		constexpr double ScreenTrimX = 0.1;
		constexpr double ScreenTrimY = 0.26;
		constexpr float ScreenRound = 0.05f;
		constexpr int32 CodeCells = 5;
		/** The code fills this much of the screen's width, centred. */
		constexpr double CodeWidth = 0.7;
		constexpr double CodeInset = 0.15;
		/** The code's data bits, row by row from the top-left (the finder squares are drawn by rule). */
		constexpr uint32 CodeBits = 0x1D5A6B3u;
		constexpr double HomeButtonUp = 0.07;
		constexpr float HomeButtonRadius = 0.035f;
	}

	/** Pads: a chunky gamepad with a d-pad and four face buttons, one lit in turn. */
	namespace PadArt
	{
		constexpr double BobSpeed = 2.2;
		constexpr double BobHeight = 0.03;
		constexpr double BodyWidth = 1.3;
		constexpr double BodyHeight = 0.62;
		constexpr float BodyRound = 0.3f;
		/** The grips: a disc under each end of the body. */
		constexpr double GripInset = 0.28;
		constexpr double GripY = 0.5;
		constexpr float GripRadius = 0.34f;
		constexpr double DPadInset = 0.32;
		constexpr double ControlsY = 0.3;
		constexpr double DPadArmLength = 0.3;
		constexpr double DPadArmWidth = 0.1;
		constexpr float DPadRound = 0.02f;
		constexpr double FaceSpacing = 0.12;
		constexpr float FaceRadius = 0.055f;
		constexpr float LitFaceRadius = 0.07f;
		constexpr double LightSpeed = 1.5;
	}

	/** House players: a friendly bot, a screen-faced head with an antenna that blinks and a wink now and then. */
	namespace BotArt
	{
		constexpr double HeadWidth = 1.0;
		constexpr double HeadHeight = 0.78;
		constexpr double HeadDrop = 0.1;
		constexpr float HeadRound = 0.2f;
		constexpr double AntennaWidth = 0.06;
		constexpr double AntennaHeight = 0.26;
		constexpr double LightUp = 0.3;
		constexpr float LightRadius = 0.08f;
		constexpr float BlinkSpeed = 4.f;
		constexpr double FaceInset = 0.1;
		constexpr float FaceRound = 0.12f;
		constexpr double EyeY = 0.34;
		constexpr double LeftEyeX = 0.34;
		constexpr double RightEyeX = 0.66;
		constexpr float EyeRadius = 0.08f;
		/** The wink: a dash where the right eye was, for a moment every few seconds. */
		constexpr double WinkEvery = 3.0;
		constexpr double WinkSeconds = 0.18;
		constexpr double WinkX = 0.58;
		constexpr double WinkY = 0.32;
		constexpr double WinkWidth = 0.16;
		constexpr double WinkHeight = 0.04;
		constexpr float WinkRound = 0.02f;
		constexpr double MouthX = 0.3;
		constexpr double MouthY = 0.52;
		constexpr double MouthWidth = 0.4;
		constexpr double MouthHeight = 0.06;
		constexpr float MouthRound = 0.03f;
	}

	void PhoneIcon(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace PhoneArt;
		const float Tilt = FMath::Sin(static_cast<float>(Now) * TiltSpeed) * TiltDegrees * FKartAccessibility::MotionScale();
		const FKartPainter T = P.Transformed(C, 1.f, Tilt);
		const FVector2D Size(Width * S, Height * S);
		const FVector2D Pos = C - Size * 0.5;
		T.Box(Pos + FVector2D(0.0, ShadowDrop * S), Size, Color::Shadow, Round * S);
		T.Box(Pos, Size, Color::Navy, Round * S);
		const FVector2D ScreenPos = Pos + FVector2D(ScreenInsetX * S, ScreenInsetTop * S);
		const FVector2D ScreenSize(Size.X - ScreenTrimX * S, Size.Y - ScreenTrimY * S);
		T.Box(ScreenPos, ScreenSize, Color::White, ScreenRound * S);
		// The code with its three finder squares (top-left, top-right, bottom-left).
		const double Cell = ScreenSize.X * CodeWidth / CodeCells;
		const FVector2D Grid = ScreenPos + FVector2D(ScreenSize.X * CodeInset, ScreenSize.Y * 0.5 - Cell * (CodeCells * 0.5));
		for (int32 Row = 0; Row < CodeCells; ++Row)
		{
			for (int32 Col = 0; Col < CodeCells; ++Col)
			{
				const bool bFinder = (Row < 2 && Col < 2) || (Row < 2 && Col > 2) || (Row > 2 && Col < 2);
				const bool bOn = bFinder ? !((Row == 1 || Row == 3) && (Col == 1 || Col == 3)) : ((CodeBits >> (Row * CodeCells + Col)) & 1u) != 0;
				if (bOn)
				{
					T.Box(Grid + FVector2D(Col * Cell, Row * Cell), FVector2D(Cell, Cell), Color::Navy);
				}
			}
		}
		T.Circle(FVector2D(C.X, Pos.Y + Size.Y - HomeButtonUp * S), HomeButtonRadius * S, Color::White);
	}

	void PadIcon(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace PadArt;
		const double Bob = FMath::Sin(Now * BobSpeed) * BobHeight * S * FKartAccessibility::MotionScale();
		const FVector2D Body(BodyWidth * S, BodyHeight * S);
		const FVector2D Pos = C - Body * 0.5 + FVector2D(0.0, Bob);
		P.Circle(Pos + FVector2D(GripInset * S, GripY * S), GripRadius * S, Color::Navy);
		P.Circle(Pos + FVector2D(Body.X - GripInset * S, GripY * S), GripRadius * S, Color::Navy);
		P.Box(Pos, Body, Color::Navy, BodyRound * S);
		const FVector2D Pad = Pos + FVector2D(DPadInset * S, ControlsY * S);
		P.Box(Pad - FVector2D(DPadArmWidth * 0.5 * S, DPadArmLength * 0.5 * S), FVector2D(DPadArmWidth * S, DPadArmLength * S), Color::White, DPadRound * S);
		P.Box(Pad - FVector2D(DPadArmLength * 0.5 * S, DPadArmWidth * 0.5 * S), FVector2D(DPadArmLength * S, DPadArmWidth * S), Color::White, DPadRound * S);
		const FVector2D Face = Pos + FVector2D(Body.X - DPadInset * S, ControlsY * S);
		static const FLinearColor Buttons[] = { Color::Sun, Color::Hot, Color::Mint, Color::Sky };
		static const FVector2D Dirs[] = { FVector2D(0, -1), FVector2D(1, 0), FVector2D(0, 1), FVector2D(-1, 0) };
		const int32 Lit = static_cast<int32>(FMath::Fmod(Now * LightSpeed, 4.0));
		for (int32 i = 0; i < 4; ++i)
		{
			P.Circle(Face + Dirs[i] * FaceSpacing * S, (i == Lit ? LitFaceRadius : FaceRadius) * S, Buttons[i]);
		}
	}

	void BotIcon(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace BotArt;
		const FVector2D Head(HeadWidth * S, HeadHeight * S);
		const FVector2D Pos = C - Head * 0.5 + FVector2D(0.0, HeadDrop * S);
		P.Box(FVector2D(C.X - AntennaWidth * 0.5 * S, Pos.Y - AntennaHeight * S), FVector2D(AntennaWidth * S, AntennaHeight * S), Color::Navy);
		const float Blink = FMath::Sin(static_cast<float>(Now) * BlinkSpeed) > 0.f ? 1.f : 0.6f;
		P.Circle(FVector2D(C.X, Pos.Y - LightUp * S), LightRadius * S, Color::WithAlpha(Color::Hot, Blink));
		P.Box(Pos, Head, Color::Navy, HeadRound * S);
		P.Box(Pos + FVector2D(FaceInset * S, FaceInset * S), Head - FVector2D(2.0 * FaceInset * S, 2.0 * FaceInset * S), Color::Sky, FaceRound * S);
		const bool bWink = FMath::Fmod(Now, WinkEvery) < WinkSeconds;
		P.Circle(Pos + FVector2D(LeftEyeX * S, EyeY * S), EyeRadius * S, Color::White);
		if (bWink)
		{
			P.Box(Pos + FVector2D(WinkX * S, WinkY * S), FVector2D(WinkWidth * S, WinkHeight * S), Color::White, WinkRound * S);
		}
		else
		{
			P.Circle(Pos + FVector2D(RightEyeX * S, EyeY * S), EyeRadius * S, Color::White);
		}
		P.Box(Pos + FVector2D(MouthX * S, MouthY * S), FVector2D(MouthWidth * S, MouthHeight * S), Color::White, MouthRound * S);
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
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, DimAlpha * KartWidgets::Entrance(Frame, 0.f, DimSeconds)));
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
			const FSlateFontInfo TitleFont = Font(EFont::Title, PanelArt::TitleSize);
			const FSlateFontInfo BodyFont = Font(EFont::Body, PanelArt::BodySize);
			for (int32 i = 0; i < UE_ARRAY_COUNT(Panels); ++i)
			{
				using namespace PanelArt;
				const float In = KartWidgets::Entrance(Frame, InAt + i * Stagger, InSeconds);
				const FVector2D Pos(Left + i * (CardW + Gap) + KartWidgets::SlideX(Frame), Top + (1.0 - In) * Rise);
				const FVector2D Size(CardW, CardH);
				const FKartPainter Card = P.Transformed(Pos + Size * 0.5, PopFrom + PopGrowth * In).Faded(In);
				Card.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, Shadow, Color::White, Rim);
				const FVector2D ArtPos = Pos + FVector2D(ArtInset, ArtInset);
				const FVector2D ArtSize(CardW - 2.0 * ArtInset, CardH * ArtHeight);
				Card.Gradient(ArtPos, ArtSize, Panels[i].Accent, FMath::Lerp(Panels[i].Accent, Color::Night, ArtShade), true, Metric::Radius);
				// The step number on the art.
				static const FString StepNumbers[] = { TEXT("1"), TEXT("2"), TEXT("3") };
				Card.Circle(ArtPos + StepBadge, StepRadius, Color::White, Color::Navy, StepOutline);
				Card.Text(StepNumbers[i], Font(EFont::Display, StepSize), ArtPos + StepNumberPos, Color::Navy, FVector2D(0.5, 0.5));
				const FVector2D Art = ArtPos + ArtSize * 0.5;
				const float S = static_cast<float>(FMath::Min(ArtSize.Y * IconHeight, ArtSize.X * IconWidth));
				switch (i)
				{
				case 0: PhoneIcon(Card, Art, S, Frame.Now); break;
				case 1: PadIcon(Card, Art, S, Frame.Now); break;
				default: BotIcon(Card, Art, S, Frame.Now); break;
				}
				const double TextX = Pos.X + TextInset;
				const double TitleY = ArtPos.Y + ArtSize.Y + TitleGap;
				// The body follows the title, which may wrap on narrow cards.
				const FVector2D TitleExtent = Card.TextWrapped(Panels[i].Title, TitleFont, FVector2D(TextX, TitleY), CardW - 2.0 * TextInset, Color::Navy);
				Card.TextWrapped(Panels[i].Body, BodyFont, FVector2D(TextX, TitleY + TitleExtent.Y + BodyGap), CardW - 2.0 * TextInset, Color::NavyDim);
			}

			KartWidgets::FItemState Play;
			Play.bFocused = true;
			Play.FocusAge = Frame.Age;
			KartWidgets::PrimaryButton(P.Faded(KartWidgets::Entrance(Frame, PlayInAt)), FVector2D(Frame.Size.X * 0.5, Top + CardH + PlayButtonDrop),
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

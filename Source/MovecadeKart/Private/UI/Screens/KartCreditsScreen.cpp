#include "UI/Screens/KartScreens.h"

#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;
using namespace KartScreens::Shared;

namespace
{
	constexpr float PanelWidth = 1100.f;
	constexpr float PanelAlpha = 0.82f;
	constexpr double LinesTop = 190.0;
	constexpr double BodyWidth = 940.0;
	constexpr double HeadingToBody = 30.0;
	constexpr double LineGap = 26.0;
	/** The star on the right: its distance in from the safe edge and its two sizes. */
	constexpr double StarInset = 260.0;
	constexpr float StarOuter = 160.f;
	constexpr float StarInner = 70.f;
	constexpr float CoreOuter = 92.f;
	constexpr float CoreInner = 40.f;
	constexpr double StarShadowDrop = 10.0;
	constexpr double StarRockDegrees = 8.0;
	/** The lines' staggered entrance and slide. */
	constexpr float LineInAt = 0.08f;
	constexpr float LineStagger = 0.07f;
	constexpr double LineSlide = 120.0;
	constexpr float HeadingSize = 22.f;
	constexpr float BodySize = 27.f;

	struct FCreditLine
	{
		FText Heading;
		FText Body;
	};

	const TArray<FCreditLine>& CreditLines()
	{
		static const TArray<FCreditLine> Lines = {
			{ LOCTEXT("C1H", "Game"), LOCTEXT("C1B", "Movecade: the kart racer from the Movecade party arcade, rebuilt in Unreal Engine 5.") },
			{ LOCTEXT("C2H", "Engine"), LOCTEXT("C2B", "Unreal Engine 5.8 by Epic Games: Lumen, Nanite, Slate.") },
			{ LOCTEXT("C3H", "Type"), LOCTEXT("C3B", "Fredoka, Nunito and Lilita One, under the SIL Open Font License 1.1.") },
			{ LOCTEXT("C4H", "Sound"), LOCTEXT("C4B", "Kart sounds from the Movecade web game. Menu sounds and music synthesised in-house.") },
			{ LOCTEXT("C5H", "Art"), LOCTEXT("C5B", "Karts, drivers and tracks from the Movecade web game, rebuilt for Unreal.") },
			{ LOCTEXT("C6H", "Thanks"), LOCTEXT("C6B", "Everyone who raced the web build with a phone in hand.") },
		};
		return Lines;
	}

	/** Who made the game and with what; Accept or Back returns. */
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
			LeftPanel(P, Frame, PanelWidth, PanelAlpha);
			KartWidgets::Header(P, Frame, LOCTEXT("CreditsTitle", "Credits"), LOCTEXT("CreditsSub", "Movecade Kart Racer"), Color::Mint);
			const TArray<FCreditLine>& Lines = CreditLines();
			double Y = Frame.SafeMin.Y + LinesTop;
			for (int32 i = 0; i < Lines.Num(); ++i)
			{
				const float In = KartWidgets::Entrance(Frame, LineInAt + i * LineStagger);
				const FKartPainter L = P.Faded(In);
				const double X = Frame.SafeMin.X + KartWidgets::SlideX(Frame) - (1.0 - In) * LineSlide;
				L.Text(Lines[i].Heading, Font(EFont::Label, HeadingSize), FVector2D(X, Y), Color::Sun);
				Y += HeadingToBody + L.TextWrapped(Lines[i].Body, Font(EFont::Body, BodySize), FVector2D(X, Y + HeadingToBody), BodyWidth, Color::White).Y + LineGap;
			}
			const FVector2D StarAt(Frame.SafeMax.X - StarInset, Frame.Size.Y * 0.5);
			const float Spin = static_cast<float>(FMath::Sin(Frame.Now) * StarRockDegrees);
			KartWidgets::Star(P, StarAt + FVector2D(0.0, StarShadowDrop), StarOuter, StarInner, Color::Shadow, Spin);
			KartWidgets::Star(P, StarAt, StarOuter, StarInner, Color::Sun, Spin);
			KartWidgets::Star(P, StarAt, CoreOuter, CoreInner, Color::SunDeep, Spin);
		}
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeCredits() { return MakeShared<FCreditsScreen>(); }
}

#undef LOCTEXT_NAMESPACE

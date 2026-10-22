#include "UI/Screens/KartScreens.h"

#include "UI/KartAccessibility.h"
#include "UI/KartFrontEnd.h"
#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;
using namespace KartScreens::Shared;

namespace
{
	/** Where the logo and the "press start" pill sit, as fractions of the canvas. */
	constexpr double LogoY = 0.3;
	constexpr double PressY = 0.74;

	/** The pill: when it appears, its type, and its press flash before the next screen. */
	constexpr float PressInAt = 1.1f;
	constexpr float PressInSeconds = 0.5f;
	constexpr float PressFontSize = 52.f;
	constexpr float PressGlyphHeight = 50.f;
	constexpr double PressHeight = 92.0;
	constexpr double PressFlashSeconds = 0.3;
	/** Seconds from the press to the next screen (the flash plays out first). */
	constexpr double AdvanceSeconds = 0.32;
	constexpr float FooterInAt = 1.4f;

	/** The attract screen: the logo over the flyover, "press start", and the way out (Back asks to quit). */
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
				Host->PushScreen(KartScreens::MakeQuitConfirm(FrontEnd.Get()));
				return true;
			}
			return false;
		}

		virtual void OnClick(int32 HitId) override { Press(); }

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			// Soft vignettes keep the type readable over the flyover.
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, Frame.Size.Y * 0.55), Color::WithAlpha(Color::Night, 0.55f), Color::WithAlpha(Color::Night, 0.f));
			P.Gradient(FVector2D(0.0, Frame.Size.Y * 0.62), FVector2D(Frame.Size.X, Frame.Size.Y * 0.38), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.75f));
			P.Hit(0, FVector2D::ZeroVector, Frame.Size);

			Logo(P, FVector2D(Frame.Size.X * 0.5, Frame.Size.Y * LogoY), 1.f, Frame.Age, Frame.Now);
			PaintPressStart(P, Frame);
			PaintFooter(P, Frame);

			if (PressedAt >= 0.0 && Frame.Now - PressedAt > AdvanceSeconds && !bAdvanced)
			{
				bAdvanced = true;
				Host->PushScreen(KartScreens::ShouldShowWelcome() ? KartScreens::MakeWelcome(FrontEnd.Get()) : KartScreens::MakeMainMenu(FrontEnd.Get()));
			}
		}

	private:
		/** "Press Start": a pulsing pill with the right glyph; it flashes white when pressed. */
		void PaintPressStart(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			const float In = KartWidgets::Entrance(Frame, PressInAt, PressInSeconds);
			if (In <= 0.f)
			{
				return;
			}
			const double Now = Frame.Now;
			const FVector2D C(Frame.Size.X * 0.5, Frame.Size.Y * PressY);
			const bool bPressed = PressedAt >= 0.0;
			const float Motion = FKartAccessibility::MotionScale();
			const float Pulse = bPressed ? 1.f + 0.15f * Motion * Ease::Pulse(static_cast<float>((Now - PressedAt) / PressFlashSeconds))
				: 1.f + 0.035f * Motion * FMath::Sin(static_cast<float>(Now) * 3.4f);
			const FKartPainter B = P.Transformed(C, Pulse * In).Faded(In);
			const bool bPad = Frame.Device == EKartInputDevice::Gamepad;
			const FText Label = bPad ? LOCTEXT("PressStart", "PRESS START") : LOCTEXT("PressEnter", "PRESS ENTER");
			const FSlateFontInfo LabelFont = Font(EFont::Display, PressFontSize, 4.f);
			const EKartMenuAction Action = bPad ? EKartMenuAction::Start : EKartMenuAction::Accept;
			const float W = static_cast<float>(FKartPainter::Measure(Label.ToString(), LabelFont).X) + KartWidgets::GlyphWidth(Action, Frame.Device, PressGlyphHeight) + 100.f;
			const FVector2D Pos = C - FVector2D(W * 0.5, PressHeight * 0.5);
			B.Box(Pos + FVector2D(0.0, 8.0), FVector2D(W, PressHeight), Color::Shadow, PressHeight * 0.5);
			B.Box(Pos, FVector2D(W, PressHeight), bPressed ? Color::White : Color::WithAlpha(Color::Night, 0.82f), PressHeight * 0.5, Color::Sun, 5.f);
			float X = static_cast<float>(Pos.X) + 36.f;
			X += KartWidgets::Glyph(B, Action, Frame.Device, FVector2D(X, C.Y), PressGlyphHeight) + 24.f;
			B.Text(Label, LabelFont, FVector2D(X, C.Y), bPressed ? Color::Navy : Color::Sun, FVector2D(0.0, 0.5), bPressed ? 0.f : 4.f);
		}

		void PaintFooter(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			const FKartPainter Foot = P.Faded(KartWidgets::Entrance(Frame, FooterInAt, 0.6f) * 0.8f);
			const FSlateFontInfo FootFont = Font(EFont::Body, 20.f, 1.f);
			Foot.Text(LOCTEXT("Footer", "A Movecade game  ·  Unreal Engine 5"), FootFont, FVector2D(Frame.SafeMin.X, Frame.SafeMax.Y), Color::White, FVector2D(0.0, 1.0));
			Foot.Text(LOCTEXT("PhonesHint", "Phones can be controllers: try Party Mode"), FootFont, FVector2D(Frame.SafeMax.X, Frame.SafeMax.Y), Color::White, FVector2D(1.0, 1.0));
		}

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
		/** When Accept was pressed (-1 until then). */
		double PressedAt = -1.0;
		bool bAdvanced = false;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeTitle(AKartFrontEnd* FrontEnd) { return MakeShared<FTitleScreen>(FrontEnd); }
}

#undef LOCTEXT_NAMESPACE

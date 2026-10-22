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

	/** The vignettes over the flyover: a shade fading down from the top, and one rising from the bottom. */
	namespace VignetteArt
	{
		constexpr double TopHeight = 0.55;
		constexpr float TopAlpha = 0.55f;
		constexpr double BottomTop = 0.62;
		constexpr double BottomHeight = 0.38;
		constexpr float BottomAlpha = 0.75f;
	}

	/** The "PRESS START" pill: when it appears, its type, its idle pulse and its press flash. */
	namespace PressArt
	{
		constexpr float InAt = 1.1f;
		constexpr float InSeconds = 0.5f;
		constexpr float FontSize = 52.f;
		constexpr float FontOutline = 4.f;
		constexpr float TextShadow = 4.f;
		constexpr float GlyphHeight = 50.f;
		constexpr double Height = 92.0;
		constexpr float Padding = 100.f;
		constexpr double ShadowDrop = 8.0;
		constexpr float FillAlpha = 0.82f;
		constexpr float Rim = 5.f;
		constexpr float GlyphX = 36.f;
		constexpr float GlyphGap = 24.f;
		constexpr float IdlePulse = 0.035f;
		constexpr float IdlePulseSpeed = 3.4f;
		constexpr float FlashPulse = 0.15f;
		constexpr double FlashSeconds = 0.3;
	}

	/** Seconds from the press to the next screen (the flash plays out first). */
	constexpr double AdvanceSeconds = 0.32;

	/** The two footer lines in the safe area's bottom corners. */
	namespace FooterArt
	{
		constexpr float InAt = 1.4f;
		constexpr float InSeconds = 0.6f;
		constexpr float Alpha = 0.8f;
		constexpr float FontSize = 20.f;
		constexpr float FontOutline = 1.f;
	}

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
			using namespace VignetteArt;
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, Frame.Size.Y * TopHeight), Color::WithAlpha(Color::Night, TopAlpha), Color::WithAlpha(Color::Night, 0.f));
			P.Gradient(FVector2D(0.0, Frame.Size.Y * BottomTop), FVector2D(Frame.Size.X, Frame.Size.Y * BottomHeight), Color::WithAlpha(Color::Night, 0.f),
				Color::WithAlpha(Color::Night, BottomAlpha));
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
			using namespace PressArt;
			const float In = KartWidgets::Entrance(Frame, InAt, InSeconds);
			if (In <= 0.f)
			{
				return;
			}
			const double Now = Frame.Now;
			const FVector2D C(Frame.Size.X * 0.5, Frame.Size.Y * PressY);
			const bool bPressed = PressedAt >= 0.0;
			const float Motion = FKartAccessibility::MotionScale();
			const float Pulse = bPressed ? 1.f + FlashPulse * Motion * Ease::Pulse(static_cast<float>((Now - PressedAt) / FlashSeconds))
				: 1.f + IdlePulse * Motion * FMath::Sin(static_cast<float>(Now) * IdlePulseSpeed);
			const FKartPainter B = P.Transformed(C, Pulse * In).Faded(In);
			const bool bPad = Frame.Device == EKartInputDevice::Gamepad;
			const FText Label = bPad ? LOCTEXT("PressStart", "PRESS START") : LOCTEXT("PressEnter", "PRESS ENTER");
			const FSlateFontInfo LabelFont = Font(EFont::Display, FontSize, FontOutline);
			const EKartMenuAction Action = bPad ? EKartMenuAction::Start : EKartMenuAction::Accept;
			const float W = static_cast<float>(FKartPainter::Measure(Label.ToString(), LabelFont).X) + KartWidgets::GlyphWidth(Action, Frame.Device, GlyphHeight) + Padding;
			const FVector2D Pos = C - FVector2D(W * 0.5, Height * 0.5);
			B.Box(Pos + FVector2D(0.0, ShadowDrop), FVector2D(W, Height), Color::Shadow, Height * 0.5);
			B.Box(Pos, FVector2D(W, Height), bPressed ? Color::White : Color::WithAlpha(Color::Night, FillAlpha), Height * 0.5, Color::Sun, Rim);
			float X = static_cast<float>(Pos.X) + GlyphX;
			X += KartWidgets::Glyph(B, Action, Frame.Device, FVector2D(X, C.Y), GlyphHeight) + GlyphGap;
			B.Text(Label, LabelFont, FVector2D(X, C.Y), bPressed ? Color::Navy : Color::Sun, FVector2D(0.0, 0.5), bPressed ? 0.f : TextShadow);
		}

		void PaintFooter(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			using namespace FooterArt;
			const FKartPainter Foot = P.Faded(KartWidgets::Entrance(Frame, InAt, InSeconds) * Alpha);
			const FSlateFontInfo FootFont = Font(EFont::Body, FontSize, FontOutline);
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

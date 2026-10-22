#include "UI/Screens/KartScreens.h"

#include "UI/KartAccessibility.h"
#include "UI/Screens/KartScreenCommon.h"

using namespace KartUI;
using namespace KartScreens::Shared;

namespace
{
	const FVector2D DialogSize(760.0, 360.0);
	const FVector2D ButtonSize(310.0, 84.0);
	constexpr double ButtonInset = 40.0;
	constexpr double ButtonsFromBottom = 124.0;
	/** The screen dims behind the dialog over this long. */
	constexpr double DimSeconds = 0.2;
	constexpr float DimAlpha = 0.6f;
	/** The dialog pops from 70 % (an eased grow without the overshoot under Reduce motion). */
	constexpr double PopSeconds = 0.3;
	constexpr float PopOvershoot = 1.8f;
	constexpr double ReducedPopSeconds = 0.2;
	constexpr float PopFrom = 0.7f;
	constexpr float PopGrowth = 0.3f;
	constexpr float Shadow = 12.f;
	constexpr float Rim = 5.f;
	/** The accent tab along the top edge. */
	const FVector2D TabSize(120.0, 10.0);
	constexpr double TabY = 14.0;
	constexpr float TabRound = 5.f;
	constexpr float TitleSize = 54.f;
	constexpr double TitleY = 84.0;
	constexpr float BodySize = 26.f;
	constexpr double BodyY = 150.0;

	enum EConfirmButton : int32 { YesButton, NoButton };

	/** A yes/no dialog over the screen beneath it. No has focus first; Back is No; each callback runs after it closes. */
	class FConfirmScreen final : public FKartScreen
	{
	public:
		FConfirmScreen(const FText& InTitle, const FText& InBody, const FText& InYes, const FText& InNo, TFunction<void()> InOnYes, TFunction<void()> InOnNo)
			: Title(InTitle), Body(InBody), Yes(InYes), No(InNo), OnYes(MoveTemp(InOnYes)), OnNo(MoveTemp(InOnNo))
		{
			Focus.Cursor.Count = 2;
			Focus.Cursor.Index = NoButton;
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
				Focus.Step(1, *Host); // two buttons: any direction flips between them
				return true;
			case EKartMenuAction::Accept:
				Choose(Focus.Cursor.Index == YesButton);
				return true;
			case EKartMenuAction::Back:
				Choose(false);
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override { Focus.HoverTo(HitId, *Host); }
		virtual void OnClick(int32 HitId) override { Choose(HitId == YesButton); }

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const float In = FKartAccessibility::ReduceMotion() ? Ease::OutCubic(static_cast<float>(Frame.Age / ReducedPopSeconds))
				: Ease::OutBack(static_cast<float>(Frame.Age / PopSeconds), PopOvershoot);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, DimAlpha * FMath::Min(1.f, static_cast<float>(Frame.Age / DimSeconds))));
			const FVector2D Pos = Frame.Centre() - DialogSize * 0.5;
			const FKartPainter D = P.Transformed(Frame.Centre(), PopFrom + PopGrowth * In);
			D.Card(Pos, DialogSize, Color::Paper, Metric::RadiusLarge, Shadow, Color::White, Rim);
			D.Box(Pos + FVector2D(DialogSize.X * 0.5 - TabSize.X * 0.5, TabY), TabSize, Color::Hot, TabRound);
			D.Text(Title, Font(EFont::Display, TitleSize), Pos + FVector2D(DialogSize.X * 0.5, TitleY), Color::Navy, FVector2D(0.5, 0.5));
			D.Text(Body, Font(EFont::Body, BodySize), Pos + FVector2D(DialogSize.X * 0.5, BodyY), Color::NavyDim, FVector2D(0.5, 0.5));
			const FVector2D YesPos = Pos + FVector2D(ButtonInset, DialogSize.Y - ButtonsFromBottom);
			const FVector2D NoPos = Pos + FVector2D(DialogSize.X - ButtonInset - ButtonSize.X, DialogSize.Y - ButtonsFromBottom);
			{
				const FKartPaintGroup Group(D);
				Group.Next();
				KartWidgets::Button(D, YesPos, ButtonSize, Yes, Focus.State(YesButton, Frame.Now), Color::Hot, YesButton);
				Group.Next();
				KartWidgets::Button(D, NoPos, ButtonSize, No, Focus.State(NoButton, Frame.Now), Color::Mint, NoButton);
			}
			// Hit regions from a transformed painter are skipped; register the settled ones.
			P.Hit(YesButton, YesPos, ButtonSize);
			P.Hit(NoButton, NoPos, ButtonSize);
		}

	private:
		void Choose(bool bYes)
		{
			Host->PlaySound(bYes ? EKartUISound::Accept : EKartUISound::Back);
			// Copied first: popping can release the stack's last reference to this dialog and its callbacks.
			const TFunction<void()> Callback = bYes ? OnYes : OnNo;
			Host->PopScreen();
			if (Callback)
			{
				Callback();
			}
		}

		FText Title;
		FText Body;
		FText Yes;
		FText No;
		TFunction<void()> OnYes;
		TFunction<void()> OnNo;
		FListFocus Focus;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeConfirm(const FText& Title, const FText& Body, const FText& Yes, const FText& No, TFunction<void()> OnYes, TFunction<void()> OnNo)
	{
		return MakeShared<FConfirmScreen>(Title, Body, Yes, No, MoveTemp(OnYes), MoveTemp(OnNo));
	}
}

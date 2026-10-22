#include "UI/Screens/KartScreens.h"

#include "UI/KartFrontEnd.h"
#include "UI/Screens/KartMenuIcons.h"
#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;
using namespace KartScreens::Shared;

namespace
{
	enum class EMainMenuItem : int32 { Race, Arcade, Party, Settings, Credits, Store, Quit, Count };
	constexpr int32 NumItems = static_cast<int32>(EMainMenuItem::Count);

	/** The button column: its panel, button size and spacing, and the staggered slide-in. */
	constexpr float PanelWidth = 720.f;
	const FVector2D ButtonSize(560.0, 88.0);
	constexpr double ButtonGap = 18.0;
	constexpr double ColumnDrop = 50.0;
	constexpr float ButtonInAt = 0.05f;
	constexpr float ButtonStagger = 0.05f;
	constexpr float ButtonInSeconds = 0.4f;
	constexpr double ButtonSlide = 260.0;

	/** The small logo over the column. */
	constexpr double LogoX = 300.0;
	constexpr double LogoY = 70.0;
	constexpr float LogoScale = 0.42f;
	/** An age well past the logo's drop-in, so it is always fully built. */
	constexpr double LogoSettledAge = 10.0;

	/** The focused entry's card on the right. */
	const FVector2D CardSize(520.0, 480.0);
	constexpr double CardDrop = 30.0;
	constexpr double CardInset = 18.0;
	constexpr double CardArtHeight = 270.0;
	constexpr float CardInAt = 0.2f;
	constexpr double CardPopSeconds = 0.35;
	constexpr float CardPopOvershoot = 2.f;
	/** A new focus pops the card from 90 % with a small tilt. */
	constexpr float CardPopFrom = 0.9f;
	constexpr float CardPopGrowth = 0.1f;
	constexpr float CardPopTilt = -4.f;
	constexpr float CardShadow = 10.f;
	constexpr float CardRim = 5.f;
	constexpr double CardIconY = 153.0;
	constexpr float CardIconSize = 110.f;
	constexpr float CardNameSize = 46.f;
	const FVector2D CardNamePos(36.0, 306.0);
	constexpr float CardDetailSize = 22.f;
	const FVector2D CardDetailPos(38.0, 380.0);
	/** The light stripes sweeping across the card's art. */
	constexpr int32 Stripes = 6;
	constexpr double StripeSpeed = 40.0;
	constexpr double StripeSpacing = 90.0;
	constexpr double StripeWidth = 40.0;
	constexpr float StripeAlpha = 0.12f;
	/** Stripes narrower than this at the art's edges are skipped. */
	constexpr double MinStripe = 1.0;

	struct FMenuEntry
	{
		FText Name;
		FText Detail;
		FLinearColor Accent;
		KartMenuIcons::FPaintIcon Icon;
	};

	/** The entries in EMainMenuItem order, built once (the menu paints them every frame). */
	const TArray<FMenuEntry>& MenuEntries()
	{
		static const TArray<FMenuEntry> Entries = {
			{ LOCTEXT("Race", "Race"), LOCTEXT("RaceDetail", "Up to four players on one screen, with bots."), Color::Hot, &KartMenuIcons::RaceFlag },
			{ LOCTEXT("Arcade", "Arcade"), LOCTEXT("ArcadeDetail", "Bowling, darts, Samurai Slice and Sky Stacker, with friends or the house."), Color::Sun,
				&KartMenuIcons::BowlingPins },
			{ LOCTEXT("Party", "Party Mode"), LOCTEXT("PartyDetail", "Phones are the controllers. Scan, tilt, race."), Color::Sky, &KartMenuIcons::PartyPhones },
			{ LOCTEXT("Settings", "Settings"), LOCTEXT("SettingsDetail", "Graphics, sound and controls."), Color::Grape, &KartMenuIcons::SettingsGear },
			{ LOCTEXT("Credits", "Credits"), LOCTEXT("CreditsDetail", "Who made this, and with what."), Color::Mint, &KartMenuIcons::CreditsStar },
			{ LOCTEXT("Store", "Store"), LOCTEXT("StoreDetail", "Kart paints, driver looks and party-game skins. Cosmetic only, plus free unlocks."), Color::Gold,
				&KartScreens::PaintStoreIcon },
			{ LOCTEXT("Quit", "Quit"), LOCTEXT("QuitDetail", "Back to the desktop."), Color::NavyDim, &KartMenuIcons::QuitDoor },
		};
		check(Entries.Num() == NumItems);
		return Entries;
	}

	/** The main menu: a column of entries and a card that previews the focused one. */
	class FMainMenuScreen final : public FKartScreen
	{
	public:
		explicit FMainMenuScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd)
		{
			Focus.Cursor.Count = NumItems;
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
				Focus.Step(Event.Action == EKartMenuAction::Up ? -1 : 1, *Host);
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Activate(static_cast<EMainMenuItem>(Focus.Cursor.Index));
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override { Focus.HoverTo(HitId, *Host); }

		virtual void OnClick(int32 HitId) override
		{
			Focus.SetFocus(HitId, NowSeconds());
			Activate(static_cast<EMainMenuItem>(HitId));
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			LeftPanel(P, Frame, PanelWidth);
			const FVector2D LogoAt(Frame.SafeMin.X + LogoX, Frame.SafeMin.Y + LogoY);
			Logo(P.Transformed(LogoAt, 1.f), LogoAt, LogoScale, LogoSettledAge, Frame.Now);
			PaintButtons(P, Frame);
			PaintCard(P, Frame);
		}

	private:
		void PaintButtons(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			const TArray<FMenuEntry>& Entries = MenuEntries();
			const double Top = Frame.Size.Y * 0.5 - (NumItems * ButtonSize.Y + (NumItems - 1) * ButtonGap) * 0.5 + ColumnDrop;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < NumItems; ++i)
			{
				Group.Next();
				const float In = KartWidgets::Entrance(Frame, ButtonInAt + i * ButtonStagger, ButtonInSeconds);
				const FVector2D Pos(Frame.SafeMin.X - (1.0 - In) * ButtonSlide + KartWidgets::SlideX(Frame), Top + i * (ButtonSize.Y + ButtonGap));
				KartWidgets::Button(P.Faded(In), Pos, ButtonSize, Entries[i].Name, Focus.State(i, Frame.Now), Entries[i].Accent, i);
			}
		}

		/** The focused entry's card: a big icon over its accent colour, its name and what it does. */
		void PaintCard(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			const FMenuEntry& Entry = MenuEntries()[Focus.Cursor.Index];
			const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - Focus.FocusAt) / CardPopSeconds), CardPopOvershoot);
			const FVector2D CardPos(Frame.SafeMax.X - CardSize.X, Frame.Size.Y * 0.5 - CardSize.Y * 0.5 + CardDrop);
			const FKartPainter Card = P.Transformed(CardPos + CardSize * 0.5, CardPopFrom + CardPopGrowth * Pop, (1.f - Pop) * CardPopTilt)
				.Faded(KartWidgets::Entrance(Frame, CardInAt));
			Card.Card(CardPos, CardSize, Color::Paper, Metric::RadiusLarge, CardShadow, Color::White, CardRim);
			Card.Box(CardPos + FVector2D(CardInset, CardInset), FVector2D(CardSize.X - 2.0 * CardInset, CardArtHeight), Entry.Accent, Metric::Radius);
			const double ArtLeft = CardPos.X + CardInset;
			const double ArtRight = CardPos.X + CardSize.X - CardInset;
			for (int32 Stripe = 0; Stripe < Stripes; ++Stripe)
			{
				const double X = ArtLeft + FMath::Fmod(Frame.Now * StripeSpeed + Stripe * StripeSpacing, CardSize.X + StripeSpacing) - StripeSpacing;
				const double X0 = FMath::Clamp(X, ArtLeft, ArtRight);
				const double X1 = FMath::Clamp(X + StripeWidth, ArtLeft, ArtRight);
				if (X1 > X0 + MinStripe)
				{
					Card.Box(FVector2D(X0, CardPos.Y + CardInset), FVector2D(X1 - X0, CardArtHeight), Color::WithAlpha(Color::White, StripeAlpha));
				}
			}
			Entry.Icon(Card, CardPos + FVector2D(CardSize.X * 0.5, CardIconY), CardIconSize, Frame.Now);
			Card.Text(Entry.Name, Font(EFont::Title, CardNameSize), CardPos + CardNamePos, Color::Navy);
			Card.TextWrapped(Entry.Detail, Font(EFont::Body, CardDetailSize), CardPos + CardDetailPos, CardSize.X - 2.0 * CardDetailPos.X, Color::NavyDim);
		}

		void Activate(EMainMenuItem Item)
		{
			AKartFrontEnd* FE = FrontEnd.Get();
			if (Item == EMainMenuItem::Party)
			{
				Host->PlaySound(EKartUISound::Go);
				if (FE)
				{
					FE->StartParty();
				}
				return;
			}
			Host->PlaySound(EKartUISound::Accept);
			switch (Item)
			{
			case EMainMenuItem::Race: Host->PushScreen(KartScreens::MakeRacers(FE)); break;
			case EMainMenuItem::Arcade: Host->PushScreen(KartScreens::MakeArcade(FE)); break;
			case EMainMenuItem::Settings: Host->PushScreen(KartScreens::MakeSettings(0)); break;
			case EMainMenuItem::Credits: Host->PushScreen(KartScreens::MakeCredits()); break;
			case EMainMenuItem::Store: Host->PushScreen(KartScreens::MakeStore(FE)); break;
			default: Host->PushScreen(KartScreens::MakeQuitConfirm(FE)); break;
			}
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FListFocus Focus;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeMainMenu(AKartFrontEnd* FrontEnd) { return MakeShared<FMainMenuScreen>(FrontEnd); }

	TSharedRef<FKartScreen> MakeQuitConfirm(AKartFrontEnd* FrontEnd)
	{
		return MakeConfirm(LOCTEXT("QuitTitle", "Quit the game?"), LOCTEXT("QuitBody", "See you on the track soon."), LOCTEXT("QuitYes", "Quit"), LOCTEXT("QuitNo", "Stay"),
			[WeakFE = TWeakObjectPtr<AKartFrontEnd>(FrontEnd)]()
			{
				if (AKartFrontEnd* FE = WeakFE.Get())
				{
					FE->QuitGame();
				}
			});
	}
}

#undef LOCTEXT_NAMESPACE

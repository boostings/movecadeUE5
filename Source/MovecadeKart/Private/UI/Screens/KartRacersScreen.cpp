#include "UI/Screens/KartScreens.h"

#include "Store/KartCosmetics.h"
#include "UI/KartAccessibility.h"
#include "UI/KartFrontEnd.h"
#include "UI/KartRoster.h"
#include "UI/KartSessionSettings.h"
#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;
using namespace KartScreens::Shared;
using KartWidgets::FItemState;

namespace
{
	constexpr int32 MaxSeats = FKartRaceSetup::MaxLocalPlayers;

	/** A player card's rows: driver, outfit, vehicle, paint. */
	enum ERacerRow : int32 { DriverRow, OutfitRow, VehicleRow, PaintRow, NumRows };
	constexpr int32 ReadyHit = 100;

	/** The cards along the bottom: their size and spacing. */
	constexpr double CardGap = 24.0;
	constexpr double MaxCardWidth = 470.0;
	constexpr double CardHeight = 486.0;
	constexpr double CardsLift = 64.0;
	/** Inside a card: the header band, the option rows and the ready bar. */
	constexpr double CardInset = 14.0;
	constexpr double BandHeight = 92.0;
	constexpr double RowsTop = 122.0;
	constexpr double RowHeight = 58.0;
	constexpr double RowGap = 10.0;
	constexpr double ReadyHeight = 54.0;

	/** The beat between everyone being ready and the rules screen. */
	constexpr double AdvanceSeconds = 0.45;
	constexpr double JoinPopSeconds = 0.4;
	constexpr double StampSeconds = 0.3;

	/** An option row sized for the player cards (KartWidgets::OptionRow is too wide for four across). */
	void CompactRow(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label, const FText& Value,
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
		const bool bSwatch = Swatch.A > 0.f;
		double TextX = VC.X;
		if (bSwatch)
		{
			V.Circle(FVector2D(L + 30.0, VC.Y), 13.f, Swatch, Color::Navy, 2.f);
			TextX += 14.0;
		}
		// Long names ("Classic Jersey") shrink to stay between the arrows.
		const double Room = (Rt - L) - 36.0 - (bSwatch ? 28.0 : 0.0);
		FSlateFontInfo ValueFont = Font(EFont::Button, 23.f);
		const double Width = FKartPainter::Measure(Value.ToString(), ValueFont).X;
		if (Width > Room)
		{
			ValueFont = Font(EFont::Button, FMath::Max(16.f, FMath::FloorToFloat(23.f * static_cast<float>(Room / Width))));
		}
		V.Text(Value, ValueFont, FVector2D(TextX, VC.Y), Color::Navy, FVector2D(0.5, 0.5));
	}

	/**
	 * Step 1 of the race setup: a card per local player (driver, outfit, vehicle, paint) over the showroom
	 * karts. Players 2..4 join with Start on their own pad; the rules screen follows once everyone is ready.
	 */
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
			if (UKartSessionSettings* S = SessionOf(Host))
			{
				S->MutableSetup().Sanitize();
				for (int32 i = 0; i < MaxSeats; ++i)
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
			const int32 Player = FMath::Clamp(Event.UserIndex, 0, MaxSeats - 1);
			FSlot& Slot = Slots[Player];
			if (!Slot.bJoined)
			{
				return HandleUnjoined(Player, Event);
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
				Slot.Focus.Step(Event.Action == EKartMenuAction::Up ? -1 : 1, *Host);
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
				Slot.ReadyAt = NowSeconds();
				Host->PlaySound(EKartUISound::Accept);
				return true;
			case EKartMenuAction::Back:
				return HandleLeave(Player);
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			const int32 Row = KartWidgets::HitBase(HitId);
			if (Row < NumRows && !Slots[0].bReady)
			{
				Slots[0].Focus.HoverTo(Row, *Host);
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
			P.Gradient(FVector2D(0.0, Frame.Size.Y * 0.45), FVector2D(Frame.Size.X, Frame.Size.Y * 0.55), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.7f));
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, 260.0), Color::WithAlpha(Color::Night, 0.6f), Color::WithAlpha(Color::Night, 0.f));
			KartWidgets::Header(P, Frame, LOCTEXT("RacersTitle", "Choose your racer"),
				LOCTEXT("RacersSub", "Players 2 to 4: press Start on your controller to join."), Color::Hot);
			KartWidgets::Steps(P, Frame, RaceSetupSteps(), 0);
			PaintCards(P, Frame);
			UpdateAdvance(Frame.Now);
		}

	private:
		struct FSlot
		{
			bool bJoined = false;
			bool bReady = false;
			double JoinedAt = -10.0;
			double ReadyAt = -10.0;
			FListFocus Focus;
		};

		/** Players 2..4 join with Start (or A), in pad order: seat N is driven by gamepad N. */
		bool HandleUnjoined(int32 Player, const FKartMenuEvent& Event)
		{
			const bool bJoinKey = Event.Action == EKartMenuAction::Start || Event.Action == EKartMenuAction::Accept;
			if (bJoinKey && Player != NumJoined())
			{
				Host->PlaySound(EKartUISound::Error);
			}
			else if (bJoinKey)
			{
				FSlot& Slot = Slots[Player];
				Slot.bJoined = true;
				Slot.bReady = false;
				Slot.JoinedAt = NowSeconds();
				Slot.Focus.Cursor.Count = NumRows;
				Host->PlaySound(EKartUISound::Start);
				SyncShowroom();
			}
			return Event.Action != EKartMenuAction::None;
		}

		/** Back on a card: the last guest seat leaves; P1 backs out to the menu. */
		bool HandleLeave(int32 Player)
		{
			if (Player == 0)
			{
				SaveJoined();
				return false;
			}
			if (Player != NumJoined() - 1)
			{
				Host->PlaySound(EKartUISound::Error); // the last seat leaves first
				return true;
			}
			Slots[Player].bJoined = false;
			Host->PlaySound(EKartUISound::Back);
			SyncShowroom();
			return true;
		}

		/** Joined players' cards, plus one "press start" card for the next free seat. */
		void PaintCards(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			const int32 Joined = NumJoined();
			const int32 Cards = FMath::Min(MaxSeats, Joined + 1);
			const double CardW = FMath::Min(MaxCardWidth, (Frame.SafeSize().X - CardGap * 3) / 4.0);
			const double Total = Cards * CardW + (Cards - 1) * CardGap;
			const double Left = Frame.Size.X * 0.5 - Total * 0.5;
			const double Top = Frame.SafeMax.Y - CardHeight - CardsLift;
			int32 Card = 0;
			for (int32 i = 0; i < MaxSeats && Card < Cards; ++i)
			{
				const FVector2D Pos(Left + Card * (CardW + CardGap) + KartWidgets::SlideX(Frame), Top);
				if (Slots[i].bJoined)
				{
					PaintCard(P, Frame, i, Pos, FVector2D(CardW, CardHeight));
					++Card;
				}
				else if (Card == Joined)
				{
					PaintJoinCard(P, Frame, i, Pos, FVector2D(CardW, CardHeight));
					++Card;
				}
			}
		}

		/** Once everyone is ready, a short beat, then on to the rules. */
		void UpdateAdvance(double Now)
		{
			if (!bAdvancing && AllReady())
			{
				bAdvancing = true;
				AdvanceAt = Now + AdvanceSeconds;
			}
			if (bAdvancing && Now >= AdvanceAt)
			{
				bAdvancing = false;
				SaveJoined();
				Host->PlaySound(EKartUISound::Whoosh);
				Host->PushScreen(KartScreens::MakeRules(FrontEnd.Get()));
			}
		}

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

		/** The player's choice in the session setup; only call with a session (check()ed). */
		FKartRacerChoice& Choice(int32 Player)
		{
			UKartSessionSettings* S = SessionOf(Host);
			check(S);
			FKartRaceSetup& Setup = S->MutableSetup();
			Setup.Sanitize();
			return Setup.Racers[Player];
		}

		void Change(int32 Player, int32 Row, int32 Dir)
		{
			if (!SessionOf(Host))
			{
				return;
			}
			FKartRacerChoice& C = Choice(Player);
			switch (Row)
			{
			case DriverRow: C.Character = FKartMenuCursor::Cycle(C.Character, Dir, FKartRoster::Characters().Num()); break;
			case OutfitRow: C.Outfit = static_cast<EKartOutfit>(FKartMenuCursor::Cycle(static_cast<int32>(C.Outfit), Dir, FKartRoster::NumOutfits())); break;
			case VehicleRow: C.Vehicle = static_cast<EKartVehicle>(FKartMenuCursor::Cycle(static_cast<int32>(C.Vehicle), Dir, FKartRoster::NumVehicles())); break;
			default: C.Paint = FKartMenuCursor::Cycle(C.Paint, Dir, FKartRoster::Paints().Num()); break;
			}
			Slots[Player].Focus.Changed(Row, Dir, NowSeconds());
			Host->PlaySound(EKartUISound::Tick);
			SyncShowroom();
		}

		void Randomise(int32 Player)
		{
			if (!SessionOf(Host))
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
			if (UKartSessionSettings* S = SessionOf(Host))
			{
				// Seats join in pad order, so the joined slots are always 0..N-1.
				FKartRaceSetup& Setup = S->MutableSetup();
				Setup.LocalPlayers = FMath::Max(1, NumJoined());
				Setup.Sanitize();
			}
		}

		/** The showroom karts on the grid wear each joined player's picks (and their store cosmetics). */
		void SyncShowroom()
		{
			AKartFrontEnd* FE = FrontEnd.Get();
			if (!FE || !SessionOf(Host))
			{
				return;
			}
			TArray<FKartLook> Looks;
			for (int32 i = 0; i < MaxSeats; ++i)
			{
				if (Slots[i].bJoined)
				{
					Looks.Add(FKartRoster::MakeLook(Choice(i), FString::Printf(TEXT("P%d"), i + 1)));
					FKartCosmetics::ApplyToLook(Looks.Last(), i);
				}
			}
			FE->SetShowroom(Looks);
		}

		void PaintCard(const FKartPainter& P, const FKartScreenFrame& Frame, int32 Player, const FVector2D& Pos, const FVector2D& Size) const
		{
			const FSlot& Slot = Slots[Player];
			const UKartSessionSettings* Settings = SessionOf(Host);
			const FKartRacerChoice C = FKartRoster::Sanitize(Settings && Settings->GetSetup().Racers.IsValidIndex(Player) ? Settings->GetSetup().Racers[Player] : FKartRoster::DefaultChoice(Player));
			const FLinearColor Paint = FKartCosmetics::PaintColor(Player, FKartRoster::PaintColor(C.Paint));
			const float Join = Ease::OutBack(static_cast<float>((Frame.Now - Slot.JoinedAt) / JoinPopSeconds), 2.f);
			const float In = FMath::Min(KartWidgets::Entrance(Frame, 0.1f + Player * 0.06f), FMath::Max(Join, 0.f));
			const FKartPainter Card = P.Transformed(Pos + Size * 0.5, 0.85f + 0.15f * In).Faded(In);
			Card.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 10.f, Slot.bReady ? FKartAccessibility::Good() : Color::White, 5.f);
			// Header band in the kart's paint with the player tag and the driver's name.
			const FText& Driver = FKartRoster::Characters()[C.Character].Name;
			Card.Box(Pos + FVector2D(CardInset, CardInset), FVector2D(Size.X - 2.0 * CardInset, BandHeight), Paint, Metric::Radius);
			Card.Circle(Pos + FVector2D(66.0, 60.0), 34.f, Color::White, Color::Navy, 3.f);
			Card.Text(SeatTag(FMath::Clamp(Player, 0, MaxSeats - 1)), Font(EFont::Display, 30.f), Pos + FVector2D(66.0, 60.0), Color::Navy, FVector2D(0.5, 0.5));
			Card.Text(Driver, Font(EFont::Display, 46.f, 3.f), Pos + FVector2D(116.0, 60.0), Color::White, FVector2D(0.0, 0.5), 3.f);

			static const FText Labels[NumRows] = { LOCTEXT("Driver", "Driver"), LOCTEXT("Outfit", "Outfit"), LOCTEXT("Vehicle", "Vehicle"), LOCTEXT("Paint", "Paint") };
			const FText Values[NumRows] = { Driver, FKartRoster::OutfitName(C.Outfit), FKartRoster::VehicleName(C.Vehicle),
				FKartCosmetics::PaintName(Player, FKartRoster::Paints()[C.Paint].Name) };
			{
				const FKartPaintGroup Group(Card);
				for (int32 Row = 0; Row < NumRows; ++Row)
				{
					Group.Next();
					FItemState State = Slot.Focus.State(Row, Frame.Now);
					State.bFocused = State.bFocused && !Slot.bReady;
					const FVector2D RowPos(Pos.X + 16.0, Pos.Y + RowsTop + Row * (RowHeight + RowGap));
					// Only P1's card is mouse-driven (the keyboard and mouse belong to player 1); a faded copy of the
					// painter counts as transformed, so the other cards register no hit regions.
					const FKartPainter RowPainter = Player == 0 ? Card : Card.Faded(0.999f);
					CompactRow(RowPainter, RowPos, FVector2D(Size.X - 32.0, RowHeight), Labels[Row], Values[Row], State, Player == 0 ? Row : -1,
						Row == PaintRow ? Paint : FLinearColor::Transparent);
				}
			}
			PaintReadyBar(Card, Frame, Player, FVector2D(Pos.X + 16.0, Pos.Y + Size.Y - 70.0), FVector2D(Size.X - 32.0, ReadyHeight));
		}

		/** The ready bar: "(A) when ready", or the READY! stamp. */
		void PaintReadyBar(const FKartPainter& Card, const FKartScreenFrame& Frame, int32 Player, const FVector2D& ReadyPos, const FVector2D& ReadySize) const
		{
			const FSlot& Slot = Slots[Player];
			if (Player == 0)
			{
				Card.Hit(ReadyHit, ReadyPos, ReadySize);
			}
			const float Round = static_cast<float>(ReadySize.Y * 0.5);
			if (Slot.bReady)
			{
				const float Stamp = Ease::OutBack(static_cast<float>((Frame.Now - Slot.ReadyAt) / StampSeconds), 3.f);
				const float Motion = FKartAccessibility::MotionScale();
				const FKartPainter S = Card.Transformed(ReadyPos + ReadySize * 0.5, 1.f + (0.6f - 0.6f * Stamp) * Motion, -4.f * Motion);
				S.Box(ReadyPos, ReadySize, FKartAccessibility::Good(), Round, Color::White, 3.f);
				S.Text(LOCTEXT("ReadyStamp", "READY!"), Font(EFont::Display, 36.f, 2.f), ReadyPos + ReadySize * 0.5, Color::White, FVector2D(0.5, 0.5), 3.f);
				return;
			}
			Card.Box(ReadyPos, ReadySize, Color::PaperShade, Round);
			const EKartInputDevice Device = Player == 0 ? Frame.Device : EKartInputDevice::Gamepad;
			const float GlyphW = KartWidgets::GlyphWidth(EKartMenuAction::Accept, Device, 36.f);
			const FSlateFontInfo LabelFont = Font(EFont::Button, 24.f);
			const FText Label = LOCTEXT("PressReady", "when ready");
			const float W = GlyphW + 10.f + static_cast<float>(FKartPainter::Measure(Label.ToString(), LabelFont).X);
			const double X = ReadyPos.X + ReadySize.X * 0.5 - W * 0.5;
			KartWidgets::Glyph(Card, EKartMenuAction::Accept, Device, FVector2D(X, ReadyPos.Y + ReadySize.Y * 0.5), 36.f);
			Card.Text(Label, LabelFont, FVector2D(X + GlyphW + 10.0, ReadyPos.Y + ReadySize.Y * 0.5), Color::NavyDim, FVector2D(0.0, 0.5));
		}

		/** The next free seat: a pulsing "press start to join" card. */
		void PaintJoinCard(const FKartPainter& P, const FKartScreenFrame& Frame, int32 Player, const FVector2D& Pos, const FVector2D& Size) const
		{
			const float In = KartWidgets::Entrance(Frame, 0.3f);
			const FKartPainter Card = P.Faded(In * (0.75f + 0.25f * FMath::Sin(static_cast<float>(Frame.Now) * 3.f)));
			Card.Box(Pos, Size, Color::WithAlpha(Color::Night, 0.55f), Metric::RadiusLarge, Color::WithAlpha(Color::White, 0.8f), 3.f);
			Card.Text(SeatTag(FMath::Clamp(Player, 0, MaxSeats - 1)), Font(EFont::Display, 64.f, 3.f), Pos + FVector2D(Size.X * 0.5, Size.Y * 0.36),
				Color::WithAlpha(Color::White, 0.9f), FVector2D(0.5, 0.5));
			const float GlyphW = KartWidgets::GlyphWidth(EKartMenuAction::Start, EKartInputDevice::Gamepad, 44.f);
			KartWidgets::Glyph(Card, EKartMenuAction::Start, EKartInputDevice::Gamepad, Pos + FVector2D(Size.X * 0.5 - GlyphW * 0.5, Size.Y * 0.56), 44.f);
			Card.Text(LOCTEXT("Join", "Press Start to join"), Font(EFont::Button, 26.f, 2.f), Pos + FVector2D(Size.X * 0.5, Size.Y * 0.7), Color::White, FVector2D(0.5, 0.5));
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FSlot Slots[MaxSeats];
		bool bAdvancing = false;
		double AdvanceAt = 0.0;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeRacers(AKartFrontEnd* FrontEnd) { return MakeShared<FRacersScreen>(FrontEnd); }
}

#undef LOCTEXT_NAMESPACE

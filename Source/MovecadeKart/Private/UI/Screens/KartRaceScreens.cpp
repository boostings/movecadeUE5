#include "UI/Screens/KartScreens.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "KartPawn.h"
#include "Net/RoomHostSubsystem.h"
#include "Race/KartRaceManager.h"
#include "Race/SKartRaceOverlay.h"
#include "UI/KartAccessibility.h"
#include "UI/KartRoster.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"
#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartRaceScreens"

using namespace KartUI;
using namespace KartScreens::Shared;

namespace
{
	/** The pause card in the middle of the screen and its column of buttons. */
	namespace PauseArt
	{
		constexpr float DimSeconds = 0.25f;
		constexpr float DimAlpha = 0.55f;
		const FVector2D CardSize(640.0, 640.0);
		/** The card pops from 80 % (an eased grow without the overshoot under Reduce motion). */
		constexpr double PopSeconds = 0.32;
		constexpr float PopOvershoot = 2.f;
		constexpr double ReducedPopSeconds = 0.2;
		constexpr float PopFrom = 0.8f;
		constexpr float PopGrowth = 0.2f;
		constexpr float FillAlpha = 0.92f;
		constexpr float Shadow = 12.f;
		constexpr float Rim = 5.f;
		constexpr float TitleSize = 84.f;
		constexpr float TitleOutline = 5.f;
		constexpr double TitleY = 82.0;
		constexpr float TrackSize = 24.f;
		constexpr double TrackY = 156.0;
		constexpr float TrackAlpha = 0.8f;
		const FVector2D ButtonSize(520.0, 86.0);
		constexpr double ButtonsX = 60.0;
		constexpr double ButtonsTop = 196.0;
		constexpr double ButtonGap = 18.0;
	}

	/** The results board: the title, rows down the left, buttons along the bottom. */
	namespace ResultsArt
	{
		constexpr float ShadeAlpha = 0.82f;
		/** The shade fades out across this much of the screen's width from the left. */
		constexpr double ShadeWidth = 0.7;
		constexpr double TitlePopSeconds = 0.5;
		constexpr float TitlePopOvershoot = 2.f;
		constexpr float TitleTilt = -8.f;
		/** The title pops about a point this far into it. */
		const FVector2D TitlePivot(300.0, 60.0);
		constexpr float KickerSize = 22.f;
		constexpr float KickerOutline = 2.f;
		const FVector2D KickerPos(4.0, -6.0);
		const FVector2D StarPos(36.0, 68.0);
		constexpr float StarOuter = 40.f;
		constexpr float StarInner = 18.f;
		constexpr double StarSpin = 40.0;
		constexpr float TitleSize = 78.f;
		constexpr float TitleOutline = 5.f;
		constexpr float TitleShadow = 6.f;
		const FVector2D TitlePos(90.0, 20.0);
		constexpr double RowsTop = 160.0;
		constexpr double RowWidth = 1080.0;
		constexpr double MaxRowHeight = 96.0;
		/** Room the title and the buttons keep, which the rows share out. */
		constexpr double Chrome = 360.0;
		constexpr double RowGap = 10.0;
		constexpr float RowInAt = 0.25f;
		constexpr float RowStagger = 0.07f;
		constexpr float RowInSeconds = 0.35f;
		constexpr double RowSlide = 300.0;
		constexpr float BotRowAlpha = 0.9f;
		constexpr float RowShadow = 5.f;
		constexpr float LocalRim = 4.f;
		/** The place badge: inset from the row's ends, its number a little under centre. */
		constexpr double BadgeInset = 6.0;
		constexpr float BadgeOutline = 3.f;
		constexpr float BadgeFont = 1.15f;
		constexpr double BadgeNumberDrop = 1.0;
		constexpr double SwatchGap = 22.0;
		constexpr double SwatchInset = 12.0;
		constexpr double SwatchWidth = 10.0;
		constexpr float SwatchRound = 5.f;
		constexpr double NameX = 24.0;
		constexpr float MaxNameSize = 32.f;
		constexpr double NameFraction = 0.4;
		constexpr double NameBaseline = 8.0;
		constexpr double SubX = 26.0;
		constexpr double SubDrop = 4.0;
		constexpr float SubSize = 18.f;
		constexpr double TimeRight = 230.0;
		constexpr float MaxTimeSize = 36.f;
		constexpr double TimeFraction = 0.5;
		constexpr double BestRight = 36.0;
		constexpr double BestLift = 2.0;
		constexpr float BestLabelSize = 13.f;
		constexpr float BestSize = 22.f;
		const FVector2D ButtonSize(400.0, 88.0);
		constexpr double ButtonGap = 22.0;
		constexpr double ButtonsLift = 70.0;
		constexpr float ButtonsInAt = 0.4f;
		constexpr double ButtonsRise = 80.0;
	}

	/** Back to the front end (at Entry), with the curtain; a party race closes its room first. */
	void ReturnToMenu(UWorld* World, EKartMenuEntry Entry)
	{
		UKartSessionSettings* Session = UKartSessionSettings::Get(World);
		UKartUISubsystem* UI = UKartUISubsystem::Get(World);
		if (!World || !Session || !UI)
		{
			return;
		}
		if (Session->GetMode() == EKartSessionMode::Party)
		{
			if (URoomHostSubsystem* Room = World->GetGameInstance()->GetSubsystem<URoomHostSubsystem>())
			{
				Room->CloseRoom(true);
			}
		}
		Session->ReturnToMenu(Entry);
		UI->TravelTo(UGameplayStatics::GetCurrentLevelName(World));
	}

	// ----------------------------------------------------------------------------------------------- Pause

	enum class EPauseItem : int32 { Resume, Restart, Settings, Quit, Count };

	/** The local pause menu over the race (SKartRaceOverlay opens it); Back, Pause and Start resume. */
	class FPauseScreen final : public FKartScreen
	{
	public:
		explicit FPauseScreen(AKartRaceManager* InRace) : Race(InRace)
		{
			Focus.Cursor.Count = static_cast<int32>(EPauseItem::Count);
		}

		virtual FName GetId() const override { return TEXT("pause"); }
		virtual void OnShown() override { Focus.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Accept, LOCTEXT("Select", "Select") }, { EKartMenuAction::Back, LOCTEXT("Resume", "Resume") } };
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
				Activate(static_cast<EPauseItem>(Focus.Cursor.Index));
				return true;
			case EKartMenuAction::Back:
			case EKartMenuAction::Pause:
			case EKartMenuAction::Start:
				Activate(EPauseItem::Resume);
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (HitId >= 0 && HitId < Focus.Cursor.Count)
			{
				Focus.HoverTo(HitId, *Host);
			}
		}

		virtual void OnClick(int32 HitId) override { Activate(static_cast<EPauseItem>(HitId)); }

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			using namespace PauseArt;
			const float In = KartWidgets::Entrance(Frame, 0.f, DimSeconds);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, DimAlpha * In));
			const FVector2D Pos = Frame.Centre() - CardSize * 0.5;
			const float Pop = FKartAccessibility::ReduceMotion() ? Ease::OutCubic(static_cast<float>(Frame.Age / ReducedPopSeconds))
				: Ease::OutBack(static_cast<float>(Frame.Age / PopSeconds), PopOvershoot);
			const FKartPainter Card = P.Transformed(Frame.Centre(), PopFrom + PopGrowth * Pop);
			Card.Card(Pos, CardSize, Color::WithAlpha(Color::Night, FillAlpha), Metric::RadiusLarge, Shadow, Color::Sun, Rim);
			Card.Text(LOCTEXT("Paused", "PAUSED"), Font(EFont::Display, TitleSize, TitleOutline), Pos + FVector2D(CardSize.X * 0.5, TitleY), Color::Sun, FVector2D(0.5, 0.5),
				TitleOutline);
			if (const AKartRaceManager* R = Race.Get(); R && R->GetTrack())
			{
				const FKartTrackInfo* Track = FKartTrackCatalog::Find(R->GetTrack()->Id);
				Card.Text(Track ? Track->Name : FText::FromString(R->GetTrack()->Name), Font(EFont::Body, TrackSize), Pos + FVector2D(CardSize.X * 0.5, TrackY),
					Color::WithAlpha(Color::White, TrackAlpha), FVector2D(0.5, 0.5));
			}
			static const FText Names[] = { LOCTEXT("ResumeButton", "Resume"), LOCTEXT("Restart", "Restart race"), LOCTEXT("Settings", "Settings"), LOCTEXT("QuitMenu", "Quit to menu") };
			static const FLinearColor Accents[] = { Color::Mint, Color::Sky, Color::Grape, Color::Hot };
			// Once the card has popped, the buttons paint untransformed so their hit regions register.
			const FKartPainter& ButtonPainter = Frame.Age > PopSeconds ? P : Card;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Focus.Cursor.Count; ++i)
			{
				Group.Next();
				const FVector2D ButtonPos(Pos.X + ButtonsX, Pos.Y + ButtonsTop + i * (ButtonSize.Y + ButtonGap));
				KartWidgets::Button(ButtonPainter, ButtonPos, ButtonSize, Names[i], Focus.State(i, Frame.Now), Accents[i], i);
			}
		}

	private:
		void Activate(EPauseItem Item)
		{
			switch (Item)
			{
			case EPauseItem::Resume:
				if (const TSharedPtr<SKartRaceOverlay> Overlay = SKartRaceOverlay::GetActive())
				{
					Overlay->ClosePauseMenu();
				}
				break;
			case EPauseItem::Restart:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("RestartTitle", "Restart the race?"), LOCTEXT("RestartBody", "Everyone goes back to the grid."),
					LOCTEXT("RestartYes", "Restart"), LOCTEXT("RestartNo", "Cancel"), [WeakRace = Race]()
					{
						if (AKartRaceManager* Manager = WeakRace.Get())
						{
							if (const TSharedPtr<SKartRaceOverlay> Overlay = SKartRaceOverlay::GetActive())
							{
								Overlay->ClosePauseMenu(false);
							}
							Manager->RestartRace();
						}
					}));
				break;
			case EPauseItem::Settings:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeSettings(0));
				break;
			default:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("QuitTitle", "Quit to the menu?"), LOCTEXT("QuitBody", "This race won't count."),
					LOCTEXT("QuitYes", "Quit"), LOCTEXT("QuitNo", "Keep racing"), [WeakRace = Race]()
					{
						if (AKartRaceManager* Manager = WeakRace.Get())
						{
							ReturnToMenu(Manager->GetWorld(), EKartMenuEntry::MainMenu);
						}
					}));
				break;
			}
		}

		TWeakObjectPtr<AKartRaceManager> Race;
		FListFocus Focus;
	};

	// --------------------------------------------------------------------------------------------- Results

	/**
	 * The results board after the "race over" banner: the final standings, then race again / change track /
	 * main menu (or, in a party room, back to the lobby / leave). It stays up until the player picks.
	 */
	class FResultsScreen final : public FKartScreen
	{
	public:
		explicit FResultsScreen(AKartRaceManager* InRace) : Race(InRace)
		{
			bRoom = InRace && InRace->OnRestartRequested;
			Focus.Cursor.Count = bRoom ? 2 : 3;
		}

		virtual FName GetId() const override { return TEXT("results"); }
		virtual bool CanGoBack() const override { return false; }
		virtual void OnShown() override { Focus.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Choose", "Choose") }, { EKartMenuAction::Accept, LOCTEXT("Select", "Select") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Focus.Step(Event.Action == EKartMenuAction::Left ? -1 : 1, *Host);
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
			if (HitId >= 0 && HitId < Focus.Cursor.Count)
			{
				Focus.HoverTo(HitId, *Host);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId >= 0 && HitId < Focus.Cursor.Count)
			{
				Activate(HitId);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const AKartRaceManager* R = Race.Get();
			if (!R)
			{
				return;
			}
			if (CachedRows != R->GetResults().Num())
			{
				BuildText(*R);
			}
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X * ResultsArt::ShadeWidth, Frame.Size.Y), Color::WithAlpha(Color::Night, ResultsArt::ShadeAlpha),
				Color::WithAlpha(Color::Night, 0.f), false);
			PaintTitle(P, Frame);
			PaintRows(P, Frame, *R);
			PaintButtons(P, Frame, R->GetResults().Num());
		}

	private:
		/** The winner (or a photo finish) over a spinning star. */
		void PaintTitle(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			using namespace ResultsArt;
			const float TitleIn = Ease::OutBack(static_cast<float>(Frame.Age / TitlePopSeconds), TitlePopOvershoot);
			const FVector2D Origin(Frame.SafeMin.X, Frame.SafeMin.Y);
			const FKartPainter T = P.Transformed(Origin + TitlePivot, TitleIn, (1.f - TitleIn) * TitleTilt * FKartAccessibility::MotionScale());
			T.Text(LOCTEXT("FinalStandings", "FINAL STANDINGS"), Font(EFont::Label, KickerSize, KickerOutline), Origin + KickerPos, Color::Sun);
			KartWidgets::Star(T, Origin + StarPos, StarOuter, StarInner, Color::Sun, static_cast<float>(Frame.Now * StarSpin * FKartAccessibility::MotionScale()));
			T.Text(TitleText, Font(EFont::Display, TitleSize, TitleOutline), Origin + TitlePos, Color::White, FVector2D::ZeroVector, TitleShadow);
		}

		/** One row per racer, staggered in: place badge, paint, name, character and vehicle, time and best lap. */
		void PaintRows(const FKartPainter& P, const FKartScreenFrame& Frame, const AKartRaceManager& R) const
		{
			using namespace ResultsArt;
			const TArray<FKartResult>& Results = R.GetResults();
			const int32 Rows = Results.Num();
			const double RowH = FMath::Min(MaxRowHeight, (Frame.SafeSize().Y - Chrome) / FMath::Max(1, Rows));
			const double Top = Frame.SafeMin.Y + RowsTop;
			const bool bShowBestLap = R.GetTrack() && R.GetTrack()->Laps > 1;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Rows; ++i)
			{
				Group.Next();
				const FKartResult& Result = Results[i];
				const float In = KartWidgets::Entrance(Frame, RowInAt + i * RowStagger, RowInSeconds);
				if (In <= 0.f)
				{
					continue;
				}
				const FVector2D Pos(Frame.SafeMin.X - (1.0 - In) * RowSlide, Top + i * RowH);
				const FKartPainter Row = P.Faded(In);
				const FKartRacer* Racer = R.GetRacers().IsValidIndex(Result.Seat) ? &R.GetRacers()[Result.Seat] : nullptr;
				const bool bLocal = Racer && Racer->Kind != EKartSeatKind::Bot;
				const FVector2D Size(RowWidth, RowH - RowGap);
				Row.Card(Pos, Size, bLocal ? Color::White : Color::WithAlpha(Color::Paper, BotRowAlpha), static_cast<float>(Size.Y * 0.5), RowShadow,
					bLocal ? Color::Sun : FLinearColor::Transparent, LocalRim);
				// Place badge.
				const FVector2D Badge(Pos.X + Size.Y * 0.5 + BadgeInset, Pos.Y + Size.Y * 0.5);
				const float BadgeR = static_cast<float>(Size.Y * 0.5 - BadgeInset);
				Row.Circle(Badge, BadgeR, Color::ForPlace(Result.Rank), Color::Navy, BadgeOutline);
				Row.Text(RowText[i].Rank, Font(EFont::Display, BadgeR * BadgeFont), Badge + FVector2D(0.0, BadgeNumberDrop), Color::Navy, FVector2D(0.5, 0.5));
				// Paint swatch + name + character / vehicle.
				const double NameLeft = Badge.X + BadgeR + SwatchGap;
				Row.Box(FVector2D(NameLeft, Pos.Y + SwatchInset), FVector2D(SwatchWidth, Size.Y - 2.0 * SwatchInset), Result.Paint, SwatchRound);
				Row.Text(Result.Name, Font(EFont::Title, FMath::Min(MaxNameSize, static_cast<float>(Size.Y * NameFraction))), FVector2D(NameLeft + NameX, Pos.Y + Size.Y * 0.5 + NameBaseline),
					Color::Navy, FVector2D(0.0, 1.0));
				if (!RowText[i].Sub.IsEmpty())
				{
					Row.Text(RowText[i].Sub, Font(EFont::Body, SubSize), FVector2D(NameLeft + SubX, Pos.Y + Size.Y * 0.5 + SubDrop), Color::NavyDim);
				}
				// Time and best lap on the right.
				Row.Text(RowText[i].Time, Font(EFont::Display, FMath::Min(MaxTimeSize, static_cast<float>(Size.Y * TimeFraction))), FVector2D(Pos.X + Size.X - TimeRight, Pos.Y + Size.Y * 0.5),
					Result.bFinished ? Color::Navy : Color::NavyDim, FVector2D(1.0, 0.5));
				if (Result.BestLapMs > 0.0 && bShowBestLap)
				{
					const FVector2D BestAt(Pos.X + Size.X - BestRight, Pos.Y + Size.Y * 0.5 - BestLift);
					Row.Text(LOCTEXT("Best", "BEST LAP"), Font(EFont::Label, BestLabelSize), BestAt, Color::NavyDim, FVector2D(1.0, 1.0));
					Row.Text(RowText[i].BestLap, Font(EFont::Button, BestSize), BestAt, Color::Navy, FVector2D(1.0, 0.0));
				}
			}
		}

		/** The buttons along the bottom, in after the last row. */
		void PaintButtons(const FKartPainter& P, const FKartScreenFrame& Frame, int32 Rows) const
		{
			using namespace ResultsArt;
			static const FText Names[] = { LOCTEXT("RaceAgain", "Race again"), LOCTEXT("ChangeTrack", "Change track"), LOCTEXT("MainMenu", "Main menu") };
			static const FText RoomNames[] = { LOCTEXT("BackToLobby", "Back to lobby"), LOCTEXT("LeaveParty", "Leave party") };
			static const FLinearColor Accents[] = { Color::Hot, Color::Mint, Color::Sky };
			const float ButtonsIn = KartWidgets::Entrance(Frame, ButtonsInAt + Rows * RowStagger);
			const double Y = Frame.SafeMax.Y - ButtonSize.Y - ButtonsLift;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Focus.Cursor.Count; ++i)
			{
				Group.Next();
				const FVector2D Pos(Frame.SafeMin.X + i * (ButtonSize.X + ButtonGap), Y + (1.0 - ButtonsIn) * ButtonsRise);
				KartWidgets::Button(P.Faded(ButtonsIn), Pos, ButtonSize, bRoom ? RoomNames[i] : Names[i], Focus.State(i, Frame.Now), Accents[i], i);
			}
		}

		void Activate(int32 Item)
		{
			AKartRaceManager* R = Race.Get();
			if (!R)
			{
				return;
			}
			Host->PlaySound(EKartUISound::Accept);
			if (bRoom)
			{
				if (Item == 0)
				{
					R->OnRestartRequested(); // the room goes back to the lobby; everyone re-readies
				}
				else
				{
					ReturnToMenu(R->GetWorld(), EKartMenuEntry::MainMenu);
				}
				return;
			}
			switch (Item)
			{
			case 0:
				if (const TSharedPtr<SKartRaceOverlay> Overlay = SKartRaceOverlay::GetActive())
				{
					Overlay->CloseMenus();
				}
				R->RestartRace();
				break;
			case 1:
				ReturnToMenu(R->GetWorld(), EKartMenuEntry::TrackSelect);
				break;
			default:
				ReturnToMenu(R->GetWorld(), EKartMenuEntry::MainMenu);
				break;
			}
		}

		/** The board's text, built once when the results arrive (they don't change while it shows). */
		void BuildText(const AKartRaceManager& R)
		{
			const TArray<FKartResult>& Results = R.GetResults();
			CachedRows = Results.Num();
			const bool bPhoto = Results.Num() > 1 && Results[1].bFinished && FMath::Abs(Results[1].TimeMs - Results[0].TimeMs) < 1.0;
			TitleText = Results.Num() == 0 ? LOCTEXT("Results", "Results")
				: bPhoto ? LOCTEXT("Photo", "Photo finish!")
				: Results[0].bFinished ? FText::Format(LOCTEXT("Wins", "{0} wins!"), FText::FromString(Results[0].Name)) : LOCTEXT("TimeUp", "Time's up!");
			RowText.SetNum(Results.Num());
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				const FKartResult& Result = Results[i];
				FRowText& Text = RowText[i];
				Text.Rank = FString::FromInt(Result.Rank);
				Text.Time = Result.bFinished ? SKartRaceOverlay::FormatRaceTime(Result.TimeMs) : FString(TEXT("DNF"));
				Text.BestLap = Result.BestLapMs > 0.0 ? SKartRaceOverlay::FormatRaceTime(Result.BestLapMs) : FString();
				const FKartRacer* Racer = R.GetRacers().IsValidIndex(Result.Seat) ? &R.GetRacers()[Result.Seat] : nullptr;
				if (Racer && Racer->Kart)
				{
					const FKartLook& Look = Racer->Kart->GetLook();
					Text.Sub = FText::Format(LOCTEXT("CharacterVehicle", "{0}  ·  {1}"), FKartRoster::CharacterNameOf(Look), FKartRoster::VehicleName(Look.Vehicle));
				}
			}
		}

		struct FRowText
		{
			FString Rank;
			FString Time;
			FString BestLap;
			FText Sub;
		};

		TWeakObjectPtr<AKartRaceManager> Race;
		FListFocus Focus;
		/** A party room's race: the board offers the lobby instead of the menus. */
		bool bRoom = false;
		int32 CachedRows = -1;
		FText TitleText;
		TArray<FRowText> RowText;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakePause(AKartRaceManager* Race) { return MakeShared<FPauseScreen>(Race); }
	TSharedRef<FKartScreen> MakeResults(AKartRaceManager* Race) { return MakeShared<FResultsScreen>(Race); }
}

#undef LOCTEXT_NAMESPACE

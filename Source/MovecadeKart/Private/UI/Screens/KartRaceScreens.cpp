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
	const FVector2D PauseCardSize(640.0, 640.0);
	const FVector2D PauseButtonSize(520.0, 86.0);
	constexpr double PauseButtonsTop = 196.0;
	constexpr double PauseButtonGap = 18.0;
	constexpr double PausePopSeconds = 0.32;

	/** The results board: rows down the left, buttons along the bottom. */
	constexpr double ResultsRowsTop = 160.0;
	constexpr double ResultsRowWidth = 1080.0;
	constexpr double ResultsMaxRowHeight = 96.0;
	/** Room the title and the buttons keep, which the rows share out. */
	constexpr double ResultsChrome = 360.0;
	constexpr float ResultsRowStagger = 0.07f;
	const FVector2D ResultsButtonSize(400.0, 88.0);
	constexpr double ResultsButtonGap = 22.0;

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
			const float In = KartWidgets::Entrance(Frame, 0.f, 0.25f);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.55f * In));
			const FVector2D Pos = Frame.Centre() - PauseCardSize * 0.5;
			const float Pop = FKartAccessibility::ReduceMotion() ? Ease::OutCubic(static_cast<float>(Frame.Age / 0.2))
				: Ease::OutBack(static_cast<float>(Frame.Age / PausePopSeconds), 2.f);
			const FKartPainter Card = P.Transformed(Frame.Centre(), 0.8f + 0.2f * Pop);
			Card.Card(Pos, PauseCardSize, Color::WithAlpha(Color::Night, 0.92f), Metric::RadiusLarge, 12.f, Color::Sun, 5.f);
			Card.Text(LOCTEXT("Paused", "PAUSED"), Font(EFont::Display, 84.f, 5.f), Pos + FVector2D(PauseCardSize.X * 0.5, 82.0), Color::Sun, FVector2D(0.5, 0.5), 5.f);
			if (const AKartRaceManager* R = Race.Get(); R && R->GetTrack())
			{
				const FKartTrackInfo* Track = FKartTrackCatalog::Find(R->GetTrack()->Id);
				Card.Text(Track ? Track->Name : FText::FromString(R->GetTrack()->Name), Font(EFont::Body, 24.f), Pos + FVector2D(PauseCardSize.X * 0.5, 156.0),
					Color::WithAlpha(Color::White, 0.8f), FVector2D(0.5, 0.5));
			}
			static const FText Names[] = { LOCTEXT("ResumeButton", "Resume"), LOCTEXT("Restart", "Restart race"), LOCTEXT("Settings", "Settings"), LOCTEXT("QuitMenu", "Quit to menu") };
			static const FLinearColor Accents[] = { Color::Mint, Color::Sky, Color::Grape, Color::Hot };
			// Once the card has popped, the buttons paint untransformed so their hit regions register.
			const FKartPainter& ButtonPainter = Frame.Age > PausePopSeconds ? P : Card;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Focus.Cursor.Count; ++i)
			{
				Group.Next();
				const FVector2D ButtonPos(Pos.X + 60.0, Pos.Y + PauseButtonsTop + i * (PauseButtonSize.Y + PauseButtonGap));
				KartWidgets::Button(ButtonPainter, ButtonPos, PauseButtonSize, Names[i], Focus.State(i, Frame.Now), Accents[i], i);
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
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X * 0.7, Frame.Size.Y), Color::WithAlpha(Color::Night, 0.82f), Color::WithAlpha(Color::Night, 0.f), false);
			PaintTitle(P, Frame);
			PaintRows(P, Frame, *R);
			PaintButtons(P, Frame, R->GetResults().Num());
		}

	private:
		/** The winner (or a photo finish) over a spinning star. */
		void PaintTitle(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			const float TitleIn = Ease::OutBack(static_cast<float>(Frame.Age / 0.5), 2.f);
			const FVector2D TitlePos(Frame.SafeMin.X, Frame.SafeMin.Y);
			const FKartPainter T = P.Transformed(TitlePos + FVector2D(300.0, 60.0), TitleIn, (1.f - TitleIn) * -8.f * FKartAccessibility::MotionScale());
			T.Text(LOCTEXT("FinalStandings", "FINAL STANDINGS"), Font(EFont::Label, 22.f, 2.f), TitlePos + FVector2D(4.0, -6.0), Color::Sun);
			KartWidgets::Star(T, TitlePos + FVector2D(36.0, 68.0), 40.f, 18.f, Color::Sun, static_cast<float>(Frame.Now * 40.0 * FKartAccessibility::MotionScale()));
			T.Text(TitleText, Font(EFont::Display, 78.f, 5.f), TitlePos + FVector2D(90.0, 20.0), Color::White, FVector2D::ZeroVector, 6.f);
		}

		/** One row per racer, staggered in: place badge, paint, name, character and vehicle, time and best lap. */
		void PaintRows(const FKartPainter& P, const FKartScreenFrame& Frame, const AKartRaceManager& R) const
		{
			const TArray<FKartResult>& Results = R.GetResults();
			const int32 Rows = Results.Num();
			const double RowH = FMath::Min(ResultsMaxRowHeight, (Frame.SafeSize().Y - ResultsChrome) / FMath::Max(1, Rows));
			const double Top = Frame.SafeMin.Y + ResultsRowsTop;
			const bool bShowBestLap = R.GetTrack() && R.GetTrack()->Laps > 1;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Rows; ++i)
			{
				Group.Next();
				const FKartResult& Result = Results[i];
				const float In = KartWidgets::Entrance(Frame, 0.25f + i * ResultsRowStagger, 0.35f);
				if (In <= 0.f)
				{
					continue;
				}
				const FVector2D Pos(Frame.SafeMin.X - (1.0 - In) * 300.0, Top + i * RowH);
				const FKartPainter Row = P.Faded(In);
				const FKartRacer* Racer = R.GetRacers().IsValidIndex(Result.Seat) ? &R.GetRacers()[Result.Seat] : nullptr;
				const bool bLocal = Racer && Racer->Kind != EKartSeatKind::Bot;
				const FVector2D Size(ResultsRowWidth, RowH - 10.0);
				Row.Card(Pos, Size, bLocal ? Color::White : Color::WithAlpha(Color::Paper, 0.9f), static_cast<float>(Size.Y * 0.5), 5.f,
					bLocal ? Color::Sun : FLinearColor::Transparent, 4.f);
				// Place badge.
				const FVector2D Badge(Pos.X + Size.Y * 0.5 + 6.0, Pos.Y + Size.Y * 0.5);
				const float BadgeR = static_cast<float>(Size.Y * 0.5 - 6.0);
				Row.Circle(Badge, BadgeR, Color::ForPlace(Result.Rank), Color::Navy, 3.f);
				Row.Text(RowText[i].Rank, Font(EFont::Display, BadgeR * 1.15f), Badge + FVector2D(0.0, 1.0), Color::Navy, FVector2D(0.5, 0.5));
				// Paint swatch + name + character / vehicle.
				const double NameX = Badge.X + BadgeR + 22.0;
				Row.Box(FVector2D(NameX, Pos.Y + 12.0), FVector2D(10.0, Size.Y - 24.0), Result.Paint, 5.f);
				Row.Text(Result.Name, Font(EFont::Title, FMath::Min(32.f, static_cast<float>(Size.Y * 0.4))), FVector2D(NameX + 24.0, Pos.Y + Size.Y * 0.5 + 8.0), Color::Navy, FVector2D(0.0, 1.0));
				if (!RowText[i].Sub.IsEmpty())
				{
					Row.Text(RowText[i].Sub, Font(EFont::Body, 18.f), FVector2D(NameX + 26.0, Pos.Y + Size.Y * 0.5 + 4.0), Color::NavyDim);
				}
				// Time and best lap on the right.
				Row.Text(RowText[i].Time, Font(EFont::Display, FMath::Min(36.f, static_cast<float>(Size.Y * 0.5))), FVector2D(Pos.X + Size.X - 230.0, Pos.Y + Size.Y * 0.5),
					Result.bFinished ? Color::Navy : Color::NavyDim, FVector2D(1.0, 0.5));
				if (Result.BestLapMs > 0.0 && bShowBestLap)
				{
					Row.Text(LOCTEXT("Best", "BEST LAP"), Font(EFont::Label, 13.f), FVector2D(Pos.X + Size.X - 36.0, Pos.Y + Size.Y * 0.5 - 2.0), Color::NavyDim, FVector2D(1.0, 1.0));
					Row.Text(RowText[i].BestLap, Font(EFont::Button, 22.f), FVector2D(Pos.X + Size.X - 36.0, Pos.Y + Size.Y * 0.5 - 2.0),
						Color::Navy, FVector2D(1.0, 0.0));
				}
			}
		}

		/** The buttons along the bottom, in after the last row. */
		void PaintButtons(const FKartPainter& P, const FKartScreenFrame& Frame, int32 Rows) const
		{
			static const FText Names[] = { LOCTEXT("RaceAgain", "Race again"), LOCTEXT("ChangeTrack", "Change track"), LOCTEXT("MainMenu", "Main menu") };
			static const FText RoomNames[] = { LOCTEXT("BackToLobby", "Back to lobby"), LOCTEXT("LeaveParty", "Leave party") };
			static const FLinearColor Accents[] = { Color::Hot, Color::Mint, Color::Sky };
			const float ButtonsIn = KartWidgets::Entrance(Frame, 0.4f + Rows * ResultsRowStagger);
			const double Y = Frame.SafeMax.Y - ResultsButtonSize.Y - 70.0;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Focus.Cursor.Count; ++i)
			{
				Group.Next();
				const FVector2D Pos(Frame.SafeMin.X + i * (ResultsButtonSize.X + ResultsButtonGap), Y + (1.0 - ButtonsIn) * 80.0);
				KartWidgets::Button(P.Faded(ButtonsIn), Pos, ResultsButtonSize, bRoom ? RoomNames[i] : Names[i], Focus.State(i, Frame.Now), Accents[i], i);
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

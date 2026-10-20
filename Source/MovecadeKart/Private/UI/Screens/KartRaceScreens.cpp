#include "UI/Screens/KartScreens.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "KartPawn.h"
#include "Net/RoomHostSubsystem.h"
#include "Race/KartRaceManager.h"
#include "Race/SKartRaceOverlay.h"
#include "UI/KartRoster.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "KartRaceScreens"

using namespace KartUI;
using KartWidgets::FItemState;

namespace
{
	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	/** Back to the front end (at Entry), with the curtain. */
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

	/** A vertical list of buttons in a centred card. */
	struct FButtonList
	{
		FKartMenuCursor Cursor;
		double FocusAt = 0.0;

		bool Handle(const FKartMenuEvent& Event, IKartScreenHost* Host)
		{
			if (Event.Action == EKartMenuAction::Up || Event.Action == EKartMenuAction::Down)
			{
				if (Cursor.Move(Event.Action == EKartMenuAction::Up ? -1 : 1))
				{
					FocusAt = NowSeconds();
					Host->PlaySound(EKartUISound::Move);
				}
				return true;
			}
			return false;
		}

		void Hover(int32 Id, IKartScreenHost* Host)
		{
			if (Id >= 0 && Id < Cursor.Count && Cursor.Set(Id))
			{
				FocusAt = NowSeconds();
				Host->PlaySound(EKartUISound::Move);
			}
		}

		FItemState State(int32 Row, double Now) const
		{
			FItemState S;
			S.bFocused = Row == Cursor.Index;
			S.FocusAge = Now - FocusAt;
			return S;
		}
	};

	// ----------------------------------------------------------------------------------------------- Pause

	class FPauseScreen final : public FKartScreen
	{
	public:
		explicit FPauseScreen(AKartRaceManager* InRace) : Race(InRace)
		{
			Buttons.Cursor.Count = 4;
		}

		virtual FName GetId() const override { return TEXT("pause"); }
		virtual void OnShown() override { Buttons.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Accept, LOCTEXT("Select", "Select") }, { EKartMenuAction::Back, LOCTEXT("Resume", "Resume") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (Buttons.Handle(Event, Host))
			{
				return true;
			}
			if (Event.Action == EKartMenuAction::Accept)
			{
				Activate(Buttons.Cursor.Index);
				return true;
			}
			if (Event.Action == EKartMenuAction::Back || Event.Action == EKartMenuAction::Pause || Event.Action == EKartMenuAction::Start)
			{
				Activate(0);
				return true;
			}
			return false;
		}

		virtual void OnHover(int32 HitId) override { Buttons.Hover(HitId, Host); }
		virtual void OnClick(int32 HitId) override { Activate(HitId); }

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const float In = KartWidgets::Entrance(Frame, 0.f, 0.25f);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.55f * In));
			const FVector2D Size(640.0, 640.0);
			const FVector2D Pos = Frame.Centre() - Size * 0.5;
			const float Pop = Ease::OutBack(static_cast<float>(Frame.Age / 0.32), 2.f);
			const FKartPainter Card = P.Transformed(Frame.Centre(), 0.8f + 0.2f * Pop);
			Card.Card(Pos, Size, Color::WithAlpha(Color::Night, 0.92f), Metric::RadiusLarge, 12.f, Color::Sun, 5.f);
			Card.Text(LOCTEXT("Paused", "PAUSED"), Font(EFont::Display, 84.f, 5.f), Pos + FVector2D(Size.X * 0.5, 82.0), Color::Sun, FVector2D(0.5, 0.5), 5.f);
			if (const AKartRaceManager* R = Race.Get(); R && R->GetTrack())
			{
				const FKartTrackInfo* Track = FKartTrackCatalog::Find(R->GetTrack()->Id);
				Card.Text(Track ? Track->Name : FText::FromString(R->GetTrack()->Name), Font(EFont::Body, 24.f), Pos + FVector2D(Size.X * 0.5, 156.0),
					Color::WithAlpha(Color::White, 0.8f), FVector2D(0.5, 0.5));
			}
			const FText Names[] = { LOCTEXT("ResumeButton", "Resume"), LOCTEXT("Restart", "Restart race"), LOCTEXT("Settings", "Settings"), LOCTEXT("QuitMenu", "Quit to menu") };
			const FLinearColor Accents[] = { Color::Mint, Color::Sky, Color::Grape, Color::Hot };
			const FVector2D ButtonSize(520.0, 86.0);
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector2D BPos(Pos.X + 60.0, Pos.Y + 196.0 + i * (ButtonSize.Y + 18.0));
				KartWidgets::Button(Frame.Age > 0.32 ? P : Card, BPos, ButtonSize, Names[i], Buttons.State(i, Frame.Now), Accents[i], i);
			}
		}

	private:
		void Activate(int32 Item)
		{
			AKartRaceManager* R = Race.Get();
			const TSharedPtr<SKartRaceOverlay> Overlay = SKartRaceOverlay::GetActive();
			switch (Item)
			{
			case 0:
				if (Overlay.IsValid())
				{
					Overlay->ClosePauseMenu();
				}
				break;
			case 1:
				Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("RestartTitle", "Restart the race?"), LOCTEXT("RestartBody", "Everyone goes back to the grid."),
					LOCTEXT("RestartYes", "Restart"), LOCTEXT("RestartNo", "Cancel"), [WeakRace = Race]()
					{
						if (AKartRaceManager* Manager = WeakRace.Get())
						{
							if (const TSharedPtr<SKartRaceOverlay> O = SKartRaceOverlay::GetActive())
							{
								O->ClosePauseMenu(false);
							}
							Manager->RestartRace();
						}
					}));
				break;
			case 2:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeSettings(0));
				break;
			default:
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
			(void)R;
		}

		TWeakObjectPtr<AKartRaceManager> Race;
		FButtonList Buttons;
	};

	// --------------------------------------------------------------------------------------------- Results

	class FResultsScreen final : public FKartScreen
	{
	public:
		explicit FResultsScreen(AKartRaceManager* InRace) : Race(InRace)
		{
			bRoom = InRace && InRace->OnRestartRequested;
			Buttons.Cursor.Count = bRoom ? 2 : 3;
		}

		virtual FName GetId() const override { return TEXT("results"); }
		virtual bool CanGoBack() const override { return false; }
		virtual void OnShown() override { Buttons.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Choose", "Choose") }, { EKartMenuAction::Accept, LOCTEXT("Select", "Select") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (Event.Action == EKartMenuAction::Left || Event.Action == EKartMenuAction::Right)
			{
				if (Buttons.Cursor.Move(Event.Action == EKartMenuAction::Left ? -1 : 1))
				{
					Buttons.FocusAt = NowSeconds();
					Host->PlaySound(EKartUISound::Move);
				}
				return true;
			}
			if (Event.Action == EKartMenuAction::Accept || Event.Action == EKartMenuAction::Start)
			{
				Activate(Buttons.Cursor.Index);
				return true;
			}
			return false;
		}

		virtual void OnHover(int32 HitId) override { Buttons.Hover(HitId, Host); }
		virtual void OnClick(int32 HitId) override
		{
			if (HitId >= 0 && HitId < Buttons.Cursor.Count)
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
			const TArray<FKartResult>& Results = R->GetResults();
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X * 0.7, Frame.Size.Y), Color::WithAlpha(Color::Night, 0.82f), Color::WithAlpha(Color::Night, 0.f), false);

			// Title: the winner (or a photo finish) over a burst of stars.
			const float TitleIn = Ease::OutBack(static_cast<float>(Frame.Age / 0.5), 2.f);
			const FVector2D TitlePos(Frame.SafeMin.X, Frame.SafeMin.Y);
			const bool bPhoto = Results.Num() > 1 && Results[1].bFinished && FMath::Abs(Results[1].TimeMs - Results[0].TimeMs) < 1.0;
			const FText Title = Results.Num() == 0 ? LOCTEXT("Results", "Results")
				: bPhoto ? LOCTEXT("Photo", "Photo finish!")
				: Results[0].bFinished ? FText::Format(LOCTEXT("Wins", "{0} wins!"), FText::FromString(Results[0].Name)) : LOCTEXT("TimeUp", "Time's up!");
			const FKartPainter T = P.Transformed(TitlePos + FVector2D(300.0, 60.0), TitleIn, (1.f - TitleIn) * -8.f);
			T.Text(LOCTEXT("FinalStandings", "FINAL STANDINGS"), Font(EFont::Label, 22.f, 2.f), TitlePos + FVector2D(4.0, -6.0), Color::Sun);
			KartWidgets::Star(T, TitlePos + FVector2D(36.0, 68.0), 40.f, 18.f, Color::Sun, static_cast<float>(Frame.Now * 40.0));
			T.Text(Title, Font(EFont::Display, 78.f, 5.f), TitlePos + FVector2D(90.0, 20.0), Color::White, FVector2D::ZeroVector, 6.f);

			// Rows, staggered in.
			const int32 Rows = Results.Num();
			const double RowH = FMath::Min(96.0, (Frame.SafeSize().Y - 360.0) / FMath::Max(1, Rows));
			const double RowW = 1080.0;
			const double Top = Frame.SafeMin.Y + 160.0;
			for (int32 i = 0; i < Rows; ++i)
			{
				const FKartResult& Result = Results[i];
				const float In = KartWidgets::Entrance(Frame, 0.25f + i * 0.07f, 0.35f);
				if (In <= 0.f)
				{
					continue;
				}
				const FVector2D Pos(Frame.SafeMin.X - (1.0 - In) * 300.0, Top + i * RowH);
				const FKartPainter Row = P.Faded(In);
				const FKartRacer* Racer = R->GetRacers().IsValidIndex(Result.Seat) ? &R->GetRacers()[Result.Seat] : nullptr;
				const bool bLocal = Racer && Racer->Kind != EKartSeatKind::Bot;
				const FVector2D Size(RowW, RowH - 10.0);
				Row.Card(Pos, Size, bLocal ? Color::White : Color::WithAlpha(Color::Paper, 0.9f), static_cast<float>(Size.Y * 0.5), 5.f,
					bLocal ? Color::Sun : FLinearColor::Transparent, 4.f);
				// Place badge.
				const FVector2D Badge(Pos.X + Size.Y * 0.5 + 6.0, Pos.Y + Size.Y * 0.5);
				const float BadgeR = static_cast<float>(Size.Y * 0.5 - 6.0);
				Row.Circle(Badge, BadgeR, Color::ForPlace(Result.Rank), Color::Navy, 3.f);
				Row.Text(FString::FromInt(Result.Rank), Font(EFont::Display, BadgeR * 1.15f), Badge + FVector2D(0.0, 1.0), Color::Navy, FVector2D(0.5, 0.5));
				// Paint swatch + name + character / vehicle.
				const double NameX = Badge.X + BadgeR + 22.0;
				Row.Box(FVector2D(NameX, Pos.Y + 12.0), FVector2D(10.0, Size.Y - 24.0), Result.Paint, 5.f);
				Row.Text(Result.Name, Font(EFont::Title, FMath::Min(32.f, static_cast<float>(Size.Y * 0.4))), FVector2D(NameX + 24.0, Pos.Y + Size.Y * 0.5 + 8.0), Color::Navy, FVector2D(0.0, 1.0));
				if (Racer && Racer->Kart)
				{
					const FKartLook& Look = Racer->Kart->GetLook();
					const FText Sub = FText::Format(LOCTEXT("CharacterVehicle", "{0}  ·  {1}"), FKartRoster::CharacterNameOf(Look), FKartRoster::VehicleName(Look.Vehicle));
					Row.Text(Sub, Font(EFont::Body, 18.f), FVector2D(NameX + 26.0, Pos.Y + Size.Y * 0.5 + 4.0), Color::NavyDim);
				}
				// Time and best lap on the right.
				const FString Time = Result.bFinished ? SKartRaceOverlay::FormatRaceTime(Result.TimeMs) : FString(TEXT("DNF"));
				Row.Text(Time, Font(EFont::Display, FMath::Min(36.f, static_cast<float>(Size.Y * 0.5))), FVector2D(Pos.X + Size.X - 230.0, Pos.Y + Size.Y * 0.5),
					Result.bFinished ? Color::Navy : Color::NavyDim, FVector2D(1.0, 0.5));
				if (Result.BestLapMs > 0.0 && R->GetTrack() && R->GetTrack()->Laps > 1)
				{
					Row.Text(LOCTEXT("Best", "BEST LAP"), Font(EFont::Label, 13.f), FVector2D(Pos.X + Size.X - 36.0, Pos.Y + Size.Y * 0.5 - 2.0), Color::NavyDim, FVector2D(1.0, 1.0));
					Row.Text(SKartRaceOverlay::FormatRaceTime(Result.BestLapMs), Font(EFont::Button, 22.f), FVector2D(Pos.X + Size.X - 36.0, Pos.Y + Size.Y * 0.5 - 2.0),
						Color::Navy, FVector2D(1.0, 0.0));
				}
			}

			// The buttons along the bottom.
			const float ButtonsIn = KartWidgets::Entrance(Frame, 0.4f + Rows * 0.07f);
			const FText Names[] = { LOCTEXT("RaceAgain", "Race again"), LOCTEXT("ChangeTrack", "Change track"), LOCTEXT("MainMenu", "Main menu") };
			const FText RoomNames[] = { LOCTEXT("BackToLobby", "Back to lobby"), LOCTEXT("LeaveParty", "Leave party") };
			const FLinearColor Accents[] = { Color::Hot, Color::Mint, Color::Sky };
			const FVector2D ButtonSize(400.0, 88.0);
			const double Y = Frame.SafeMax.Y - ButtonSize.Y - 70.0;
			for (int32 i = 0; i < Buttons.Cursor.Count; ++i)
			{
				const FVector2D Pos(Frame.SafeMin.X + i * (ButtonSize.X + 22.0), Y + (1.0 - ButtonsIn) * 80.0);
				KartWidgets::Button(P.Faded(ButtonsIn), Pos, ButtonSize, bRoom ? RoomNames[i] : Names[i], Buttons.State(i, Frame.Now), Accents[i], i);
			}
		}

	private:
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

		TWeakObjectPtr<AKartRaceManager> Race;
		FButtonList Buttons;
		bool bRoom = false;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakePause(AKartRaceManager* Race) { return MakeShared<FPauseScreen>(Race); }
	TSharedRef<FKartScreen> MakeResults(AKartRaceManager* Race) { return MakeShared<FResultsScreen>(Race); }
}

#undef LOCTEXT_NAMESPACE

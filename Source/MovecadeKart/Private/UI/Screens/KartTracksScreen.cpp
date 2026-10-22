#include "UI/Screens/KartScreens.h"

#include "UI/KartAccessibility.h"
#include "UI/KartFrontEnd.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"
#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;
using namespace KartScreens::Shared;
using KartWidgets::FItemState;

namespace
{
	/** The track cards, side by side in the middle of the screen. */
	const FVector2D CardSize(620.0, 590.0);
	constexpr double CardGap = 48.0;
	constexpr double CardsLift = 30.0;
	constexpr double CardRise = 120.0;
	constexpr float FocusedPop = 0.04f;
	constexpr float UnfocusedScale = 0.95f;
	constexpr double FocusPopSeconds = 0.25;
	/** The outline panel at the top of each card, and the marker that runs a lap of it. */
	constexpr double MapHeight = 330.0;
	constexpr double MarkerLapsPerSecond = 0.09;
	/** The arrows either side of the row, and the start button under it. */
	constexpr double ArrowGap = 50.0;
	constexpr double ArrowBob = 5.0;
	constexpr double StartButtonDrop = 66.0;
	constexpr int32 StartHit = 99;

	/** Step 3 of the race setup: the tracks as cards with their outline, pitch and lap length; Accept starts the race. */
	class FTracksScreen final : public FKartScreen
	{
	public:
		explicit FTracksScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd)
		{
			Focus.Cursor.Count = FKartTrackCatalog::All().Num();
			Focus.Cursor.bWrap = false;
		}

		virtual FName GetId() const override { return TEXT("tracks"); }

		virtual void OnShown() override
		{
			if (const UKartSessionSettings* S = SessionOf(Host))
			{
				Focus.Cursor.Set(FMath::Max(0, FKartTrackCatalog::IndexOf(S->GetSetup().TrackId)));
			}
			Focus.FocusAt = NowSeconds();
			bStarting = false;
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
		}

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Choose", "Choose") }, { EKartMenuAction::Accept, LOCTEXT("StartRace", "Start race") }, { EKartMenuAction::Back, LOCTEXT("Back", "Back") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (bStarting)
			{
				return true;
			}
			switch (Event.Action)
			{
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Select(Focus.Cursor.Index + (Event.Action == EKartMenuAction::Left ? -1 : 1));
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Start();
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (HitId < Focus.Cursor.Count)
			{
				Select(HitId);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId < Focus.Cursor.Count)
			{
				Select(HitId);
			}
			Start();
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, 300.0), Color::WithAlpha(Color::Night, 0.65f), Color::WithAlpha(Color::Night, 0.f));
			P.Gradient(FVector2D(0.0, Frame.Size.Y * 0.4), FVector2D(Frame.Size.X, Frame.Size.Y * 0.6), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.75f));
			KartWidgets::Header(P, Frame, LOCTEXT("TracksTitle", "Choose a track"), FText::GetEmpty(), Color::Mint);
			KartWidgets::Steps(P, Frame, RaceSetupSteps(), 2);

			const TArray<FKartTrackInfo>& Tracks = FKartTrackCatalog::All();
			const UKartSessionSettings* Session = SessionOf(Host);
			const FKartRaceSetup* Setup = Session ? &Session->GetSetup() : nullptr;
			const double Total = Tracks.Num() * CardSize.X + (Tracks.Num() - 1) * CardGap;
			const double Left = Frame.Size.X * 0.5 - Total * 0.5;
			const double Top = Frame.Size.Y * 0.5 - CardSize.Y * 0.5 - CardsLift;
			{
				const FKartPaintGroup Group(P);
				for (int32 i = 0; i < Tracks.Num(); ++i)
				{
					Group.Next();
					const float In = KartWidgets::Entrance(Frame, 0.05f + i * 0.08f, 0.45f);
					const FVector2D Pos(Left + i * (CardSize.X + CardGap) + KartWidgets::SlideX(Frame), Top + (1.0 - In) * CardRise);
					PaintTrackCard(P, Frame, Tracks[i], i, Pos, In, Setup);
				}
			}
			// Arrows either side when there is more to pick.
			const double MidY = Top + CardSize.Y * 0.5;
			const double Bob = FMath::Sin(Frame.Now * 6.0) * ArrowBob;
			if (Focus.Cursor.Index > 0)
			{
				KartWidgets::Arrow(P, FVector2D(Left - ArrowGap - Bob, MidY), 24.f, false, Color::White);
			}
			if (Focus.Cursor.Index < Tracks.Num() - 1)
			{
				KartWidgets::Arrow(P, FVector2D(Left + Total + ArrowGap + Bob, MidY), 24.f, true, Color::White);
			}
			FItemState Start;
			Start.bFocused = true;
			Start.FocusAge = Frame.Now - Focus.FocusAt;
			KartWidgets::PrimaryButton(P.Faded(KartWidgets::Entrance(Frame, 0.35f)), FVector2D(Frame.Size.X * 0.5, Top + CardSize.Y + StartButtonDrop),
				LOCTEXT("StartRaceButton", "START RACE"), EKartMenuAction::Accept, Frame.Device, Start, StartHit);
		}

	private:
		/** One track's card: the outline panel, name, tagline and stat chips; the cards not picked are dimmed. */
		void PaintTrackCard(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartTrackInfo& Track, int32 Index, const FVector2D& Pos, float In,
			const FKartRaceSetup* Setup)
		{
			const bool bFocused = Index == Focus.Cursor.Index;
			P.Hit(Index, Pos, CardSize);
			const float Pop = bFocused ? 1.f + FocusedPop * Ease::OutBack(static_cast<float>((Frame.Now - Focus.FocusAt) / FocusPopSeconds), FKartAccessibility::ReduceMotion() ? 0.f : 3.f)
				: UnfocusedScale;
			const FKartPainter Card = P.Transformed(Pos + CardSize * 0.5, Pop, bFocused ? -1.f : 0.f).Faded(In);
			if (bFocused)
			{
				Card.Box(Pos - FVector2D(10.0, 10.0), CardSize + FVector2D(20.0, 20.0), Color::WithAlpha(Track.Accent, 0.5f), Metric::RadiusLarge + 10.f);
			}
			Card.Card(Pos, CardSize, Color::Paper, Metric::RadiusLarge, 12.f, bFocused ? Color::White : FLinearColor::Transparent, 6.f);
			// The outline panel: the track drawn from its centreline, with a dot running a lap.
			const FVector2D MapPos = Pos + FVector2D(18.0, 18.0);
			const FVector2D MapSize(CardSize.X - 36.0, MapHeight);
			Card.Gradient(MapPos, MapSize, Track.Accent, FMath::Lerp(Track.Accent, Color::Night, 0.4f), true, Metric::Radius);
			const TSharedPtr<const FKartTrackPreview> Preview = FKartTrackCatalog::GetPreview(Track.Id);
			if (Preview.IsValid())
			{
				KartWidgets::TrackOutline(Card, Preview->Outline, MapPos + FVector2D(40.0, 30.0), MapSize - FVector2D(80.0, 60.0), Color::White, 9.f,
					bFocused ? Frame.Now * MarkerLapsPerSecond : -1.0, Color::Sun);
			}
			else
			{
				Card.Text(LOCTEXT("Loading", "..."), Font(EFont::Display, 60.f), MapPos + MapSize * 0.5, Color::White, FVector2D(0.5, 0.5));
			}
			Card.Text(Track.Name, Font(EFont::Display, 56.f), Pos + FVector2D(34.0, 364.0), Color::Navy);
			Card.TextWrapped(Track.Tagline, Font(EFont::Body, 21.f), Pos + FVector2D(36.0, 446.0), CardSize.X - 72.0, Color::NavyDim);
			// Stat chips: the lap length and the race's lap count.
			double ChipX = Pos.X + 34.0;
			const FSlateFontInfo ChipFont = Font(EFont::Label, 19.f);
			auto Chip = [&](const FString& Text, const FLinearColor& Fill)
			{
				const double W = FKartPainter::Measure(Text, ChipFont).X + 32.0;
				Card.Box(FVector2D(ChipX, Pos.Y + CardSize.Y - 68.0), FVector2D(W, 40.0), Fill, 20.f);
				Card.Text(Text, ChipFont, FVector2D(ChipX + W * 0.5, Pos.Y + CardSize.Y - 48.0), Color::TextOn(Fill), FVector2D(0.5, 0.5));
				ChipX += W + 12.0;
			};
			if (Preview.IsValid())
			{
				Chip(LapLengthChip(Track.Id, Preview->LapMetres), Color::PaperShade);
			}
			if (Setup)
			{
				Chip(LapsChip(Setup->Laps), Track.Accent);
			}
			if (!bFocused)
			{
				// The card not picked steps back: dimmed rather than see-through.
				Card.Box(Pos, CardSize, Color::WithAlpha(Color::Night, 0.28f), Metric::RadiusLarge);
			}
		}

		/** "1.23 km lap", built once per track. */
		const FString& LapLengthChip(const FString& TrackId, double LapMetres)
		{
			if (const FString* Found = LapLengthChips.Find(TrackId))
			{
				return *Found;
			}
			return LapLengthChips.Add(TrackId, FString::Printf(TEXT("%.2f km lap"), LapMetres / 1000.0));
		}

		/** "3 LAPS", rebuilt when the lap count changes. */
		const FString& LapsChip(int32 Laps)
		{
			if (Laps != LapsChipValue)
			{
				LapsChipValue = Laps;
				LapsChipText = FString::Printf(TEXT("%d %s"), Laps, Laps == 1 ? TEXT("LAP") : TEXT("LAPS"));
			}
			return LapsChipText;
		}

		void Select(int32 Index)
		{
			if (!Focus.SetFocus(Index, NowSeconds()))
			{
				return;
			}
			Host->PlaySound(EKartUISound::Move);
			if (UKartSessionSettings* S = SessionOf(Host))
			{
				S->MutableSetup().TrackId = FKartTrackCatalog::All()[Focus.Cursor.Index].Id;
			}
		}

		void Start()
		{
			if (bStarting)
			{
				return;
			}
			bStarting = true;
			if (UKartSessionSettings* S = SessionOf(Host))
			{
				S->MutableSetup().TrackId = FKartTrackCatalog::All()[Focus.Cursor.Index].Id;
			}
			Host->PlaySound(EKartUISound::Go);
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->StartRace();
			}
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FListFocus Focus;
		bool bStarting = false;
		/** Chip labels, built once per track / laps value. */
		TMap<FString, FString> LapLengthChips;
		int32 LapsChipValue = -1;
		FString LapsChipText;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeTracks(AKartFrontEnd* FrontEnd) { return MakeShared<FTracksScreen>(FrontEnd); }
}

#undef LOCTEXT_NAMESPACE

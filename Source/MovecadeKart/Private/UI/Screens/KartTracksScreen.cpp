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
	constexpr float CardInAt = 0.05f;
	constexpr float CardStagger = 0.08f;
	constexpr float CardInSeconds = 0.45f;

	/** Inside a track card: the focus treatment, the outline panel, the text and the stat chips. */
	namespace TrackCardArt
	{
		constexpr float FocusedPop = 0.04f;
		constexpr float FocusOvershoot = 3.f;
		constexpr float UnfocusedScale = 0.95f;
		constexpr double FocusPopSeconds = 0.25;
		constexpr float FocusedTilt = -1.f;
		constexpr double GlowPad = 10.0;
		constexpr float GlowAlpha = 0.5f;
		constexpr float Shadow = 12.f;
		constexpr float FocusedRim = 6.f;
		/** The card not picked steps back: dimmed rather than see-through. */
		constexpr float DimAlpha = 0.28f;
		constexpr double MapInset = 18.0;
		constexpr double MapHeight = 330.0;
		constexpr float MapShade = 0.4f;
		/** The outline sits inside the panel with this much room around it. */
		const FVector2D OutlinePad(40.0, 30.0);
		constexpr float RoadWidth = 9.f;
		constexpr double MarkerLapsPerSecond = 0.09;
		constexpr float LoadingSize = 60.f;
		constexpr float NameSize = 56.f;
		const FVector2D NamePos(34.0, 364.0);
		constexpr float TaglineSize = 21.f;
		const FVector2D TaglinePos(36.0, 446.0);
		constexpr double ChipsX = 34.0;
		constexpr float ChipFontSize = 19.f;
		constexpr double ChipPadding = 32.0;
		/** The chips' top edge and text centre, measured up from the card's bottom. */
		constexpr double ChipLift = 68.0;
		constexpr double ChipTextLift = 48.0;
		constexpr double ChipHeight = 40.0;
		constexpr float ChipRound = 20.f;
		constexpr double ChipGap = 12.0;
	}

	/** The shade under the header and behind the cards. */
	namespace ShadeArt
	{
		constexpr double TopHeight = 300.0;
		constexpr float TopAlpha = 0.65f;
		constexpr double BottomTop = 0.4;
		constexpr double BottomHeight = 0.6;
		constexpr float BottomAlpha = 0.75f;
	}

	/** The arrows either side of the row, and the start button under it. */
	constexpr double ArrowGap = 50.0;
	constexpr double ArrowBob = 5.0;
	constexpr double ArrowBobSpeed = 6.0;
	constexpr float ArrowSize = 24.f;
	/** Room kept for each arrow beside the row, and the smallest the cards shrink to. */
	constexpr double ArrowRoom = 40.0;
	constexpr double MinCardFit = 0.6;
	constexpr float StartButtonInAt = 0.35f;
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
			P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, ShadeArt::TopHeight), Color::WithAlpha(Color::Night, ShadeArt::TopAlpha), Color::WithAlpha(Color::Night, 0.f));
			P.Gradient(FVector2D(0.0, Frame.Size.Y * ShadeArt::BottomTop), FVector2D(Frame.Size.X, Frame.Size.Y * ShadeArt::BottomHeight), Color::WithAlpha(Color::Night, 0.f),
				Color::WithAlpha(Color::Night, ShadeArt::BottomAlpha));
			KartWidgets::Header(P, Frame, LOCTEXT("TracksTitle", "Choose a track"), FText::GetEmpty(), Color::Mint);
			KartWidgets::Steps(P, Frame, RaceSetupSteps(), 2);

			const TArray<FKartTrackInfo>& Tracks = FKartTrackCatalog::All();
			const UKartSessionSettings* Session = SessionOf(Host);
			const FKartRaceSetup* Setup = Session ? &Session->GetSetup() : nullptr;
			// The row shrinks to fit between the arrows inside the safe area (three cards and more outgrow the canvas).
			const double Natural = Tracks.Num() * CardSize.X + (Tracks.Num() - 1) * CardGap;
			const double Room = Frame.SafeSize().X - 2.0 * (ArrowGap + ArrowRoom);
			const float Fit = static_cast<float>(FMath::Clamp(Room / FMath::Max(Natural, 1.0), MinCardFit, 1.0));
			const double Total = Natural * Fit;
			const double Left = Frame.Size.X * 0.5 - Total * 0.5;
			const double Top = Frame.Size.Y * 0.5 - CardSize.Y * Fit * 0.5 - CardsLift;
			{
				const FKartPaintGroup Group(P);
				for (int32 i = 0; i < Tracks.Num(); ++i)
				{
					Group.Next();
					const float In = KartWidgets::Entrance(Frame, CardInAt + i * CardStagger, CardInSeconds);
					const FVector2D Centre(Left + (i * (CardSize.X + CardGap) + CardSize.X * 0.5) * Fit + KartWidgets::SlideX(Frame),
						Top + CardSize.Y * Fit * 0.5 + (1.0 - In) * CardRise);
					PaintTrackCard(P, Frame, Tracks[i], i, Centre, Fit, In, Setup);
				}
			}
			// Arrows either side when there is more to pick.
			const double MidY = Top + CardSize.Y * Fit * 0.5;
			const double Bob = FMath::Sin(Frame.Now * ArrowBobSpeed) * ArrowBob;
			if (Focus.Cursor.Index > 0)
			{
				KartWidgets::Arrow(P, FVector2D(Left - ArrowGap - Bob, MidY), ArrowSize, false, Color::White);
			}
			if (Focus.Cursor.Index < Tracks.Num() - 1)
			{
				KartWidgets::Arrow(P, FVector2D(Left + Total + ArrowGap + Bob, MidY), ArrowSize, true, Color::White);
			}
			FItemState Start;
			Start.bFocused = true;
			Start.FocusAge = Frame.Now - Focus.FocusAt;
			KartWidgets::PrimaryButton(P.Faded(KartWidgets::Entrance(Frame, StartButtonInAt)), FVector2D(Frame.Size.X * 0.5, Top + CardSize.Y * Fit + StartButtonDrop),
				LOCTEXT("StartRaceButton", "START RACE"), EKartMenuAction::Accept, Frame.Device, Start, StartHit);
		}

	private:
		/**
		 * One track's card centred on Centre, drawn at Fit of its design size: the outline panel, name, tagline and stat
		 * chips; the cards not picked are dimmed.
		 */
		void PaintTrackCard(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartTrackInfo& Track, int32 Index, const FVector2D& Centre, float Fit,
			float In, const FKartRaceSetup* Setup)
		{
			using namespace TrackCardArt;
			const bool bFocused = Index == Focus.Cursor.Index;
			P.Hit(Index, Centre - CardSize * Fit * 0.5, CardSize * Fit);
			const float Pop = bFocused ? 1.f + FocusedPop * Ease::OutBack(static_cast<float>((Frame.Now - Focus.FocusAt) / FocusPopSeconds), FKartAccessibility::ReduceMotion() ? 0.f : FocusOvershoot)
				: UnfocusedScale;
			// The card is laid out at its design size around Centre; the painter scales it to fit.
			const FVector2D Pos = Centre - CardSize * 0.5;
			const FKartPainter Card = P.Transformed(Centre, Pop * Fit, bFocused ? FocusedTilt : 0.f).Faded(In);
			if (bFocused)
			{
				Card.Box(Pos - FVector2D(GlowPad, GlowPad), CardSize + FVector2D(2.0 * GlowPad, 2.0 * GlowPad), Color::WithAlpha(Track.Accent, GlowAlpha),
					Metric::RadiusLarge + static_cast<float>(GlowPad));
			}
			Card.Card(Pos, CardSize, Color::Paper, Metric::RadiusLarge, Shadow, bFocused ? Color::White : FLinearColor::Transparent, FocusedRim);
			// The outline panel: the track drawn from its centreline, with a dot running a lap.
			const FVector2D MapPos = Pos + FVector2D(MapInset, MapInset);
			const FVector2D MapSize(CardSize.X - 2.0 * MapInset, MapHeight);
			Card.Gradient(MapPos, MapSize, Track.Accent, FMath::Lerp(Track.Accent, Color::Night, MapShade), true, Metric::Radius);
			const TSharedPtr<const FKartTrackPreview> Preview = FKartTrackCatalog::GetPreview(Track.Id);
			if (Preview.IsValid())
			{
				KartWidgets::TrackOutline(Card, Preview->Outline, MapPos + OutlinePad, MapSize - OutlinePad * 2.0, Color::White, RoadWidth,
					bFocused ? Frame.Now * MarkerLapsPerSecond : -1.0, Color::Sun);
			}
			else
			{
				Card.Text(LOCTEXT("Loading", "..."), Font(EFont::Display, LoadingSize), MapPos + MapSize * 0.5, Color::White, FVector2D(0.5, 0.5));
			}
			Card.Text(Track.Name, Font(EFont::Display, NameSize), Pos + NamePos, Color::Navy);
			Card.TextWrapped(Track.Tagline, Font(EFont::Body, TaglineSize), Pos + TaglinePos, CardSize.X - 2.0 * TaglinePos.X, Color::NavyDim);
			// Stat chips: the lap length and the race's lap count.
			double ChipX = Pos.X + ChipsX;
			const FSlateFontInfo ChipFont = Font(EFont::Label, ChipFontSize);
			auto Chip = [&](const FString& Text, const FLinearColor& Fill)
			{
				const double W = FKartPainter::Measure(Text, ChipFont).X + ChipPadding;
				Card.Box(FVector2D(ChipX, Pos.Y + CardSize.Y - ChipLift), FVector2D(W, ChipHeight), Fill, ChipRound);
				Card.Text(Text, ChipFont, FVector2D(ChipX + W * 0.5, Pos.Y + CardSize.Y - ChipTextLift), Color::TextOn(Fill), FVector2D(0.5, 0.5));
				ChipX += W + ChipGap;
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
				Card.Box(Pos, CardSize, Color::WithAlpha(Color::Night, DimAlpha), Metric::RadiusLarge);
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

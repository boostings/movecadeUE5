#pragma once

#include "CoreMinimal.h"
#include "Sim/KartItems.h"
#include "UI/KartSafeArea.h"
#include "Widgets/SLeafWidget.h"

class AKartRaceManager;
class FKartPainter;
struct FKartRacer;
namespace MovecadeKart { struct FKartState; }

/**
 * The in-race HUD, painted immediate-mode for every view in its split-screen rectangle:
 * the item slot with its roulette, lap, place (bouncing on changes), speed, minimap, the final-lap and
 * finish banners and status messages; and full-screen layers for the intro card, the countdown and GO,
 * the race clock and the standings. Layout scales with each view, and the 3- and 4-view layouts move
 * the minimap and standings into the shared space. Owned by SKartRaceOverlay.
 *
 * Files: SKartRaceHud.cpp (frame, views, shared space), SKartRaceHudView.cpp (one view's widgets),
 * SKartRaceHudPanels.cpp (minimap, standings, countdown, intro, banners).
 */
class SKartRaceHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKartRaceHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AKartRaceManager>, Manager)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** Text a seat's widgets show, rebuilt when its value changes rather than every paint. */
	struct FSeatText
	{
		int32 Speed = -1;
		FString SpeedText;
		int32 Lap = -1;
		int32 Laps = -1;
		FString LapText;
		int32 Place = -1;
		FString PlaceNumber;
		FString PlaceSuffix;
		FText FinishText;
		FString BoostKey;
		FText BoostLabel;
	};

	/** Animation state per seat, updated in Tick from the race (times are Slate seconds). */
	struct FSeatAnim
	{
		int32 Place = 0;
		double PlaceAt = -10.0;
		/** +1 gained a place, -1 lost one. */
		int32 PlaceDir = 0;
		int32 Lap = 0;
		double LapAt = -10.0;
		double FinalLapAt = -10.0;
		bool bFinished = false;
		double FinishedAt = -10.0;
		/** The speed readout, eased towards the kart's real speed (km/h). */
		double ShownSpeed = 0.0;
		FSeatText Text;
	};

	/** One player's view rectangle on the screen. */
	struct FView
	{
		FVector2D Pos;
		FVector2D Size;
		int32 Seat = 0;
	};

	/** A view's layout: its safe corners and the scale its widgets draw at. */
	struct FViewLayout
	{
		FVector2D Min;
		FVector2D Max;
		float Scale = 1.f;
		/** Where the bottom-left and bottom-right widgets sit: Max.Y, or above the touch controls on a phone. */
		double BottomLeft = 0.0;
		double BottomRight = 0.0;
		/** The solo minimap's height. */
		float MapHeight = 0.f;
		/** 0 while the widgets slide in at the countdown, 1 settled. */
		float SlideIn = 1.f;
	};

	// SKartRaceHud.cpp
	/** Fills Views (reused every paint). */
	void GatherViews(const FVector2D& Screen) const;
	void PaintClock(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const;
	/** The 3- and 4-view layouts' shared space: the minimap (and the standings for 3). */
	void PaintSharedSpace(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen) const;

	// SKartRaceHudView.cpp
	void PaintView(const FKartPainter& P, const AKartRaceManager& Race, const FView& View, int32 NumViews, double Now) const;
	FViewLayout LayoutView(const FKartPainter& P, const AKartRaceManager& Race, const FView& View, int32 NumViews, double Now) const;
	void PaintLap(const FKartPainter& P, FSeatAnim& Anim, int32 Laps, const FVector2D& Anchor, float Scale, double Now) const;
	void PaintSpeed(const FKartPainter& P, FSeatAnim& Anim, const MovecadeKart::FKartState& S, const FVector2D& Origin, float Scale) const;
	void PaintStatus(const FKartPainter& P, const AKartRaceManager& Race, const FKartRacer& Racer, FSeatAnim& Anim, const FView& View, float Scale, double Now) const;
	void PaintViewBanners(const FKartPainter& P, const FKartRacer& Racer, FSeatAnim& Anim, const FView& View, float Scale, double Now) const;
	void PaintItemSlot(const FKartPainter& P, const AKartRaceManager& Race, const FKartRacer& Racer, const FVector2D& Pos, float Scale, double Now) const;
	void PaintPlace(const FKartPainter& P, const FKartRacer& Racer, const FVector2D& BottomRight, float Scale, double Now) const;
	/** The item names shown beside the slot ("Banana", or "Banana!" as it is used), built once per item. */
	const FText& ItemText(MovecadeKart::EKartItem Item, bool bUsed) const;

	// SKartRaceHudPanels.cpp
	void PaintMinimap(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Pos, float Height, int32 FocusSeat) const;
	void PaintStandings(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& TopRight, float Scale) const;
	void PaintCountdown(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const;
	void PaintIntro(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const;
	void PaintRoomPause(const FKartPainter& P, const FVector2D& Screen, double Now) const;
	void PaintBanner(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Text, const FLinearColor& Fill,
		double Age, double Hold, float Scale, bool bChequered) const;

	TWeakObjectPtr<AKartRaceManager> Manager;
	/** Per seat; mutable because paint keeps each seat's text cache up to date. */
	mutable TArray<FSeatAnim> Seats;
	mutable TArray<FView, TInlineAllocator<4>> Views;
	/** This paint's safe area (the notch and the touch controls on a phone; the TV margin on a PC). */
	mutable KartSafeArea::FKartSafeFrame Safe;

	/** The minimap road: the centreline sampled once per track (world units), and the per-paint screen copy. */
	mutable const void* MinimapTrack = nullptr;
	mutable TArray<FVector2D> MinimapRoad;
	mutable TArray<FVector2D> MinimapScratch;

	/** The race clock and the time-left warning, rebuilt once a second. */
	mutable int64 ClockSecond = -1;
	mutable FText ClockText;
	mutable int32 WarnSecond = -1;
	mutable FText WarnText;
	/** The intro card's track name (upper case) and "3 LAPS · 8 RACERS", rebuilt when the track or the counts change. */
	mutable const void* IntroNameTrack = nullptr;
	mutable FText IntroName;
	mutable FString IntroFacts;
	mutable int32 IntroFactsKey = -1;
	double RaceFinishedAt = -10.0;
	int32 LastPhase = -1;
	double PhaseAt = 0.0;
};

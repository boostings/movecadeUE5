#pragma once

#include "CoreMinimal.h"
#include "Sim/KartItems.h"
#include "Widgets/SLeafWidget.h"

class AKartRaceManager;
class FKartPainter;
struct FKartRacer;

/**
 * The in-race HUD, painted immediate-mode for every view in its split-screen rectangle:
 * the item slot with its roulette, lap, place (bouncing on changes), speed, minimap, the final-lap and
 * finish banners and status messages; and full-screen layers for the intro card, the countdown and GO,
 * the race clock and the standings. Layout scales with each view, and the 3- and 4-view layouts move
 * the minimap and standings into the shared space.
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

	/** True while the results board should wait (the finish banner is on screen). */
	bool IsShowingFinish() const;

private:
	/** Animation state per seat, updated in Tick from the race. */
	struct FSeatAnim
	{
		int32 Place = 0;
		double PlaceAt = -10.0;
		int32 PlaceDir = 0;
		int32 Lap = 0;
		double LapAt = -10.0;
		double FinalLapAt = -10.0;
		bool bFinished = false;
		double FinishedAt = -10.0;
		bool bRolling = false;
		double LandedAt = -10.0;
		MovecadeKart::EKartItem Held = MovecadeKart::EKartItem::None;
		double ShownSpeed = 0.0;
	};

	struct FView
	{
		FVector2D Pos;
		FVector2D Size;
		int32 Seat = 0;
	};

	TArray<FView> GatherViews(const FVector2D& Screen) const;
	void PaintView(const FKartPainter& P, const AKartRaceManager& Race, const FView& View, int32 NumViews, double Now) const;
	void PaintItemSlot(const FKartPainter& P, const AKartRaceManager& Race, const FKartRacer& Racer, const FVector2D& Pos, float Scale, double Now) const;
	void PaintPlace(const FKartPainter& P, const FKartRacer& Racer, const FVector2D& BottomRight, float Scale, double Now) const;
	void PaintMinimap(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Pos, float Height, int32 FocusSeat) const;
	void PaintStandings(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& TopRight, float Scale) const;
	void PaintCountdown(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const;
	void PaintIntro(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const;
	void PaintRoomPause(const FKartPainter& P, const AKartRaceManager& Race, const FVector2D& Screen, double Now) const;
	void PaintBanner(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Text, const FLinearColor& Fill,
		double Age, double Hold, float Scale, bool bChequered) const;

	TWeakObjectPtr<AKartRaceManager> Manager;
	TArray<FSeatAnim> Seats;
	double RaceFinishedAt = -10.0;
	int32 LastPhase = -1;
	double PhaseAt = 0.0;
};

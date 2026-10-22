#pragma once

#include "CoreMinimal.h"

class APlayerController;

/**
 * A fixed, localized HUD label, built once: `P.Text(ARCADE_LABEL("Stacker", "Wind", "WIND"), ...)`. Expands to a
 * function-local `static const FText` (NSLOCTEXT, so the localization gatherer still finds it), so painting it costs
 * nothing per frame. Use it for every constant label painted in OnPaint / Paint / GetHints; text with values goes
 * through FCachedText or an FText::Format cached on change.
 */
#define ARCADE_LABEL(Namespace, Key, SourceText) \
	([]() -> const FText& { static const FText Label = NSLOCTEXT(Namespace, Key, SourceText); return Label; }())

/**
 * Small building blocks for the party games' painted HUDs (S<Game>Hud SLeafWidgets): one camera projection per
 * paint, text that is only formatted when its value changes, and a standings order that is only re-sorted when a
 * score changes. Each lives as a (mutable) member of the HUD widget; none of them allocates once warmed up.
 */

/**
 * The viewer's camera projection, taken once per HUD paint: projecting many world points through it costs a matrix
 * multiply each instead of a trip through the local player and viewport.
 */
struct MOVECADEKART_API FArcadeHudView
{
	FMatrix ViewProjection = FMatrix::Identity;
	/** The constrained view rect inside the viewport, in viewport pixels. */
	FIntRect ViewRect;
	/** The whole viewport, in pixels: what a full-screen HUD paints over. */
	FVector2D ViewportSize = FVector2D(1.0, 1.0);
	bool bValid = false;

	/** Viewer's current camera; invalid (Project fails) without a local player, a viewport or a non-empty size. */
	static FArcadeHudView Capture(const APlayerController* Viewer);
	/** A view from explicit matrices (tests, render targets). Invalid when either size is empty. */
	static FArcadeHudView FromMatrix(const FMatrix& InViewProjection, const FIntRect& InViewRect, const FVector2D& InViewportSize);

	/** World (UE cm) to 0..1 across the whole viewport; false behind the camera or when the view is invalid. */
	bool Project(const FVector& World, FVector2D& OutViewport01) const;
	/** World (UE cm) to 0..1 across the constrained view rect (a letterboxed camera); false as Project. */
	bool ProjectToViewRect(const FVector& World, FVector2D& OutRect01) const;
};

/**
 * A number's label, rebuilt only when the number changes: `Text.Get(Score, [](int32 V) { return FString::FromInt(V); })`.
 * The formatter is called on the first use and on every change, never on a repaint with the same value.
 */
template <typename TValue>
class TArcadeCachedText
{
public:
	template <typename TFormat>
	const FString& Get(const TValue Value, TFormat&& Format)
	{
		if (!bSet || Value != Shown)
		{
			Shown = Value;
			bSet = true;
			Text = Format(Value);
		}
		return Text;
	}

	/** Forces the next Get to format again (a language or style change). */
	void Invalidate() { bSet = false; }

private:
	FString Text;
	TValue Shown{};
	bool bSet = false;
};

using FCachedText = TArcadeCachedText<int32>;
using FCachedFloatText = TArcadeCachedText<float>;

/**
 * Seats ranked best first by a score key, re-sorted only when a key changes. Ties keep seat order (a stable sort),
 * as every game's standings card does today. `const TArray<int32>& Order = Ranks.Update(Towers.Num(),
 * [&](int32 i) { return Towers[i].Score; });` returns seat indices, best first.
 */
class MOVECADEKART_API FArcadeStandingsOrder
{
public:
	/** Ranks Count seats by KeyOf(seat) (higher first). Allocation-free once Count has been seen. */
	const TArray<int32>& Update(int32 Count, TFunctionRef<int64(int32 Seat)> KeyOf);

	const TArray<int32>& Get() const { return Order; }
	/** Forgets the keys: the next Update sorts again (a restart with new seats). */
	void Reset();

private:
	TArray<int64> Keys;
	TArray<int32> Order;
};

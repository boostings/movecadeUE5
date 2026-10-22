#pragma once

#include "CoreMinimal.h"
#include "Layout/Geometry.h"
#include "UI/KartUIStyle.h"

class FSlateWindowElementList;

/** A clickable rectangle a screen painted this frame (local, untransformed units). */
struct FKartHitRegion
{
	int32 Id = INDEX_NONE;
	FVector2D Pos = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;

	bool Contains(const FVector2D& P) const
	{
		return P.X >= Pos.X && P.Y >= Pos.Y && P.X <= Pos.X + Size.X && P.Y <= Pos.Y + Size.Y;
	}
};

/**
 * Immediate-mode drawing for the menus and the HUD: rounded cards, gradients, outlined text, circles and
 * filled polygons, in a widget's local space. Transformed() returns a painter whose output is scaled and
 * rotated about a point, which is how every pop, bounce and tilt is drawn. Opacity multiplies everything.
 *
 * Later calls always draw on top. Layers come from the shared counter, but consecutive primitives that Slate
 * batches together (same batch parameters: plain rectangles, same-size rounded boxes, polygons and square
 * gradients, same-font text) share one layer, so Slate draws them as one batch without reordering anything.
 * The widget returns the counter's final value from OnPaint.
 */
class MOVECADEKART_API FKartPainter
{
public:
	FKartPainter(const FGeometry& InGeometry, FSlateWindowElementList& InOut, int32& InLayerCounter, TArray<FKartHitRegion>* InHits = nullptr);

	/** A painter scaled by Scale and rotated by Degrees about the local point Pivot, then moved by Translate. */
	FKartPainter Transformed(const FVector2D& Pivot, float Scale, float Degrees = 0.f, const FVector2D& Translate = FVector2D::ZeroVector) const;
	FKartPainter Faded(float Alpha) const;

	void Box(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, float Radius = 0.f,
		const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f) const;
	/** A card with a soft drop shadow beneath it. */
	void Card(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, float Radius = KartUI::Metric::Radius,
		float ShadowDepth = 8.f, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f) const;
	void Gradient(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& From, const FLinearColor& To,
		bool bVertical = true, float Radius = 0.f) const;
	void Circle(const FVector2D& Centre, float Radius, const FLinearColor& Fill,
		const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f) const;
	/** A convex or star-shaped polygon, filled as a fan from its centroid. Takes a TArray, an inline array or { a, b, c }. */
	void Polygon(TConstArrayView<FVector2D> Points, const FLinearColor& Fill) const;
	void Lines(TConstArrayView<FVector2D> Points, const FLinearColor& Color, float Thickness, bool bClosed = false) const;

	/** Draws String with its anchor at Pos (Align: 0,0 top-left .. 1,1 bottom-right). Returns its size. */
	FVector2D Text(const FString& String, const FSlateFontInfo& Font, const FVector2D& Pos, const FLinearColor& Color,
		const FVector2D& Align = FVector2D::ZeroVector, float ShadowDepth = 0.f) const;
	FVector2D Text(const FText& String, const FSlateFontInfo& Font, const FVector2D& Pos, const FLinearColor& Color,
		const FVector2D& Align = FVector2D::ZeroVector, float ShadowDepth = 0.f) const
	{
		return Text(String.ToString(), Font, Pos, Color, Align, ShadowDepth);
	}
	static FVector2D Measure(const FString& String, const FSlateFontInfo& Font);
	/** Word-wraps String to MaxWidth from Pos (AlignX 0 left, 0.5 centred on Pos.X). Returns the block's size. */
	FVector2D TextWrapped(const FText& String, const FSlateFontInfo& Font, const FVector2D& Pos, double MaxWidth, const FLinearColor& Color,
		double AlignX = 0.0, float ShadowDepth = 0.f) const;
	static TArray<FString> WrapLines(const FString& String, const FSlateFontInfo& Font, double MaxWidth);
	/** WrapLines through a bounded cache (text, font and width): what TextWrapped uses every frame. */
	static const TArray<FString>& WrapLinesCached(const FString& String, const FSlateFontInfo& Font, double MaxWidth);

	/** Clips everything drawn until PopClip to a rectangle (axis-aligned in this painter's space). */
	void PushClip(const FVector2D& Pos, const FVector2D& Size) const;
	void PopClip() const;

	/**
	 * Item groups, for lists whose items never overlap one another (menu buttons, option rows, standings):
	 * inside a group, the Nth primitive of every item goes to the same layer, so Slate batches the whole list's
	 * backgrounds, then its icons, then its labels, instead of one layer per primitive. Paint order within an
	 * item is unchanged. Use FKartPaintGroup rather than calling these directly.
	 */
	/** False if a group is already open (a nested group just continues the outer item). */
	bool BeginGroup() const;
	void NextItem() const;
	void EndGroup() const;

	/** Registers a clickable region for mouse hover and clicks. */
	void Hit(int32 Id, const FVector2D& Pos, const FVector2D& Size) const;

	const FGeometry& Geometry() const { return Geo; }
	FVector2D Size() const { return FVector2D(Geo.GetLocalSize()); }
	float GetOpacity() const { return Opacity; }

private:
	FLinearColor Fade(const FLinearColor& Color) const { return FLinearColor(Color.R, Color.G, Color.B, Color.A * Opacity); }
	/** The layer for the next element with batch key Key: the current one while the run's key repeats. */
	int32 LayerFor(uint64 Key, bool bNewRun = false) const;
	bool IsRotated() const;

	FGeometry Geo;
	FSlateWindowElementList* Out;
	int32* Layer;
	float Opacity = 1.f;
	TArray<FKartHitRegion>* Hits;
	/** Hit regions are recorded only by untransformed painters (a pop doesn't move the click target). */
	bool bTransformed = false;
};

/** Scoped item group: construct before the first item, call Next() before each further item. */
struct FKartPaintGroup
{
	explicit FKartPaintGroup(const FKartPainter& InPainter) : Painter(InPainter), bOwner(Painter.BeginGroup()) {}
	~FKartPaintGroup() { if (bOwner) { Painter.EndGroup(); } }
	void Next() const { if (bOwner) { Painter.NextItem(); } }

	FKartPaintGroup(const FKartPaintGroup&) = delete;
	FKartPaintGroup& operator=(const FKartPaintGroup&) = delete;

private:
	const FKartPainter& Painter;
	bool bOwner;
};

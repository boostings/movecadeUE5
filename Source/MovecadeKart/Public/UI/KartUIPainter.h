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
 * Every primitive takes the next layer from the shared counter, so later calls always draw on top (Slate may
 * otherwise reorder same-layer batches). The widget returns the counter's final value from OnPaint.
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
	/** A convex or star-shaped polygon, filled as a fan from its centroid. */
	void Polygon(const TArray<FVector2D>& Points, const FLinearColor& Fill) const;
	void Lines(const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness, bool bClosed = false) const;

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

	/** Clips everything drawn until PopClip to a rectangle (axis-aligned in this painter's space). */
	void PushClip(const FVector2D& Pos, const FVector2D& Size) const;
	void PopClip() const;

	/** Registers a clickable region for mouse hover and clicks. */
	void Hit(int32 Id, const FVector2D& Pos, const FVector2D& Size) const;

	const FGeometry& Geometry() const { return Geo; }
	FVector2D Size() const { return FVector2D(Geo.GetLocalSize()); }
	float GetOpacity() const { return Opacity; }

private:
	FLinearColor Fade(const FLinearColor& Color) const { return FLinearColor(Color.R, Color.G, Color.B, Color.A * Opacity); }

	FGeometry Geo;
	FSlateWindowElementList* Out;
	int32 NextLayer() const { return (*Layer)++; }

	int32* Layer;
	float Opacity = 1.f;
	TArray<FKartHitRegion>* Hits;
	/** Hit regions are recorded only by untransformed painters (a pop doesn't move the click target). */
	bool bTransformed = false;
};

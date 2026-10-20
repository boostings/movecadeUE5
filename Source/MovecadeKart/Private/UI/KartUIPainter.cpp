#include "UI/KartUIPainter.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	FPaintGeometry RectGeometry(const FGeometry& Geo, const FVector2D& Pos, const FVector2D& Size)
	{
		return Geo.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos)));
	}
}

FKartPainter::FKartPainter(const FGeometry& InGeometry, FSlateWindowElementList& InOut, int32& InLayerCounter, TArray<FKartHitRegion>* InHits)
	: Geo(InGeometry), Out(&InOut), Layer(&InLayerCounter), Hits(InHits)
{
}

FKartPainter FKartPainter::Transformed(const FVector2D& Pivot, float Scale, float Degrees, const FVector2D& Translate) const
{
	FKartPainter Child = *this;
	const FVector2D LocalSize = FVector2D(Geo.GetLocalSize());
	const FVector2f PivotFraction(
		LocalSize.X > 0.0 ? static_cast<float>(Pivot.X / LocalSize.X) : 0.f,
		LocalSize.Y > 0.0 ? static_cast<float>(Pivot.Y / LocalSize.Y) : 0.f);
	const FSlateRenderTransform Transform(::Concatenate(FScale2f(Scale), FQuat2f(FMath::DegreesToRadians(Degrees))), FVector2f(Translate));
	Child.Geo = Geo.MakeChild(Transform, PivotFraction);
	Child.bTransformed = true;
	return Child;
}

FKartPainter FKartPainter::Faded(float Alpha) const
{
	FKartPainter Child = *this;
	Child.Opacity *= FMath::Clamp(Alpha, 0.f, 1.f);
	return Child;
}

void FKartPainter::Box(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth) const
{
	if (Size.X <= 0.0 || Size.Y <= 0.0 || (Fill.A * Opacity <= 0.001f && (OutlineWidth <= 0.f || Outline.A * Opacity <= 0.001f)))
	{
		return;
	}
	const float R = FMath::Min(Radius, static_cast<float>(FMath::Min(Size.X, Size.Y) * 0.5));
	// Brushes are copied into the draw element, so a stack brush is fine.
	const FSlateRoundedBoxBrush Brush(Fade(Fill), R, Fade(Outline), OutlineWidth);
	FSlateDrawElement::MakeBox(*Out, NextLayer(), RectGeometry(Geo, Pos, Size), &Brush, ESlateDrawEffect::None, FLinearColor::White);
}

void FKartPainter::Card(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, float Radius, float ShadowDepth, const FLinearColor& Outline, float OutlineWidth) const
{
	if (ShadowDepth > 0.f)
	{
		Box(Pos + FVector2D(0.0, ShadowDepth), Size, KartUI::Color::WithAlpha(KartUI::Color::Shadow, Fill.A), Radius);
	}
	Box(Pos, Size, Fill, Radius, Outline, OutlineWidth);
}

void FKartPainter::Gradient(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& From, const FLinearColor& To, bool bVertical, float Radius) const
{
	TArray<FSlateGradientStop> Stops;
	Stops.Emplace(FVector2f(0.f, 0.f), Fade(From));
	Stops.Emplace(bVertical ? FVector2f(0.f, Size.Y) : FVector2f(Size.X, 0.f), Fade(To));
	const float R = FMath::Min(Radius, static_cast<float>(FMath::Min(Size.X, Size.Y) * 0.5));
	// EOrientation names the stop axis: horizontal stops run left to right, so a vertical blend uses Orient_Horizontal.
	FSlateDrawElement::MakeGradient(*Out, NextLayer(), RectGeometry(Geo, Pos, Size), MoveTemp(Stops),
		bVertical ? Orient_Horizontal : Orient_Vertical, ESlateDrawEffect::None, FVector4f(R, R, R, R));
}

void FKartPainter::Circle(const FVector2D& Centre, float Radius, const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth) const
{
	Box(Centre - FVector2D(Radius), FVector2D(Radius * 2.f), Fill, Radius, Outline, OutlineWidth);
}

void FKartPainter::Polygon(const TArray<FVector2D>& Points, const FLinearColor& Fill) const
{
	if (Points.Num() < 3 || Fill.A * Opacity <= 0.001f || !FSlateApplication::IsInitialized())
	{
		return;
	}
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
	const FSlateRenderTransform& Transform = Geo.GetAccumulatedRenderTransform();
	const FColor Color = Fade(Fill).ToFColor(true);
	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& P : Points)
	{
		Centroid += P;
	}
	Centroid /= Points.Num();

	TArray<FSlateVertex> Verts;
	TArray<SlateIndex> Indices;
	Verts.Reserve(Points.Num() + 1);
	Verts.Add(FSlateVertex::Make(Transform, FVector2f(Centroid), FVector2f(0.5f, 0.5f), Color));
	for (const FVector2D& P : Points)
	{
		Verts.Add(FSlateVertex::Make(Transform, FVector2f(P), FVector2f(0.5f, 0.5f), Color));
	}
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		Indices.Add(0);
		Indices.Add(static_cast<SlateIndex>(1 + i));
		Indices.Add(static_cast<SlateIndex>(1 + (i + 1) % Points.Num()));
	}
	FSlateDrawElement::MakeCustomVerts(*Out, NextLayer(), Handle, Verts, Indices, nullptr, 0, 0);
}

void FKartPainter::Lines(const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness, bool bClosed) const
{
	if (Points.Num() < 2)
	{
		return;
	}
	TArray<FVector2f> Pts;
	Pts.Reserve(Points.Num() + 1);
	for (const FVector2D& P : Points)
	{
		Pts.Add(FVector2f(P));
	}
	if (bClosed)
	{
		Pts.Add(FVector2f(Points[0]));
	}
	FSlateDrawElement::MakeLines(*Out, NextLayer(), Geo.ToPaintGeometry(), MoveTemp(Pts), ESlateDrawEffect::None, Fade(Color), true, Thickness);
}

FVector2D FKartPainter::Measure(const FString& String, const FSlateFontInfo& Font)
{
	if (!FSlateApplication::IsInitialized())
	{
		return FVector2D(String.Len() * Font.Size * 0.55, Font.Size * 1.2);
	}
	return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(String, Font));
}

FVector2D FKartPainter::Text(const FString& String, const FSlateFontInfo& Font, const FVector2D& Pos, const FLinearColor& Color, const FVector2D& Align, float ShadowDepth) const
{
	if (String.IsEmpty())
	{
		return FVector2D::ZeroVector;
	}
	const FVector2D Size = Measure(String, Font);
	const FVector2D TopLeft = Pos - Size * Align;
	if (ShadowDepth > 0.f)
	{
		FSlateFontInfo ShadowFont = Font;
		ShadowFont.OutlineSettings.OutlineColor = KartUI::Color::WithAlpha(KartUI::Color::Shadow, Color.A * Opacity * 1.6f);
		FSlateDrawElement::MakeText(*Out, NextLayer(), RectGeometry(Geo, TopLeft + FVector2D(0.0, ShadowDepth), Size), String, ShadowFont,
			ESlateDrawEffect::None, KartUI::Color::WithAlpha(KartUI::Color::Shadow, Color.A * Opacity * 1.6f));
	}
	FSlateFontInfo Faded = Font;
	Faded.OutlineSettings.OutlineColor = Fade(Font.OutlineSettings.OutlineColor);
	FSlateDrawElement::MakeText(*Out, NextLayer(), RectGeometry(Geo, TopLeft, Size), String, Faded, ESlateDrawEffect::None, Fade(Color));
	return Size;
}

void FKartPainter::Hit(int32 Id, const FVector2D& Pos, const FVector2D& Size) const
{
	if (Hits && !bTransformed && Opacity > 0.5f)
	{
		Hits->Add({ Id, Pos, Size });
	}
}

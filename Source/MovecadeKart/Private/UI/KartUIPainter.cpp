#include "UI/KartUIPainter.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Hash/CityHash.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	FPaintGeometry RectGeometry(const FGeometry& Geo, const FVector2D& Pos, const FVector2D& Size)
	{
		return Geo.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos)));
	}

	/**
	 * Layer batching. Slate merges every batchable element of a layer into its first compatible batch, so two
	 * elements may share a layer only if nothing that could draw between them does. The painter therefore
	 * keeps a run: consecutive primitives with the same batch key (identical Slate batch parameters) share
	 * one layer; anything else starts a new one. Paint order is exactly the call order, as before, but a row
	 * of chequer cells, a card and its shadow, or a list of same-font labels become one batch.
	 *
	 * The run is per thread and tied to the counter it was taken from and the value the counter had, so a
	 * different widget, another painter or a direct use of the counter always starts a fresh layer.
	 */
	struct FLayerRun
	{
		const int32* Counter = nullptr;
		int32 After = MIN_int32;
		uint64 Key = 0;
	};
	thread_local FLayerRun GRun;

	/** An open item group on a counter: every item restarts at Base; Planes is the deepest item so far. */
	struct FGroup
	{
		const int32* Counter = nullptr;
		int32 Base = 0;
		int32 Plane = 0;
		int32 Planes = 0;
		/** The previous primitive's key in this item: a repeat stays on its plane (a card and its shadow). */
		uint64 LastKey = 0;
	};
	thread_local FGroup GGroup;

	/** 0 turns off layer runs and item groups (one layer per primitive, as before): for measuring with stat Slate. */
	TAutoConsoleVariable<int32> CVarBatching(TEXT("Kart.UI.Batching"), 3,
		TEXT("Painter layer batching: 0 off, 1 runs only, 2 groups only, 3 both (default)."));

	constexpr uint64 Mix(uint64 Hash, uint64 Value)
	{
		return (Hash ^ (Value + 0x9e3779b97f4a7c15ull + (Hash << 6) + (Hash >> 2))) | 1ull;
	}

	uint64 Bits(double Value)
	{
		const float F = static_cast<float>(Value);
		uint32 Out;
		FMemory::Memcpy(&Out, &F, sizeof(Out));
		return Out;
	}

	enum class EKind : uint64 { FlatBox = 11, RoundBox, Gradient, Text, Verts, Lines };

	const FSlateBrush* WhiteBrush()
	{
		static const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
		return Brush;
	}

	/** The white texture custom verts draw with, looked up once. */
	const FSlateResourceHandle& WhiteHandle()
	{
		static FSlateResourceHandle Handle;
		if (!Handle.IsValid() && FSlateApplication::IsInitialized())
		{
			Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush());
		}
		return Handle;
	}

	/** Reused vertex and index buffers (MakeCustomVerts copies them into the element). */
	TArray<FSlateVertex>& ScratchVerts()
	{
		thread_local TArray<FSlateVertex> Verts;
		Verts.Reset();
		return Verts;
	}

	TArray<SlateIndex>& ScratchIndices()
	{
		thread_local TArray<SlateIndex> Indices;
		Indices.Reset();
		return Indices;
	}

	uint64 FontKey(const FSlateFontInfo& Font)
	{
		uint64 Key = Mix(static_cast<uint64>(EKind::Text), reinterpret_cast<UPTRINT>(Font.FontObject.Get()));
		Key = Mix(Key, reinterpret_cast<UPTRINT>(Font.CompositeFont.Get()));
		Key = Mix(Key, Bits(Font.Size));
		Key = Mix(Key, static_cast<uint64>(Font.OutlineSettings.OutlineSize));
		return Mix(Key, GetTypeHash(Font.TypefaceFontName));
	}

	/** Word-wrap results, keyed by text, font and width: menus wrap the same few strings every frame. */
	struct FWrapCache
	{
		TMap<uint64, TArray<FString>> Entries;
	};
}

FKartPainter::FKartPainter(const FGeometry& InGeometry, FSlateWindowElementList& InOut, int32& InLayerCounter, TArray<FKartHitRegion>* InHits)
	: Geo(InGeometry), Out(&InOut), Layer(&InLayerCounter), Hits(InHits)
{
	// A new paint on this counter: never continue a run left over from an earlier frame or widget.
	if (GRun.Counter == Layer)
	{
		GRun = FLayerRun();
	}
}

int32 FKartPainter::LayerFor(uint64 Key, bool bNewRun) const
{
	const int32 Mode = CVarBatching.GetValueOnAnyThread();
	if (GGroup.Counter == Layer && (Mode & 2))
	{
		if ((Mode & 1) && !bNewRun && Key != 0 && Key == GGroup.LastKey && GGroup.Plane > 0)
		{
			return GGroup.Base + GGroup.Plane - 1;
		}
		GGroup.LastKey = Key;
		const int32 Id = GGroup.Base + GGroup.Plane++;
		GGroup.Planes = FMath::Max(GGroup.Planes, GGroup.Plane);
		*Layer = FMath::Max(*Layer, Id + 1);
		GRun = FLayerRun();
		return Id;
	}
	if ((Mode & 1) && !bNewRun && Key != 0 && GRun.Counter == Layer && GRun.After == *Layer && GRun.Key == Key)
	{
		return *Layer - 1;
	}
	const int32 Id = (*Layer)++;
	GRun.Counter = Layer;
	GRun.After = *Layer;
	GRun.Key = Key;
	return Id;
}

bool FKartPainter::IsRotated() const
{
	float A, B, C, D;
	Geo.GetAccumulatedRenderTransform().GetMatrix().GetMatrix(A, B, C, D);
	return FMath::Abs(B) > 1e-4f || FMath::Abs(C) > 1e-4f;
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
	if (R < 0.5f && OutlineWidth <= 0.f && !IsRotated())
	{
		// A plain rectangle: the white box, which batches with every other plain rectangle.
		FSlateDrawElement::MakeBox(*Out, LayerFor(static_cast<uint64>(EKind::FlatBox)), RectGeometry(Geo, Pos, Size), WhiteBrush(), ESlateDrawEffect::None, Fade(Fill));
		return;
	}
	// A transparent outline still insets the fill, so the weight is kept as given.
	const float Weight = FMath::Max(OutlineWidth, 0.f);
	// The rounded-box shader takes the size, radius and outline weight as batch parameters.
	uint64 Key = Mix(static_cast<uint64>(EKind::RoundBox), Bits(Size.X));
	Key = Mix(Mix(Mix(Key, Bits(Size.Y)), Bits(R)), Bits(Weight));
	// The element takes the fill from the tint and the outline from the brush (copied, so a stack brush is fine).
	const FSlateRoundedBoxBrush Brush(FLinearColor::White, R, Fade(Outline), Weight);
	FSlateDrawElement::MakeBox(*Out, LayerFor(Key), RectGeometry(Geo, Pos, Size), &Brush, ESlateDrawEffect::None, Fade(Fill));
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
	if (Size.X <= 0.0 || Size.Y <= 0.0 || (From.A * Opacity <= 0.001f && To.A * Opacity <= 0.001f))
	{
		return;
	}
	const float R = FMath::Min(Radius, static_cast<float>(FMath::Min(Size.X, Size.Y) * 0.5));
	if (R < 0.5f && FSlateApplication::IsInitialized() && WhiteHandle().IsValid())
	{
		// Square corners: a four-vertex quad, no gradient element (and no stop array) needed; it batches with polygons.
		const FSlateRenderTransform& Transform = Geo.GetAccumulatedRenderTransform();
		const FColor A = Fade(From).ToFColor(true), B = Fade(To).ToFColor(true);
		TArray<FSlateVertex>& Verts = ScratchVerts();
		TArray<SlateIndex>& Indices = ScratchIndices();
		const FVector2f P0(Pos), P1(Pos + Size);
		Verts.Add(FSlateVertex::Make(Transform, FVector2f(P0.X, P0.Y), FVector2f(0.5f, 0.5f), A));
		Verts.Add(FSlateVertex::Make(Transform, FVector2f(P1.X, P0.Y), FVector2f(0.5f, 0.5f), bVertical ? A : B));
		Verts.Add(FSlateVertex::Make(Transform, FVector2f(P1.X, P1.Y), FVector2f(0.5f, 0.5f), B));
		Verts.Add(FSlateVertex::Make(Transform, FVector2f(P0.X, P1.Y), FVector2f(0.5f, 0.5f), bVertical ? B : A));
		Indices.Append({ 0, 1, 2, 0, 2, 3 });
		FSlateDrawElement::MakeCustomVerts(*Out, LayerFor(static_cast<uint64>(EKind::Verts)), WhiteHandle(), Verts, Indices, nullptr, 0, 0);
		return;
	}
	TArray<FSlateGradientStop> Stops;
	Stops.Reserve(2);
	Stops.Emplace(FVector2f(0.f, 0.f), Fade(From));
	Stops.Emplace(bVertical ? FVector2f(0.f, Size.Y) : FVector2f(Size.X, 0.f), Fade(To));
	const uint64 Key = Mix(Mix(Mix(static_cast<uint64>(EKind::Gradient), Bits(Size.X)), Bits(Size.Y)), Bits(R));
	// EOrientation names the stop axis: horizontal stops run left to right, so a vertical blend uses Orient_Horizontal.
	FSlateDrawElement::MakeGradient(*Out, LayerFor(Key), RectGeometry(Geo, Pos, Size), MoveTemp(Stops),
		bVertical ? Orient_Horizontal : Orient_Vertical, ESlateDrawEffect::None, FVector4f(R, R, R, R));
}

void FKartPainter::Circle(const FVector2D& Centre, float Radius, const FLinearColor& Fill, const FLinearColor& Outline, float OutlineWidth) const
{
	Box(Centre - FVector2D(Radius), FVector2D(Radius * 2.f), Fill, Radius, Outline, OutlineWidth);
}

void FKartPainter::Polygon(TConstArrayView<FVector2D> Points, const FLinearColor& Fill) const
{
	if (Points.Num() < 3 || Fill.A * Opacity <= 0.001f || !FSlateApplication::IsInitialized() || !WhiteHandle().IsValid())
	{
		return;
	}
	const FSlateRenderTransform& Transform = Geo.GetAccumulatedRenderTransform();
	const FColor Color = Fade(Fill).ToFColor(true);
	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& P : Points)
	{
		Centroid += P;
	}
	Centroid /= Points.Num();

	TArray<FSlateVertex>& Verts = ScratchVerts();
	TArray<SlateIndex>& Indices = ScratchIndices();
	Verts.Reserve(Points.Num() + 1);
	Indices.Reserve(Points.Num() * 3);
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
	FSlateDrawElement::MakeCustomVerts(*Out, LayerFor(static_cast<uint64>(EKind::Verts)), WhiteHandle(), Verts, Indices, nullptr, 0, 0);
}

void FKartPainter::Lines(TConstArrayView<FVector2D> Points, const FLinearColor& Color, float Thickness, bool bClosed) const
{
	if (Points.Num() < 2 || Color.A * Opacity <= 0.001f)
	{
		return;
	}
	// The element owns its points, so this one array is the only allocation.
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
	FSlateDrawElement::MakeLines(*Out, LayerFor(Mix(static_cast<uint64>(EKind::Lines), Bits(Thickness))), Geo.ToPaintGeometry(), MoveTemp(Pts),
		ESlateDrawEffect::None, Fade(Color), true, Thickness);
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
	if (Color.A * Opacity <= 0.001f)
	{
		return Size;
	}
	const FVector2D TopLeft = Pos - Size * Align;
	const uint64 Key = FontKey(Font);
	if (ShadowDepth > 0.f)
	{
		FSlateFontInfo ShadowFont = Font;
		ShadowFont.OutlineSettings.OutlineColor = KartUI::Color::WithAlpha(KartUI::Color::Shadow, Color.A * Opacity * 1.6f);
		FSlateDrawElement::MakeText(*Out, LayerFor(Key), RectGeometry(Geo, TopLeft + FVector2D(0.0, ShadowDepth), Size), String, ShadowFont,
			ESlateDrawEffect::None, KartUI::Color::WithAlpha(KartUI::Color::Shadow, Color.A * Opacity * 1.6f));
	}
	// The text always goes over its own shadow: its outline would otherwise merge into the shadow's batch.
	const int32 TextLayer = LayerFor(Key, ShadowDepth > 0.f);
	if (Opacity >= 1.f)
	{
		FSlateDrawElement::MakeText(*Out, TextLayer, RectGeometry(Geo, TopLeft, Size), String, Font, ESlateDrawEffect::None, Color);
	}
	else
	{
		FSlateFontInfo FadedFont = Font;
		FadedFont.OutlineSettings.OutlineColor = Fade(Font.OutlineSettings.OutlineColor);
		FSlateDrawElement::MakeText(*Out, TextLayer, RectGeometry(Geo, TopLeft, Size), String, FadedFont, ESlateDrawEffect::None, Fade(Color));
	}
	return Size;
}

const TArray<FString>& FKartPainter::WrapLinesCached(const FString& String, const FSlateFontInfo& Font, double MaxWidth)
{
	static FWrapCache Cache;
	const uint64 Key = Mix(Mix(FontKey(Font), CityHash64(reinterpret_cast<const char*>(*String), String.Len() * sizeof(TCHAR))), Bits(MaxWidth));
	if (const TArray<FString>* Found = Cache.Entries.Find(Key))
	{
		return *Found;
	}
	if (Cache.Entries.Num() >= 512)
	{
		Cache.Entries.Reset(); // a bounded cache: menus only ever wrap a few dozen strings
	}
	TArray<FString> Words;
	String.ParseIntoArray(Words, TEXT(" "));
	TArray<FString> Lines;
	FString Line;
	for (const FString& Word : Words)
	{
		const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		if (!Line.IsEmpty() && Measure(Candidate, Font).X > MaxWidth)
		{
			Lines.Add(Line);
			Line = Word;
		}
		else
		{
			Line = Candidate;
		}
	}
	if (!Line.IsEmpty())
	{
		Lines.Add(Line);
	}
	return Cache.Entries.Add(Key, MoveTemp(Lines));
}

TArray<FString> FKartPainter::WrapLines(const FString& String, const FSlateFontInfo& Font, double MaxWidth)
{
	return WrapLinesCached(String, Font, MaxWidth);
}

FVector2D FKartPainter::TextWrapped(const FText& String, const FSlateFontInfo& Font, const FVector2D& Pos, double MaxWidth, const FLinearColor& Color,
	double AlignX, float ShadowDepth) const
{
	const TArray<FString>& Lines = WrapLinesCached(String.ToString(), Font, MaxWidth);
	const double LineH = Font.Size * 1.3;
	double Widest = 0.0;
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		Widest = FMath::Max(Widest, Text(Lines[i], Font, Pos + FVector2D(0.0, i * LineH), Color, FVector2D(AlignX, 0.0), ShadowDepth).X);
	}
	return FVector2D(Widest, Lines.Num() * LineH);
}

void FKartPainter::PushClip(const FVector2D& Pos, const FVector2D& Size) const
{
	Out->PushClip(FSlateClippingZone(Geo.MakeChild(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos)))));
	GRun.Key = 0; // a new clipping state never batches with what came before
}

void FKartPainter::PopClip() const
{
	Out->PopClip();
	GRun.Key = 0;
}

bool FKartPainter::BeginGroup() const
{
	if (GGroup.Counter != nullptr)
	{
		return false;
	}
	GGroup.Counter = Layer;
	GGroup.Base = *Layer;
	GGroup.Plane = 0;
	GGroup.Planes = 0;
	return true;
}

void FKartPainter::NextItem() const
{
	if (GGroup.Counter == Layer)
	{
		GGroup.Plane = 0;
		GGroup.LastKey = 0;
	}
}

void FKartPainter::EndGroup() const
{
	if (GGroup.Counter == Layer)
	{
		*Layer = FMath::Max(*Layer, GGroup.Base + GGroup.Planes);
		GGroup = FGroup();
		GRun = FLayerRun();
	}
}

void FKartPainter::Hit(int32 Id, const FVector2D& Pos, const FVector2D& Size) const
{
	if (Hits && !bTransformed && Opacity > 0.5f)
	{
		Hits->Add({ Id, Pos, Size });
	}
}

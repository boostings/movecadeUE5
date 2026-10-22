#include "UI/Widgets/SKartCurtain.h"

#include "UI/KartUIPainter.h"

using namespace KartUI;

namespace
{
	/** The wipe holds a moment past full cover before the callback (and before it reports itself open). */
	constexpr double SettleSeconds = 0.05;
	/** Level loads hitch; each tick advances at most this much so the wipe doesn't skip to the end. */
	constexpr float MaxStep = 1.f / 30.f;
	/** The bands' slant, as a fraction of the screen's height, and each band's width as a fraction of its width. */
	constexpr double Slant = 0.35;
	constexpr double BandWidth = 0.07;
	constexpr int32 Bands = 4;
	/** Bands overlap by a unit, and the fill runs a little past the screen's edges, so no seams show. */
	constexpr double Overlap = 1.0;
	constexpr double EdgeClamp = 2.0;
	constexpr double FillOverrun = 4.0;
	/** The wordmark fades in over the last bit of closing and out over the first bit of opening. */
	constexpr float WordmarkFrom = 0.95f;
	constexpr float WordmarkTo = 1.1f;
	constexpr float WordmarkFadeIn = 0.05f;
	constexpr float WordmarkFadeOut = 0.1f;
	/** The wordmark in the bottom-right corner, with three bobbing dots under it. */
	constexpr double WordmarkRight = 260.0;
	constexpr double WordmarkUp = 110.0;
	constexpr float WordmarkSize = 44.f;
	constexpr double DotSpin = 6.0;
	constexpr double DotPhase = 0.9;
	constexpr double DotBob = 6.0;
	constexpr double DotSpacing = 28.0;
	constexpr double DotsDrop = 46.0;
	constexpr float DotRadius = 7.f;
}

void SKartCurtain::Construct(const FArguments& InArgs)
{
	bClosing = InArgs._Closing;
	SetVisibility(EVisibility::HitTestInvisible);
	ForceVolatile(true);
}

void SKartCurtain::Close(TFunction<void()> InOnClosed)
{
	bClosing = true;
	Elapsed = 0.0;
	OnClosed = MoveTemp(InOnClosed);
	SetVisibility(EVisibility::Visible); // swallow clicks while the screen changes
}

void SKartCurtain::Open()
{
	bClosing = false;
	Elapsed = 0.0;
	OnClosed = nullptr;
	SetVisibility(EVisibility::HitTestInvisible);
}

bool SKartCurtain::IsFinishedOpening() const
{
	return !bClosing && Elapsed >= OpenSeconds + SettleSeconds;
}

float SKartCurtain::Coverage() const
{
	return bClosing
		? Ease::OutCubic(static_cast<float>(Elapsed / CloseSeconds))
		: 1.f + Ease::InOutSine(static_cast<float>(Elapsed / OpenSeconds));
}

void SKartCurtain::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	Elapsed += FMath::Min(InDeltaTime, MaxStep);
	if (bClosing && Elapsed >= CloseSeconds + SettleSeconds && OnClosed)
	{
		TFunction<void()> Callback = MoveTemp(OnClosed);
		OnClosed = nullptr;
		Callback();
	}
}

int32 SKartCurtain::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float C = Coverage();
	if (C <= 0.f || C >= 2.f)
	{
		return LayerId;
	}
	int32 Layer = LayerId;
	FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const FVector2D Size = P.Size();
	const double SlantX = Size.Y * Slant;
	const double Span = Size.X + SlantX;
	static const FLinearColor BandColors[Bands] = { Color::Sun, Color::Hot, Color::Sky, Color::Grape };
	const double BandW = Size.X * BandWidth;
	auto Para = [&](double X0, double X1, const FLinearColor& Fill)
	{
		X0 = FMath::Max(X0, -SlantX - EdgeClamp);
		X1 = FMath::Min(X1, Size.X + SlantX + EdgeClamp);
		if (X1 <= X0)
		{
			return;
		}
		P.Polygon({ FVector2D(X0 + SlantX, 0.0), FVector2D(X1 + SlantX, 0.0), FVector2D(X1, Size.Y), FVector2D(X0, Size.Y) }, Fill);
	};
	if (C <= 1.f)
	{
		// Closing: the bands lead the night fill in from the right.
		const double Lead = Size.X + SlantX - (Span + BandW * Bands) * C;
		for (int32 i = 0; i < Bands; ++i)
		{
			Para(Lead + i * BandW, Lead + (i + 1) * BandW + Overlap, BandColors[i]);
		}
		Para(Lead + Bands * BandW, Size.X + SlantX + FillOverrun, Color::Night);
	}
	else
	{
		// Opening: the fill's trailing edge, then the bands, carry on off to the left.
		const double Trail = Size.X - (Size.X + SlantX + BandW * Bands) * (C - 1.f);
		Para(-SlantX - FillOverrun, Trail + Overlap, Color::Night);
		for (int32 i = 0; i < Bands; ++i)
		{
			Para(Trail + i * BandW, Trail + (i + 1) * BandW + Overlap, BandColors[Bands - 1 - i]);
		}
	}

	// While fully covered: the wordmark, small, so a long load doesn't look frozen on a blank screen.
	if (C > WordmarkFrom && C < WordmarkTo)
	{
		const float A = C <= 1.f ? (C - WordmarkFrom) / WordmarkFadeIn : 1.f - (C - 1.f) / WordmarkFadeOut;
		const FKartPainter Faded = P.Faded(A);
		const FVector2D Centre(Size.X - WordmarkRight, Size.Y - WordmarkUp);
		Faded.Text(TEXT("MOVECADE"), Font(EFont::Display, WordmarkSize), Centre, Color::White, FVector2D(0.5, 0.5));
		const double Spin = FPlatformTime::Seconds() * DotSpin;
		for (int32 i = 0; i < 3; ++i)
		{
			const double Bob = FMath::Sin(Spin - i * DotPhase) * DotBob;
			Faded.Circle(Centre + FVector2D(-DotSpacing + i * DotSpacing, DotsDrop + Bob), DotRadius, BandColors[i]);
		}
	}
	return Layer;
}

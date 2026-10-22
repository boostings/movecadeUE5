#include "UI/Widgets/SKartCurtain.h"

#include "UI/KartUIPainter.h"

using namespace KartUI;

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
	return !bClosing && Elapsed >= OpenSeconds + 0.05;
}

float SKartCurtain::Coverage() const
{
	return bClosing
		? Ease::OutCubic(static_cast<float>(Elapsed / CloseSeconds))
		: 1.f + Ease::InOutSine(static_cast<float>(Elapsed / OpenSeconds));
}

void SKartCurtain::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Level loads hitch; clamp so the wipe doesn't skip straight to the end.
	Elapsed += FMath::Min(InDeltaTime, 1.f / 30.f);
	if (bClosing && Elapsed >= CloseSeconds + 0.05 && OnClosed)
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
	const double Slant = Size.Y * 0.35;
	const double Span = Size.X + Slant;
	const FLinearColor Bands[] = { Color::Sun, Color::Hot, Color::Sky, Color::Grape };
	const double BandW = Size.X * 0.07;
	auto Para = [&](double X0, double X1, const FLinearColor& Fill)
	{
		X0 = FMath::Max(X0, -Slant - 2.0);
		X1 = FMath::Min(X1, Size.X + Slant + 2.0);
		if (X1 <= X0)
		{
			return;
		}
		P.Polygon({ FVector2D(X0 + Slant, 0.0), FVector2D(X1 + Slant, 0.0), FVector2D(X1, Size.Y), FVector2D(X0, Size.Y) }, Fill);
	};
	if (C <= 1.f)
	{
		// Closing: the bands lead the night fill in from the right.
		const double Lead = Size.X + Slant - (Span + BandW * 4.0) * C;
		for (int32 i = 0; i < 4; ++i)
		{
			Para(Lead + i * BandW, Lead + (i + 1) * BandW + 1.0, Bands[i]);
		}
		Para(Lead + 4.0 * BandW, Size.X + Slant + 4.0, Color::Night);
	}
	else
	{
		// Opening: the fill's trailing edge, then the bands, carry on off to the left.
		const double Trail = Size.X - (Size.X + Slant + BandW * 4.0) * (C - 1.f);
		Para(-Slant - 4.0, Trail + 1.0, Color::Night);
		for (int32 i = 0; i < 4; ++i)
		{
			Para(Trail + i * BandW, Trail + (i + 1) * BandW + 1.0, Bands[3 - i]);
		}
	}

	// While fully covered: the wordmark, small, so a long load doesn't look frozen on a blank screen.
	if (C > 0.95f && C < 1.1f)
	{
		const float A = C <= 1.f ? (C - 0.95f) / 0.05f : 1.f - (C - 1.f) / 0.1f;
		const FKartPainter Faded = P.Faded(A);
		const FVector2D Centre(Size.X - 260.0, Size.Y - 110.0);
		Faded.Text(TEXT("MOVECADE"), Font(EFont::Display, 44.f), Centre, Color::White, FVector2D(0.5, 0.5));
		const double Spin = FPlatformTime::Seconds() * 6.0;
		for (int32 i = 0; i < 3; ++i)
		{
			const double Bob = FMath::Sin(Spin - i * 0.9) * 6.0;
			Faded.Circle(Centre + FVector2D(-28.0 + i * 28.0, 46.0 + Bob), 7.f, Bands[i]);
		}
	}
	return Layer;
}

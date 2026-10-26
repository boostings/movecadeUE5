#include "Mobile/SMovecadeSafeAreaMask.h"

#include "Mobile/MovecadeOrientationSubsystem.h"
#include "UI/KartUIPainter.h"

namespace
{
	/** The unsafe bands: dark enough to read as "covered", light enough to see what is under them. */
	const FLinearColor BandColor(0.f, 0.f, 0.f, 0.55f);
	/** The safe edge line. */
	const FLinearColor EdgeColor(0.2f, 0.9f, 1.f, 0.8f);
	const FLinearColor HomeBarColor(1.f, 1.f, 1.f, 0.8f);

	/** Camera housing (Dynamic Island) proportions: its long side as a share of the screen edge, its thickness and
	 *  its gap from the edge as shares of the inset it sits in (iPhone 15 Pro: 126 x 37 pt, 11 pt down, 59 pt inset). */
	constexpr float HousingLength = 0.32f;
	constexpr float HousingThickness = 37.f / 59.f;
	constexpr float HousingGap = 11.f / 59.f;
	/** Home indicator: 134 x 5 pt on a 393 pt wide phone, centred in the bottom inset. */
	constexpr float HomeBarLength = 134.f / 393.f;
	constexpr float HomeBarThicknessShare = 0.12f;
}

void SMovecadeSafeAreaMask::Construct(const FArguments& InArgs, const UMovecadeOrientationSubsystem* InScreen)
{
	Screen = InScreen;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SMovecadeSafeAreaMask::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UMovecadeOrientationSubsystem* Subsystem = Screen.Get();
	if (!Subsystem || Subsystem->GetSafeArea().IsZero())
	{
		return LayerId;
	}
	int32 Layer = LayerId;
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const FVector2D Size = FVector2D(AllottedGeometry.GetLocalSize());
	const FMargin In = Subsystem->GetState().SafeInsets(Size);
	const double Middle = Size.Y - In.Top - In.Bottom;

	// Bands: top and bottom full width, the sides between them.
	auto Band = [&P](const FVector2D& Pos, const FVector2D& BandSize)
	{
		if (BandSize.X > 0.0 && BandSize.Y > 0.0)
		{
			P.Box(Pos, BandSize, BandColor);
		}
	};
	Band(FVector2D(0, 0), FVector2D(Size.X, In.Top));
	Band(FVector2D(0, Size.Y - In.Bottom), FVector2D(Size.X, In.Bottom));
	Band(FVector2D(0, In.Top), FVector2D(In.Left, Middle));
	Band(FVector2D(Size.X - In.Right, In.Top), FVector2D(In.Right, Middle));

	// The safe rect's edge.
	const FVector2D TopLeft(In.Left, In.Top);
	const FVector2D BottomRight(Size.X - In.Right, Size.Y - In.Bottom);
	P.Lines({ TopLeft, FVector2D(BottomRight.X, TopLeft.Y), BottomRight, FVector2D(TopLeft.X, BottomRight.Y) }, EdgeColor, 1.f, true);

	// The camera housing sits in the top inset upright, in the left inset sideways.
	if (Subsystem->IsPortrait() && In.Top > 0.f)
	{
		const FVector2D Housing(Size.X * HousingLength, In.Top * HousingThickness);
		P.Box(FVector2D((Size.X - Housing.X) * 0.5, In.Top * HousingGap), Housing, FLinearColor::Black, Housing.Y * 0.5f);
	}
	else if (!Subsystem->IsPortrait() && In.Left > 0.f)
	{
		const FVector2D Housing(In.Left * HousingThickness, Size.Y * HousingLength);
		P.Box(FVector2D(In.Left * HousingGap, (Size.Y - Housing.Y) * 0.5), Housing, FLinearColor::Black, Housing.X * 0.5f);
	}
	if (In.Bottom > 0.f)
	{
		const FVector2D Bar(FMath::Min(Size.X, Size.Y) * HomeBarLength, FMath::Max(2.f, In.Bottom * HomeBarThicknessShare));
		P.Box(FVector2D((Size.X - Bar.X) * 0.5, Size.Y - (In.Bottom + Bar.Y) * 0.5), Bar, HomeBarColor, Bar.Y * 0.5f);
	}
	return Layer;
}

#include "UI/KartPaintWidget.h"

#include "Framework/Application/SlateApplication.h"
#include "UI/KartSafeArea.h"
#include "Widgets/SLeafWidget.h"

namespace
{
	class SKartPaintBox : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SKartPaintBox) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UKartPaintWidget>, Owner)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Owner = InArgs._Owner;
			SetVisibility(EVisibility::HitTestInvisible);
			ForceVolatile(true); // painted from game state every frame
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1.0, 1.0); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const UKartPaintWidget* Widget = Owner.Get();
			if (!Widget || !Widget->OnPaintKart)
			{
				return LayerId;
			}
			int32 Layer = LayerId;
			const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
			FKartScreenFrame Frame;
			Frame.Size = P.Size();
			const KartSafeArea::FKartSafeFrame Safe = KartSafeArea::FKartSafeFrame::ForHud(AllottedGeometry);
			Frame.SafeMin = Safe.SafeMin;
			Frame.SafeMax = Safe.SafeMax;
			Frame.bPortrait = Safe.bPortrait;
			Frame.bPhone = Safe.bPhone;
			Frame.Now = FSlateApplication::Get().GetCurrentTime();
			Frame.Age = 10.0; // well past every entrance animation: painted settled
			const UKartUISubsystem* UI = UKartUISubsystem::Get(Widget->GetWorld());
			Frame.Device = UI ? UI->GetInputDevice() : EKartInputDevice::Keyboard;
			Widget->OnPaintKart(P, Frame);
			return Layer;
		}

	private:
		TWeakObjectPtr<UKartPaintWidget> Owner;
	};
}

TSharedRef<SWidget> UKartPaintWidget::RebuildWidget()
{
	return SNew(SKartPaintBox).Owner(this);
}

#include "SDeviceTouchControls.h"

#include "Input/DeviceControlsSubsystem.h"
#include "Mobile/MovecadeOrientationSubsystem.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

#define LOCTEXT_NAMESPACE "DeviceTouchControls"

using namespace MovecadeKart::Input;

namespace
{
	/** The mouse's pointer id (touch pointers are 0..9). */
	constexpr int32 MousePointer = 1000;

	// Sizes, as shares of the screen's short side (all at or above SDeviceTouchControls::MinTargetShare).
	constexpr float MarginShare = 0.05f;
	constexpr float LargeButtonShare = 0.30f;
	constexpr float ButtonShare = 0.22f;
	constexpr float ChipShare = 0.14f;
	constexpr float CalibrateChipWidth = 2.6f;
	constexpr float SensitivityLabelWidth = 1.7f;
	constexpr float ChipGap = 0.25f;
	/** A raised button sits this far in from the edge (of its own diameter) and this much above its neighbour. */
	constexpr float RaisedInset = 0.35f;
	constexpr float RaisedGap = 0.06f;
	/** Fingers are wide: a button answers a little outside its circle. */
	constexpr float HitSlop = 1.15f;

	constexpr float IdleAlpha = 0.42f;
	constexpr float PressedAlpha = 0.78f;
	constexpr float LabelShare = 0.2f;
	constexpr float ChipLabelShare = 0.38f;
	constexpr float HintShare = 0.055f;

	FLinearColor ButtonColor(EDeviceButton Button)
	{
		switch (Button)
		{
		case EDeviceButton::Gas: return KartUI::Color::Mint;
		case EDeviceButton::Brake: return KartUI::Color::Hot;
		case EDeviceButton::Drift: return KartUI::Color::Sky;
		case EDeviceButton::Item: return KartUI::Color::Sun;
		case EDeviceButton::B: return KartUI::Color::Grape;
		default: return KartUI::Color::Sun;
		}
	}
}

void SDeviceTouchControls::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	Layout = InArgs._Layout;
	ButtonDown.Init(false, Layout.Buttons.Num());
	SetCanTick(true);
	ForceVolatile(true);
}

bool SDeviceTouchControls::IsBlocked() const
{
	return IsMenuOpen && IsMenuOpen();
}

void SDeviceTouchControls::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// A menu opening under a held finger lets that finger go: nothing stays pressed behind the menu.
	const bool bBlocked = IsBlocked();
	SetVisibility(bBlocked ? EVisibility::HitTestInvisible : EVisibility::Visible);
	if (bBlocked && Held.Num() > 0)
	{
		const FVector2D Size(AllottedGeometry.GetLocalSize());
		TArray<int32> Pointers;
		Held.GetKeys(Pointers);
		for (const int32 Pointer : Pointers)
		{
			Up(Pointer, FVector2D(-1.0, -1.0), Size);
		}
	}
	if (const UMovecadeOrientationSubsystem* Screen = UMovecadeOrientationSubsystem::Get(Owner.Get()))
	{
		SafeArea = Screen->GetSafeArea();
	}
	if (const UDeviceControlsSubsystem* Subsystem = Owner.Get(); Subsystem && Subsystem->GetSensitivity() != ShownSensitivity)
	{
		ShownSensitivity = Subsystem->GetSensitivity();
		SensitivityLabel = FString::Printf(TEXT("%.1fx"), ShownSensitivity);
	}
}

void SDeviceTouchControls::SafeRect(const FVector2D& Size, FVector2D& OutOrigin, FVector2D& OutSize) const
{
	// Inside the notch, the rounded corners and the home indicator (UMovecadeOrientationSubsystem).
	const FMargin Insets = SafeArea.Insets(Size);
	OutOrigin = FVector2D(Insets.Left, Insets.Top);
	OutSize = FVector2D(FMath::Max(1.0, Size.X - Insets.Left - Insets.Right), FMath::Max(1.0, Size.Y - Insets.Top - Insets.Bottom));
}

void SDeviceTouchControls::ButtonCircle(int32 Index, const FVector2D& FullSize, FVector2D& OutCentre, float& OutRadius) const
{
	FVector2D Origin, Size;
	SafeRect(FullSize, Origin, Size);
	const float Short = static_cast<float>(FMath::Min(Size.X, Size.Y));
	const float Margin = Short * MarginShare;
	const FDeviceButtonSpec& Spec = Layout.Buttons[Index];
	const float D = Short * (Spec.bLarge ? LargeButtonShare : ButtonShare);
	OutRadius = D * 0.5f;
	const float Base = Short * LargeButtonShare;
	const float RaisedY = Size.Y - Margin - Base - Short * RaisedGap - OutRadius;
	switch (Spec.Anchor)
	{
	case EButtonAnchor::BottomLeft: OutCentre = FVector2D(Margin + OutRadius, Size.Y - Margin - OutRadius); break;
	case EButtonAnchor::LeftRaised: OutCentre = FVector2D(Margin + OutRadius + D * RaisedInset, RaisedY); break;
	case EButtonAnchor::RightRaised: OutCentre = FVector2D(Size.X - Margin - OutRadius - D * RaisedInset, RaisedY); break;
	case EButtonAnchor::BottomCentre: OutCentre = FVector2D(Size.X * 0.5, Size.Y - Margin - OutRadius); break;
	default: OutCentre = FVector2D(Size.X - Margin - OutRadius, Size.Y - Margin - OutRadius); break;
	}
	OutCentre += Origin;
}

void SDeviceTouchControls::Chips(const FVector2D& FullSize, TArray<FChip, TInlineAllocator<4>>& Out) const
{
	FVector2D Origin, Size;
	SafeRect(FullSize, Origin, Size);
	const float Short = static_cast<float>(FMath::Min(Size.X, Size.Y));
	const float H = Short * ChipShare;
	const float Gap = H * ChipGap;
	const float Total = H + Gap + H * CalibrateChipWidth + Gap + H + H * SensitivityLabelWidth + H;
	float X = static_cast<float>(Origin.X + (Size.X - Total) * 0.5);
	const float Y = static_cast<float>(Origin.Y) + Short * MarginShare * 0.6f;
	Out.Add({ ETarget::Pause, FVector2D(X, Y), FVector2D(H, H) });
	X += H + Gap;
	Out.Add({ ETarget::Calibrate, FVector2D(X, Y), FVector2D(H * CalibrateChipWidth, H) });
	X += H * CalibrateChipWidth + Gap;
	Out.Add({ ETarget::SensitivityDown, FVector2D(X, Y), FVector2D(H, H) });
	X += H + H * SensitivityLabelWidth;
	Out.Add({ ETarget::SensitivityUp, FVector2D(X, Y), FVector2D(H, H) });
}

SDeviceTouchControls::FHit SDeviceTouchControls::HitAt(const FVector2D& Local, const FVector2D& Size) const
{
	FHit Hit;
	TArray<FChip, TInlineAllocator<4>> ChipList;
	Chips(Size, ChipList);
	for (const FChip& Chip : ChipList)
	{
		if (Local.X >= Chip.Pos.X && Local.Y >= Chip.Pos.Y && Local.X <= Chip.Pos.X + Chip.Size.X && Local.Y <= Chip.Pos.Y + Chip.Size.Y)
		{
			Hit.Target = Chip.Target;
			return Hit;
		}
	}
	for (int32 i = 0; i < Layout.Buttons.Num(); ++i)
	{
		FVector2D Centre;
		float Radius = 0.f;
		ButtonCircle(i, Size, Centre, Radius);
		if (FVector2D::Distance(Local, Centre) <= Radius * HitSlop)
		{
			Hit.Target = ETarget::Button;
			Hit.Button = i;
			return Hit;
		}
	}
	if (Layout.bSurface)
	{
		Hit.Target = ETarget::Surface;
	}
	return Hit;
}

FReply SDeviceTouchControls::Down(int32 Pointer, const FVector2D& Local, const FVector2D& Size)
{
	UDeviceControlsSubsystem* Subsystem = Owner.Get();
	const FHit Hit = HitAt(Local, Size);
	if (!Subsystem || Hit.Target == ETarget::None || IsBlocked())
	{
		return FReply::Unhandled();
	}
	Held.Add(Pointer, Hit);
	switch (Hit.Target)
	{
	case ETarget::Button:
		ButtonDown[Hit.Button] = true;
		Subsystem->HandleButton(Layout.Buttons[Hit.Button].Button, true);
		break;
	case ETarget::Surface:
		Subsystem->HandleTouch({ ETouchPhase::Down, Pointer, Local / Size, Subsystem->Now() });
		break;
	default:
		break; // chips act on release, like a button
	}
	return FReply::Handled();
}

FReply SDeviceTouchControls::Move(int32 Pointer, const FVector2D& Local, const FVector2D& Size)
{
	UDeviceControlsSubsystem* Subsystem = Owner.Get();
	const FHit* Hit = Held.Find(Pointer);
	if (!Subsystem || !Hit)
	{
		return FReply::Unhandled();
	}
	if (Hit->Target == ETarget::Surface)
	{
		Subsystem->HandleTouch({ ETouchPhase::Move, Pointer, Local / Size, Subsystem->Now() });
	}
	return FReply::Handled();
}

FReply SDeviceTouchControls::Up(int32 Pointer, const FVector2D& Local, const FVector2D& Size)
{
	UDeviceControlsSubsystem* Subsystem = Owner.Get();
	FHit Hit;
	if (!Held.RemoveAndCopyValue(Pointer, Hit) || !Subsystem)
	{
		return FReply::Unhandled();
	}
	const bool bStillOver = HitAt(Local, Size).Target == Hit.Target;
	switch (Hit.Target)
	{
	case ETarget::Button:
		ButtonDown[Hit.Button] = false;
		Subsystem->HandleButton(Layout.Buttons[Hit.Button].Button, false);
		break;
	case ETarget::Surface:
		Subsystem->HandleTouch({ ETouchPhase::Up, Pointer, Local / Size, Subsystem->Now() });
		break;
	case ETarget::Pause:
		if (bStillOver && OpenPause)
		{
			OpenPause();
		}
		break;
	case ETarget::Calibrate:
		if (bStillOver)
		{
			Subsystem->Calibrate();
		}
		break;
	case ETarget::SensitivityDown:
	case ETarget::SensitivityUp:
		if (bStillOver)
		{
			Subsystem->NudgeSensitivity(Hit.Target == ETarget::SensitivityUp ? 1 : -1);
		}
		break;
	default:
		break;
	}
	return FReply::Handled();
}

FReply SDeviceTouchControls::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	return Down(Event.GetPointerIndex(), FVector2D(MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition())), FVector2D(MyGeometry.GetLocalSize()));
}

FReply SDeviceTouchControls::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	return Move(Event.GetPointerIndex(), FVector2D(MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition())), FVector2D(MyGeometry.GetLocalSize()));
}

FReply SDeviceTouchControls::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	return Up(Event.GetPointerIndex(), FVector2D(MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition())), FVector2D(MyGeometry.GetLocalSize()));
}

FReply SDeviceTouchControls::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	if (Event.IsTouchEvent() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	return Down(MousePointer, FVector2D(MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition())), FVector2D(MyGeometry.GetLocalSize()));
}

FReply SDeviceTouchControls::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	if (Event.IsTouchEvent())
	{
		return FReply::Unhandled();
	}
	return Move(MousePointer, FVector2D(MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition())), FVector2D(MyGeometry.GetLocalSize()));
}

FReply SDeviceTouchControls::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	if (Event.IsTouchEvent() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	return Up(MousePointer, FVector2D(MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition())), FVector2D(MyGeometry.GetLocalSize()));
}

int32 SDeviceTouchControls::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	if (IsBlocked())
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const FVector2D Size = P.Size();
	const float Short = static_cast<float>(FMath::Min(Size.X, Size.Y));
	static const FText PauseLabel = LOCTEXT("Pause", "II");
	static const FText CalibrateLabel = LOCTEXT("Calibrate", "CALIBRATE");

	TArray<FChip, TInlineAllocator<4>> ChipList;
	Chips(Size, ChipList);
	const FSlateFontInfo ChipFont = KartUI::Font(KartUI::EFont::Label, Short * ChipShare * ChipLabelShare);
	for (const FChip& Chip : ChipList)
	{
		P.Box(Chip.Pos, Chip.Size, KartUI::Color::WithAlpha(KartUI::Color::Night, 0.55f), Chip.Size.Y * 0.5f, KartUI::Color::WithAlpha(KartUI::Color::White, 0.5f), 2.f);
		const FVector2D Mid = Chip.Pos + Chip.Size * 0.5;
		switch (Chip.Target)
		{
		case ETarget::Pause: P.Text(PauseLabel, ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5)); break;
		case ETarget::Calibrate: P.Text(CalibrateLabel, ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5)); break;
		case ETarget::SensitivityDown: P.Text(FString(TEXT("-")), ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5)); break;
		case ETarget::SensitivityUp:
			P.Text(FString(TEXT("+")), ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5));
			// The value sits between the - and + chips.
			P.Text(SensitivityLabel, ChipFont, FVector2D(Chip.Pos.X - Chip.Size.Y * SensitivityLabelWidth * 0.5, Mid.Y), KartUI::Color::White, FVector2D(0.5, 0.5), 2.f);
			break;
		default: break;
		}
	}
	if (!Layout.Hint.IsEmpty() && ChipList.Num() > 0)
	{
		const FVector2D Below(Size.X * 0.5, ChipList[0].Pos.Y + ChipList[0].Size.Y + Short * 0.02f);
		P.Text(Layout.Hint, KartUI::Font(KartUI::EFont::Body, Short * HintShare), Below, KartUI::Color::White, FVector2D(0.5, 0.0), 2.f);
	}
	for (int32 i = 0; i < Layout.Buttons.Num(); ++i)
	{
		FVector2D Centre;
		float Radius = 0.f;
		ButtonCircle(i, Size, Centre, Radius);
		const FDeviceButtonSpec& Spec = Layout.Buttons[i];
		const bool bDown = ButtonDown.IsValidIndex(i) && ButtonDown[i];
		const FLinearColor Color = ButtonColor(Spec.Button);
		P.Circle(Centre, Radius * (bDown ? 0.94f : 1.f), KartUI::Color::WithAlpha(Color, bDown ? PressedAlpha : IdleAlpha),
			KartUI::Color::WithAlpha(KartUI::Color::White, 0.8f), 3.f);
		P.Text(Spec.Label, KartUI::Font(KartUI::EFont::Button, Radius * 2.f * LabelShare, 2.f), Centre, KartUI::Color::White, FVector2D(0.5, 0.5));
	}
	return Layer;
}

#undef LOCTEXT_NAMESPACE

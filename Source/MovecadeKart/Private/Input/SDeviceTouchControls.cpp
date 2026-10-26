#include "SDeviceTouchControls.h"

#include "Input/DeviceControlsSettings.h"
#include "Input/DeviceControlsSubsystem.h"
#include "Mobile/MovecadeOrientationSubsystem.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "DeviceTouchControls"

using namespace MovecadeKart::Input;

namespace
{
	/** The mouse's pointer id (touch pointers are 0..9). */
	constexpr int32 MousePointer = 1000;

	constexpr float IdleAlpha = 0.3f;
	constexpr float PressedAlpha = 0.78f;
	/** Font sizes (Slate points, ~1.33 px each) as shares of the button's diameter or the chip's height. */
	constexpr float LabelShare = 0.15f;
	constexpr float ChipLabelShare = 0.26f;
	constexpr float HintShare = 0.03f;
	/** The sensitivity value's centre, in chip heights left of the + chip (the middle of the gap the zones leave). */
	constexpr float SensitivityValueOffset = 0.85f;
	/** The steering arrows, as a share of the button's radius. */
	constexpr float SteerArrowShare = 0.42f;
	/** The steering chip reads dimmer when there is no tilt to choose. */
	constexpr float UnavailableAlpha = 0.55f;
	/** The hint shows for this long after the controls appear (or change), then fades over HintFadeSeconds. */
	constexpr double HintSeconds = 6.0;
	constexpr double HintFadeSeconds = 1.0;
	/** Where the hint sits, down the screen. */
	constexpr float HintHeight = 0.62f;
	constexpr float MaxHintStep = 0.1f;

	FLinearColor ButtonColor(EDeviceButton Button)
	{
		switch (Button)
		{
		case EDeviceButton::Gas: return KartUI::Color::Mint;
		case EDeviceButton::Brake: return KartUI::Color::Hot;
		case EDeviceButton::Drift: return KartUI::Color::Sky;
		case EDeviceButton::Item: return KartUI::Color::Sun;
		case EDeviceButton::B: return KartUI::Color::Grape;
		case EDeviceButton::SteerLeft:
		case EDeviceButton::SteerRight: return KartUI::Color::White;
		default: return KartUI::Color::Sun;
		}
	}
}

void SDeviceTouchControls::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	Layout = InArgs._Layout;
	if (const UDeviceControlsSubsystem* Subsystem = Owner.Get(); Subsystem && Subsystem->GetScheme())
	{
		LayoutRevision = Subsystem->GetScheme()->GetLayoutRevision();
	}
	ButtonDown.Init(false, Layout.Buttons.Num());
	SetCanTick(true);
	ForceVolatile(true);
}

bool SDeviceTouchControls::IsBlocked() const
{
	return IsMenuOpen && IsMenuOpen();
}

bool SDeviceTouchControls::HasSteeringChoice() const
{
	const UDeviceControlsSubsystem* Subsystem = Owner.Get();
	return Subsystem && Subsystem->HasSteeringChoice();
}

void SDeviceTouchControls::ReleaseAll(const FVector2D& Size)
{
	TArray<int32> Pointers;
	Held.GetKeys(Pointers);
	for (const int32 Pointer : Pointers)
	{
		Up(Pointer, FVector2D(-1.0, -1.0), Size);
	}
}

void SDeviceTouchControls::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const FVector2D Size(AllottedGeometry.GetLocalSize());
	// A menu opening under a held finger lets that finger go: nothing stays pressed behind the menu.
	const bool bBlocked = IsBlocked();
	// Frame time, capped: a load hitch doesn't eat the hint before anyone has seen it.
	HintAge += FMath::Min(InDeltaTime, MaxHintStep);
	SetVisibility(bBlocked ? EVisibility::HitTestInvisible : EVisibility::Visible);
	if (bBlocked && Held.Num() > 0)
	{
		ReleaseAll(Size);
	}
	if (const UMovecadeOrientationSubsystem* Screen = UMovecadeOrientationSubsystem::Get(Owner.Get()))
	{
		SafeArea = Screen->GetSafeArea();
	}
	const UDeviceControlsSubsystem* Subsystem = Owner.Get();
	if (!Subsystem)
	{
		return;
	}
	if (Subsystem->GetSensitivity() != ShownSensitivity)
	{
		ShownSensitivity = Subsystem->GetSensitivity();
		SensitivityLabel = FString::Printf(TEXT("%.1fx"), ShownSensitivity);
	}
	bTouchSteering = Subsystem->IsTouchSteering();
	bMotionAvailable = Subsystem->IsMotionAvailable();
	// The scheme swapped its buttons (the kart's steering arrows): let go of the old ones, then take the new set.
	if (const IDeviceControlScheme* Scheme = Subsystem->GetScheme(); Scheme && Scheme->GetLayoutRevision() != LayoutRevision)
	{
		ReleaseAll(Size);
		LayoutRevision = Scheme->GetLayoutRevision();
		Layout = Scheme->GetLayout();
		ButtonDown.Init(false, Layout.Buttons.Num());
		HintAge = 0.0;
	}
}

FDeviceTouchZones SDeviceTouchControls::Zones(const FVector2D& Size) const
{
	// Inside the notch, the rounded corners and the home indicator (UMovecadeOrientationSubsystem).
	FDeviceTouchZones::FRowOptions Row;
	Row.bSettingsOpen = bSettingsOpen;
	Row.bSteering = HasSteeringChoice();
	return FDeviceTouchZones::Compute(Layout, Size, SafeArea.Insets(Size), Row);
}

SDeviceTouchControls::FHit SDeviceTouchControls::HitAt(const FVector2D& Local, const FVector2D& Size) const
{
	FHit Hit;
	const FDeviceTouchZones Z = Zones(Size);
	if (const FTouchChipZone* Chip = Z.ChipAt(Local))
	{
		Hit.Target = Chip->Target;
		return Hit;
	}
	Hit.Button = Z.ButtonAt(Local);
	if (Hit.Button != INDEX_NONE)
	{
		Hit.Target = ETarget::Button;
		return Hit;
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
		if (ButtonDown.IsValidIndex(Hit.Button) && Layout.Buttons.IsValidIndex(Hit.Button))
		{
			ButtonDown[Hit.Button] = false;
			Subsystem->HandleButton(Layout.Buttons[Hit.Button].Button, false);
		}
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
	case ETarget::Settings:
		bSettingsOpen = bStillOver ? !bSettingsOpen : bSettingsOpen;
		break;
	case ETarget::Calibrate:
		if (bStillOver)
		{
			Subsystem->Calibrate();
			bSettingsOpen = false;
		}
		break;
	case ETarget::SensitivityDown:
	case ETarget::SensitivityUp:
		if (bStillOver)
		{
			Subsystem->NudgeSensitivity(Hit.Target == ETarget::SensitivityUp ? 1 : -1);
		}
		break;
	case ETarget::Steering:
		if (bStillOver)
		{
			// The choice is the player's even without sensors: it applies when tilt is there.
			Subsystem->SetTiltSteering(!UDeviceControlsSettings::Get()->bTiltSteering);
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
	static const FText SettingsLabel = LOCTEXT("Settings", "...");
	static const FText SteerTiltLabel = LOCTEXT("SteerTilt", "STEER: TILT");
	static const FText SteerArrowsLabel = LOCTEXT("SteerArrows", "STEER: ARROWS");
	static const FText NoTiltLabel = LOCTEXT("NoTilt", "NO TILT: ARROWS");

	const FDeviceTouchZones Z = Zones(Size);
	const FSlateFontInfo ChipFont = KartUI::Font(KartUI::EFont::Label, Z.ChipHeight * ChipLabelShare);
	for (const FTouchChipZone& Chip : Z.Chips)
	{
		const bool bDim = Chip.Target == ETarget::Steering && !bMotionAvailable;
		const FKartPainter C = bDim ? P.Faded(UnavailableAlpha) : P;
		C.Box(Chip.Pos, Chip.Size, KartUI::Color::WithAlpha(KartUI::Color::Night, 0.55f), Chip.Size.Y * 0.5f, KartUI::Color::WithAlpha(KartUI::Color::White, 0.5f), 2.f);
		const FVector2D Mid = Chip.Pos + Chip.Size * 0.5;
		switch (Chip.Target)
		{
		case ETarget::Pause: C.Text(PauseLabel, ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5)); break;
		case ETarget::Settings: C.Text(SettingsLabel, ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5)); break;
		case ETarget::Calibrate: C.Text(CalibrateLabel, ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5)); break;
		case ETarget::SensitivityDown: C.Text(FString(TEXT("-")), ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5)); break;
		case ETarget::SensitivityUp:
			C.Text(FString(TEXT("+")), ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5));
			// The value sits between the - and + chips.
			C.Text(SensitivityLabel, ChipFont, FVector2D(Chip.Pos.X - Z.ChipHeight * SensitivityValueOffset, Mid.Y), KartUI::Color::White, FVector2D(0.5, 0.5), 2.f);
			break;
		case ETarget::Steering:
			C.Text(!bMotionAvailable ? NoTiltLabel : bTouchSteering ? SteerArrowsLabel : SteerTiltLabel, ChipFont, Mid, KartUI::Color::White, FVector2D(0.5, 0.5));
			break;
		default: break;
		}
	}
	// What to do, for the first few seconds (and never over the settings row).
	const float HintAlpha = 1.f - FMath::Clamp(static_cast<float>((HintAge - HintSeconds) / HintFadeSeconds), 0.f, 1.f);
	if (!Layout.Hint.IsEmpty() && HintAlpha > 0.f && !bSettingsOpen)
	{
		P.Faded(HintAlpha).Text(Layout.Hint, KartUI::Font(KartUI::EFont::Body, Short * HintShare), FVector2D(Size.X * 0.5, Size.Y * HintHeight), KartUI::Color::White,
			FVector2D(0.5, 0.5), 2.f);
	}
	for (const FTouchButtonZone& B : Z.Buttons)
	{
		const FDeviceButtonSpec& Spec = Layout.Buttons[B.Index];
		const bool bDown = ButtonDown.IsValidIndex(B.Index) && ButtonDown[B.Index];
		const FLinearColor Color = ButtonColor(Spec.Button);
		P.Circle(B.Centre, B.Radius * (bDown ? 0.94f : 1.f), KartUI::Color::WithAlpha(Color, bDown ? PressedAlpha : IdleAlpha),
			KartUI::Color::WithAlpha(KartUI::Color::White, 0.8f), 3.f);
		if (Spec.Button == EDeviceButton::SteerLeft || Spec.Button == EDeviceButton::SteerRight)
		{
			KartWidgets::Arrow(P, B.Centre, B.Radius * SteerArrowShare, Spec.Button == EDeviceButton::SteerRight, KartUI::Color::White);
			continue;
		}
		P.Text(Spec.Label, KartUI::Font(KartUI::EFont::Button, B.Radius * 2.f * LabelShare, 2.f), B.Centre, KartUI::Color::White, FVector2D(0.5, 0.5));
	}
	return Layer;
}

#undef LOCTEXT_NAMESPACE

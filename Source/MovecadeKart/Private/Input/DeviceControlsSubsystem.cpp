#include "Input/DeviceControlsSubsystem.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Input/DeviceControlsSettings.h"
#include "Input/Events.h"
#include "Misc/FileHelper.h"
#include "Mobile/MovecadeOrientationSubsystem.h"
#include "SDeviceTouchControls.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeviceControls, Log, All);

using namespace MovecadeKart::Input;

namespace
{
	/** Over the game's HUDs and the arcade overlay's bar, under the pause menu's screens (which sit at 100+). */
	constexpr int32 OverlayZOrder = 40;

	TOptional<EScreenOrientation> ReportedOrientation()
	{
		switch (FPlatformMisc::GetDeviceOrientation())
		{
		case EDeviceScreenOrientation::Portrait: return EScreenOrientation::Portrait;
		case EDeviceScreenOrientation::PortraitUpsideDown: return EScreenOrientation::PortraitUpsideDown;
		case EDeviceScreenOrientation::LandscapeLeft: return EScreenOrientation::LandscapeLeft;
		case EDeviceScreenOrientation::LandscapeRight: return EScreenOrientation::LandscapeRight;
		default: return {}; // face up / down or unknown: the screen did not turn
		}
	}

	/** The orientation the engine reoriented the reading for: the viewport's shape decides portrait or landscape. */
	EScreenOrientation CurrentOrientation(const UObject* Context, EScreenOrientation Last)
	{
		const UMovecadeOrientationSubsystem* Screen = UMovecadeOrientationSubsystem::Get(Context);
		const bool bPortrait = Screen ? Screen->IsPortrait() : (Last == EScreenOrientation::Portrait || Last == EScreenOrientation::PortraitUpsideDown);
		const TOptional<EScreenOrientation> Reported = ReportedOrientation();
		return ResolveOrientation(Reported.IsSet() ? Reported : TOptional<EScreenOrientation>(Last), bPortrait);
	}
}

/** Catches every FMotionEvent Slate routes (the device's sensors) without consuming it. */
class FDeviceMotionInputProcessor : public IInputProcessor
{
public:
	explicit FDeviceMotionInputProcessor(UDeviceControlsSubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleMotionDetectedEvent(FSlateApplication& SlateApp, const FMotionEvent& MotionEvent) override
	{
		if (UDeviceControlsSubsystem* Subsystem = Owner.Get())
		{
			FEngineMotionReading Reading;
			Reading.Tilt = MotionEvent.GetTilt();
			Reading.RotationRate = MotionEvent.GetRotationRate();
			Reading.Gravity = MotionEvent.GetGravity();
			Reading.Acceleration = MotionEvent.GetAcceleration();
			Reading.Orientation = Orientation = CurrentOrientation(Subsystem, Orientation);
			Reading.TimeSeconds = Subsystem->Now();
			Subsystem->HandleMotion(Reading);
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("DeviceMotion"); }

private:
	TWeakObjectPtr<UDeviceControlsSubsystem> Owner;
	EScreenOrientation Orientation = EScreenOrientation::Portrait;
};

UDeviceControlsSubsystem* UDeviceControlsSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDeviceControlsSubsystem>() : nullptr;
}

bool UDeviceControlsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

double UDeviceControlsSubsystem::Now() const
{
	return FPlatformTime::Seconds();
}

void UDeviceControlsSubsystem::Activate(TSharedRef<IDeviceControlScheme> Scheme, TSharedRef<IDeviceInputSink> Sink,
	TFunction<bool()> IsMenuOpen, TFunction<void()> OpenPause)
{
	Deactivate();
	MenuOpenQuery = MoveTemp(IsMenuOpen);
	PauseAction = MoveTemp(OpenPause);
	Convention = FMotionConvention::ForPlatform();
	Controller = MakeUnique<FLocalDeviceController>(Scheme, Sink);
	Controller->SetSensitivity(UDeviceControlsSettings::Get()->GetSensitivity());
	Controller->SetMotionSteeringWanted(UDeviceControlsSettings::Get()->bTiltSteering);
	ActivatedAt = Now();
	StartMotionSource();
	ShowOverlay();
	UE_LOG(LogDeviceControls, Display, TEXT("Device controls on (%s motion, %.0f Hz frames, sensitivity %.1f)"),
		Script ? TEXT("simulated") : (Replay.Events.Num() > 0 ? TEXT("replayed") : TEXT("live")), Scheme->GetLayout().SendHz, Controller->GetSensitivity());
}

void UDeviceControlsSubsystem::Deactivate()
{
	if (!Controller)
	{
		return;
	}
	WriteRecording();
	HideOverlay();
	if (MotionProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(MotionProcessor);
	}
	MotionProcessor.Reset();
	Script.Reset();
	Replay.Events.Reset();
	ReplayNext = 0;
	UE_LOG(LogDeviceControls, Display, TEXT("Device controls off (%d frames sent)"), Controller->GetFramesSent());
	Controller.Reset();
}

void UDeviceControlsSubsystem::Deinitialize()
{
	Deactivate();
	Super::Deinitialize();
}

void UDeviceControlsSubsystem::StartMotionSource()
{
	const FDeviceControlsFlags& Flags = FDeviceControlsFlags::Get();
#if !UE_BUILD_SHIPPING
	if (Flags.Source == EDeviceMotionSource::Simulated)
	{
		Script = Controller->GetScheme().MakeAutoplayScript();
		UE_CLOG(!Script, LogDeviceControls, Warning, TEXT("-DeviceMotion=sim: this game's controls have no autoplay script"));
	}
	else if (Flags.Source == EDeviceMotionSource::Replay)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Flags.ReplayPath) || !FDeviceRecording::FromText(Text, Replay))
		{
			UE_LOG(LogDeviceControls, Error, TEXT("-DeviceMotionReplay: can't read %s"), *Flags.ReplayPath);
		}
	}
	bRecording = !Flags.RecordPath.IsEmpty();
	Recording.Events.Reset();
#endif
	if (Flags.Source == EDeviceMotionSource::Live && FSlateApplication::IsInitialized())
	{
		MotionProcessor = MakeShared<FDeviceMotionInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(MotionProcessor);
	}
}

void UDeviceControlsSubsystem::ShowOverlay()
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport || !Controller)
	{
		return;
	}
	Overlay = SNew(SDeviceTouchControls).Owner(this).Layout(Controller->GetLayout());
	Overlay->IsMenuOpen = MenuOpenQuery;
	Overlay->OpenPause = PauseAction;
	OverlayRoot = Overlay;
	Viewport->AddViewportWidgetContent(OverlayRoot.ToSharedRef(), OverlayZOrder);
}

void UDeviceControlsSubsystem::HideOverlay()
{
	UWorld* World = GetWorld();
	if (UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr; Viewport && OverlayRoot.IsValid())
	{
		Viewport->RemoveViewportWidgetContent(OverlayRoot.ToSharedRef());
	}
	OverlayRoot.Reset();
	Overlay.Reset();
}

void UDeviceControlsSubsystem::Calibrate()
{
	if (Controller)
	{
		Controller->RequestCalibration();
	}
}

void UDeviceControlsSubsystem::NudgeSensitivity(int32 Steps)
{
	UDeviceControlsSettings* Settings = UDeviceControlsSettings::Get();
	Settings->NudgeSensitivity(Steps);
	if (Controller)
	{
		Controller->SetSensitivity(Settings->GetSensitivity());
	}
}

double UDeviceControlsSubsystem::GetSensitivity() const
{
	return UDeviceControlsSettings::Get()->GetSensitivity();
}

FDeviceTouchZones UDeviceControlsSubsystem::GetTouchZones(const FVector2D& Size, const FMargin& Insets, bool bSettingsOpen) const
{
	if (!Controller)
	{
		return FDeviceTouchZones();
	}
	FDeviceTouchZones::FRowOptions Row;
	Row.bSettingsOpen = bSettingsOpen;
	Row.bSteering = HasSteeringChoice();
	return FDeviceTouchZones::Compute(Controller->GetLayout(), Size, Insets, Row);
}

bool UDeviceControlsSubsystem::HasSteeringChoice() const
{
	return Controller && Controller->GetScheme().HasTouchSteering();
}

bool UDeviceControlsSubsystem::IsTouchSteering() const
{
	return Controller && Controller->IsTouchSteering();
}

bool UDeviceControlsSubsystem::IsMotionAvailable() const
{
	return Controller && Controller->IsMotionAvailable(Now());
}

void UDeviceControlsSubsystem::SetTiltSteering(bool bTilt)
{
	UDeviceControlsSettings::Get()->SetTiltSteering(bTilt);
	if (Controller)
	{
		Controller->SetMotionSteeringWanted(bTilt);
	}
	UE_LOG(LogDeviceControls, Display, TEXT("Steering: %s"), bTilt ? TEXT("tilt") : TEXT("on-screen"));
}

void UDeviceControlsSubsystem::HandleMotion(const FEngineMotionReading& Reading)
{
	if (!Controller)
	{
		return;
	}
	if (bRecording)
	{
		FDeviceRecordedEvent& E = Recording.Events.AddDefaulted_GetRef();
		E.Kind = FDeviceRecordedEvent::EKind::Motion;
		E.Time = Reading.TimeSeconds - ActivatedAt;
		E.Reading = Reading;
		E.Reading.TimeSeconds = E.Time;
	}
	Controller->PushMotion(FromEngine(Reading, Convention));
}

void UDeviceControlsSubsystem::HandleButton(EDeviceButton Button, bool bDown)
{
	if (!Controller)
	{
		return;
	}
	if (bRecording)
	{
		FDeviceRecordedEvent& E = Recording.Events.AddDefaulted_GetRef();
		E.Kind = FDeviceRecordedEvent::EKind::Button;
		E.Time = Now() - ActivatedAt;
		E.Button = Button;
		E.bDown = bDown;
	}
	Controller->PushButton(Button, bDown, Now());
}

void UDeviceControlsSubsystem::HandleTouch(const FDeviceTouch& Touch)
{
	if (!Controller)
	{
		return;
	}
	if (bRecording)
	{
		FDeviceRecordedEvent& E = Recording.Events.AddDefaulted_GetRef();
		E.Kind = FDeviceRecordedEvent::EKind::Touch;
		E.Time = Touch.Time - ActivatedAt;
		E.Touch = Touch;
		E.Touch.Time = E.Time;
	}
	Controller->PushTouch(Touch);
}

void UDeviceControlsSubsystem::Tick(float DeltaTime)
{
	if (!Controller)
	{
		return;
	}
	StepScript();
	StepReplay();
	Controller->Tick(Now());
}

TStatId UDeviceControlsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDeviceControlsSubsystem, STATGROUP_Tickables);
}

void UDeviceControlsSubsystem::StepScript()
{
	if (!Script)
	{
		return;
	}
	// The scripted motion goes through the engine's reading and back, like the device's would.
	const double T = Now() - ActivatedAt;
	FDeviceMotion Motion = Script->Step(T, [this](EDeviceButton Button, bool bDown) { HandleButton(Button, bDown); });
	Motion.TimeSeconds = Now();
	HandleMotion(ToEngine(Motion, Convention));
}

void UDeviceControlsSubsystem::StepReplay()
{
	const double T = Now() - ActivatedAt;
	while (Replay.Events.IsValidIndex(ReplayNext) && Replay.Events[ReplayNext].Time <= T)
	{
		const FDeviceRecordedEvent& E = Replay.Events[ReplayNext++];
		switch (E.Kind)
		{
		case FDeviceRecordedEvent::EKind::Motion:
		{
			FEngineMotionReading Reading = E.Reading;
			Reading.TimeSeconds = ActivatedAt + E.Time;
			HandleMotion(Reading);
			break;
		}
		case FDeviceRecordedEvent::EKind::Button:
			HandleButton(E.Button, E.bDown);
			break;
		case FDeviceRecordedEvent::EKind::Touch:
		{
			FDeviceTouch Touch = E.Touch;
			Touch.Time = ActivatedAt + E.Time;
			HandleTouch(Touch);
			break;
		}
		}
	}
}

void UDeviceControlsSubsystem::WriteRecording()
{
#if !UE_BUILD_SHIPPING
	const FString& Path = FDeviceControlsFlags::Get().RecordPath;
	if (!bRecording || Path.IsEmpty() || Recording.Events.Num() == 0)
	{
		return;
	}
	if (FFileHelper::SaveStringToFile(Recording.ToText(), *Path))
	{
		UE_LOG(LogDeviceControls, Display, TEXT("Device motion recorded: %d events to %s"), Recording.Events.Num(), *Path);
	}
	Recording.Events.Reset();
#endif
}

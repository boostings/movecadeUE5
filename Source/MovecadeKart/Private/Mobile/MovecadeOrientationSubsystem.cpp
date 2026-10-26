#include "Mobile/MovecadeOrientationSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Mobile/SMovecadeSafeAreaMask.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogMovecadeOrientation, Log, All);

using namespace MovecadeKart;
using Arcade::EArcadeOrientation;
using Mobile::EScreenShape;

namespace
{
#if !UE_BUILD_SHIPPING
	/** Room left on the desktop for the window's title bar and frame, in pixels. */
	constexpr int32 PreviewWindowChrome = 48;
	/** Frames to wait for a requested window size to land before centering it anyway. */
	constexpr int32 PreviewCenterMaxFrames = 120;
#endif

	FString LevelName(const UWorld& World)
	{
		return UWorld::RemovePIEPrefix(World.GetMapName());
	}

#if !UE_BUILD_SHIPPING
	void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value, ECVF_SetByCode);
		}
	}

	/**
	 * SafeZone.Ratio.<Side> is the share of the screen kept on that side's half (1 = nothing covered); zero area turns
	 * the overrides off.
	 */
	void SetSafeZoneOverride(const Mobile::FMovecadeSafeArea& Area)
	{
		SetCVar(TEXT("SafeZone.EnableOverrides"), Area.IsZero() ? 0.f : 1.f);
		SetCVar(TEXT("SafeZone.Ratio.Left"), 1.f - 2.f * Area.Fraction.Left);
		SetCVar(TEXT("SafeZone.Ratio.Top"), 1.f - 2.f * Area.Fraction.Top);
		SetCVar(TEXT("SafeZone.Ratio.Right"), 1.f - 2.f * Area.Fraction.Right);
		SetCVar(TEXT("SafeZone.Ratio.Bottom"), 1.f - 2.f * Area.Fraction.Bottom);
	}
#endif
}

UMovecadeOrientationSubsystem* UMovecadeOrientationSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance && !WorldContext && GEngine && GEngine->GameViewport)
	{
		GameInstance = GEngine->GameViewport->GetGameInstance();
	}
	return GameInstance ? GameInstance->GetSubsystem<UMovecadeOrientationSubsystem>() : nullptr;
}

void UMovecadeOrientationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMovecadeOrientationSubsystem::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMovecadeOrientationSubsystem::HandlePostLoadMap);
	ViewportResizedHandle = FViewport::ViewportResizedEvent.AddUObject(this, &UMovecadeOrientationSubsystem::HandleViewportResized);
	SafeFrameHandle = FCoreDelegates::OnSafeFrameChangedEvent.AddUObject(this, &UMovecadeOrientationSubsystem::HandleSafeFrameChanged);
	OrientationHandle = FCoreDelegates::ApplicationReceivedScreenOrientationChangedNotificationDelegate.AddUObject(
		this, &UMovecadeOrientationSubsystem::HandleScreenOrientationChanged);

#if !UE_BUILD_SHIPPING
	// The preview resizes the desktop window: never the editor's own, nor PIE's (it isn't the game window).
	if (!GIsEditor)
	{
		Preview = Mobile::FMobilePreview::Parse(FCommandLine::Get());
		if (!Preview.IsSet() && FCString::Strifind(FCommandLine::Get(), TEXT("MobilePreview")))
		{
			UE_LOG(LogMovecadeOrientation, Warning, TEXT("-MobilePreview: expected portrait|landscape|auto[,iphone15pro|iphonese|pixel8]; ignored"));
		}
	}
	if (Preview.IsSet())
	{
		bPreviewMask = !FParse::Param(FCommandLine::Get(), TEXT("MobilePreviewNoMask"));
		UE_LOG(LogMovecadeOrientation, Display, TEXT("Mobile preview on (%s)"), Preview->Device->Name);
	}
#endif

	// The first level loads before any PostLoadMap reaches us in a cooked game; the menus' orientation until then.
	ApplyForLevel(FString());
}

void UMovecadeOrientationSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	FViewport::ViewportResizedEvent.Remove(ViewportResizedHandle);
	FCoreDelegates::OnSafeFrameChangedEvent.Remove(SafeFrameHandle);
	FCoreDelegates::ApplicationReceivedScreenOrientationChangedNotificationDelegate.Remove(OrientationHandle);
#if !UE_BUILD_SHIPPING
	FTSTicker::GetCoreTicker().RemoveTicker(CenterTicker);
	PreviewMask.Reset();
	if (Preview.IsSet())
	{
		SetSafeZoneOverride({});
	}
#endif
	ScreenChanged.Clear();
	Super::Deinitialize();
}

void UMovecadeOrientationSubsystem::SetOverride(EArcadeOrientation Orientation)
{
	Override = Orientation;
	ApplyForLevel(CurrentLevel);
}

void UMovecadeOrientationSubsystem::ClearOverride()
{
	if (Override.IsSet())
	{
		Override.Reset();
		ApplyForLevel(CurrentLevel);
	}
}

void UMovecadeOrientationSubsystem::HandlePreLoadMap(const FString& MapName)
{
	// Lock the new orientation before the level loads, so the phone turns behind the travel curtain.
	Override.Reset();
	ApplyForLevel(MapName);
}

void UMovecadeOrientationSubsystem::HandlePostLoadMap(UWorld* World)
{
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	ApplyForLevel(LevelName(*World));
#if !UE_BUILD_SHIPPING
	if (Preview.IsSet() && bPreviewMask)
	{
		AttachPreviewMask();
	}
#endif
}

void UMovecadeOrientationSubsystem::HandleViewportResized(FViewport* Viewport, uint32 Unused)
{
	const UGameViewportClient* Client = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (Client && Viewport == Client->Viewport)
	{
		Refresh();
	}
}

void UMovecadeOrientationSubsystem::HandleSafeFrameChanged()
{
	Refresh();
}

void UMovecadeOrientationSubsystem::HandleScreenOrientationChanged(int32 Orientation)
{
	Refresh();
}

void UMovecadeOrientationSubsystem::ApplyForLevel(const FString& Level)
{
	CurrentLevel = Level;
	State.Wanted = Mobile::ResolveWanted(Level, Override);
	ApplyWanted();
	Refresh();
}

void UMovecadeOrientationSubsystem::ApplyWanted()
{
	// The generic (desktop) implementation only records the mask; iOS and Android rotate to it.
	const EDeviceScreenOrientation Mask = Mobile::ToDeviceOrientation(State.Wanted);
	if (FPlatformMisc::GetAllowedDeviceOrientation() != Mask)
	{
		UE_LOG(LogMovecadeOrientation, Log, TEXT("%s: %s"), CurrentLevel.IsEmpty() ? TEXT("(startup)") : *CurrentLevel, Arcade::LexToString(State.Wanted));
		FPlatformMisc::SetAllowedDeviceOrientation(Mask);
	}
#if !UE_BUILD_SHIPPING
	if (Preview.IsSet())
	{
		ApplyPreview();
	}
#endif
}

FIntPoint UMovecadeOrientationSubsystem::ReadViewportSize() const
{
	const UGameViewportClient* Client = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	return Client && Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
}

void UMovecadeOrientationSubsystem::Refresh()
{
	FMovecadeScreenState Next = State;
	Next.ViewportSize = ReadViewportSize();
	EScreenShape Fallback = State.Wanted == EArcadeOrientation::Portrait ? EScreenShape::Portrait : EScreenShape::Landscape;
#if !UE_BUILD_SHIPPING
	Next.bPreview = Preview.IsSet();
	if (Preview.IsSet())
	{
		Fallback = PreviewShape;
	}
#endif
	Next.Shape = Mobile::ShapeOf(Next.ViewportSize, Fallback);

	if (FSlateApplication::IsInitialized())
	{
		// Slate's safe zone in pixels of the display (the override size is only honoured in editor builds, so pass
		// the display's own), back to fractions. On a phone the display is the viewport.
		FSlateApplication& Slate = FSlateApplication::Get();
		FDisplayMetrics Metrics;
		Slate.GetCachedDisplayMetrics(Metrics);
		if (Metrics.PrimaryDisplayWidth > 0 && Metrics.PrimaryDisplayHeight > 0)
		{
			const FVector2D Display(Metrics.PrimaryDisplayWidth, Metrics.PrimaryDisplayHeight);
			FMargin Pixels;
			Slate.GetSafeZoneSize(Pixels, Display);
			Next.SafeArea = Mobile::FMovecadeSafeArea::FromSlateRatio(FMargin(Pixels.Left * 2.f / Display.X, Pixels.Top * 2.f / Display.Y,
				Pixels.Right * 2.f / Display.X, Pixels.Bottom * 2.f / Display.Y));
		}
	}

	if (Next != State)
	{
		State = Next;
		UE_LOG(LogMovecadeOrientation, Log, TEXT("Screen %s %dx%d (wants %s), safe L%.3f T%.3f R%.3f B%.3f"), Mobile::LexToString(State.Shape),
			State.ViewportSize.X, State.ViewportSize.Y, Arcade::LexToString(State.Wanted), State.SafeArea.Fraction.Left, State.SafeArea.Fraction.Top,
			State.SafeArea.Fraction.Right, State.SafeArea.Fraction.Bottom);
		ScreenChanged.Broadcast(State);
	}
}

#if !UE_BUILD_SHIPPING

void UMovecadeOrientationSubsystem::ApplyPreview()
{
	const EScreenShape Shape = Preview->ShapeFor(State.Wanted, PreviewShape);
	PreviewShape = Shape;
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	// The engine's safe zone overrides (what mobile PIE uses), so Slate's SSafeZone / USafeZone and GetState() agree.
	// Not Slate's custom safe zone: in editor builds ULocalPlayer clears that every frame unless a preview platform
	// is active. Re-reading the display metrics applies them; the safe frame event makes widgets (and Refresh) see it.
	SetSafeZoneOverride(Preview->Device->SafeAreaFor(Shape));
	FSlateApplication& Slate = FSlateApplication::Get();
	FDisplayMetrics Metrics;
	Slate.GetDisplayMetrics(Metrics);
	FCoreDelegates::OnSafeFrameChangedEvent.Broadcast();
	const FPlatformRect& Work = Metrics.PrimaryDisplayWorkAreaRect;
	const FIntPoint WorkArea(Work.Right - Work.Left, Work.Bottom - Work.Top - PreviewWindowChrome);
	const FIntPoint Size = Mobile::FitPreviewWindow(Preview->Device->PixelsFor(Shape), WorkArea);
	// Already asked for (the window may not exist yet), or already showing.
	const FIntPoint Viewport = ReadViewportSize();
	if (Size == FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY) && (Viewport == Size || Viewport == FIntPoint::ZeroValue))
	{
		return;
	}

	UE_LOG(LogMovecadeOrientation, Display, TEXT("Mobile preview: %s %s, window %dx%d"), Preview->Device->Name, Mobile::LexToString(Shape), Size.X, Size.Y);
	FSystemResolution::RequestResolutionChange(Size.X, Size.Y, EWindowMode::Windowed);
	// Applied now, so a window created after this (the first one, at startup) already has the phone's shape.
	IConsoleManager::Get().CallAllConsoleVariableSinks();

	CenterAttempts = 0;
	if (!CenterTicker.IsValid())
	{
		CenterTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMovecadeOrientationSubsystem::CenterPreviewWindow));
	}
}

bool UMovecadeOrientationSubsystem::CenterPreviewWindow(float DeltaSeconds)
{
	const UGameViewportClient* Client = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	const TSharedPtr<SWindow> Window = Client ? Client->GetWindow() : nullptr;
	const bool bSized = ReadViewportSize() == FIntPoint(GSystemResolution.ResX, GSystemResolution.ResY);
	if (!Window.IsValid() || (!bSized && ++CenterAttempts < PreviewCenterMaxFrames))
	{
		return true; // keep waiting
	}

	FDisplayMetrics Metrics;
	FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
	const FPlatformRect& Work = Metrics.PrimaryDisplayWorkAreaRect;
	const FVector2D Outer = Window->GetSizeInScreen();
	const FVector2D Position(Work.Left + FMath::Max(0.0, ((Work.Right - Work.Left) - Outer.X) * 0.5),
		Work.Top + FMath::Max(0.0, ((Work.Bottom - Work.Top) - Outer.Y) * 0.5));
	Window->MoveWindowTo(Position);
	CenterTicker.Reset();
	CenterAttempts = 0;
	Refresh();
	return false;
}

void UMovecadeOrientationSubsystem::AttachPreviewMask()
{
	UGameViewportClient* Client = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	if (!Client)
	{
		return;
	}
	if (!PreviewMask.IsValid())
	{
		PreviewMask = SNew(SMovecadeSafeAreaMask, this);
	}
	// Above every game HUD and menu (they use small Z orders), so screenshots show what the phone covers.
	constexpr int32 MaskZOrder = 10000;
	Client->RemoveViewportWidgetContent(PreviewMask.ToSharedRef());
	Client->AddViewportWidgetContent(PreviewMask.ToSharedRef(), MaskZOrder);
	UE_LOG(LogMovecadeOrientation, Log, TEXT("Mobile preview: safe area mask on the viewport"));
}

#endif

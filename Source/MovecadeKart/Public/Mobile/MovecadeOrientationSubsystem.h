#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Mobile/MovecadeOrientation.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MovecadeOrientationSubsystem.generated.h"

class FViewport;
class UWorld;

/** What the screen is doing now: what the level wants, the shape it actually has, and its safe area. */
struct FMovecadeScreenState
{
	/** What the current level (or an override) asked the device for. */
	MovecadeKart::Arcade::EArcadeOrientation Wanted = MovecadeKart::Arcade::MenuOrientation;
	/**
	 * The logical orientation: the shape of the game viewport right now. Lay out by this, not by Wanted: a desktop
	 * window is landscape even in a portrait game, and a phone may still be mid-rotation.
	 */
	MovecadeKart::Mobile::EScreenShape Shape = MovecadeKart::Mobile::EScreenShape::Landscape;
	/** The game viewport in pixels (0 x 0 before it exists or under -nullrhi). */
	FIntPoint ViewportSize = FIntPoint::ZeroValue;
	/** Notch, rounded corners and home indicator, as fractions of the viewport (zero on a plain desktop window). */
	MovecadeKart::Mobile::FMovecadeSafeArea SafeArea;
	/** True while -MobilePreview imitates a phone on the desktop. */
	bool bPreview = false;

	bool IsPortrait() const { return Shape == MovecadeKart::Mobile::EScreenShape::Portrait; }
	/** The safe insets in the units of Size (e.g. a full-screen HUD widget's local size). */
	FMargin SafeInsets(const FVector2D& Size) const { return SafeArea.Insets(Size); }

	bool operator==(const FMovecadeScreenState& Other) const
	{
		return Wanted == Other.Wanted && Shape == Other.Shape && ViewportSize == Other.ViewportSize && SafeArea == Other.SafeArea
			&& bPreview == Other.bPreview;
	}
	bool operator!=(const FMovecadeScreenState& Other) const { return !(*this == Other); }
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovecadeScreenChanged, const FMovecadeScreenState&);

/**
 * Phone orientation and the safe area, in one place for HUD and UI code.
 *
 *  - Applies: as a level loads (and so when a game starts or the player returns to the menus), locks the device to
 *    that level's orientation, Arcade::OrientationForLevel (the catalog's per-game value; track levels, which host
 *    the races, the front end and the Party lobby, are landscape), through FPlatformMisc::SetAllowedDeviceOrientation.
 *    The lock is requested before the load, so the phone turns behind the curtain, not in the new level.
 *  - Exposes: GetState() (wanted orientation, the viewport's actual shape and size, the safe area) and
 *    OnScreenChanged(), which fires on any change (level, rotation, window resize, safe area).
 *  - Desktop preview (not in Shipping): -MobilePreview=portrait|landscape|auto[,device] sizes the window to a phone
 *    and simulates its safe area through the engine's SafeZone.Ratio.* overrides (so SSafeZone/USafeZone agree with
 *    GetState()), drawn as a translucent mask unless -MobilePreviewNoMask.
 *    See Docs/Agents/BRIEF.md "Phone layouts on the desktop".
 *
 * Use:
 *   if (const UMovecadeOrientationSubsystem* Screen = UMovecadeOrientationSubsystem::Get(this))
 *   {
 *       const FMovecadeScreenState& S = Screen->GetState();
 *       const FMargin Safe = S.SafeInsets(AllottedGeometry.GetLocalSize());   // in OnPaint
 *       Handle = Screen->OnScreenChanged().AddSP(this, &SMyHud::Relayout);    // remove it in the destructor
 *   }
 */
UCLASS()
class MOVECADEKART_API UMovecadeOrientationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The subsystem of WorldContext's game instance; with no context, the game viewport's (null in commandlets). */
	static UMovecadeOrientationSubsystem* Get(const UObject* WorldContext = nullptr);

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	const FMovecadeScreenState& GetState() const { return State; }
	bool IsPortrait() const { return State.IsPortrait(); }
	const MovecadeKart::Mobile::FMovecadeSafeArea& GetSafeArea() const { return State.SafeArea; }
	FOnMovecadeScreenChanged& OnScreenChanged() { return ScreenChanged; }

	/**
	 * Holds Orientation over the level's own until ClearOverride or the next level load (e.g. a screen that must be
	 * read upright). Applied at once.
	 */
	void SetOverride(MovecadeKart::Arcade::EArcadeOrientation Orientation);
	void ClearOverride();

	/** Re-reads the viewport and safe area and broadcasts if anything changed (the engine events call it). */
	void Refresh();

private:
	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMap(UWorld* World);
	void HandleViewportResized(FViewport* Viewport, uint32 Unused);
	void HandleSafeFrameChanged();
	void HandleScreenOrientationChanged(int32 Orientation);

	/** Sets Wanted from Level (plus the override) and locks the device to it. */
	void ApplyForLevel(const FString& Level);
	void ApplyWanted();
	FIntPoint ReadViewportSize() const;

#if !UE_BUILD_SHIPPING
	/** -MobilePreview: resizes the window to the phone for Shape and sets the simulated safe area. */
	void ApplyPreview();
	bool CenterPreviewWindow(float DeltaSeconds);
	/** Puts the safe area mask on the game viewport (travel removes viewport widgets). */
	void AttachPreviewMask();
	TOptional<MovecadeKart::Mobile::FMobilePreview> Preview;
	TSharedPtr<SWidget> PreviewMask;
	bool bPreviewMask = true;
	MovecadeKart::Mobile::EScreenShape PreviewShape = MovecadeKart::Mobile::EScreenShape::Landscape;
	FTSTicker::FDelegateHandle CenterTicker;
	int32 CenterAttempts = 0;
#endif

	FMovecadeScreenState State;
	FString CurrentLevel;
	TOptional<MovecadeKart::Arcade::EArcadeOrientation> Override;
	FOnMovecadeScreenChanged ScreenChanged;

	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle ViewportResizedHandle;
	FDelegateHandle SafeFrameHandle;
	FDelegateHandle OrientationHandle;
};

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Platform/Mobile/MobileLifecycle.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MobileLifecycleSubsystem.generated.h"

class AKartRaceManager;
class SMobileStatusOverlay;
class USoundClass;
class USoundMix;
class UWorld;

/** Fired once per run when the first menu is on screen (splash gone, shaders warm): bFirstLaunch on the very first run. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMobileReadyForOnboarding, bool /*bFirstLaunch*/);

/**
 * How Movecade Kart behaves as a phone app, for the life of the game instance. It listens to the OS
 * (FCoreDelegates: deactivate, background, foreground, terminate, audio interruptions and focus, the audio route,
 * the network) and runs MovecadeKart::Mobile's rules (Platform/Mobile/MobileLifecycle.h):
 *   - leaving the player pauses the game (local play on its pause menu; Party Mode until the player is back), mutes
 *     everything, and saves the settings, the store and the profile; another app's music ducks ours; headphones
 *     coming out pause a local game;
 *   - a Party Mode room reconnects at once (URoomHostSubsystem::CloseRoom(false) + OpenRoom(): the same room and
 *     host token) after a network switch or a long trip to the background, and a banner says what the
 *     connection is doing;
 *   - the screen stays awake only while playing (and in a lobby with a room open), menus and pauses run at 30 fps;
 *   - the boot and level loads show a splash (MoviePlayer), and a first launch shows the shader warm-up's progress.
 * Kart race haptics (walls, karts, items, throws) go to the local player's phone through FHaptics.
 *
 * On desktop all of it is off unless -MobileSim (Platform/Mobile/MobileDebugFlags.h), so desktop play is unchanged;
 * the Mobile.Sim console command and -MobileSimScript= drive the same handlers as the OS would.
 */
UCLASS()
class MOVECADEKART_API UMobileLifecycleSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UMobileLifecycleSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The onboarding hook: show the "hold your phone like this" hint from here (bFirstLaunch is the first run ever). */
	FOnMobileReadyForOnboarding OnReadyForOnboarding;
	/** True until the first run reached its first menu. */
	static bool IsFirstLaunch();

	/** Runs an OS event through the lifecycle as the delegates would (the desktop simulation, tests). */
	void HandleAppEvent(MovecadeKart::Mobile::EAppEvent Event);
	/** Overrides the device's network type (the desktop simulation); a real change still arrives through the OS. */
	void SimulateNetwork(ENetworkConnectionType Type);
	/** One Mobile.Sim / -MobileSimScript command (Platform/Mobile/MobileDebugFlags.h). False if it isn't one. */
	bool RunSimCommand(const FString& Command);
	/** This run is the first ever (valid from OnReadyForOnboarding on, for screens that open later). */
	bool WasFirstLaunchThisRun() const { return bFirstLaunchThisRun; }

	const MovecadeKart::Mobile::FLifecycleMachine& GetLifecycle() const { return Lifecycle; }
	MovecadeKart::Mobile::EConnectionBanner GetBanner() const { return Banner; }
	MovecadeKart::Mobile::EPlayState GetPlayState() const { return PlayState; }

private:
	// Binding to the OS.
	void BindPlatformDelegates();
	void UnbindPlatformDelegates();
	/** iOS fires some of these off the game thread; everything runs on the game thread. */
	void QueueAppEvent(MovecadeKart::Mobile::EAppEvent Event);
	void HandleNetworkChanged(ENetworkConnectionType Type);

	// Carrying out the lifecycle's actions.
	void PauseCurrentGame(bool bLocalOnly);
	void ResumeRoomGame();
	void SaveEverything(bool bBlocking) const;
	void ApplyAudio();

	// The poll (TickInterval).
	bool Tick(float DeltaSeconds);
	void UpdateRoom(double Now);
	void UpdatePolicy();
	void UpdateWarmup();
	void UpdateOnboarding();
	void BindRaceHaptics(UWorld& World);
	void RunSimScript(double Now);

	// The splash and the overlay.
	void PrepareLoadingScreen();
	void EnsureOverlay(UWorld& World);
	void RemoveOverlay();

	UWorld* GetGameWorld() const;
	MovecadeKart::Mobile::EPlayState ReadPlayState(const UWorld& World) const;
	double Now() const;

	MovecadeKart::Mobile::FLifecycleMachine Lifecycle;
	MovecadeKart::Mobile::FRoomRecovery Recovery;
	MovecadeKart::Mobile::EConnectionBanner Banner = MovecadeKart::Mobile::EConnectionBanner::None;
	MovecadeKart::Mobile::EPlayState PlayState = MovecadeKart::Mobile::EPlayState::Menu;

	/** Phone behaviour (a phone build, or -MobileSim): policy, splash, overlay, haptics. */
	bool bPhoneBehaviour = false;
	bool bDelegatesBound = false;
	FTSTicker::FDelegateHandle TickHandle;
	double StartTime = 0.0;
	double TickAccumulator = 0.0;

	// Audio.
	bool bMutedAll = false;
	bool bMusicDucked = false;
	UPROPERTY()
	TObjectPtr<USoundMix> DuckMix;
	UPROPERTY()
	TObjectPtr<USoundClass> MusicClass;

	// The room.
	bool bRoomWasOpen = false;
	bool bRoomWentDown = false;
	double RoomRestoredAt = -1.0;
	EMcConnection LastRoomConnection = EMcConnection::Idle;
	TOptional<ENetworkConnectionType> SimulatedNetwork;
	/** Mobile.Sim warmup=N: the pipelines left in a fake warm-up. */
	TOptional<double> SimulatedWarmup;
	/** Mobile.Sim banner=...: the connection banner held on screen (tests and screenshots). */
	TOptional<MovecadeKart::Mobile::EConnectionBanner> SimulatedBanner;
	/** The kart race paused for the app in Party Mode (resumed when the player is back). */
	TWeakObjectPtr<AKartRaceManager> RacePausedForApp;

	// The device policy last applied.
	bool bScreenKeptAwake = false;
	bool bPolicyApplied = false;
	float AppliedFrameCap = 0.f;
	/** t.MaxFPS before the menu cap went on, restored when it comes off. */
	float FrameCapBefore = 0.f;

	// First launch.
	uint32 MaxPrecompilesSeen = 0;
	bool bWarmupShown = false;
	bool bWarmupFast = false;
	bool bOnboardingSent = false;
	bool bFirstLaunchThisRun = false;
	double NetworkPollAccumulator = 0.0;

	// Kart race haptics.
	TWeakObjectPtr<AKartRaceManager> HapticRace;
	FDelegateHandle RaceHapticHandle;
	FDelegateHandle RaceCueHandle;

	TSharedPtr<SMobileStatusOverlay> Overlay;
	TWeakObjectPtr<UWorld> OverlayWorld;

	// -MobileSimScript.
	int32 NextSimStep = 0;

	// FCoreDelegates handles.
	TArray<TPair<TMulticastDelegate<void()>*, FDelegateHandle>> VoidHandles;
	FDelegateHandle TerminateHandle;
	FDelegateHandle AudioInterruptHandle;
	FDelegateHandle AudioFocusHandle;
	FDelegateHandle MusicInterruptHandle;
	FDelegateHandle AudioRouteHandle;
	FDelegateHandle NetworkHandle;
	FDelegateHandle PrepareLoadingScreenHandle;
};

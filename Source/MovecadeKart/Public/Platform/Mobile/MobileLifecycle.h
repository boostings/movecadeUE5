#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Net/RoomTypes.h"

/**
 * The pure rules of how Movecade Kart behaves as a phone app: what to do when the OS takes the screen or the audio
 * away, when the network changes under a Party Mode room, what the status banner says, and when to keep the screen
 * awake or cap the frame rate. No engine state in here: UMobileLifecycleSubsystem feeds these from FCoreDelegates
 * and the running game, and carries out what they decide. MovecadeKart.Mobile.* tests drive them directly.
 */
namespace MovecadeKart::Mobile
{
	/** What the OS tells the app (FCoreDelegates), plus the audio route and focus events, normalised across iOS and Android. */
	enum class EAppEvent : uint8
	{
		/** The app is about to lose the screen's focus: a call, Control Centre, the sleep button. iOS and Android both. */
		WillDeactivate,
		HasReactivated,
		/** Home, the app switcher: the OS may kill the app from here without another word. */
		WillEnterBackground,
		HasEnteredForeground,
		/** May never come on a phone (the background save is the real one). */
		WillTerminate,
		/** iOS AVAudioSession interruption (a call, an alarm, Siri). */
		AudioInterruptBegan,
		AudioInterruptEnded,
		/** Android audio focus. */
		AudioFocusLost,
		AudioFocusGained,
		/** iOS: another app plays music (UserMusicInterruptDelegate); the game ducks its own music. */
		OtherAudioStarted,
		OtherAudioStopped,
		/** Headphones or a Bluetooth headset went away: the game must not suddenly play out loud. */
		HeadphonesUnplugged,
		HeadphonesPlugged,
	};

	MOVECADEKART_API const TCHAR* LexToString(EAppEvent Event);

	enum class EAppState : uint8
	{
		Active,
		/** Visible but not focused (a call banner, Control Centre). */
		Inactive,
		Background,
		Terminating,
	};

	/** What one event asks the glue to do. Everything else (mute, policy) is state, read with the getters. */
	struct FLifecycleActions
	{
		/** Pause the current game (only if one is being played and isn't paused already). */
		bool bPauseGame = false;
		/**
		 * The pause is the player's to lift (headphones out while in front): only local play pauses, on its pause
		 * menu. A Party Mode game has no local pause menu, so it plays on.
		 */
		bool bPauseLocalOnly = false;
		/** Party Mode resumes on return (the room has no local pause menu); local play stays on its pause menu. */
		bool bResumeRoomGame = false;
		/** Write settings, the store and the profile now: the OS may kill the app next. */
		bool bSave = false;
		/** The save must finish before returning (terminate). */
		bool bSaveBlocking = false;
		/** Back in front: let the room recovery look at the socket (FRoomRecovery::NoteForeground with AwaySeconds). */
		bool bReturnedToForeground = false;
		/** How long the app was away, when bReturnedToForeground. */
		double AwaySeconds = 0.0;
	};

	/**
	 * The app lifecycle state machine. Handle() every event in the order the OS sends it; iOS sends
	 * WillDeactivate, WillEnterBackground ... HasEnteredForeground, HasReactivated, Android may skip the first or
	 * repeat events. Every action fires once per trip, however many duplicates arrive.
	 */
	class MOVECADEKART_API FLifecycleMachine
	{
	public:
		FLifecycleActions Handle(EAppEvent Event, double Now);

		EAppState GetState() const { return State; }
		/** Everything silent: not in front, or the OS took the audio (a call). */
		bool ShouldMuteAll() const;
		/** Only the music ducked: another app is playing its own. */
		bool ShouldMuteMusic() const { return bOtherAudio; }
		/** In front, focused and holding the audio: what "the player is here" means. */
		bool IsAttentive() const { return State == EAppState::Active && !bAudioInterrupted && !bAudioFocusLost; }
		/** Times the app went to the background (the save count, for tests and the log). */
		int32 GetBackgroundTrips() const { return BackgroundTrips; }

	private:
		/** Leaving the player (deactivate, background, a call): pause once until the player is back. */
		void Interrupt(FLifecycleActions& Out);
		/** The player is back: Party Mode resumes once nothing else is still interrupting. */
		void MaybeReturn(FLifecycleActions& Out);

		EAppState State = EAppState::Active;
		bool bAudioInterrupted = false;
		bool bAudioFocusLost = false;
		bool bOtherAudio = false;
		/** A pause has been asked since the player was last here. */
		bool bInterrupted = false;
		bool bSavedThisTrip = false;
		double BackgroundSince = -1.0;
		int32 BackgroundTrips = 0;
	};

	/** Seconds after which a socket that says Open is presumed dead after a trip to the background (iOS suspends sockets). */
	constexpr double StaleSocketSeconds = 8.0;
	/** Wi-Fi to mobile data (or back): wait this long for the switch to settle before reconnecting. */
	constexpr double NetworkSettleSeconds = 1.0;
	/** How long "Back online" stays up once the room is reachable again. */
	constexpr double RestoredBannerSeconds = 2.5;

	/** True for a connection type that can carry the room's socket. Unknown counts as online (desktop, old OS). */
	MOVECADEKART_API bool IsOnline(ENetworkConnectionType Type);

	/**
	 * Decides when a Party Mode room should drop its socket and reclaim the room at once (URoomHostSubsystem's
	 * CloseRoom(false) + OpenRoom(): same code, same host token) rather than wait out the reconnect backoff: the
	 * device switched networks (the old socket is bound to the dead interface), came back online, or returned from
	 * a long trip to the background. It never touches a room nobody opened or one that ended for good (Failed).
	 */
	class MOVECADEKART_API FRoomRecovery
	{
	public:
		/** Device connectivity, from FCoreDelegates::OnNetworkConnectionChanged or a poll. Repeats are ignored. */
		void NoteNetwork(ENetworkConnectionType Type, double Now);
		void NoteForeground(double AwaySeconds, double Now);
		/**
		 * True when the room should reconnect now (and clears the request). RoomConnection is the room's state;
		 * Idle (no room) and Failed (the room ended) never reconnect.
		 */
		bool ConsumeReconnect(double Now, EMcConnection RoomConnection);

		bool IsDeviceOffline() const { return bOffline; }
		ENetworkConnectionType GetNetwork() const { return Network; }

	private:
		enum class EReason : uint8 { None, NetworkChanged, BackOnline, Foreground };

		ENetworkConnectionType Network = ENetworkConnectionType::Unknown;
		bool bHasNetwork = false;
		bool bOffline = false;
		EReason Pending = EReason::None;
		double PendingAt = 0.0;
		double PendingAway = 0.0;
	};

	/** The status banner over everything (Party Mode only: local play needs no network). */
	enum class EConnectionBanner : uint8
	{
		None,
		/** The device has no network: "No connection. Waiting for Wi-Fi or mobile data." */
		NoNetwork,
		/** The room's socket is down and retrying. */
		Reconnecting,
		/** Briefly, once the room answers again. */
		Restored,
		/** The room ended (closed by the server, replaced): the players must open a new one. */
		Lost,
	};

	struct FConnectionView
	{
		/** The room's socket (Idle: no room). */
		EMcConnection Room = EMcConnection::Idle;
		/** The room has been Open since it was last opened: a later Connecting or Failed is a lost connection. */
		bool bWasOpen = false;
		/** A game is up (playing or paused). Menus and the lobby show the room's state themselves. */
		bool bInGame = true;
		bool bDeviceOffline = false;
		/** Seconds since the room last came back to Open after being down, or < 0 if it never went down. */
		double SinceRestored = -1.0;
	};

	MOVECADEKART_API EConnectionBanner ChooseBanner(const FConnectionView& View);

	/** What the world is doing, as far as the battery is concerned. */
	enum class EPlayState : uint8
	{
		/** Menus, the lobby, a results board, loading. */
		Menu,
		/** A race or party game is running. */
		Playing,
		/** A game is up but paused (pause menu, a room pause). */
		Paused,
	};

	/** Battery-friendly frame cap in menus and while paused. */
	constexpr float MenuFrameCap = 30.f;

	struct FDevicePolicy
	{
		/** Stop the screen dimming and locking. */
		bool bKeepScreenAwake = false;
		/** 0: no cap of ours (the player's or the renderer's own limit applies). */
		float FrameCap = 0.f;
	};

	/**
	 * Keep the screen awake while playing, and in a Party Mode lobby whose QR the phones are still scanning (a
	 * dimmed screen there stops people joining); everywhere else let it sleep. Menus and pauses run at 30 fps.
	 */
	MOVECADEKART_API FDevicePolicy ChoosePolicy(EAppState App, EPlayState Play, bool bRoomOpen);

	/** First-launch shader warm-up: show the progress card only for a cache big enough to notice. */
	constexpr uint32 MinPrecompilesWorthShowing = 64;

	/** 0..1 done, from the largest remaining count seen and the current one. */
	MOVECADEKART_API float PrecompileProgress(uint32 MaxRemainingSeen, uint32 Remaining);
}

#include "Platform/Mobile/MobileLifecycle.h"

namespace MovecadeKart::Mobile
{
	const TCHAR* LexToString(EAppEvent Event)
	{
		switch (Event)
		{
		case EAppEvent::WillDeactivate: return TEXT("WillDeactivate");
		case EAppEvent::HasReactivated: return TEXT("HasReactivated");
		case EAppEvent::WillEnterBackground: return TEXT("WillEnterBackground");
		case EAppEvent::HasEnteredForeground: return TEXT("HasEnteredForeground");
		case EAppEvent::WillTerminate: return TEXT("WillTerminate");
		case EAppEvent::AudioInterruptBegan: return TEXT("AudioInterruptBegan");
		case EAppEvent::AudioInterruptEnded: return TEXT("AudioInterruptEnded");
		case EAppEvent::AudioFocusLost: return TEXT("AudioFocusLost");
		case EAppEvent::AudioFocusGained: return TEXT("AudioFocusGained");
		case EAppEvent::OtherAudioStarted: return TEXT("OtherAudioStarted");
		case EAppEvent::OtherAudioStopped: return TEXT("OtherAudioStopped");
		case EAppEvent::HeadphonesUnplugged: return TEXT("HeadphonesUnplugged");
		case EAppEvent::HeadphonesPlugged: return TEXT("HeadphonesPlugged");
		}
		return TEXT("?");
	}

	// FLifecycleMachine ---------------------------------------------------------------------------------------

	FLifecycleActions FLifecycleMachine::Handle(EAppEvent Event, double Now)
	{
		FLifecycleActions Out;
		if (State == EAppState::Terminating)
		{
			return Out;
		}
		switch (Event)
		{
		case EAppEvent::WillDeactivate:
			if (State == EAppState::Active)
			{
				State = EAppState::Inactive;
			}
			Interrupt(Out);
			break;

		case EAppEvent::HasReactivated:
			// iOS reactivates after the foreground; a reactivate while still in the background is a stray.
			if (State == EAppState::Inactive)
			{
				State = EAppState::Active;
				MaybeReturn(Out);
			}
			break;

		case EAppEvent::WillEnterBackground:
			if (State != EAppState::Background)
			{
				State = EAppState::Background;
				BackgroundSince = Now;
				++BackgroundTrips;
			}
			Interrupt(Out);
			if (!bSavedThisTrip)
			{
				bSavedThisTrip = true;
				Out.bSave = true;
			}
			break;

		case EAppEvent::HasEnteredForeground:
			if (State != EAppState::Background)
			{
				break;
			}
			// Straight to Active: Android doesn't always follow with a reactivate, and a missed one would leave the game muted.
			State = EAppState::Active;
			bSavedThisTrip = false;
			Out.bReturnedToForeground = true;
			Out.AwaySeconds = BackgroundSince >= 0.0 ? FMath::Max(0.0, Now - BackgroundSince) : 0.0;
			BackgroundSince = -1.0;
			MaybeReturn(Out);
			break;

		case EAppEvent::WillTerminate:
			State = EAppState::Terminating;
			Out.bSave = true;
			Out.bSaveBlocking = true;
			break;

		case EAppEvent::AudioInterruptBegan:
			bAudioInterrupted = true;
			Interrupt(Out);
			break;

		case EAppEvent::AudioInterruptEnded:
			bAudioInterrupted = false;
			MaybeReturn(Out);
			break;

		case EAppEvent::AudioFocusLost:
			bAudioFocusLost = true;
			Interrupt(Out);
			break;

		case EAppEvent::AudioFocusGained:
			bAudioFocusLost = false;
			MaybeReturn(Out);
			break;

		case EAppEvent::OtherAudioStarted:
			bOtherAudio = true;
			break;

		case EAppEvent::OtherAudioStopped:
			bOtherAudio = false;
			break;

		case EAppEvent::HeadphonesUnplugged:
			// In front and listening: stop before the game plays out loud. Away, the interruption has paused it already.
			if (IsAttentive())
			{
				Out.bPauseGame = true;
				Out.bPauseLocalOnly = true;
			}
			break;

		case EAppEvent::HeadphonesPlugged:
			break;
		}
		return Out;
	}

	bool FLifecycleMachine::ShouldMuteAll() const
	{
		return State != EAppState::Active || bAudioInterrupted || bAudioFocusLost;
	}

	void FLifecycleMachine::Interrupt(FLifecycleActions& Out)
	{
		if (!bInterrupted)
		{
			bInterrupted = true;
			Out.bPauseGame = true;
		}
	}

	void FLifecycleMachine::MaybeReturn(FLifecycleActions& Out)
	{
		if (bInterrupted && IsAttentive())
		{
			bInterrupted = false;
			Out.bResumeRoomGame = true;
		}
	}

	// FRoomRecovery -------------------------------------------------------------------------------------------

	bool IsOnline(ENetworkConnectionType Type)
	{
		return Type != ENetworkConnectionType::None && Type != ENetworkConnectionType::AirplaneMode;
	}

	void FRoomRecovery::NoteNetwork(ENetworkConnectionType Type, double Now)
	{
		if (bHasNetwork && Type == Network)
		{
			return;
		}
		const bool bOnline = IsOnline(Type);
		if (!bHasNetwork)
		{
			// The first reading is where we start from, not a change.
			bHasNetwork = true;
			Network = Type;
			bOffline = !bOnline;
			return;
		}
		const bool bWasOnline = !bOffline;
		Network = Type;
		bOffline = !bOnline;
		if (!bOnline)
		{
			// Nothing to reconnect to: wait for the network to come back.
			Pending = EReason::None;
			return;
		}
		if (!bWasOnline)
		{
			Pending = EReason::BackOnline;
			PendingAt = Now;
		}
		else
		{
			// Wi-Fi <-> mobile data: the old socket is bound to the interface that went away. Types flap while the
			// OS switches, so each change restarts the wait.
			Pending = EReason::NetworkChanged;
			PendingAt = Now + NetworkSettleSeconds;
		}
	}

	void FRoomRecovery::NoteForeground(double AwaySeconds, double Now)
	{
		// A network change already forces a reconnect; the foreground check only applies on its own.
		if (Pending == EReason::None)
		{
			Pending = EReason::Foreground;
			PendingAt = Now;
			PendingAway = AwaySeconds;
		}
	}

	bool FRoomRecovery::ConsumeReconnect(double Now, EMcConnection RoomConnection)
	{
		if (Pending == EReason::None || Now < PendingAt || bOffline)
		{
			return false;
		}
		const EReason Reason = Pending;
		Pending = EReason::None;
		if (RoomConnection == EMcConnection::Idle || RoomConnection == EMcConnection::Failed)
		{
			return false;
		}
		if (Reason == EReason::Foreground)
		{
			// A retry waiting out its backoff goes now; a socket that says Open after a long trip away is presumed dead.
			return RoomConnection == EMcConnection::Reconnecting || PendingAway >= StaleSocketSeconds;
		}
		return true;
	}

	// Banner, policy, warm-up ---------------------------------------------------------------------------------

	EConnectionBanner ChooseBanner(const FConnectionView& View)
	{
		if (View.Room == EMcConnection::Idle || !View.bInGame)
		{
			return EConnectionBanner::None;
		}
		if (View.Room == EMcConnection::Failed)
		{
			// A room that never opened shows its error in the lobby; one that was running lost its players.
			return View.bWasOpen ? EConnectionBanner::Lost : EConnectionBanner::None;
		}
		if (View.bDeviceOffline)
		{
			return EConnectionBanner::NoNetwork;
		}
		if (View.Room == EMcConnection::Reconnecting || (View.Room == EMcConnection::Connecting && View.bWasOpen))
		{
			return EConnectionBanner::Reconnecting;
		}
		if (View.Room == EMcConnection::Open && View.SinceRestored >= 0.0 && View.SinceRestored < RestoredBannerSeconds)
		{
			return EConnectionBanner::Restored;
		}
		return EConnectionBanner::None;
	}

	FDevicePolicy ChoosePolicy(EAppState App, EPlayState Play, bool bRoomOpen)
	{
		FDevicePolicy Policy;
		if (App != EAppState::Active)
		{
			return Policy;
		}
		switch (Play)
		{
		case EPlayState::Playing:
			Policy.bKeepScreenAwake = true;
			break;
		case EPlayState::Paused:
			Policy.FrameCap = MenuFrameCap;
			break;
		case EPlayState::Menu:
			Policy.bKeepScreenAwake = bRoomOpen;
			Policy.FrameCap = MenuFrameCap;
			break;
		}
		return Policy;
	}

	float PrecompileProgress(uint32 MaxRemainingSeen, uint32 Remaining)
	{
		if (MaxRemainingSeen == 0)
		{
			return 1.f;
		}
		const float Done = 1.f - static_cast<float>(FMath::Min(Remaining, MaxRemainingSeen)) / static_cast<float>(MaxRemainingSeen);
		return FMath::Clamp(Done, 0.f, 1.f);
	}
}

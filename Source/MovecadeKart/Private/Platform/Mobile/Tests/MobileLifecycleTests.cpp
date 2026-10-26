// MovecadeKart.Mobile.*: the phone app's lifecycle rules, driven with the OS events in the orders iOS and Android send them.

#include "Misc/AutomationTest.h"
#include "Platform/Mobile/MobileDebugFlags.h"
#include "Platform/Mobile/MobileHaptics.h"
#include "Platform/Mobile/MobileLifecycle.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Mobile;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileLifecycleIosTripTest, "MovecadeKart.Mobile.Lifecycle.IosTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileLifecycleIosTripTest::RunTest(const FString& Parameters)
{
	// iOS, the home button: resign active, enter background ... enter foreground, become active.
	FLifecycleMachine M;
	TestTrue(TEXT("starts attentive"), M.IsAttentive() && !M.ShouldMuteAll());

	const FLifecycleActions Deactivate = M.Handle(EAppEvent::WillDeactivate, 10.0);
	TestTrue(TEXT("deactivate pauses"), Deactivate.bPauseGame && !Deactivate.bPauseLocalOnly);
	TestFalse(TEXT("deactivate doesn't save yet"), Deactivate.bSave);
	TestTrue(TEXT("muted while inactive"), M.ShouldMuteAll());

	const FLifecycleActions Background = M.Handle(EAppEvent::WillEnterBackground, 10.5);
	TestFalse(TEXT("one pause per trip"), Background.bPauseGame);
	TestTrue(TEXT("background saves"), Background.bSave && !Background.bSaveBlocking);
	TestEqual(TEXT("background state"), M.GetState(), EAppState::Background);

	const FLifecycleActions Again = M.Handle(EAppEvent::WillEnterBackground, 11.0);
	TestFalse(TEXT("a repeated background neither pauses nor saves again"), Again.bPauseGame || Again.bSave);
	TestEqual(TEXT("one trip counted"), M.GetBackgroundTrips(), 1);

	const FLifecycleActions Foreground = M.Handle(EAppEvent::HasEnteredForeground, 40.5);
	TestTrue(TEXT("foreground reports the return"), Foreground.bReturnedToForeground);
	TestEqual(TEXT("away for 30 s"), Foreground.AwaySeconds, 30.0);
	TestTrue(TEXT("Party Mode resumes on return"), Foreground.bResumeRoomGame);
	TestFalse(TEXT("unmuted"), M.ShouldMuteAll());

	const FLifecycleActions Reactivate = M.Handle(EAppEvent::HasReactivated, 40.6);
	TestFalse(TEXT("the late reactivate is a no-op"), Reactivate.bResumeRoomGame || Reactivate.bPauseGame);
	TestTrue(TEXT("attentive again"), M.IsAttentive());

	// The next trip saves again.
	M.Handle(EAppEvent::WillEnterBackground, 50.0);
	TestEqual(TEXT("second trip"), M.GetBackgroundTrips(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileLifecycleAndroidTripTest, "MovecadeKart.Mobile.Lifecycle.AndroidTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileLifecycleAndroidTripTest::RunTest(const FString& Parameters)
{
	// Android may go straight to the background (no deactivate) and come back without a reactivate.
	FLifecycleMachine M;
	const FLifecycleActions Background = M.Handle(EAppEvent::WillEnterBackground, 0.0);
	TestTrue(TEXT("background alone pauses and saves"), Background.bPauseGame && Background.bSave);
	TestTrue(TEXT("muted"), M.ShouldMuteAll());
	const FLifecycleActions Foreground = M.Handle(EAppEvent::HasEnteredForeground, 3.0);
	TestTrue(TEXT("back without a reactivate"), Foreground.bResumeRoomGame && M.IsAttentive() && !M.ShouldMuteAll());

	// A stray reactivate or foreground while in front does nothing.
	TestFalse(TEXT("stray foreground"), M.Handle(EAppEvent::HasEnteredForeground, 4.0).bReturnedToForeground);
	TestFalse(TEXT("stray reactivate"), M.Handle(EAppEvent::HasReactivated, 4.0).bResumeRoomGame);

	// Deactivate and reactivate without leaving (the notification shade): pause, then the room game resumes.
	TestTrue(TEXT("shade pauses"), M.Handle(EAppEvent::WillDeactivate, 5.0).bPauseGame);
	TestTrue(TEXT("shade up resumes"), M.Handle(EAppEvent::HasReactivated, 6.0).bResumeRoomGame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileLifecycleTerminateTest, "MovecadeKart.Mobile.Lifecycle.Terminate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileLifecycleTerminateTest::RunTest(const FString& Parameters)
{
	FLifecycleMachine M;
	M.Handle(EAppEvent::WillEnterBackground, 0.0);
	const FLifecycleActions Terminate = M.Handle(EAppEvent::WillTerminate, 1.0);
	TestTrue(TEXT("terminate saves, blocking, even after the background save"), Terminate.bSave && Terminate.bSaveBlocking);
	TestEqual(TEXT("terminating"), M.GetState(), EAppState::Terminating);
	const FLifecycleActions After = M.Handle(EAppEvent::HasEnteredForeground, 2.0);
	TestFalse(TEXT("nothing after terminate"), After.bReturnedToForeground || After.bSave || After.bResumeRoomGame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileLifecycleAudioTest, "MovecadeKart.Mobile.Lifecycle.Audio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileLifecycleAudioTest::RunTest(const FString& Parameters)
{
	// A phone call: the audio session is interrupted while the app stays in front.
	FLifecycleMachine M;
	const FLifecycleActions Call = M.Handle(EAppEvent::AudioInterruptBegan, 0.0);
	TestTrue(TEXT("a call pauses and mutes"), Call.bPauseGame && M.ShouldMuteAll() && !M.IsAttentive());
	const FLifecycleActions Hangup = M.Handle(EAppEvent::AudioInterruptEnded, 60.0);
	TestTrue(TEXT("hanging up resumes the room game and unmutes"), Hangup.bResumeRoomGame && !M.ShouldMuteAll());

	// The call goes on while the app is in the background: back in front, still on the call, nothing resumes yet.
	M.Handle(EAppEvent::AudioInterruptBegan, 100.0);
	M.Handle(EAppEvent::WillEnterBackground, 101.0);
	const FLifecycleActions BackOnCall = M.Handle(EAppEvent::HasEnteredForeground, 110.0);
	TestFalse(TEXT("still on the call"), BackOnCall.bResumeRoomGame);
	TestTrue(TEXT("still muted"), M.ShouldMuteAll());
	TestTrue(TEXT("the call ends: resume"), M.Handle(EAppEvent::AudioInterruptEnded, 120.0).bResumeRoomGame);

	// Android audio focus behaves the same.
	TestTrue(TEXT("focus lost pauses"), M.Handle(EAppEvent::AudioFocusLost, 130.0).bPauseGame);
	TestTrue(TEXT("muted without focus"), M.ShouldMuteAll());
	TestTrue(TEXT("focus back resumes"), M.Handle(EAppEvent::AudioFocusGained, 131.0).bResumeRoomGame);

	// Another app's music: only our music ducks, nothing pauses.
	const FLifecycleActions Music = M.Handle(EAppEvent::OtherAudioStarted, 140.0);
	TestFalse(TEXT("music doesn't pause"), Music.bPauseGame);
	TestTrue(TEXT("music ducked, the rest audible"), M.ShouldMuteMusic() && !M.ShouldMuteAll());
	M.Handle(EAppEvent::OtherAudioStopped, 150.0);
	TestFalse(TEXT("music back"), M.ShouldMuteMusic());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileLifecycleHeadphonesTest, "MovecadeKart.Mobile.Lifecycle.Headphones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileLifecycleHeadphonesTest::RunTest(const FString& Parameters)
{
	FLifecycleMachine M;
	const FLifecycleActions Unplug = M.Handle(EAppEvent::HeadphonesUnplugged, 0.0);
	TestTrue(TEXT("unplugging pauses local play only"), Unplug.bPauseGame && Unplug.bPauseLocalOnly);
	TestFalse(TEXT("and doesn't mute"), M.ShouldMuteAll());
	TestFalse(TEXT("plugging in does nothing"), M.Handle(EAppEvent::HeadphonesPlugged, 1.0).bPauseGame);

	// Away, the trip has paused the game already.
	M.Handle(EAppEvent::WillEnterBackground, 2.0);
	TestFalse(TEXT("no second pause while away"), M.Handle(EAppEvent::HeadphonesUnplugged, 3.0).bPauseGame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileRoomRecoveryTest, "MovecadeKart.Mobile.RoomRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileRoomRecoveryTest::RunTest(const FString& Parameters)
{
	FRoomRecovery R;
	R.NoteNetwork(ENetworkConnectionType::WiFi, 0.0);
	TestFalse(TEXT("the first reading is no change"), R.ConsumeReconnect(0.0, EMcConnection::Open));

	// Wi-Fi to mobile data: reconnect once the switch settled, then not again.
	R.NoteNetwork(ENetworkConnectionType::Cell, 10.0);
	TestFalse(TEXT("waits for the switch to settle"), R.ConsumeReconnect(10.5, EMcConnection::Open));
	R.NoteNetwork(ENetworkConnectionType::WiFi, 10.6);
	TestFalse(TEXT("a flap restarts the wait"), R.ConsumeReconnect(11.2, EMcConnection::Open));
	TestTrue(TEXT("settled: reconnect"), R.ConsumeReconnect(11.7, EMcConnection::Open));
	TestFalse(TEXT("once"), R.ConsumeReconnect(12.0, EMcConnection::Open));

	// Losing the network: nothing to reconnect to; coming back reconnects at once.
	R.NoteNetwork(ENetworkConnectionType::None, 20.0);
	TestTrue(TEXT("offline"), R.IsDeviceOffline());
	TestFalse(TEXT("no reconnect while offline"), R.ConsumeReconnect(25.0, EMcConnection::Reconnecting));
	R.NoteNetwork(ENetworkConnectionType::Cell, 30.0);
	TestFalse(TEXT("online again"), R.IsDeviceOffline());
	TestTrue(TEXT("back online: reconnect now"), R.ConsumeReconnect(30.0, EMcConnection::Reconnecting));
	R.NoteNetwork(ENetworkConnectionType::AirplaneMode, 31.0);
	TestTrue(TEXT("airplane mode is offline"), R.IsDeviceOffline());
	R.NoteNetwork(ENetworkConnectionType::Cell, 32.0);

	// No room, or a room that ended: never.
	TestFalse(TEXT("no room"), R.ConsumeReconnect(32.0, EMcConnection::Idle));
	R.NoteNetwork(ENetworkConnectionType::WiFi, 40.0);
	TestFalse(TEXT("a failed room stays failed"), R.ConsumeReconnect(45.0, EMcConnection::Failed));

	// Foreground: a short trip keeps a healthy socket, a long one reclaims the room, a waiting retry goes now.
	R.NoteForeground(2.0, 50.0);
	TestFalse(TEXT("short trip, socket open: keep it"), R.ConsumeReconnect(50.0, EMcConnection::Open));
	R.NoteForeground(2.0, 51.0);
	TestTrue(TEXT("short trip, retry waiting: now"), R.ConsumeReconnect(51.0, EMcConnection::Reconnecting));
	R.NoteForeground(StaleSocketSeconds + 1.0, 52.0);
	TestTrue(TEXT("long trip: the open socket is presumed dead"), R.ConsumeReconnect(52.0, EMcConnection::Open));

	// A network change during the trip wins over the foreground check.
	R.NoteNetwork(ENetworkConnectionType::Cell, 60.0);
	R.NoteForeground(1.0, 60.1);
	TestFalse(TEXT("still settling"), R.ConsumeReconnect(60.2, EMcConnection::Open));
	TestTrue(TEXT("the network change reconnects"), R.ConsumeReconnect(61.1, EMcConnection::Open));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileBannerTest, "MovecadeKart.Mobile.Banner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileBannerTest::RunTest(const FString& Parameters)
{
	auto Banner = [](EMcConnection Room, bool bWasOpen, bool bOffline, double SinceRestored)
	{
		FConnectionView View;
		View.Room = Room;
		View.bWasOpen = bWasOpen;
		View.bDeviceOffline = bOffline;
		View.SinceRestored = SinceRestored;
		return ChooseBanner(View);
	};
	FConnectionView InLobby;
	InLobby.Room = EMcConnection::Reconnecting;
	InLobby.bWasOpen = true;
	InLobby.bDeviceOffline = true;
	InLobby.bInGame = false;
	TestEqual(TEXT("menus and the lobby show the room's state themselves"), ChooseBanner(InLobby), EConnectionBanner::None);
	TestEqual(TEXT("no room, no banner (even offline)"), Banner(EMcConnection::Idle, false, true, -1.0), EConnectionBanner::None);
	TestEqual(TEXT("first connect: the lobby shows it"), Banner(EMcConnection::Connecting, false, false, -1.0), EConnectionBanner::None);
	TestEqual(TEXT("open"), Banner(EMcConnection::Open, true, false, -1.0), EConnectionBanner::None);
	TestEqual(TEXT("offline wins over reconnecting"), Banner(EMcConnection::Reconnecting, true, true, -1.0), EConnectionBanner::NoNetwork);
	TestEqual(TEXT("reconnecting"), Banner(EMcConnection::Reconnecting, true, false, -1.0), EConnectionBanner::Reconnecting);
	TestEqual(TEXT("our own reclaim reads as reconnecting"), Banner(EMcConnection::Connecting, true, false, -1.0), EConnectionBanner::Reconnecting);
	TestEqual(TEXT("back online, briefly"), Banner(EMcConnection::Open, true, false, 1.0), EConnectionBanner::Restored);
	TestEqual(TEXT("then gone"), Banner(EMcConnection::Open, true, false, RestoredBannerSeconds + 0.1), EConnectionBanner::None);
	TestEqual(TEXT("a running room that ended"), Banner(EMcConnection::Failed, true, false, -1.0), EConnectionBanner::Lost);
	TestEqual(TEXT("a room that never opened: the lobby's error"), Banner(EMcConnection::Failed, false, false, -1.0), EConnectionBanner::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobilePolicyTest, "MovecadeKart.Mobile.DevicePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobilePolicyTest::RunTest(const FString& Parameters)
{
	const FDevicePolicy Playing = ChoosePolicy(EAppState::Active, EPlayState::Playing, false);
	TestTrue(TEXT("playing: awake, uncapped"), Playing.bKeepScreenAwake && Playing.FrameCap == 0.f);
	const FDevicePolicy Paused = ChoosePolicy(EAppState::Active, EPlayState::Paused, true);
	TestTrue(TEXT("paused: may sleep, 30 fps"), !Paused.bKeepScreenAwake && Paused.FrameCap == MenuFrameCap);
	const FDevicePolicy Menu = ChoosePolicy(EAppState::Active, EPlayState::Menu, false);
	TestTrue(TEXT("menu: may sleep, 30 fps"), !Menu.bKeepScreenAwake && Menu.FrameCap == MenuFrameCap);
	const FDevicePolicy Lobby = ChoosePolicy(EAppState::Active, EPlayState::Menu, true);
	TestTrue(TEXT("a lobby with a room open stays awake for scanning, at 30 fps"), Lobby.bKeepScreenAwake && Lobby.FrameCap == MenuFrameCap);
	const FDevicePolicy Away = ChoosePolicy(EAppState::Background, EPlayState::Playing, true);
	TestFalse(TEXT("away: nothing held"), Away.bKeepScreenAwake);

	TestEqual(TEXT("no warm-up is done"), PrecompileProgress(0, 0), 1.f);
	TestEqual(TEXT("warm-up start"), PrecompileProgress(400, 400), 0.f);
	TestEqual(TEXT("warm-up half"), PrecompileProgress(400, 200), 0.5f);
	TestEqual(TEXT("a remaining count above the max seen clamps"), PrecompileProgress(400, 900), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileHapticsTest, "MovecadeKart.Mobile.Haptics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileHapticsTest::RunTest(const FString& Parameters)
{
	const FHapticPulse Tap = PulseFor(EHaptic::Tap);
	const FHapticPulse Throw = PulseFor(EHaptic::Throw);
	const FHapticPulse Hit = PulseFor(EHaptic::Hit);
	const FHapticPulse Big = PulseFor(EHaptic::Big);
	TestTrue(TEXT("kinds rise in weight"), Tap.Intensity < Throw.Intensity && Throw.Intensity < Hit.Intensity && Hit.Intensity < Big.Intensity);
	TestTrue(TEXT("a soft hit is lighter than a hard one"), PulseFor(EHaptic::Hit, 0.1f).Intensity < Hit.Intensity);
	TestTrue(TEXT("but never lighter than a tap"), PulseFor(EHaptic::Throw, 0.f).Intensity >= Tap.Intensity);
	for (const EHaptic Kind : { EHaptic::Tap, EHaptic::Throw, EHaptic::Landing, EHaptic::Hit, EHaptic::Big })
	{
		const FHapticPulse Pulse = PulseFor(Kind, 1.f);
		TestTrue(TEXT("pulses fit between throttled kicks"), Pulse.Duration > 0.f && Pulse.Duration < MinHapticGapSeconds && Pulse.Intensity <= 1.f);
	}

	FHapticThrottle Throttle;
	TestTrue(TEXT("first pulse"), Throttle.Allow(EHaptic::Hit, 1.0));
	TestFalse(TEXT("a scrape's next contact merges"), Throttle.Allow(EHaptic::Hit, 1.05));
	TestTrue(TEXT("a big moment always gets through"), Throttle.Allow(EHaptic::Big, 1.06));
	TestTrue(TEXT("after the gap"), Throttle.Allow(EHaptic::Throw, 1.07 + MinHapticGapSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSimFlagsTest, "MovecadeKart.Mobile.SimFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileSimFlagsTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("off by default"), FMobileDebugFlags::Parse(TEXT("-game")).bSimulate);
	TestTrue(TEXT("-MobileSim"), FMobileDebugFlags::Parse(TEXT("-game -MobileSim")).bSimulate);

	const FMobileDebugFlags Scripted = FMobileDebugFlags::Parse(TEXT("-game -MobileSimScript=14:foreground,10:background,12:network=cell,13:bogus"));
	TestTrue(TEXT("a script implies -MobileSim"), Scripted.bSimulate);
	if (TestEqual(TEXT("three valid steps"), Scripted.Script.Num(), 3))
	{
		TestTrue(TEXT("sorted by time"), Scripted.Script[0].At == 10.0 && Scripted.Script[1].At == 12.0 && Scripted.Script[2].At == 14.0);
		TestEqual(TEXT("command kept"), Scripted.Script[1].Command, FString(TEXT("network=cell")));
	}

	auto EventOf = [](const TCHAR* Text) { return ParseSimCommand(Text).Event.Get(EAppEvent::HeadphonesPlugged); };
	TestEqual(TEXT("background"), EventOf(TEXT("background")), EAppEvent::WillEnterBackground);
	TestEqual(TEXT("case-insensitive"), EventOf(TEXT(" Interrupt-End ")), EAppEvent::AudioInterruptEnded);
	TestEqual(TEXT("network"), ParseSimCommand(TEXT("network=wifi")).Network.Get(ENetworkConnectionType::Unknown), ENetworkConnectionType::WiFi);
	TestEqual(TEXT("fake warm-up"), ParseSimCommand(TEXT("warmup=300")).Warmup.Get(0u), 300u);
	TestFalse(TEXT("unknown network"), ParseSimCommand(TEXT("network=5g")).Network.IsSet());
	TestFalse(TEXT("unknown command"), ParseSimCommand(TEXT("explode")).Event.IsSet());
	return true;
}

#endif

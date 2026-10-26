#include "Platform/Mobile/MobileDebugFlags.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace MovecadeKart::Mobile
{
	FMobileDebugFlags FMobileDebugFlags::Parse(const TCHAR* CommandLine)
	{
		FMobileDebugFlags Flags;
#if !UE_BUILD_SHIPPING
		Flags.bSimulate = FParse::Param(CommandLine, TEXT("MobileSim"));
		FString ScriptText;
		if (FParse::Value(CommandLine, TEXT("-MobileSimScript="), ScriptText, false))
		{
			TArray<FString> Steps;
			ScriptText.TrimQuotes().ParseIntoArray(Steps, TEXT(","));
			for (const FString& Step : Steps)
			{
				FString At, Command;
				const FSimCommand Parsed = ParseSimCommand(Step.Split(TEXT(":"), &At, &Command) ? Command : FString());
				if (At.IsNumeric() && (Parsed.Event || Parsed.Network || Parsed.Warmup || Parsed.Banner || Parsed.bBannerOff))
				{
					Flags.Script.Add({ FCString::Atod(*At), Command.TrimStartAndEnd() });
				}
			}
			Flags.Script.StableSort([](const FSimStep& A, const FSimStep& B) { return A.At < B.At; });
			Flags.bSimulate = true;
		}
#endif
		return Flags;
	}

	const FMobileDebugFlags& FMobileDebugFlags::Get()
	{
		static const FMobileDebugFlags Flags = Parse(FCommandLine::Get());
		return Flags;
	}

	FSimCommand ParseSimCommand(const FString& Text)
	{
		static const TPair<const TCHAR*, EAppEvent> Events[] = {
			{ TEXT("background"), EAppEvent::WillEnterBackground },
			{ TEXT("foreground"), EAppEvent::HasEnteredForeground },
			{ TEXT("deactivate"), EAppEvent::WillDeactivate },
			{ TEXT("reactivate"), EAppEvent::HasReactivated },
			{ TEXT("interrupt"), EAppEvent::AudioInterruptBegan },
			{ TEXT("interrupt-end"), EAppEvent::AudioInterruptEnded },
			{ TEXT("focus-lost"), EAppEvent::AudioFocusLost },
			{ TEXT("focus"), EAppEvent::AudioFocusGained },
			{ TEXT("music"), EAppEvent::OtherAudioStarted },
			{ TEXT("music-end"), EAppEvent::OtherAudioStopped },
			{ TEXT("unplug"), EAppEvent::HeadphonesUnplugged },
			{ TEXT("plug"), EAppEvent::HeadphonesPlugged },
		};
		static const TPair<const TCHAR*, ENetworkConnectionType> Networks[] = {
			{ TEXT("none"), ENetworkConnectionType::None },
			{ TEXT("wifi"), ENetworkConnectionType::WiFi },
			{ TEXT("cell"), ENetworkConnectionType::Cell },
			{ TEXT("airplane"), ENetworkConnectionType::AirplaneMode },
		};

		FSimCommand Out;
		const FString Command = Text.TrimStartAndEnd();
		FString Count;
		if (Command.Split(TEXT("="), nullptr, &Count) && Command.StartsWith(TEXT("warmup="), ESearchCase::IgnoreCase) && Count.IsNumeric())
		{
			Out.Warmup = static_cast<uint32>(FMath::Max(0, FCString::Atoi(*Count)));
			return Out;
		}
		FString BannerName;
		if (Command.Split(TEXT("="), nullptr, &BannerName) && Command.StartsWith(TEXT("banner="), ESearchCase::IgnoreCase))
		{
			static const TPair<const TCHAR*, EConnectionBanner> Banners[] = {
				{ TEXT("nonetwork"), EConnectionBanner::NoNetwork },
				{ TEXT("reconnecting"), EConnectionBanner::Reconnecting },
				{ TEXT("restored"), EConnectionBanner::Restored },
				{ TEXT("lost"), EConnectionBanner::Lost },
			};
			Out.bBannerOff = BannerName.Equals(TEXT("off"), ESearchCase::IgnoreCase);
			for (const TPair<const TCHAR*, EConnectionBanner>& Entry : Banners)
			{
				if (BannerName.Equals(Entry.Key, ESearchCase::IgnoreCase))
				{
					Out.Banner = Entry.Value;
				}
			}
			return Out;
		}
		FString Network;
		if (Command.Split(TEXT("="), nullptr, &Network) && Command.StartsWith(TEXT("network="), ESearchCase::IgnoreCase))
		{
			for (const TPair<const TCHAR*, ENetworkConnectionType>& Entry : Networks)
			{
				if (Network.Equals(Entry.Key, ESearchCase::IgnoreCase))
				{
					Out.Network = Entry.Value;
				}
			}
			return Out;
		}
		for (const TPair<const TCHAR*, EAppEvent>& Entry : Events)
		{
			if (Command.Equals(Entry.Key, ESearchCase::IgnoreCase))
			{
				Out.Event = Entry.Value;
			}
		}
		return Out;
	}

	bool IsPhoneBehaviourOn()
	{
#if PLATFORM_IOS || PLATFORM_ANDROID
		return true;
#else
		return FMobileDebugFlags::Get().bSimulate;
#endif
	}
}

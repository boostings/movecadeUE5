#pragma once

#include "CoreMinimal.h"
#include "Store/KartStoreBackend.h"

/**
 * The phone stores. Each is compiled only on its platform: UnrealBuildTool leaves the Store/IOS and Store/Android
 * folders out of every other platform's build, so the editor, the tests and desktop never see them.
 */
namespace MovecadeKart::Store
{
	/** Save slot of the free items a phone holds itself (Mobile::FLocalFreeItems). */
	inline const TCHAR* const MobileFreeItemsSlot = TEXT("KartStoreFree");

#if PLATFORM_IOS
	/** StoreKit 2 (Store/IOS/AppStoreBackend.cpp); null when the device can't make payments at all. */
	TUniquePtr<IKartStoreBackend> MakeAppStoreBackend();
#endif

#if PLATFORM_ANDROID
	/** Google Play Billing through OnlineSubsystemGooglePlay (Store/Android/GooglePlayBackend.cpp); null without it. */
	TUniquePtr<IKartStoreBackend> MakeGooglePlayBackend();
#endif
}

#pragma once

#include "CoreMinimal.h"
#include "Store/KartStoreBackend.h"

namespace MovecadeKart::Store
{
	/** The Steam backend when this build has Steamworks (MOVECADE_WITH_STEAM) and Steam is running; else null. */
	MOVECADEKART_API TUniquePtr<IKartStoreBackend> MakeSteamBackend(int32 DevAppId);
}

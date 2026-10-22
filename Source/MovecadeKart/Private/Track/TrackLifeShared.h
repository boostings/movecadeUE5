#pragma once

#include "CoreMinimal.h"
#include "FX/KartFx.h"
#include "KartAssetLoad.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTrackLife, Log, All);

/** Shared by ATrackLife's source files. */
namespace MovecadeKart::LifeShared
{
	/** The karts the shared material parameters and the trample field follow (MPC_TrackLife Kart0..Kart7). */
	constexpr int32 MaxKarts = 8;

	/** An asset in Content/Tracks/Shared/Life (Tools/ue/make_track_life.py); one warning if it's missing. */
	template <typename T>
	T* LoadLife(const TCHAR* Name)
	{
		return LoadKartAsset<T>(*FString::Printf(TEXT("/Game/Tracks/Shared/Life/%s.%s"), Name, Name), AssetSource::TrackLife);
	}

	/** One of the palettes' sRGB hex colours, linear. */
	inline FLinearColor Srgb(const TCHAR* Hex)
	{
		return Fx::Hex(Hex);
	}
}

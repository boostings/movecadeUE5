#pragma once

#include "CoreMinimal.h"
#include "FX/KartFx.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTrackLife, Log, All);

/** Shared by ATrackLife's source files. */
namespace MovecadeKart::LifeShared
{
	/** The karts the shared material parameters and the trample field follow (MPC_TrackLife Kart0..Kart7). */
	constexpr int32 MaxKarts = 8;

	/** An asset in Content/Tracks/Shared/Life (Tools/ue/make_track_life.py), quietly. */
	template <typename T>
	T* LoadLife(const TCHAR* Name)
	{
		return LoadObject<T>(nullptr, *FString::Printf(TEXT("/Game/Tracks/Shared/Life/%s.%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** One of the palettes' sRGB hex colours, linear. */
	inline FLinearColor Srgb(const TCHAR* Hex)
	{
		return Fx::Hex(Hex);
	}
}

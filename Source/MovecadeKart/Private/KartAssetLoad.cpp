#include "KartAssetLoad.h"

DEFINE_LOG_CATEGORY(LogKartAssets);

namespace MovecadeKart
{
	void WarnMissingAsset(const TCHAR* Path, const TCHAR* MadeBy)
	{
		check(IsInGameThread());
		// A deliberate process-wide cache: each missing path is reported once per session.
		static TSet<FString> Reported;
		bool bAlreadyReported = false;
		Reported.Add(Path, &bAlreadyReported);
		if (!bAlreadyReported)
		{
			UE_LOG(LogKartAssets, Warning, TEXT("Missing %s (run %s); carrying on without it."), Path, MadeBy);
		}
	}
}

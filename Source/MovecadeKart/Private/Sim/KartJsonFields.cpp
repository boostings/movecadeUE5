#include "Sim/KartJsonFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartJson, Log, All);

namespace MovecadeKart::Json
{
	int32 ToInt32(double V, int32 Default, int32 Min, int32 Max)
	{
		if (!FMath::IsFinite(V))
		{
			return Default;
		}
		// Clamp as a double first: the cast is only defined for values that fit.
		return static_cast<int32>(FMath::Clamp(FMath::TruncToDouble(V), static_cast<double>(Min), static_cast<double>(Max)));
	}

	int32 IntField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, int32 Default, int32 Min, int32 Max)
	{
		double V = 0.0;
		if (!O.IsValid() || !O->TryGetNumberField(Key, V))
		{
			return Default;
		}
		const int32 Out = ToInt32(V, Default, Min, Max);
		if (!FMath::IsFinite(V) || Out != FMath::TruncToDouble(V))
		{
			UE_LOG(LogKartJson, Warning, TEXT("\"%s\" is %g; using %d (expected %d..%d)"), Key, V, Out, Min, Max);
		}
		return Out;
	}
}

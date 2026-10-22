#include "UI/KartAccessibility.h"

#include "Engine/UserInterfaceSettings.h"
#include "UI/KartPlayerPrefs.h"
#include "UI/KartUIStyle.h"

namespace
{
	const UKartPlayerPrefs* Prefs()
	{
		return GetDefault<UKartPlayerPrefs>();
	}

	FLinearColor Hex(const TCHAR* Code)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Code));
	}

	const float ScaleSteps[] = { 0.85f, 0.9f, 1.f, 1.1f, 1.2f, 1.25f };
}

bool FKartAccessibility::ReduceMotion()
{
	const UKartPlayerPrefs* P = Prefs();
	return P && P->bReduceMotion;
}

float FKartAccessibility::MotionScale()
{
	return ReduceMotion() ? 0.25f : 1.f;
}

float FKartAccessibility::UIScale()
{
	const UKartPlayerPrefs* P = Prefs();
	return P ? FMath::Clamp(P->UIScale, ScaleSteps[0], ScaleSteps[UE_ARRAY_COUNT(ScaleSteps) - 1]) : 1.f;
}

EKartColorVision FKartAccessibility::ColorVision()
{
	const UKartPlayerPrefs* P = Prefs();
	return P ? static_cast<EKartColorVision>(FMath::Clamp<int32>(P->ColorVision, 0, static_cast<int32>(EKartColorVision::Count) - 1)) : EKartColorVision::Standard;
}

FLinearColor FKartAccessibility::Good()
{
	// Okabe-Ito inspired pairs: each good/bad pair differs in lightness as well as hue.
	static const FLinearColor RedGreen = Hex(TEXT("3d9cff"));
	static const FLinearColor BlueYellow = Hex(TEXT("1fc7b6"));
	switch (ColorVision())
	{
	case EKartColorVision::RedGreen: return RedGreen;
	case EKartColorVision::BlueYellow: return BlueYellow;
	default: return KartUI::Color::Mint;
	}
}

FLinearColor FKartAccessibility::Bad()
{
	static const FLinearColor RedGreen = Hex(TEXT("ffa31f"));
	static const FLinearColor BlueYellow = Hex(TEXT("e8336b"));
	switch (ColorVision())
	{
	case EKartColorVision::RedGreen: return RedGreen;
	case EKartColorVision::BlueYellow: return BlueYellow;
	default: return KartUI::Color::Hot;
	}
}

FLinearColor FKartAccessibility::Warn()
{
	static const FLinearColor BlueYellow = Hex(TEXT("ff6b9a"));
	return ColorVision() == EKartColorVision::BlueYellow ? BlueYellow : KartUI::Color::Sun;
}

TConstArrayView<float> FKartAccessibility::UIScaleSteps()
{
	return MakeArrayView(ScaleSteps);
}

void FKartAccessibility::ApplyUIScale()
{
	if (UUserInterfaceSettings* Settings = GetMutableDefault<UUserInterfaceSettings>())
	{
		// The game layer multiplies its DPI curve by this every frame.
		Settings->ApplicationScale = UIScale();
	}
}

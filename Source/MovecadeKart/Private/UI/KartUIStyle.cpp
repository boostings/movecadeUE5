#include "UI/KartUIStyle.h"

#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "FX/KartFx.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartUIStyle, Log, All);

namespace
{
	FLinearColor Hex(const TCHAR* Code, float Alpha = 1.f)
	{
		FLinearColor C = MovecadeKart::Fx::Hex(Code);
		C.A = Alpha;
		return C;
	}

	struct FFontSet
	{
		TSharedPtr<const FCompositeFont> Faces[5];
		bool bLoaded = false;
	};

	const TCHAR* FacePaths[] = {
		TEXT("/Game/UI/Fonts/LilitaOne-Regular.LilitaOne-Regular"),
		TEXT("/Game/UI/Fonts/Fredoka-Bold.Fredoka-Bold"),
		TEXT("/Game/UI/Fonts/Fredoka-SemiBold.Fredoka-SemiBold"),
		TEXT("/Game/UI/Fonts/Nunito-Black.Nunito-Black"),
		TEXT("/Game/UI/Fonts/Nunito-ExtraBold.Nunito-ExtraBold"),
	};

	const FFontSet& Fonts()
	{
		static FFontSet Set;
		static bool bTried = false;
		if (!bTried && IsInGameThread())
		{
			bTried = true;
			Set.bLoaded = true;
			for (int32 i = 0; i < UE_ARRAY_COUNT(FacePaths); ++i)
			{
				UFontFace* Face = LoadObject<UFontFace>(nullptr, FacePaths[i]);
				if (!Face)
				{
					UE_LOG(LogKartUIStyle, Warning, TEXT("Missing UI font %s (run Tools/ue/import_ui_assets.py); using Roboto."), FacePaths[i]);
					Set.bLoaded = false;
					continue;
				}
				// The faces live for the whole session; the composite font only holds a raw pointer.
				Face->AddToRoot();
				TSharedRef<FCompositeFont> Composite = MakeShared<FCompositeFont>();
				FTypefaceEntry& Entry = Composite->DefaultTypeface.Fonts.Add_GetRef(FTypefaceEntry(TEXT("Regular")));
				Entry.Font = FFontData(Face);
				Set.Faces[i] = Composite;
			}
		}
		return Set;
	}
}

namespace KartUI::Color
{
	const FLinearColor Night = Hex(TEXT("1b1745"));
	const FLinearColor NightSoft = Hex(TEXT("2b2768"));
	const FLinearColor Navy = Hex(TEXT("221f55"));
	const FLinearColor NavyDim = Hex(TEXT("6f6b9e"));
	const FLinearColor White = Hex(TEXT("ffffff"));
	const FLinearColor Paper = Hex(TEXT("fffbf3"));
	const FLinearColor PaperShade = Hex(TEXT("ece5f5"));
	const FLinearColor Sun = Hex(TEXT("ffd43b"));
	const FLinearColor SunDeep = Hex(TEXT("ff9a1f"));
	const FLinearColor Sky = Hex(TEXT("38b6ff"));
	const FLinearColor SkyDeep = Hex(TEXT("2a62e0"));
	const FLinearColor Hot = Hex(TEXT("ff4d7d"));
	const FLinearColor Mint = Hex(TEXT("34dfa0"));
	const FLinearColor Grape = Hex(TEXT("8f6bff"));
	const FLinearColor Gold = Hex(TEXT("ffc928"));
	const FLinearColor Silver = Hex(TEXT("cfd9ea"));
	const FLinearColor Bronze = Hex(TEXT("ec9a5c"));
	const FLinearColor Shadow = Hex(TEXT("0c0a2a"), 0.38f);

	FLinearColor ForPlace(int32 Place)
	{
		switch (Place)
		{
		case 1: return Gold;
		case 2: return Silver;
		case 3: return Bronze;
		default: return Sky;
		}
	}

	FLinearColor TextOn(const FLinearColor& Background)
	{
		return Background.GetLuminance() > 0.35f ? Navy : White;
	}

	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha);
	}
}

namespace KartUI
{
	FSlateFontInfo Font(EFont Face, float Size, float OutlineSize, const FLinearColor& OutlineColor)
	{
		const FFontSet& Set = Fonts();
		const TSharedPtr<const FCompositeFont>& Composite = Set.Faces[static_cast<int32>(Face)];
		static const FName Regular(TEXT("Regular"));
		FSlateFontInfo Info = Composite.IsValid()
			? FSlateFontInfo(Composite, Size, Regular)
			: FCoreStyle::GetDefaultFontStyle(Face == EFont::Body ? "Bold" : "Black", FMath::RoundToInt32(Size * 0.8f));
		if (OutlineSize > 0.f)
		{
			Info.OutlineSettings.OutlineSize = FMath::RoundToInt32(OutlineSize);
			Info.OutlineSettings.OutlineColor = OutlineColor;
			Info.OutlineSettings.bSeparateFillAlpha = false;
		}
		return Info;
	}

	bool HasGameFonts()
	{
		return Fonts().bLoaded;
	}
}

namespace KartUI::Ease
{
	float OutCubic(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		const float U = 1.f - T;
		return 1.f - U * U * U;
	}

	float InCubic(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return T * T * T;
	}

	float InOutSine(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return 0.5f - 0.5f * FMath::Cos(UE_PI * T);
	}

	float OutBack(float T, float Overshoot)
	{
		T = FMath::Clamp(T, 0.f, 1.f) - 1.f;
		return 1.f + T * T * ((Overshoot + 1.f) * T + Overshoot);
	}

	float OutElastic(float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		if (T <= 0.f || T >= 1.f)
		{
			return T;
		}
		return FMath::Pow(2.f, -10.f * T) * FMath::Sin((T * 10.f - 0.75f) * (2.f * UE_PI / 3.f)) + 1.f;
	}

	float Pulse(float T)
	{
		return FMath::Sin(FMath::Clamp(T, 0.f, 1.f) * UE_PI);
	}

	float Progress(double Now, double Start, double Duration)
	{
		return Duration <= 0.0 ? 1.f : static_cast<float>(FMath::Clamp((Now - Start) / Duration, 0.0, 1.0));
	}
}

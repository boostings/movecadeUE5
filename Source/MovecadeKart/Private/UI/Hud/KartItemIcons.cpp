#include "UI/Hud/KartItemIcons.h"

#include "UI/KartUIPainter.h"
#include "UI/KartWidgets.h"

using namespace KartUI;
using MovecadeKart::EKartItem;

namespace
{
	FLinearColor Hex(const TCHAR* Code)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Code));
	}

	/** The buzz disc seen from above: eight swept teeth, a coloured disc, a pale bumper ring and a hub. */
	void Disc(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Body, double Now)
	{
		const float R = S * 0.36f;
		const double Spin = Now * 3.0;
		for (int32 k = 0; k < 8; ++k)
		{
			const double A = Spin + 2.0 * PI * k / 8.0;
			const FVector2D Out(FMath::Cos(A), FMath::Sin(A));
			const FVector2D Along(-Out.Y, Out.X);
			P.Polygon({ C + Out * R * 0.9, C + Out * R * 1.3 + Along * R * 0.18, C + Out * R * 0.95 + Along * R * 0.42 }, Color::White);
		}
		P.Circle(C, R, Color::White, Color::Night, S * 0.04f);
		P.Circle(C, R * 0.8f, Body);
		P.Circle(C, R * 0.28f, Color::White, Color::Night, S * 0.025f);
		P.Circle(C + FVector2D(-R * 0.4, -R * 0.42), R * 0.16f, Color::WithAlpha(Color::White, 0.7f));
	}

	TArray<FVector2D> Arc(const FVector2D& C, double R, double From, double To, int32 Steps)
	{
		TArray<FVector2D> Points;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double A = FMath::DegreesToRadians(FMath::Lerp(From, To, static_cast<double>(i) / Steps));
			Points.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		return Points;
	}

	void Flame(const FKartPainter& P, const FVector2D& C, float S)
	{
		// A teardrop pointing up: orange outside, yellow core.
		auto Drop = [&](float Scale, const FLinearColor& Fill)
		{
			TArray<FVector2D> Points = Arc(C + FVector2D(0.0, S * 0.12 * Scale), S * 0.3 * Scale, -20.0, 200.0, 14);
			Points.Insert(C + FVector2D(0.0, -S * 0.48 * Scale), 0);
			P.Polygon(Points, Fill);
		};
		Drop(1.f, Hex(TEXT("ff6a2a")));
		Drop(0.62f, Color::Sun);
	}
}

namespace KartItemIcons
{
	FLinearColor Tint(EKartItem Item)
	{
		switch (Item)
		{
		case EKartItem::Shot: return Hex(TEXT("37c45a"));
		case EKartItem::Seeker: return Hex(TEXT("ec3b4b"));
		case EKartItem::SkySeeker: return Hex(TEXT("3b7cf0"));
		case EKartItem::Banana: return Hex(TEXT("ff7a22"));
		case EKartItem::Nitro:
		case EKartItem::TripleNitro: return Hex(TEXT("ff8a2a"));
		case EKartItem::Prism: return Hex(TEXT("ffe25a"));
		case EKartItem::Lightning: return Hex(TEXT("ffd21f"));
		case EKartItem::Ink: return Hex(TEXT("4a3f7a"));
		case EKartItem::Bomb: return Hex(TEXT("2b2b38"));
		case EKartItem::Rocket: return Hex(TEXT("f2f4f8"));
		default: return Color::White;
		}
	}

	void Draw(const FKartPainter& P, EKartItem Item, const FVector2D& C, float S, double Now)
	{
		switch (Item)
		{
		case EKartItem::Shot:
			Disc(P, C, S, Tint(Item), Now);
			break;
		case EKartItem::Seeker:
			// The homing disc: a target reticle on its hub.
			Disc(P, C, S, Tint(Item), Now);
			P.Lines({ C + FVector2D(-S * 0.2, 0.0), C + FVector2D(S * 0.2, 0.0) }, Color::Night, S * 0.03f);
			P.Lines({ C + FVector2D(0.0, -S * 0.2), C + FVector2D(0.0, S * 0.2) }, Color::Night, S * 0.03f);
			break;
		case EKartItem::SkySeeker:
		{
			// The drone: four rotor rings round a smaller disc.
			for (int32 k = 0; k < 4; ++k)
			{
				const double A = PI * 0.25 + PI * 0.5 * k;
				const FVector2D Rotor = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * S * 0.34;
				P.Circle(Rotor, S * 0.13f, Color::WithAlpha(Color::White, 0.35f), Color::White, S * 0.03f);
			}
			Disc(P, C, S * 0.72f, Tint(Item), Now);
			break;
		}
		case EKartItem::Banana:
		{
			// The traffic cone: an orange cone with two white bands on a dark foot.
			P.Box(C + FVector2D(-S * 0.36, S * 0.3), FVector2D(S * 0.72, S * 0.12), Color::Night, S * 0.03f);
			const FVector2D Tip = C + FVector2D(0.0, -S * 0.46);
			const FVector2D L = C + FVector2D(-S * 0.27, S * 0.3), R = C + FVector2D(S * 0.27, S * 0.3);
			P.Polygon({ Tip, R, L }, Tint(Item));
			auto Band = [&](float From, float To)
			{
				P.Polygon({ FMath::Lerp(Tip, L, From), FMath::Lerp(Tip, R, From), FMath::Lerp(Tip, R, To), FMath::Lerp(Tip, L, To) }, Color::White);
			};
			Band(0.32f, 0.45f);
			Band(0.62f, 0.76f);
			P.Lines({ Tip, R, L }, Color::Night, S * 0.035f, true);
			break;
		}
		case EKartItem::Nitro:
			Flame(P, C, S);
			break;
		case EKartItem::TripleNitro:
			Flame(P, C + FVector2D(-S * 0.24, S * 0.1), S * 0.6f);
			Flame(P, C + FVector2D(S * 0.24, S * 0.1), S * 0.6f);
			Flame(P, C + FVector2D(0.0, -S * 0.12), S * 0.66f);
			break;
		case EKartItem::Prism:
		{
			// The prism crystal, cycling through the rainbow: a cut gem with a crown and pavilion.
			const FLinearColor Hue = FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(Now * 180.0, 256.0)), 170, 255);
			const FVector2D Top = C + FVector2D(0.0, -S * 0.3), Bottom = C + FVector2D(0.0, S * 0.46);
			const FVector2D L = C + FVector2D(-S * 0.42, -S * 0.1), R = C + FVector2D(S * 0.42, -S * 0.1);
			const FVector2D TL = C + FVector2D(-S * 0.22, -S * 0.3), TR = C + FVector2D(S * 0.22, -S * 0.3);
			P.Polygon({ TL, TR, R, Bottom, L }, Hue);
			P.Polygon({ TL, TR, R, L }, Color::WithAlpha(Color::White, 0.45f));
			P.Lines({ TL, TR, R, Bottom, L }, Color::Night, S * 0.035f, true);
			P.Lines({ L, R }, Color::Night, S * 0.025f);
			P.Lines({ C + FVector2D(-S * 0.1, -S * 0.1), Bottom, C + FVector2D(S * 0.1, -S * 0.1) }, Color::WithAlpha(Color::Night, 0.5f), S * 0.02f);
			break;
		}
		case EKartItem::Lightning:
		{
			const TArray<FVector2D> Bolt = {
				C + FVector2D(S * 0.12, -S * 0.5), C + FVector2D(-S * 0.28, S * 0.06), C + FVector2D(-S * 0.02, S * 0.06),
				C + FVector2D(-S * 0.14, S * 0.5), C + FVector2D(S * 0.3, -S * 0.1), C + FVector2D(S * 0.04, -S * 0.1),
			};
			// Two convex halves.
			P.Polygon({ Bolt[0], Bolt[1], Bolt[2], Bolt[5] }, Tint(Item));
			P.Polygon({ Bolt[5], Bolt[2], Bolt[3], Bolt[4] }, Tint(Item));
			TArray<FVector2D> Outline = Bolt;
			P.Lines(Outline, Color::Night, S * 0.035f, true);
			break;
		}
		case EKartItem::Ink:
			P.Circle(C + FVector2D(0.0, -S * 0.05), S * 0.3f, Tint(Item));
			P.Circle(C + FVector2D(-S * 0.24, S * 0.1), S * 0.16f, Tint(Item));
			P.Circle(C + FVector2D(S * 0.26, S * 0.12), S * 0.14f, Tint(Item));
			P.Circle(C + FVector2D(S * 0.05, S * 0.32), S * 0.1f, Tint(Item));
			P.Circle(C + FVector2D(-S * 0.1, -S * 0.14), S * 0.06f, Color::WithAlpha(Color::White, 0.55f));
			break;
		case EKartItem::Bomb:
		{
			P.Circle(C + FVector2D(0.0, S * 0.08), S * 0.36f, Tint(Item), Color::White, S * 0.03f);
			P.Box(C + FVector2D(-S * 0.08, -S * 0.36), FVector2D(S * 0.16, S * 0.12), Color::NavyDim, S * 0.03f);
			P.Lines({ C + FVector2D(0.0, -S * 0.36), C + FVector2D(S * 0.1, -S * 0.44), C + FVector2D(S * 0.2, -S * 0.42) }, Hex(TEXT("c99a5a")), S * 0.04f);
			const float Spark = 0.8f + 0.3f * FMath::Sin(static_cast<float>(Now) * 30.f);
			KartWidgets::Star(P, C + FVector2D(S * 0.24, -S * 0.44), S * 0.12f * Spark, S * 0.05f, Color::Sun, static_cast<float>(Now * 200.0));
			P.Circle(C + FVector2D(-S * 0.13, -S * 0.02), S * 0.07f, Color::WithAlpha(Color::White, 0.6f));
			break;
		}
		case EKartItem::Rocket:
		{
			const FVector2D Body(S * 0.7, S * 0.36);
			P.Box(C - FVector2D(S * 0.4, Body.Y * 0.5), FVector2D(S * 0.55, Body.Y), Tint(Item), S * 0.06f, Color::Night, S * 0.03f);
			P.Polygon({ C + FVector2D(S * 0.15, -Body.Y * 0.5), C + FVector2D(S * 0.46, 0.0), C + FVector2D(S * 0.15, Body.Y * 0.5) }, Tint(Item));
			P.Box(C + FVector2D(-S * 0.1, -Body.Y * 0.5), FVector2D(S * 0.08, Body.Y), Color::Hot);
			P.Polygon({ C + FVector2D(-S * 0.4, -Body.Y * 0.5), C + FVector2D(-S * 0.5, -Body.Y * 0.95), C + FVector2D(-S * 0.24, -Body.Y * 0.5) }, Color::Hot);
			P.Polygon({ C + FVector2D(-S * 0.4, Body.Y * 0.5), C + FVector2D(-S * 0.5, Body.Y * 0.95), C + FVector2D(-S * 0.24, Body.Y * 0.5) }, Color::Hot);
			// A porthole, not an eye.
			P.Circle(C + FVector2D(S * 0.05, 0.0), S * 0.075f, Hex(TEXT("5fd6ff")), Color::Night, S * 0.025f);
			break;
		}
		default:
			break;
		}
	}
}

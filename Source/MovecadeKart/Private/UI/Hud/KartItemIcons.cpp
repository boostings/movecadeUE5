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

	/** A shell: coloured dome, white rim band, a highlight. */
	void Shell(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Body)
	{
		const float R = S * 0.42f;
		P.Circle(C, R, Body, Color::Night, S * 0.05f);
		P.Box(C + FVector2D(-R, R * 0.12), FVector2D(R * 2.0, R * 0.34), Color::White, R * 0.17f, Color::Night, S * 0.03f);
		P.Circle(C + FVector2D(-R * 0.35, -R * 0.4), R * 0.2f, Color::WithAlpha(Color::White, 0.75f));
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
		case EKartItem::Banana: return Hex(TEXT("ffd84a"));
		case EKartItem::Nitro:
		case EKartItem::TripleNitro: return Hex(TEXT("ff8a2a"));
		case EKartItem::Prism: return Hex(TEXT("ffe25a"));
		case EKartItem::Lightning: return Hex(TEXT("ffd21f"));
		case EKartItem::Ink: return Hex(TEXT("4a3f7a"));
		case EKartItem::Bomb: return Hex(TEXT("2b2b38"));
		case EKartItem::Rocket: return Hex(TEXT("3d3f4f"));
		default: return Color::White;
		}
	}

	void Draw(const FKartPainter& P, EKartItem Item, const FVector2D& C, float S, double Now)
	{
		switch (Item)
		{
		case EKartItem::Shot:
			Shell(P, C, S, Tint(Item));
			break;
		case EKartItem::Seeker:
			Shell(P, C, S, Tint(Item));
			P.Circle(C + FVector2D(0.0, -S * 0.16), S * 0.07f, Color::White);
			break;
		case EKartItem::SkySeeker:
		{
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const double Flap = FMath::Sin(Now * 14.0) * S * 0.05;
				P.Polygon({ C + FVector2D(Side * S * 0.25, -S * 0.08), C + FVector2D(Side * S * 0.55, -S * 0.3 - Flap), C + FVector2D(Side * S * 0.5, S * 0.05) }, Color::White);
			}
			Shell(P, C, S * 0.9f, Tint(Item));
			break;
		}
		case EKartItem::Banana:
		{
			// A crescent: an outer arc back along a smaller, offset inner arc.
			TArray<FVector2D> Outer = Arc(C + FVector2D(S * 0.08, -S * 0.08), S * 0.44, 60.0, 200.0, 16);
			TArray<FVector2D> Inner = Arc(C + FVector2D(S * 0.2, -S * 0.2), S * 0.36, 200.0, 60.0, 16);
			// Fill it as quads between matching arc points (it isn't convex).
			for (int32 i = 0; i < 16; ++i)
			{
				P.Polygon({ Outer[i], Outer[i + 1], Inner[15 - i], Inner[16 - i] }, Tint(Item));
			}
			P.Lines(Outer, Color::Night, S * 0.04f);
			P.Circle(Outer[0], S * 0.05f, Hex(TEXT("6b4b1e")));
			P.Circle(Outer.Last(), S * 0.05f, Hex(TEXT("6b4b1e")));
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
			const FLinearColor Hue = FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(Now * 180.0, 256.0)), 190, 255);
			KartWidgets::Star(P, C, S * 0.5f, S * 0.22f, Color::Night, 0.f);
			KartWidgets::Star(P, C, S * 0.43f, S * 0.18f, Hue, 0.f);
			P.Circle(C + FVector2D(-S * 0.07, -S * 0.04), S * 0.04f, Color::Night);
			P.Circle(C + FVector2D(S * 0.07, -S * 0.04), S * 0.04f, Color::Night);
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
			P.Circle(C + FVector2D(-S * 0.1, -S * 0.12), S * 0.07f, Color::White);
			P.Circle(C + FVector2D(S * 0.1, -S * 0.12), S * 0.07f, Color::White);
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
			P.Circle(C + FVector2D(S * 0.18, -S * 0.04), S * 0.06f, Color::White);
			break;
		}
		default:
			break;
		}
	}
}

#include "UI/Screens/KartScreens.h"

#include "Framework/Application/SlateApplication.h"
#include "Store/KartStore.h"
#include "Store/KartStoreSubsystem.h"
#include "UI/KartFrontEnd.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "KartStoreScreen"

using namespace KartUI;
using namespace MovecadeKart::Store;
using KartWidgets::FItemState;

namespace
{
	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	FKartStore* Store()
	{
		return UKartStoreSubsystem::Active();
	}

	FLinearColor Lighter(const FLinearColor& C, float T) { return FMath::Lerp(C, FLinearColor::White, T); }
	FLinearColor Darker(const FLinearColor& C, float T) { return FMath::Lerp(C, Color::Night, T); }

	/** The store's tabs and which items each shows. */
	enum class ETab : uint8 { Karts, Drivers, Party, Bundles, Free, Num };

	FText TabName(ETab Tab)
	{
		switch (Tab)
		{
		case ETab::Karts: return LOCTEXT("TabKarts", "Karts");
		case ETab::Drivers: return LOCTEXT("TabDrivers", "Drivers");
		case ETab::Party: return LOCTEXT("TabParty", "Party Games");
		case ETab::Bundles: return LOCTEXT("TabBundles", "Bundles");
		default: return LOCTEXT("TabFree", "Free Unlocks");
		}
	}

	FLinearColor TabAccent(ETab Tab)
	{
		switch (Tab)
		{
		case ETab::Karts: return Color::Hot;
		case ETab::Drivers: return Color::Sky;
		case ETab::Party: return Color::Sun;
		case ETab::Bundles: return Color::Grape;
		default: return Color::Mint;
		}
	}

	TArray<const FItem*> ItemsFor(ETab Tab)
	{
		TArray<const FItem*> Out;
		for (const FItem& I : Catalog())
		{
			bool bIn = false;
			switch (Tab)
			{
			case ETab::Karts: bIn = I.Slot == ESlot::KartPaint; break;
			case ETab::Drivers: bIn = I.Slot == ESlot::Colourway || I.Slot == ESlot::Headwear || I.Slot == ESlot::VictoryEmote; break;
			case ETab::Party: bIn = I.Slot == ESlot::BowlingBall || I.Slot == ESlot::Katana || I.Slot == ESlot::StackerBlock; break;
			case ETab::Bundles: bIn = I.Slot == ESlot::Bundle; break;
			default: bIn = I.IsFree(); break;
			}
			if (bIn)
			{
				Out.Add(&I);
			}
		}
		return Out;
	}

	// ------------------------------------------------------------------------------------------------ Previews

	void PaintPreview(const FKartPainter& P, const FItem& Item, const FVector2D& C, float S, double Now, int32 Depth = 0);

	void KartPreview(const FKartPainter& P, const FLinearColor& Paint, float Metallic, const FVector2D& C, float S, double Now)
	{
		const double Bob = FMath::Sin(Now * 3.0) * 0.02 * S;
		const FVector2D O = C + FVector2D(0.0, Bob);
		// Shadow, wheels, body, cockpit, driver's helmet.
		P.Box(O + FVector2D(-0.82 * S, 0.42 * S), FVector2D(1.64 * S, 0.12 * S), Color::WithAlpha(Color::Night, 0.25f), 0.06f * S);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			P.Circle(O + FVector2D(Side * 0.52 * S, 0.3 * S), 0.2f * S, Color::Night);
			P.Circle(O + FVector2D(Side * 0.52 * S, 0.3 * S), 0.09f * S, Color::Silver);
		}
		const TArray<FVector2D> Body = {
			O + FVector2D(-0.9 * S, 0.22 * S), O + FVector2D(-0.82 * S, -0.06 * S), O + FVector2D(-0.3 * S, -0.16 * S),
			O + FVector2D(0.42 * S, -0.12 * S), O + FVector2D(0.9 * S, 0.02 * S), O + FVector2D(0.88 * S, 0.24 * S) };
		P.Polygon(Body, Darker(Paint, 0.35f));
		const TArray<FVector2D> Top = {
			O + FVector2D(-0.84 * S, 0.12 * S), O + FVector2D(-0.78 * S, -0.04 * S), O + FVector2D(-0.3 * S, -0.12 * S),
			O + FVector2D(0.4 * S, -0.08 * S), O + FVector2D(0.84 * S, 0.04 * S), O + FVector2D(0.82 * S, 0.14 * S) };
		P.Polygon(Top, Paint);
		// A sheen band that sweeps across metallic finishes.
		if (Metallic > 0.4f)
		{
			const double X = FMath::Fmod(Now * 0.6, 2.0) - 1.0;
			P.Polygon({ O + FVector2D((X - 0.08) * S, -0.12 * S), O + FVector2D((X + 0.04) * S, -0.12 * S), O + FVector2D((X - 0.06) * S, 0.14 * S), O + FVector2D((X - 0.18) * S, 0.14 * S) },
				Color::WithAlpha(Color::White, 0.55f));
		}
		P.Box(O + FVector2D(-0.28 * S, -0.3 * S), FVector2D(0.5 * S, 0.18 * S), Color::Night, 0.08f * S);
		P.Circle(O + FVector2D(-0.04 * S, -0.42 * S), 0.17f * S, Color::White, Color::Night, 0.03f * S);
		P.Box(O + FVector2D(-0.19 * S, -0.52 * S), FVector2D(0.3 * S, 0.05 * S), Color::Hot, 0.02f * S);
		P.Box(O + FVector2D(0.02 * S, -0.44 * S), FVector2D(0.12 * S, 0.1 * S), Color::Night, 0.04f * S);
		P.Box(O + FVector2D(0.62 * S, -0.02 * S), FVector2D(0.18 * S, 0.06 * S), Color::Sun, 0.02f * S);
	}

	void DriverPreview(const FKartPainter& P, const FLinearColor& Jersey, const FLinearColor& Pants, const FLinearColor& Shoes, const FVector2D& C, float S, double Now)
	{
		const double Sway = FMath::Sin(Now * 2.4) * 0.03 * S;
		const FVector2D O = C + FVector2D(Sway, 0.0);
		const FLinearColor Skin = FLinearColor::FromSRGBColor(FColor(0xd0, 0x8a, 0x68));
		P.Box(O + FVector2D(-0.3 * S, 0.62 * S), FVector2D(0.26 * S, 0.12 * S), Shoes, 0.05f * S, Color::Night, 0.02f * S);
		P.Box(O + FVector2D(0.04 * S, 0.62 * S), FVector2D(0.26 * S, 0.12 * S), Shoes, 0.05f * S, Color::Night, 0.02f * S);
		P.Box(O + FVector2D(-0.26 * S, 0.16 * S), FVector2D(0.22 * S, 0.5 * S), Pants, 0.06f * S);
		P.Box(O + FVector2D(0.04 * S, 0.16 * S), FVector2D(0.22 * S, 0.5 * S), Pants, 0.06f * S);
		P.Box(O + FVector2D(-0.46 * S, -0.3 * S), FVector2D(0.16 * S, 0.44 * S), Jersey, 0.07f * S);
		P.Box(O + FVector2D(0.3 * S, -0.3 * S), FVector2D(0.16 * S, 0.44 * S), Jersey, 0.07f * S);
		P.Box(O + FVector2D(-0.32 * S, -0.34 * S), FVector2D(0.64 * S, 0.56 * S), Jersey, 0.12f * S, Color::WithAlpha(Color::Night, 0.4f), 0.02f * S);
		P.Box(O + FVector2D(-0.32 * S, -0.06 * S), FVector2D(0.64 * S, 0.07 * S), Darker(Jersey, 0.25f));
		P.Circle(O + FVector2D(-0.38 * S, 0.18 * S), 0.07f * S, Skin);
		P.Circle(O + FVector2D(0.38 * S, 0.18 * S), 0.07f * S, Skin);
		P.Circle(O + FVector2D(0.0, -0.58 * S), 0.24f * S, Skin, Color::WithAlpha(Color::Night, 0.4f), 0.02f * S);
		P.Box(O + FVector2D(-0.24 * S, -0.84 * S), FVector2D(0.48 * S, 0.16 * S), Color::NavyDim, 0.08f * S);
	}

	void HeadPreview(const FKartPainter& P, EKartAccessory Accessory, const FLinearColor& Tint, const FVector2D& C, float S)
	{
		const FLinearColor Skin = FLinearColor::FromSRGBColor(FColor(0xd0, 0x8a, 0x68));
		P.Box(C + FVector2D(-0.36 * S, 0.42 * S), FVector2D(0.72 * S, 0.4 * S), Color::Hot, 0.2f * S);
		P.Circle(C, 0.5f * S, Skin, Color::WithAlpha(Color::Night, 0.4f), 0.03f * S);
		if (Accessory != EKartAccessory::Cap)
		{
			P.Box(C + FVector2D(-0.48 * S, -0.56 * S), FVector2D(0.96 * S, 0.3 * S), FLinearColor::FromSRGBColor(FColor(0x3a, 0x24, 0x1c)), 0.15f * S);
		}
		P.Circle(C + FVector2D(-0.18 * S, 0.02 * S), 0.06f * S, Color::Night);
		P.Circle(C + FVector2D(0.18 * S, 0.02 * S), 0.06f * S, Color::Night);
		P.Box(C + FVector2D(-0.14 * S, 0.22 * S), FVector2D(0.28 * S, 0.06 * S), Darker(Skin, 0.4f), 0.03f * S);
		switch (Accessory)
		{
		case EKartAccessory::Cap:
			P.Polygon({ C + FVector2D(-0.52 * S, -0.12 * S), C + FVector2D(-0.4 * S, -0.48 * S), C + FVector2D(0.0, -0.6 * S), C + FVector2D(0.4 * S, -0.48 * S), C + FVector2D(0.52 * S, -0.12 * S) }, Tint);
			P.Box(C + FVector2D(0.1 * S, -0.2 * S), FVector2D(0.62 * S, 0.1 * S), Darker(Tint, 0.3f), 0.05f * S);
			P.Circle(C + FVector2D(0.0, -0.36 * S), 0.08f * S, Color::White);
			break;
		case EKartAccessory::Glasses:
			P.Box(C + FVector2D(-0.4 * S, -0.08 * S), FVector2D(0.34 * S, 0.2 * S), Tint, 0.08f * S, Color::Night, 0.03f * S);
			P.Box(C + FVector2D(0.06 * S, -0.08 * S), FVector2D(0.34 * S, 0.2 * S), Tint, 0.08f * S, Color::Night, 0.03f * S);
			P.Box(C + FVector2D(-0.06 * S, -0.02 * S), FVector2D(0.12 * S, 0.04 * S), Color::Night);
			P.Box(C + FVector2D(-0.34 * S, -0.06 * S), FVector2D(0.1 * S, 0.05 * S), Color::WithAlpha(Color::White, 0.7f), 0.02f * S);
			break;
		case EKartAccessory::Headband:
			P.Box(C + FVector2D(-0.5 * S, -0.34 * S), FVector2D(1.0 * S, 0.16 * S), Tint, 0.06f * S, Darker(Tint, 0.3f), 0.02f * S);
			P.Polygon({ C + FVector2D(0.46 * S, -0.3 * S), C + FVector2D(0.72 * S, -0.1 * S), C + FVector2D(0.6 * S, -0.02 * S), C + FVector2D(0.44 * S, -0.2 * S) }, Tint);
			break;
		default:
			break;
		}
	}

	void BallPreview(const FKartPainter& P, const FVisual& V, const FVector2D& C, float S, double Now)
	{
		if (V.Emissive > 0.f)
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 3.f);
			P.Circle(C, 0.72f * S, Color::WithAlpha(V.Secondary, 0.25f + 0.2f * Pulse));
		}
		P.Box(C + FVector2D(-0.5 * S, 0.56 * S), FVector2D(1.0 * S, 0.1 * S), Color::WithAlpha(Color::Night, 0.25f), 0.05f * S);
		P.Circle(C, 0.6f * S, V.Primary, Darker(V.Primary, 0.5f), 0.03f * S);
		// Swirl specks that roll with the ball.
		for (int32 i = 0; i < 7; ++i)
		{
			const double A = Now * 1.2 + i * 0.9;
			const double R = (0.18 + 0.05 * (i % 3)) * S * (1.0 + 0.6 * (i % 2));
			P.Circle(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, (0.04f + 0.02f * (i % 3)) * S, Color::WithAlpha(V.Secondary, 0.75f));
		}
		P.Circle(C + FVector2D(-0.22 * S, -0.24 * S), 0.14f * S, Color::WithAlpha(V.Tertiary, 0.35f + 0.4f * V.Metallic));
		for (int32 Hole = 0; Hole < 3; ++Hole)
		{
			const FVector2D H = C + FVector2D(0.14 * S + (Hole == 2 ? 0.14 * S : Hole * 0.18 * S), -0.1 * S + (Hole == 2 ? 0.2 * S : 0.0));
			P.Circle(H, 0.065f * S, Color::Night);
		}
	}

	void KatanaPreview(const FKartPainter& P, const FVisual& V, const FVector2D& C, float S, double Now)
	{
		const FKartPainter R = P.Transformed(C, 1.f, -32.f + static_cast<float>(FMath::Sin(Now * 1.6) * 3.0));
		// Blade (Primary) with its edge (Secondary), the guard and the wrapped grip (Tertiary).
		R.Polygon({ C + FVector2D(-0.42 * S, -0.07 * S), C + FVector2D(0.86 * S, -0.06 * S), C + FVector2D(1.0 * S, 0.02 * S), C + FVector2D(-0.42 * S, 0.07 * S) }, V.Primary);
		R.Polygon({ C + FVector2D(-0.42 * S, 0.02 * S), C + FVector2D(0.94 * S, 0.0), C + FVector2D(1.0 * S, 0.02 * S), C + FVector2D(-0.42 * S, 0.07 * S) }, V.Secondary);
		if (V.Emissive > 0.f)
		{
			R.Box(C + FVector2D(-0.42 * S, -0.1 * S), FVector2D(1.42 * S, 0.2 * S), Color::WithAlpha(V.Secondary, 0.2f), 0.1f * S);
		}
		R.Box(C + FVector2D(-0.5 * S, -0.16 * S), FVector2D(0.08 * S, 0.32 * S), V.Tertiary, 0.03f * S, Color::Night, 0.02f * S);
		R.Box(C + FVector2D(-0.96 * S, -0.06 * S), FVector2D(0.46 * S, 0.12 * S), Darker(V.Tertiary, 0.45f), 0.04f * S);
		for (int32 i = 0; i < 5; ++i)
		{
			const FVector2D D = C + FVector2D((-0.9 + i * 0.09) * S, 0.0);
			R.Polygon({ D + FVector2D(0.0, -0.05 * S), D + FVector2D(0.04 * S, 0.0), D + FVector2D(0.0, 0.05 * S), D + FVector2D(-0.04 * S, 0.0) }, Lighter(V.Tertiary, 0.4f));
		}
	}

	void BlocksPreview(const FKartPainter& P, const FLinearColor& Block, const FVector2D& C, float S, double Now)
	{
		auto Cube = [&P, &Block, S](const FVector2D& At)
		{
			const double W = 0.5 * S, H = 0.3 * S, D = 0.14 * S;
			P.Polygon({ At + FVector2D(-W * 0.5, -H * 0.5), At + FVector2D(-W * 0.5 + D, -H * 0.5 - D), At + FVector2D(W * 0.5 + D, -H * 0.5 - D), At + FVector2D(W * 0.5, -H * 0.5) }, Lighter(Block, 0.3f));
			P.Polygon({ At + FVector2D(W * 0.5, -H * 0.5), At + FVector2D(W * 0.5 + D, -H * 0.5 - D), At + FVector2D(W * 0.5 + D, H * 0.5 - D), At + FVector2D(W * 0.5, H * 0.5) }, Darker(Block, 0.3f));
			P.Box(At - FVector2D(W, H) * 0.5, FVector2D(W, H), Block, 0.02f * S, Color::WithAlpha(Color::Night, 0.5f), 0.02f * S);
		};
		const double Sway = FMath::Sin(Now * 2.0) * 0.04 * S;
		Cube(C + FVector2D(-0.12 * S, 0.42 * S));
		Cube(C + FVector2D(0.06 * S + Sway * 0.5, 0.1 * S));
		Cube(C + FVector2D(-0.04 * S + Sway, -0.22 * S));
	}

	void EmotePreview(const FKartPainter& P, const FLinearColor& Tint, const FVector2D& C, float S, double Now)
	{
		const double Hop = FMath::Abs(FMath::Sin(Now * 4.0)) * 0.1 * S;
		const FVector2D O = C - FVector2D(0.0, Hop);
		KartWidgets::Star(P, C + FVector2D(-0.55 * S, -0.5 * S), 0.16f * S, 0.07f * S, Color::Sun, static_cast<float>(Now * 60.0));
		KartWidgets::Star(P, C + FVector2D(0.55 * S, -0.35 * S), 0.12f * S, 0.05f * S, Color::Sun, static_cast<float>(-Now * 60.0));
		P.Box(O + FVector2D(-0.18 * S, -0.2 * S), FVector2D(0.36 * S, 0.5 * S), Tint, 0.1f * S);
		P.Polygon({ O + FVector2D(-0.16 * S, -0.16 * S), O + FVector2D(-0.5 * S, -0.62 * S), O + FVector2D(-0.42 * S, -0.68 * S), O + FVector2D(-0.06 * S, -0.22 * S) }, Tint);
		P.Polygon({ O + FVector2D(0.16 * S, -0.16 * S), O + FVector2D(0.5 * S, -0.62 * S), O + FVector2D(0.42 * S, -0.68 * S), O + FVector2D(0.06 * S, -0.22 * S) }, Tint);
		P.Circle(O + FVector2D(0.0, -0.42 * S), 0.18f * S, Color::PaperShade, Color::Night, 0.02f * S);
		P.Box(O + FVector2D(-0.16 * S, 0.28 * S), FVector2D(0.12 * S, 0.36 * S), Color::Navy, 0.05f * S);
		P.Box(O + FVector2D(0.04 * S, 0.28 * S), FVector2D(0.12 * S, 0.36 * S), Color::Navy, 0.05f * S);
	}

	void PaintPreview(const FKartPainter& P, const FItem& Item, const FVector2D& C, float S, double Now, int32 Depth)
	{
		const FVisual& V = Item.Visual;
		switch (Item.Slot)
		{
		case ESlot::KartPaint: KartPreview(P, V.Primary, V.Metallic, C, S, Now); break;
		case ESlot::Colourway: DriverPreview(P, V.Primary, V.Secondary, V.Tertiary, C, S * 0.9f, Now); break;
		case ESlot::Headwear: HeadPreview(P, V.Accessory, V.Primary, C + FVector2D(0.0, 0.06 * S), S * 0.9f); break;
		case ESlot::BowlingBall: BallPreview(P, V, C, S, Now); break;
		case ESlot::Katana: KatanaPreview(P, V, C, S * 0.9f, Now); break;
		case ESlot::StackerBlock: BlocksPreview(P, V.Primary, C, S, Now); break;
		case ESlot::VictoryEmote: EmotePreview(P, V.Primary, C, S, Now); break;
		case ESlot::Bundle:
			if (Depth == 0)
			{
				// A fan of what's inside.
				const int32 Shown = FMath::Min(3, Item.Contents.Num());
				for (int32 i = 0; i < Shown; ++i)
				{
					const int32 Pick = Item.Contents[(i * Item.Contents.Num()) / FMath::Max(Shown, 1)];
					if (const FItem* Part = FindItem(Pick))
					{
						const float Tilt = (i - (Shown - 1) * 0.5f) * 12.f;
						const FVector2D At = C + FVector2D((i - (Shown - 1) * 0.5) * 0.5 * S, FMath::Abs(i - (Shown - 1) * 0.5) * 0.08 * S);
						const FKartPainter Card = P.Transformed(At, 1.f, Tilt);
						Card.Card(At - FVector2D(0.34 * S, 0.44 * S), FVector2D(0.68 * S, 0.88 * S), Lighter(Part->Visual.Primary, 0.65f), 0.08f * S, 4.f, Color::White, 2.f);
						PaintPreview(Card, *Part, At, S * 0.3f, Now, 1);
					}
				}
				P.Circle(C + FVector2D(0.62 * S, -0.5 * S), 0.2f * S, Color::Hot, Color::White, 0.03f * S);
				P.Text(FString::Printf(TEXT("x%d"), Item.Contents.Num()), Font(EFont::Label, 0.18f * S), C + FVector2D(0.62 * S, -0.5 * S), Color::White, FVector2D(0.5, 0.5));
			}
			break;
		default:
			break;
		}
	}

	/** A small lock (earned items not earned yet). */
	void Lock(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Fill)
	{
		P.Box(C + FVector2D(-0.3 * S, -0.62 * S), FVector2D(0.6 * S, 0.7 * S), FLinearColor::Transparent, 0.3f * S, Fill, 0.14f * S);
		P.Box(C + FVector2D(-0.5 * S, -0.2 * S), FVector2D(1.0 * S, 0.8 * S), Fill, 0.14f * S);
		P.Circle(C + FVector2D(0.0, 0.14 * S), 0.12f * S, Color::Paper);
	}

	void Spinner(const FKartPainter& P, const FVector2D& C, float R, double Now, const FLinearColor& Fill)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			const double A = Now * 6.0 - i * UE_DOUBLE_PI / 4.0;
			P.Circle(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, R * (0.28f - i * 0.025f), Color::WithAlpha(Fill, 1.f - i * 0.11f));
		}
	}

	/** A pill with Label. Returns its width. */
	float Chip(const FKartPainter& P, const FVector2D& Pos, const FText& Label, const FLinearColor& Fill, float Height = 36.f, float Align = 0.f)
	{
		const FSlateFontInfo F = Font(EFont::Label, Height * 0.5f);
		const float W = static_cast<float>(FKartPainter::Measure(Label.ToString(), F).X) + Height;
		const FVector2D At(Pos.X - W * Align, Pos.Y);
		P.Box(At, FVector2D(W, Height), Fill, Height * 0.5f);
		P.Text(Label, F, At + FVector2D(W, Height) * 0.5, Color::TextOn(Fill), FVector2D(0.5, 0.5));
		return W;
	}

	/** A big rounded action button; disabled ones are grey and don't take clicks. */
	void ActionButton(const FKartPainter& P, const FVector2D& Centre, const FVector2D& Size, const FText& Label, const FLinearColor& Fill, bool bEnabled,
		bool bFocused, double FocusAge, EKartMenuAction Action, EKartInputDevice Device, int32 HitId)
	{
		const FVector2D Pos = Centre - Size * 0.5;
		if (bEnabled)
		{
			P.Hit(HitId, Pos, Size);
		}
		const float Breath = bFocused && bEnabled ? 1.f + 0.025f * FMath::Sin(static_cast<float>(FocusAge) * 4.f) : 1.f;
		const FKartPainter B = P.Transformed(Centre, Breath);
		const float R = static_cast<float>(Size.Y) * 0.5f;
		const FLinearColor Body = bEnabled ? Fill : Color::PaperShade;
		B.Box(Pos + FVector2D(0.0, 7.0), Size, Color::Shadow, R);
		B.Gradient(Pos, Size, Body, Darker(Body, 0.18f), true, R);
		B.Box(Pos, Size, FLinearColor::Transparent, R, Color::White, bEnabled ? 4.f : 2.f);
		const FSlateFontInfo LabelFont = Font(EFont::Title, static_cast<float>(Size.Y) * 0.42f);
		const float GlyphH = static_cast<float>(Size.Y) * 0.5f;
		const float TextW = static_cast<float>(FKartPainter::Measure(Label.ToString(), LabelFont).X);
		const bool bGlyph = bEnabled && Action != EKartMenuAction::None;
		const float Total = TextW + (bGlyph ? KartWidgets::GlyphWidth(Action, Device, GlyphH) + 18.f : 0.f);
		float X = static_cast<float>(Centre.X) - Total * 0.5f;
		if (bGlyph)
		{
			X += KartWidgets::Glyph(B, Action, Device, FVector2D(X, Centre.Y), GlyphH) + 18.f;
		}
		B.Text(Label, LabelFont, FVector2D(X, Centre.Y), bEnabled ? Color::TextOn(Fill) : Color::NavyDim, FVector2D(0.0, 0.5), 0.f);
	}

	/** What the primary button does for an item right now. */
	struct FPrimary
	{
		FText Label;
		FLinearColor Fill = Color::Hot;
		bool bEnabled = true;
	};

	FPrimary PrimaryFor(const FKartStore* S, const FItem& Item)
	{
		FPrimary Out;
		if (!S)
		{
			Out.Label = LOCTEXT("StoreUnavailable", "Store unavailable");
			Out.bEnabled = false;
			return Out;
		}
		switch (S->GetState(Item.DefId))
		{
		case EItemState::Equipped:
			Out.Label = LOCTEXT("Unequip", "Take off");
			Out.Fill = Color::Paper;
			break;
		case EItemState::Owned:
			if (Item.Slot == ESlot::Bundle)
			{
				Out.Label = LOCTEXT("AllOwned", "All owned");
				Out.bEnabled = false;
			}
			else
			{
				Out.Label = LOCTEXT("Equip", "Equip");
				Out.Fill = Color::Mint;
			}
			break;
		case EItemState::ForSale:
			Out.Label = FText::Format(LOCTEXT("BuyFor", "Buy  {0}"), S->GetPriceText(Item.DefId));
			Out.Fill = Color::Sun;
			Out.bEnabled = S->CanPurchase(Item.DefId);
			if (!Out.bEnabled)
			{
				Out.Label = S->IsPurchasing() ? LOCTEXT("OtherPending", "Checkout open") : !S->IsOnline() ? LOCTEXT("Offline", "Store offline") : S->GetPriceText(Item.DefId);
			}
			break;
		case EItemState::Pending:
			Out.Label = LOCTEXT("Pending", "Waiting for Steam...");
			Out.bEnabled = false;
			break;
		case EItemState::Locked:
			Out.Label = LOCTEXT("Earning", "Keep playing to unlock");
			Out.bEnabled = false;
			break;
		case EItemState::NotDropped:
			Out.Label = LOCTEXT("NotDropped", "Drops while you play");
			Out.bEnabled = false;
			break;
		default:
			Out.Label = LOCTEXT("ComingSoon", "Coming soon");
			Out.bEnabled = false;
			break;
		}
		return Out;
	}

	// ----------------------------------------------------------------------------------------- Purchase dialog

	enum class EDialogPhase : uint8 { Confirm, Waiting, Success, Cancelled, Failed };

	/**
	 * Buy one item: confirm (the price, what it is, that Steam takes the payment), then wait for the overlay, then
	 * the result. The store finishes the purchase whether or not this dialog is still open.
	 */
	class FPurchaseDialog final : public FKartScreen
	{
	public:
		FPurchaseDialog(int32 InDefId, EDialogPhase InPhase) : DefId(InDefId), Phase(InPhase)
		{
			Cursor.Count = 2;
			Cursor.Index = 0;
		}

		virtual ~FPurchaseDialog() override
		{
			if (FKartStore* S = Store(); S && S == BoundStore)
			{
				S->OnPurchaseFinished.Remove(Handle);
			}
		}

		virtual FName GetId() const override
		{
			return Phase == EDialogPhase::Confirm ? FName(TEXT("store-buy")) : Phase == EDialogPhase::Success ? FName(TEXT("store-done")) : FName(TEXT("store-purchase"));
		}
		virtual bool IsModal() const override { return true; }
		virtual TArray<FKartHint> GetHints() const override { return {}; }

		virtual void OnShown() override
		{
			FocusAt = PhaseAt = NowSeconds();
			if (FKartStore* S = Store(); S && !BoundStore)
			{
				BoundStore = S;
				Handle = S->OnPurchaseFinished.AddSP(this, &FPurchaseDialog::OnFinished);
			}
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				if (NumButtons() > 1 && Cursor.Move(1))
				{
					FocusAt = NowSeconds();
					Host->PlaySound(EKartUISound::Move);
				}
				return true;
			case EKartMenuAction::Accept:
				Choose(Cursor.Index);
				return true;
			case EKartMenuAction::Back:
				Choose(NumButtons() > 1 ? 1 : 0);
				return true;
			default:
				return true; // modal: nothing reaches the store underneath
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (HitId < NumButtons() && Cursor.Set(HitId))
			{
				FocusAt = NowSeconds();
				Host->PlaySound(EKartUISound::Move);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId < NumButtons())
			{
				Choose(HitId);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const FItem* Item = FindItem(DefId);
			if (!Item)
			{
				return;
			}
			const FKartStore* S = Store();
			const float In = Ease::OutBack(static_cast<float>(Frame.Age / 0.3), 1.8f);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.66f * FMath::Min(1.f, static_cast<float>(Frame.Age / 0.2))));
			const FVector2D Size(1000.0, 600.0);
			const FVector2D Pos = Frame.Centre() - Size * 0.5;
			const FKartPainter D = P.Transformed(Frame.Centre(), 0.7f + 0.3f * In);
			const FLinearColor Accent = Phase == EDialogPhase::Success ? Color::Mint : Phase == EDialogPhase::Failed ? Color::Hot : Phase == EDialogPhase::Cancelled ? Color::NavyDim : Color::Sun;
			D.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 12.f, Color::White, 5.f);
			D.Box(Pos, FVector2D(Size.X, 16.0), Accent, 8.f);

			// The item on the left, the words on the right.
			const FVector2D Art(Pos.X + 40.0, Pos.Y + 56.0);
			const FVector2D ArtSize(340.0, 340.0);
			D.Gradient(Art, ArtSize, Lighter(Item->Visual.Primary, 0.55f), Lighter(Item->Visual.Primary, 0.2f), true, Metric::Radius);
			const float Pop = Phase == EDialogPhase::Success ? 1.f + 0.12f * Ease::Pulse(static_cast<float>((Frame.Now - PhaseAt) / 0.5)) : 1.f;
			PaintPreview(D.Transformed(Art + ArtSize * 0.5, Pop), *Item, Art + ArtSize * 0.5, 130.f, Frame.Now);
			if (Phase == EDialogPhase::Success)
			{
				Confetti(D, Art + ArtSize * 0.5, Frame.Now - PhaseAt);
			}
			const double TX = Art.X + ArtSize.X + 40.0;
			const double TW = Pos.X + Size.X - 40.0 - TX;
			double Y = Pos.Y + 56.0;
			Chip(D, FVector2D(TX, Y), SlotName(Item->Slot), Accent, 34.f);
			Y += 52.0;
			D.Text(Title(*Item), Font(EFont::Display, 46.f), FVector2D(TX, Y), Color::Navy);
			Y += 64.0;
			if (Phase == EDialogPhase::Confirm)
			{
				D.Text(S ? S->GetPriceText(DefId) : FText::GetEmpty(), Font(EFont::Display, 52.f, 2.f), FVector2D(TX, Y), Color::Hot);
				Y += 70.0;
			}
			else if (Phase == EDialogPhase::Waiting)
			{
				Spinner(D, FVector2D(TX + 26.0, Y + 26.0), 22.f, Frame.Now, Color::Sky);
				Y += 64.0;
			}
			for (const FText& Line : Lines(S, *Item))
			{
				if (!Line.IsEmpty())
				{
					Y += D.TextWrapped(Line, Font(EFont::Body, 21.f), FVector2D(TX, Y), TW, Color::NavyDim).Y + 10.0;
				}
			}

			// Buttons along the bottom.
			const TArray<FText> Labels = ButtonLabels(S);
			const FVector2D ButtonSize(Labels.Num() > 1 ? 420.0 : 520.0, 84.0);
			for (int32 i = 0; i < Labels.Num(); ++i)
			{
				const double CX = Labels.Num() > 1 ? Pos.X + Size.X * (i == 0 ? 0.27 : 0.73) : Pos.X + Size.X * 0.5;
				const FVector2D C(CX, Pos.Y + Size.Y - 82.0);
				const bool bFocus = Cursor.Index == i;
				const FLinearColor Fill = i == 0 ? (Phase == EDialogPhase::Confirm ? Color::Sun : Phase == EDialogPhase::Success ? Color::Mint : Color::Sky) : Color::Paper;
				const FKartPainter BP = bFocus ? D : D.Faded(0.92f);
				ActionButton(BP, C, ButtonSize, Labels[i], Fill, true, bFocus, Frame.Now - FocusAt, bFocus ? EKartMenuAction::Accept : (i == 1 ? EKartMenuAction::Back : EKartMenuAction::None), Frame.Device, -1);
				if (bFocus)
				{
					D.Box(C - ButtonSize * 0.5 - FVector2D(6.0, 6.0), ButtonSize + FVector2D(12.0, 12.0), FLinearColor::Transparent, 48.f, Color::Navy, 4.f);
				}
				// Settled hit regions (the transformed painter records none).
				P.Hit(i, C - ButtonSize * 0.5, ButtonSize);
			}
		}

	private:
		int32 NumButtons() const { return Phase == EDialogPhase::Confirm || Phase == EDialogPhase::Failed || (Phase == EDialogPhase::Success && CanEquip()) ? 2 : 1; }

		bool CanEquip() const
		{
			const FItem* Item = FindItem(DefId);
			return Item && Item->IsEquippable();
		}

		FText Title(const FItem& Item) const
		{
			switch (Phase)
			{
			case EDialogPhase::Confirm: return FText::Format(LOCTEXT("BuyTitle", "Buy {0}?"), Item.Name);
			case EDialogPhase::Waiting: return LOCTEXT("WaitTitle", "Finish in Steam");
			case EDialogPhase::Success: return LOCTEXT("DoneTitle", "It's yours!");
			case EDialogPhase::Cancelled: return LOCTEXT("CancelTitle", "Purchase cancelled");
			default: return LOCTEXT("FailTitle", "That didn't go through");
			}
		}

		TArray<FText> Lines(const FKartStore* S, const FItem& Item) const
		{
			const bool bMock = S && S->GetBackend().GetName() == TEXT("mock");
			switch (Phase)
			{
			case EDialogPhase::Confirm:
			{
				TArray<FText> Out = { LOCTEXT("CosmeticOnly", "Cosmetic only: it changes how things look, never how they play.") };
				if (Item.Slot == ESlot::Bundle && S)
				{
					const int32 Have = S->NumOwnedContents(Item.DefId);
					Out.Add(Have > 0 ? FText::Format(LOCTEXT("BundleHave", "You already own {0} of the {1} items. The bundle price stays the same."), Have, Item.Contents.Num())
						: FText::Format(LOCTEXT("BundleAll", "{0} items, all at once."), Item.Contents.Num()));
				}
				Out.Add(bMock ? LOCTEXT("MockPay", "Demo store: no real money. In the Steam version, you pay in the Steam overlay.")
					: LOCTEXT("SteamPay", "You'll confirm and pay in the Steam overlay. Steam handles payment, receipts and refunds."));
				return Out;
			}
			case EDialogPhase::Waiting:
				return { FText::Format(LOCTEXT("WaitBody", "The Steam overlay has your checkout for {0}. Approve it there, or close it to cancel."), Item.Name),
					LOCTEXT("WaitNote", "You can close this; the item arrives as soon as Steam confirms.") };
			case EDialogPhase::Success:
				return { FText::Format(LOCTEXT("DoneBody", "{0} is in your collection."), Item.Name),
					CanEquip() ? LOCTEXT("DoneEquip", "Wear it now, or later from the store.") : LOCTEXT("DoneBundle", "Everything inside is ready to equip in its tab.") };
			case EDialogPhase::Cancelled:
				return { LOCTEXT("CancelBody", "No charge was made. You can buy it any time.") };
			default:
				return { LOCTEXT("FailBody", "You weren't charged. Check that Steam is online and the overlay is on, then try again."),
					Detail.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("FailDetail", "Steam said: {0}"), FText::FromString(Detail)) };
			}
		}

		TArray<FText> ButtonLabels(const FKartStore* S) const
		{
			switch (Phase)
			{
			case EDialogPhase::Confirm:
				return { FText::Format(LOCTEXT("BuyButton", "Buy  {0}"), S ? S->GetPriceText(DefId) : FText::GetEmpty()), LOCTEXT("NotNow", "Not now") };
			case EDialogPhase::Waiting:
				return { LOCTEXT("Hide", "Close") };
			case EDialogPhase::Success:
				return CanEquip() ? TArray<FText>{ LOCTEXT("EquipNow", "Equip now"), LOCTEXT("Later", "Later") } : TArray<FText>{ LOCTEXT("Great", "Great!") };
			case EDialogPhase::Cancelled:
				return { LOCTEXT("Ok", "OK") };
			default:
				return { LOCTEXT("TryAgain", "Try again"), LOCTEXT("Close", "Close") };
			}
		}

		void SetPhase(EDialogPhase NewPhase)
		{
			Phase = NewPhase;
			PhaseAt = FocusAt = NowSeconds();
			Cursor.Count = NumButtons();
			Cursor.Index = 0;
		}

		void Choose(int32 Button)
		{
			FKartStore* S = Store();
			switch (Phase)
			{
			case EDialogPhase::Confirm:
				if (Button == 0 && S && S->Purchase(DefId))
				{
					Host->PlaySound(EKartUISound::Accept);
					SetPhase(EDialogPhase::Waiting);
				}
				else if (Button == 0)
				{
					Host->PlaySound(EKartUISound::Error);
				}
				else
				{
					Host->PlaySound(EKartUISound::Back);
					Host->PopScreen();
				}
				break;
			case EDialogPhase::Failed:
				if (Button == 0)
				{
					Host->PlaySound(EKartUISound::Accept);
					SetPhase(EDialogPhase::Confirm);
				}
				else
				{
					Host->PlaySound(EKartUISound::Back);
					Host->PopScreen();
				}
				break;
			case EDialogPhase::Success:
				if (Button == 0 && CanEquip() && S)
				{
					S->Equip(DefId);
					Host->PlaySound(EKartUISound::Go);
				}
				else
				{
					Host->PlaySound(EKartUISound::Back);
				}
				Host->PopScreen();
				break;
			default:
				Host->PlaySound(EKartUISound::Back);
				Host->PopScreen();
				break;
			}
		}

		void OnFinished(int32 Def, EPurchaseResult Result, const FString& InDetail)
		{
			if (Def != DefId || Phase != EDialogPhase::Waiting)
			{
				return;
			}
			Detail = InDetail;
			SetPhase(Result == EPurchaseResult::Success || Result == EPurchaseResult::AlreadyOwned ? EDialogPhase::Success
				: Result == EPurchaseResult::Cancelled ? EDialogPhase::Cancelled : EDialogPhase::Failed);
			if (Host)
			{
				Host->PlaySound(Phase == EDialogPhase::Success ? EKartUISound::Go : EKartUISound::Error);
			}
		}

		static void Confetti(const FKartPainter& P, const FVector2D& C, double Age)
		{
			if (Age > 1.6)
			{
				return;
			}
			static const FLinearColor Colors[] = { Color::Hot, Color::Sun, Color::Sky, Color::Mint, Color::Grape };
			const float T = static_cast<float>(Age / 1.6);
			for (int32 i = 0; i < 28; ++i)
			{
				const double A = i * 2.399; // golden angle
				const double Speed = 160.0 + (i % 5) * 40.0;
				const FVector2D At = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Speed * Ease::OutCubic(T) + FVector2D(0.0, 120.0 * T * T);
				P.Faded(1.f - T).Box(At, FVector2D(12.0, 7.0), Colors[i % UE_ARRAY_COUNT(Colors)], 2.f);
			}
		}

		int32 DefId = 0;
		EDialogPhase Phase = EDialogPhase::Confirm;
		FKartMenuCursor Cursor;
		double FocusAt = 0.0;
		double PhaseAt = 0.0;
		FString Detail;
		FKartStore* BoundStore = nullptr;
		FDelegateHandle Handle;
	};

	// ---------------------------------------------------------------------------------------------- The store

	class FStoreScreen final : public FKartScreen
	{
	public:
		static constexpr int32 Columns = 5;
		static constexpr int32 VisibleRows = 3;
		static constexpr int32 TabHit = 1000;
		static constexpr int32 PrimaryHit = 2000;
		static constexpr int32 RestoreHit = 2001;

		FStoreScreen(AKartFrontEnd* InFrontEnd, FName InTourStep) : FrontEnd(InFrontEnd), TourStep(InTourStep)
		{
			if (TourStep == TEXT("storeparty"))
			{
				Tab = ETab::Party;
			}
			else if (TourStep == TEXT("storefree"))
			{
				Tab = ETab::Free;
			}
			Items = ItemsFor(Tab);
		}

		virtual ~FStoreScreen() override { Unbind(); }

		virtual FName GetId() const override { return TourStep.IsNone() || TourStep == TEXT("store") ? FName(TEXT("store")) : TourStep; }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::PrevTab, FText::GetEmpty() }, { EKartMenuAction::NextTab, LOCTEXT("HintTabs", "Tabs") },
				{ EKartMenuAction::Alt, LOCTEXT("HintRestore", "Restore purchases") }, { EKartMenuAction::Accept, LOCTEXT("HintSelect", "Select") },
				{ EKartMenuAction::Back, LOCTEXT("HintBack", "Back") } };
		}

		virtual void OnShown() override
		{
			FocusAt = NowSeconds();
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
			Bind();
			if (!bTourOpened && (TourStep == TEXT("storebuy") || TourStep == TEXT("storedone")))
			{
				bTourOpened = true;
				const FItem* Item = FindItem(FName(TEXT("paint.chrome")));
				if (Item)
				{
					Focus = Items.IndexOfByKey(Item);
					Host->PushScreen(MakeShared<FPurchaseDialog>(Item->DefId, TourStep == TEXT("storebuy") ? EDialogPhase::Confirm : EDialogPhase::Success));
				}
			}
			if (TourStep == TEXT("storeparty"))
			{
				const int32 Katana = Items.IndexOfByPredicate([](const FItem* I) { return I->Key == TEXT("katana.neon"); });
				Focus = Katana == INDEX_NONE ? 0 : Katana;
			}
			else if (TourStep == TEXT("storefree"))
			{
				const int32 Ball = Items.IndexOfByPredicate([](const FItem* I) { return I->Key == TEXT("ball.alleycat"); });
				Focus = Ball == INDEX_NONE ? 0 : Ball;
			}
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::Left: MoveFocus(-1, 0); return true;
			case EKartMenuAction::Right: MoveFocus(1, 0); return true;
			case EKartMenuAction::Up: MoveFocus(0, -1); return true;
			case EKartMenuAction::Down: MoveFocus(0, 1); return true;
			case EKartMenuAction::PrevTab:
			case EKartMenuAction::NextTab:
				SetTab(static_cast<ETab>(FKartMenuCursor::Cycle(static_cast<int32>(Tab), Event.Action == EKartMenuAction::PrevTab ? -1 : 1, static_cast<int32>(ETab::Num))));
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Activate();
				return true;
			case EKartMenuAction::Alt:
				Restore();
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (HitId >= 0 && HitId < Items.Num() && HitId != Focus)
			{
				Focus = HitId;
				FocusAt = NowSeconds();
				Host->PlaySound(EKartUISound::Move);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId >= TabHit && HitId < TabHit + static_cast<int32>(ETab::Num))
			{
				SetTab(static_cast<ETab>(HitId - TabHit));
			}
			else if (HitId == PrimaryHit)
			{
				Activate();
			}
			else if (HitId == RestoreHit)
			{
				Restore();
			}
			else if (HitId >= 0 && HitId < Items.Num())
			{
				// A click picks the tile; a second click on it acts, like Accept.
				if (HitId == Focus)
				{
					Activate();
				}
				else
				{
					Focus = HitId;
					FocusAt = NowSeconds();
					Host->PlaySound(EKartUISound::Move);
				}
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const FKartStore* S = Store();
			// A dark wash over the orbit so the grid reads; the detail side a touch darker.
			P.Gradient(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.72f), Color::WithAlpha(Color::Night, 0.86f), false);
			KartWidgets::Header(P, Frame, LOCTEXT("StoreTitle", "Store"),
				LOCTEXT("StoreSub", "Cosmetics only. Every kart, ball and blade plays exactly the same."), Color::Hot);
			PaintStatus(P, Frame, S);
			PaintTabs(P, Frame);

			const double Top = Frame.SafeMin.Y + 248.0;
			PaintGrid(P, Frame, S, FVector2D(Frame.SafeMin.X + KartWidgets::SlideX(Frame), Top));
			if (Items.IsValidIndex(Focus))
			{
				PaintDetail(P, Frame, S, *Items[Focus], FVector2D(Frame.SafeMax.X - DetailW, Top));
			}
			PaintToast(P, Frame);
		}

	private:
		static constexpr double TileW = 206.0;
		static constexpr double TileH = 200.0;
		static constexpr double Gap = 14.0;
		static constexpr double DetailW = 600.0;

		void Bind()
		{
			FKartStore* S = Store();
			if (!S || BoundStore == S)
			{
				return;
			}
			Unbind();
			BoundStore = S;
			UnlockHandle = S->OnUnlocked.AddSP(this, &FStoreScreen::OnUnlocked);
			RestoreHandle = S->OnRestored.AddSP(this, &FStoreScreen::OnRestored);
		}

		void Unbind()
		{
			if (FKartStore* S = Store(); S && S == BoundStore)
			{
				S->OnUnlocked.Remove(UnlockHandle);
				S->OnRestored.Remove(RestoreHandle);
			}
			BoundStore = nullptr;
		}

		void OnUnlocked(int32 DefId)
		{
			if (const FItem* Item = FindItem(DefId))
			{
				Toast(FText::Format(LOCTEXT("UnlockedToast", "Unlocked: {0}"), Item->Name), Color::Mint);
			}
		}

		void OnRestored(bool bOk)
		{
			if (bRestoreRequested)
			{
				bRestoreRequested = false;
				Toast(bOk ? LOCTEXT("Restored", "Purchases restored from Steam") : LOCTEXT("RestoreFailed", "Couldn't reach Steam. Showing what we had."), bOk ? Color::Sky : Color::Hot);
			}
		}

		void Toast(const FText& Text, const FLinearColor& Accent)
		{
			ToastText = Text;
			ToastAccent = Accent;
			ToastAt = NowSeconds();
		}

		void SetTab(ETab NewTab)
		{
			if (NewTab == Tab)
			{
				return;
			}
			Tab = NewTab;
			Items = ItemsFor(Tab);
			Focus = 0;
			FirstRow = 0;
			TabAt = FocusAt = NowSeconds();
			Host->PlaySound(EKartUISound::Tick);
		}

		void MoveFocus(int32 DX, int32 DY)
		{
			if (Items.Num() == 0)
			{
				return;
			}
			const int32 Col = Focus % Columns, Row = Focus / Columns;
			int32 Next = Focus;
			if (DX != 0)
			{
				const int32 NewCol = Col + DX;
				if (NewCol >= 0 && NewCol < Columns && Row * Columns + NewCol < Items.Num())
				{
					Next = Row * Columns + NewCol;
				}
			}
			else
			{
				const int32 NewRow = Row + DY;
				const int32 Rows = (Items.Num() + Columns - 1) / Columns;
				if (NewRow >= 0 && NewRow < Rows)
				{
					Next = FMath::Min(NewRow * Columns + Col, Items.Num() - 1);
				}
			}
			if (Next != Focus)
			{
				Focus = Next;
				FocusAt = NowSeconds();
				Host->PlaySound(EKartUISound::Move);
			}
			else
			{
				Host->PlaySound(EKartUISound::Error);
			}
		}

		void Activate()
		{
			FKartStore* S = Store();
			if (!S || !Items.IsValidIndex(Focus))
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			const FItem& Item = *Items[Focus];
			switch (S->GetState(Item.DefId))
			{
			case EItemState::Owned:
				if (Item.IsEquippable() && S->Equip(Item.DefId))
				{
					Host->PlaySound(EKartUISound::Go);
					Toast(FText::Format(LOCTEXT("EquippedToast", "Equipped: {0}"), Item.Name), Color::Mint);
					return;
				}
				break;
			case EItemState::Equipped:
				S->Unequip(Item.Slot);
				Host->PlaySound(EKartUISound::Back);
				return;
			case EItemState::ForSale:
				if (S->CanPurchase(Item.DefId))
				{
					Host->PlaySound(EKartUISound::Accept);
					Host->PushScreen(MakeShared<FPurchaseDialog>(Item.DefId, EDialogPhase::Confirm));
					return;
				}
				break;
			default:
				break;
			}
			Host->PlaySound(EKartUISound::Error);
		}

		void Restore()
		{
			if (FKartStore* S = Store(); S && !S->IsRestoring())
			{
				bRestoreRequested = true;
				S->Restore();
				Host->PlaySound(EKartUISound::Accept);
				Toast(LOCTEXT("Restoring", "Checking Steam for your items..."), Color::Sky);
			}
		}

		// ------------------------------------------------------------------------------------------ Painting

		void PaintStatus(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* S)
		{
			const FKartPainter F = P.Faded(KartWidgets::Entrance(Frame, 0.1f));
			FText Status;
			FLinearColor Dot = Color::Mint;
			if (!S)
			{
				Status = LOCTEXT("StatusNone", "Store unavailable");
				Dot = Color::Hot;
			}
			else if (S->GetBackend().GetName() == TEXT("mock"))
			{
				Status = S->IsOnline() ? LOCTEXT("StatusMock", "Demo store (offline test mode)") : LOCTEXT("StatusOffline", "Offline: showing items you own");
				Dot = S->IsOnline() ? Color::Sun : Color::Hot;
			}
			else
			{
				Status = S->IsOnline() ? LOCTEXT("StatusSteam", "Steam Inventory") : LOCTEXT("StatusSteamOff", "Steam offline: showing items you own");
				Dot = S->IsOnline() ? Color::Mint : Color::Hot;
			}
			const FVector2D At(Frame.SafeMax.X, Frame.SafeMin.Y + 20.0);
			const FSlateFontInfo StatusFont = Font(EFont::Label, 20.f);
			const float W = static_cast<float>(FKartPainter::Measure(Status.ToString(), StatusFont).X) + 64.f;
			F.Box(At - FVector2D(W, 0.0), FVector2D(W, 44.0), Color::WithAlpha(Color::Night, 0.75f), 22.f, Color::WithAlpha(Color::White, 0.3f), 2.f);
			F.Circle(At + FVector2D(-W + 24.0, 22.0), 8.f, Dot);
			F.Text(Status, StatusFont, At + FVector2D(-W + 42.0, 22.0), Color::White, FVector2D(0.0, 0.5));

			// Restore purchases: a clickable chip under the status.
			const FText RestoreLabel = S && S->IsRestoring() ? LOCTEXT("RestoringChip", "Restoring...") : LOCTEXT("RestoreChip", "Restore purchases");
			const FSlateFontInfo RF = Font(EFont::Button, 22.f);
			const float GH = 30.f;
			const float RW = static_cast<float>(FKartPainter::Measure(RestoreLabel.ToString(), RF).X) + KartWidgets::GlyphWidth(EKartMenuAction::Alt, Frame.Device, GH) + 60.f;
			const FVector2D RPos(Frame.SafeMax.X - RW, Frame.SafeMin.Y + 76.0);
			F.Hit(RestoreHit, RPos, FVector2D(RW, 50.0));
			F.Box(RPos, FVector2D(RW, 50.0), Color::WithAlpha(Color::Paper, 0.92f), 25.f);
			float X = static_cast<float>(RPos.X) + 18.f;
			X += KartWidgets::Glyph(F, EKartMenuAction::Alt, Frame.Device, FVector2D(X, RPos.Y + 25.0), GH) + 12.f;
			F.Text(RestoreLabel, RF, FVector2D(X, RPos.Y + 25.0), Color::Navy, FVector2D(0.0, 0.5));
		}

		void PaintTabs(const FKartPainter& P, const FKartScreenFrame& Frame)
		{
			const FSlateFontInfo TabFont = Font(EFont::Button, 26.f);
			const double H = 58.0, TabGap = 12.0;
			double X = Frame.SafeMin.X + 56.0;
			const double Y = Frame.SafeMin.Y + 166.0;
			const float In = KartWidgets::Entrance(Frame, 0.05f);
			const float GH = 36.f;
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::PrevTab, Frame.Device, FVector2D(Frame.SafeMin.X, Y + H * 0.5), GH);
			for (int32 i = 0; i < static_cast<int32>(ETab::Num); ++i)
			{
				const ETab T = static_cast<ETab>(i);
				const bool bOn = T == Tab;
				const FText Name = TabName(T);
				const double W = FKartPainter::Measure(Name.ToString(), TabFont).X + 56.0;
				const FVector2D Pos(X, Y + (1.0 - In) * -40.0);
				P.Hit(TabHit + i, Pos, FVector2D(W, H));
				const float Pop = bOn ? 1.f + 0.06f * Ease::OutBack(static_cast<float>((Frame.Now - TabAt) / 0.25), 3.f) : 1.f;
				const FKartPainter TP = P.Transformed(Pos + FVector2D(W, H) * 0.5, Pop).Faded(In);
				TP.Card(Pos, FVector2D(W, H), bOn ? TabAccent(T) : Color::WithAlpha(Color::Paper, 0.9f), static_cast<float>(H * 0.5), 6.f, bOn ? Color::White : FLinearColor::Transparent, 4.f);
				TP.Text(Name, TabFont, Pos + FVector2D(W, H) * 0.5, bOn ? Color::TextOn(TabAccent(T)) : Color::Navy, FVector2D(0.5, 0.5));
				X += W + TabGap;
			}
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::NextTab, Frame.Device, FVector2D(X + 8.0, Y + H * 0.5), GH);
		}

		void PaintGrid(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* S, const FVector2D& Origin)
		{
			const int32 FocusRow = Focus / Columns;
			if (FocusRow < FirstRow)
			{
				FirstRow = FocusRow;
			}
			else if (FocusRow >= FirstRow + VisibleRows)
			{
				FirstRow = FocusRow - VisibleRows + 1;
			}
			const int32 Rows = (Items.Num() + Columns - 1) / Columns;
			const FVector2D Area(Columns * TileW + (Columns - 1) * Gap, VisibleRows * TileH + (VisibleRows - 1) * Gap);
			const float TabIn = Ease::OutCubic(static_cast<float>((Frame.Now - TabAt) / 0.25));
			P.PushClip(Origin - FVector2D(12.0, 12.0), Area + FVector2D(24.0, 24.0));
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				const int32 Row = i / Columns - FirstRow;
				if (Row < 0 || Row >= VisibleRows)
				{
					continue;
				}
				const int32 Col = i % Columns;
				const float In = FMath::Min(KartWidgets::Entrance(Frame, 0.08f + (Row * Columns + Col) * 0.025f), TabIn);
				const FVector2D Pos = Origin + FVector2D(Col * (TileW + Gap), Row * (TileH + Gap) + (1.0 - In) * 40.0);
				PaintTile(P.Faded(In), Frame, S, *Items[i], Pos, i, i == Focus);
			}
			P.PopClip();
			// Scroll hints when more rows hide above or below.
			if (FirstRow > 0)
			{
				KartWidgets::Arrow(P.Transformed(Origin + FVector2D(Area.X * 0.5, -18.0), 1.f, -90.f), Origin + FVector2D(Area.X * 0.5, -18.0), 14.f, true, Color::Sun);
			}
			if (FirstRow + VisibleRows < Rows)
			{
				KartWidgets::Arrow(P.Transformed(Origin + FVector2D(Area.X * 0.5, Area.Y + 22.0), 1.f, 90.f), Origin + FVector2D(Area.X * 0.5, Area.Y + 22.0), 14.f, true, Color::Sun);
			}
		}

		void PaintTile(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* S, const FItem& Item, const FVector2D& Pos, int32 Index, bool bFocused)
		{
			const FVector2D Size(TileW, TileH);
			P.Hit(Index, Pos, Size);
			const EItemState State = S ? S->GetState(Item.DefId) : EItemState::ComingSoon;
			const float Pop = bFocused ? 1.f + 0.05f * Ease::OutBack(static_cast<float>((Frame.Now - FocusAt) / 0.3), 3.f) : 1.f;
			const FKartPainter T = P.Transformed(Pos + Size * 0.5, Pop);
			const bool bDim = State == EItemState::ComingSoon;
			const FLinearColor Edge = bFocused ? Color::Sun : State == EItemState::Equipped ? Color::Mint : FLinearColor::Transparent;
			T.Card(Pos, Size, bDim ? Color::PaperShade : Color::Paper, Metric::Radius, bFocused ? 10.f : 6.f, Edge, bFocused ? 6.f : 4.f);
			const FVector2D Art(Pos.X + 10.0, Pos.Y + 10.0);
			const FVector2D ArtSize(Size.X - 20.0, 116.0);
			T.Gradient(Art, ArtSize, Lighter(Item.Visual.Primary, 0.6f), Lighter(Item.Visual.Primary, 0.3f), true, Metric::Radius - 6.f);
			const FKartPainter ArtP = bDim || State == EItemState::Locked || State == EItemState::NotDropped ? T.Faded(0.55f) : T;
			PaintPreview(ArtP, Item, Art + ArtSize * 0.5, 48.f, bFocused ? Frame.Now : 0.0);
			if (State == EItemState::Locked || State == EItemState::NotDropped)
			{
				Lock(T, Art + FVector2D(ArtSize.X - 24.0, 30.0), 22.f, Color::Navy);
			}
			// Name and its state line.
			// The name shrinks to fit the tile rather than running off it.
			float NameSize = 21.f;
			while (NameSize > 15.f && FKartPainter::Measure(Item.Name.ToString(), Font(EFont::Button, NameSize)).X > Size.X - 26.0)
			{
				NameSize -= 1.f;
			}
			T.Text(Item.Name, Font(EFont::Button, NameSize), FVector2D(Pos.X + 14.0, Pos.Y + 134.0), Color::Navy);
			const FVector2D ChipPos(Pos.X + 12.0, Pos.Y + Size.Y - 38.0);
			switch (State)
			{
			case EItemState::Equipped:
				Chip(T, ChipPos, LOCTEXT("ChipEquipped", "EQUIPPED"), Color::Mint, 30.f);
				break;
			case EItemState::Owned:
				Chip(T, ChipPos, LOCTEXT("ChipOwned", "OWNED"), Color::Sky, 30.f);
				break;
			case EItemState::Pending:
				Spinner(T, ChipPos + FVector2D(16.0, 15.0), 11.f, Frame.Now, Color::Sky);
				break;
			case EItemState::Locked:
			{
				int32 Cur = 0, Target = 1;
				S->GetProgress(Item.DefId, Cur, Target);
				const double BarW = Size.X - 90.0;
				T.Box(ChipPos + FVector2D(0.0, 10.0), FVector2D(BarW, 12.0), Color::PaperShade, 6.f);
				T.Box(ChipPos + FVector2D(0.0, 10.0), FVector2D(FMath::Max(12.0, BarW * Cur / FMath::Max(Target, 1)), 12.0), Color::Mint, 6.f);
				T.Text(FString::Printf(TEXT("%d/%d"), Cur, Target), Font(EFont::Label, 17.f), ChipPos + FVector2D(BarW + 8.0, 16.0), Color::NavyDim, FVector2D(0.0, 0.5));
				break;
			}
			case EItemState::NotDropped:
				Chip(T, ChipPos, LOCTEXT("ChipGift", "GIFT DROP"), Color::Grape, 30.f);
				break;
			case EItemState::ComingSoon:
				Chip(T, ChipPos, LOCTEXT("ChipSoon", "SOON"), Color::NavyDim, 30.f);
				break;
			default:
				Chip(T, ChipPos, S && S->HasPrice(Item.DefId) ? S->GetPriceText(Item.DefId) : LOCTEXT("ChipNoPrice", "NO PRICE"), S && S->HasPrice(Item.DefId) ? Color::Sun : Color::PaperShade, 30.f);
				break;
			}
		}

		void PaintDetail(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* S, const FItem& Item, const FVector2D& Pos)
		{
			const FVector2D Size(DetailW, 3 * TileH + 2 * Gap);
			const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - FocusAt) / 0.35), 2.f);
			const FKartPainter Card = P.Transformed(Pos + Size * 0.5, 0.95f + 0.05f * Pop).Faded(KartWidgets::Entrance(Frame, 0.2f));
			Card.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 10.f, Color::White, 5.f);
			const FVector2D Art(Pos.X + 18.0, Pos.Y + 18.0);
			const FVector2D ArtSize(Size.X - 36.0, 230.0);
			Card.Gradient(Art, ArtSize, Lighter(Item.Visual.Primary, 0.5f), FMath::Lerp(Item.Visual.Primary, Color::Night, 0.15f), true, Metric::Radius);
			for (int32 Stripe = 0; Stripe < 5; ++Stripe)
			{
				const double X = Art.X + FMath::Fmod(Frame.Now * 40.0 + Stripe * 110.0, ArtSize.X + 110.0) - 110.0;
				const double X0 = FMath::Clamp(X, Art.X, Art.X + ArtSize.X);
				const double X1 = FMath::Clamp(X + 46.0, Art.X, Art.X + ArtSize.X);
				if (X1 > X0 + 1.0)
				{
					Card.Box(FVector2D(X0, Art.Y), FVector2D(X1 - X0, ArtSize.Y), Color::WithAlpha(Color::White, 0.12f));
				}
			}
			PaintPreview(Card, Item, Art + ArtSize * 0.5, 96.f, Frame.Now);
			const EItemState State = S ? S->GetState(Item.DefId) : EItemState::ComingSoon;

			double Y = Art.Y + ArtSize.Y + 18.0;
			const float ChipW = Chip(Card, FVector2D(Pos.X + 30.0, Y), SlotName(Item.Slot), TabAccent(Tab), 32.f);
			Chip(Card, FVector2D(Pos.X + 42.0 + ChipW, Y), LOCTEXT("CosmeticChip", "COSMETIC ONLY"), Color::PaperShade, 32.f);
			Y += 46.0;
			Card.Text(Item.Name, Font(EFont::Display, 44.f), FVector2D(Pos.X + 30.0, Y), Color::Navy);
			Y += 60.0;
			Y += Card.TextWrapped(Item.Description, Font(EFont::Body, 21.f), FVector2D(Pos.X + 32.0, Y), Size.X - 64.0, Color::NavyDim).Y + 12.0;

			// The state: a price, progress, or how to get it.
			const FVector2D Line(Pos.X + 32.0, Y);
			if (State == EItemState::Locked && S)
			{
				int32 Cur = 0, Target = 1;
				S->GetProgress(Item.DefId, Cur, Target);
				const double BarW = Size.X - 64.0;
				Card.Box(Line + FVector2D(0.0, 6.0), FVector2D(BarW, 26.0), Color::PaperShade, 13.f);
				Card.Box(Line + FVector2D(0.0, 6.0), FVector2D(FMath::Max(26.0, BarW * Cur / FMath::Max(Target, 1)), 26.0), Color::Mint, 13.f);
				Card.Text(FText::Format(LOCTEXT("Progress", "{0} of {1}"), Cur, Target), Font(EFont::Label, 18.f), Line + FVector2D(BarW * 0.5, 19.0), Color::Navy, FVector2D(0.5, 0.5));
			}
			else if (State == EItemState::ForSale && S)
			{
				Card.Text(S->GetPriceText(Item.DefId), Font(EFont::Display, 40.f, 2.f), Line + FVector2D(0.0, -4.0), Color::Hot);
				if (Item.Slot == ESlot::Bundle && S->NumOwnedContents(Item.DefId) > 0)
				{
					Card.Text(FText::Format(LOCTEXT("BundlePart", "You own {0} of {1}"), S->NumOwnedContents(Item.DefId), Item.Contents.Num()),
						Font(EFont::Label, 18.f), Line + FVector2D(Size.X - 64.0, 20.0), Color::NavyDim, FVector2D(1.0, 0.5));
				}
			}
			else if (State == EItemState::Equipped)
			{
				Card.Text(LOCTEXT("WearingNow", "Wearing it now"), Font(EFont::Button, 24.f), Line, Color::Mint);
			}
			else if (State == EItemState::NotDropped)
			{
				Card.Text(LOCTEXT("DropNote", "Arcade gifts drop now and then while you play. Free."), Font(EFont::Body, 20.f), Line, Color::Grape);
			}

			const FPrimary Primary = PrimaryFor(S, Item);
			ActionButton(P.Faded(KartWidgets::Entrance(Frame, 0.3f)), FVector2D(Pos.X + Size.X * 0.5, Pos.Y + Size.Y - 58.0), FVector2D(Size.X - 80.0, 78.0),
				Primary.Label, Primary.Fill, Primary.bEnabled, true, Frame.Now - FocusAt, EKartMenuAction::Accept, Frame.Device, PrimaryHit);
		}

		void PaintToast(const FKartPainter& P, const FKartScreenFrame& Frame)
		{
			const double Age = Frame.Now - ToastAt;
			if (ToastText.IsEmpty() || Age > 3.0)
			{
				return;
			}
			const float In = Ease::OutBack(static_cast<float>(Age / 0.3), 2.f) * (1.f - Ease::InCubic(static_cast<float>((Age - 2.6) / 0.4)));
			const FSlateFontInfo F = Font(EFont::Button, 28.f);
			const double W = FKartPainter::Measure(ToastText.ToString(), F).X + 80.0;
			const FVector2D C(Frame.Size.X * 0.5, Frame.SafeMax.Y - 110.0);
			const FKartPainter T = P.Transformed(C, FMath::Max(In, 0.01f)).Faded(FMath::Clamp(In, 0.f, 1.f));
			T.Card(C - FVector2D(W * 0.5, 32.0), FVector2D(W, 64.0), Color::Paper, 32.f, 8.f, ToastAccent, 5.f);
			T.Text(ToastText, F, C, Color::Navy, FVector2D(0.5, 0.5));
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FName TourStep;
		bool bTourOpened = false;
		ETab Tab = ETab::Karts;
		TArray<const FItem*> Items;
		int32 Focus = 0;
		int32 FirstRow = 0;
		double FocusAt = 0.0;
		double TabAt = -10.0;
		FText ToastText;
		FLinearColor ToastAccent = Color::Mint;
		double ToastAt = -10.0;
		bool bRestoreRequested = false;
		FKartStore* BoundStore = nullptr;
		FDelegateHandle UnlockHandle;
		FDelegateHandle RestoreHandle;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeStore(AKartFrontEnd* FrontEnd, FName TourStep)
	{
		return MakeShared<FStoreScreen>(FrontEnd, TourStep);
	}

	void PaintStoreIcon(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		// A shopping bag with a star tag.
		P.Box(C + FVector2D(-0.3 * S, -0.82 * S), FVector2D(0.6 * S, 0.6 * S), FLinearColor::Transparent, 0.3f * S, Color::Navy, 0.1f * S);
		P.Polygon({ C + FVector2D(-0.66 * S, -0.42 * S), C + FVector2D(0.66 * S, -0.42 * S), C + FVector2D(0.78 * S, 0.74 * S), C + FVector2D(-0.78 * S, 0.74 * S) }, Color::Navy);
		P.Polygon({ C + FVector2D(-0.56 * S, -0.32 * S), C + FVector2D(0.56 * S, -0.32 * S), C + FVector2D(0.66 * S, 0.64 * S), C + FVector2D(-0.66 * S, 0.64 * S) }, Color::Paper);
		KartWidgets::Star(P, C + FVector2D(0.0, 0.16 * S), 0.34f * S, 0.15f * S, Color::Hot, static_cast<float>(FMath::Sin(Now * 2.0) * 10.0));
	}
}

#undef LOCTEXT_NAMESPACE

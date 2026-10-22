#include "UI/Screens/Store/KartStoreArt.h"

#include "Store/KartStoreCatalog.h"
#include "UI/Screens/KartScreens.h"

using namespace KartUI;
using namespace MovecadeKart::Store;

namespace
{
	using KartStoreUI::Darker;
	using KartStoreUI::Lighter;

	// Each preview is drawn about a centre C at size S (it spans about 2 S), in the item's own colours.

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

	void PaintItem(const FKartPainter& P, const FItem& Item, const FVector2D& C, float S, double Now, int32 Depth)
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
						PaintItem(Card, *Part, At, S * 0.3f, Now, 1);
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
}

namespace KartStoreUI
{
	FLinearColor Lighter(const FLinearColor& C, float T) { return FMath::Lerp(C, FLinearColor::White, T); }
	FLinearColor Darker(const FLinearColor& C, float T) { return FMath::Lerp(C, Color::Night, T); }

	void PaintPreview(const FKartPainter& P, const FItem& Item, const FVector2D& Centre, float Size, double Now)
	{
		PaintItem(P, Item, Centre, Size, Now, 0);
	}

	void PaintLock(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Fill)
	{
		P.Box(C + FVector2D(-0.3 * S, -0.62 * S), FVector2D(0.6 * S, 0.7 * S), FLinearColor::Transparent, 0.3f * S, Fill, 0.14f * S);
		P.Box(C + FVector2D(-0.5 * S, -0.2 * S), FVector2D(1.0 * S, 0.8 * S), Fill, 0.14f * S);
		P.Circle(C + FVector2D(0.0, 0.14 * S), 0.12f * S, Color::Paper);
	}

	void PaintSpinner(const FKartPainter& P, const FVector2D& C, float R, double Now, const FLinearColor& Fill)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			const double A = Now * 6.0 - i * UE_DOUBLE_PI / 4.0;
			P.Circle(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, R * (0.28f - i * 0.025f), Color::WithAlpha(Fill, 1.f - i * 0.11f));
		}
	}

	float PaintChip(const FKartPainter& P, const FVector2D& Pos, const FText& Label, const FLinearColor& Fill, float Height, float Align)
	{
		const FSlateFontInfo F = Font(EFont::Label, Height * 0.5f);
		const float W = static_cast<float>(FKartPainter::Measure(Label.ToString(), F).X) + Height;
		const FVector2D At(Pos.X - W * Align, Pos.Y);
		P.Box(At, FVector2D(W, Height), Fill, Height * 0.5f);
		P.Text(Label, F, At + FVector2D(W, Height) * 0.5, Color::TextOn(Fill), FVector2D(0.5, 0.5));
		return W;
	}

	void PaintActionButton(const FKartPainter& P, const FActionButton& Button)
	{
		const FVector2D& Size = Button.Size;
		const FVector2D Pos = Button.Centre - Size * 0.5;
		if (Button.bEnabled)
		{
			P.Hit(Button.HitId, Pos, Size);
		}
		const float Breath = Button.bFocused && Button.bEnabled ? 1.f + 0.025f * FMath::Sin(static_cast<float>(Button.FocusAge) * 4.f) : 1.f;
		const FKartPainter B = P.Transformed(Button.Centre, Breath);
		const float R = static_cast<float>(Size.Y) * 0.5f;
		const FLinearColor Body = Button.bEnabled ? Button.Fill : Color::PaperShade;
		B.Box(Pos + FVector2D(0.0, 7.0), Size, Color::Shadow, R);
		B.Gradient(Pos, Size, Body, Darker(Body, 0.18f), true, R);
		B.Box(Pos, Size, FLinearColor::Transparent, R, Color::White, Button.bEnabled ? 4.f : 2.f);
		// The glyph and the label, centred together.
		const FSlateFontInfo LabelFont = Font(EFont::Title, static_cast<float>(Size.Y) * 0.42f);
		const float GlyphH = static_cast<float>(Size.Y) * 0.5f;
		const float TextW = static_cast<float>(FKartPainter::Measure(Button.Label.ToString(), LabelFont).X);
		const bool bGlyph = Button.bEnabled && Button.Action != EKartMenuAction::None;
		const float Total = TextW + (bGlyph ? KartWidgets::GlyphWidth(Button.Action, Button.Device, GlyphH) + 18.f : 0.f);
		float X = static_cast<float>(Button.Centre.X) - Total * 0.5f;
		if (bGlyph)
		{
			X += KartWidgets::Glyph(B, Button.Action, Button.Device, FVector2D(X, Button.Centre.Y), GlyphH) + 18.f;
		}
		B.Text(Button.Label, LabelFont, FVector2D(X, Button.Centre.Y), Button.bEnabled ? Color::TextOn(Button.Fill) : Color::NavyDim, FVector2D(0.0, 0.5), 0.f);
	}

	void PaintConfetti(const FKartPainter& P, const FVector2D& C, double Age)
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
}

namespace KartScreens
{
	void PaintStoreIcon(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		// A shopping bag with a star tag.
		P.Box(C + FVector2D(-0.3 * S, -0.82 * S), FVector2D(0.6 * S, 0.6 * S), FLinearColor::Transparent, 0.3f * S, Color::Navy, 0.1f * S);
		P.Polygon({ C + FVector2D(-0.66 * S, -0.42 * S), C + FVector2D(0.66 * S, -0.42 * S), C + FVector2D(0.78 * S, 0.74 * S), C + FVector2D(-0.78 * S, 0.74 * S) }, Color::Navy);
		P.Polygon({ C + FVector2D(-0.56 * S, -0.32 * S), C + FVector2D(0.56 * S, -0.32 * S), C + FVector2D(0.66 * S, 0.64 * S), C + FVector2D(-0.66 * S, 0.64 * S) }, Color::Paper);
		KartWidgets::Star(P, C + FVector2D(0.0, 0.16 * S), 0.34f * S, 0.15f * S, Color::Hot, static_cast<float>(FMath::Sin(Now * 2.0) * 10.0));
	}
}

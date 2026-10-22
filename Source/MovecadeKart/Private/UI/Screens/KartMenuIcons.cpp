#include "UI/Screens/KartMenuIcons.h"

#include "UI/KartUIPainter.h"
#include "UI/KartWidgets.h"

using namespace KartUI;

// The icons are drawn in units of S (the Size argument) around C (the Centre): every coordinate below is a
// fraction of S, so one icon scales to any card.

namespace KartMenuIcons
{
	void RaceFlag(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		P.Box(C + FVector2D(-0.62 * S, -0.7 * S), FVector2D(0.08 * S, 1.5 * S), Color::Navy, 0.04f * S);
		constexpr int32 Cols = 6, Rows = 4;
		const double Cell = 0.2 * S;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Col = 0; Col < Cols; ++Col)
			{
				const double Wave = FMath::Sin(Now * 4.0 - Col * 0.8) * 0.06 * S * (Col + 1) / Cols;
				const FVector2D Pos(C.X - 0.54 * S + Col * Cell, C.Y - 0.66 * S + Row * Cell + Wave);
				P.Box(Pos, FVector2D(Cell + 0.5, Cell + 0.5), (Row + Col) % 2 == 0 ? Color::Navy : Color::White);
			}
		}
	}

	void PartyPhones(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const FVector2D Pos = C + FVector2D(-0.62 * S + i * 0.66 * S, -0.5 * S + i * 0.18 * S);
			P.Box(Pos, FVector2D(0.56 * S, 0.98 * S), Color::Navy, 0.1f * S);
			P.Box(Pos + FVector2D(0.06 * S, 0.1 * S), FVector2D(0.44 * S, 0.7 * S), i == 0 ? Color::Sky : Color::Hot, 0.05f * S);
			P.Circle(Pos + FVector2D(0.28 * S, 0.88 * S), 0.04f * S, Color::White);
		}
		for (int32 Ring = 0; Ring < 3; ++Ring)
		{
			const float A = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 4.f - Ring);
			P.Box(C + FVector2D(-0.1 * S - Ring * 0.12 * S, -0.92 * S - Ring * 0.12 * S), FVector2D(0.2 * S + Ring * 0.24 * S, 0.2 * S + Ring * 0.24 * S),
				FLinearColor::Transparent, 0.2f * S, Color::WithAlpha(Color::Navy, A), 0.045f * S);
		}
	}

	void SettingsGear(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		constexpr int32 Teeth = 8;
		TArray<FVector2D, TInlineAllocator<Teeth * 4>> Gear;
		const double Spin = Now * 0.6;
		for (int32 i = 0; i < Teeth * 4; ++i)
		{
			const double A = Spin + i * UE_DOUBLE_PI * 2.0 / (Teeth * 4);
			const double R = (i % 4 < 2) ? 0.8 * S : 0.6 * S;
			Gear.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		P.Polygon(Gear, Color::Navy);
		P.Circle(C, 0.3f * S, Color::Paper);
	}

	void CreditsStar(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		KartWidgets::Star(P, C, 0.85f * S, 0.38f * S, Color::Navy, static_cast<float>(FMath::Sin(Now * 1.5) * 12.0));
		KartWidgets::Star(P, C, 0.5f * S, 0.22f * S, Color::Sun, static_cast<float>(FMath::Sin(Now * 1.5) * 12.0));
	}

	void QuitDoor(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		P.Box(C + FVector2D(-0.5 * S, -0.75 * S), FVector2D(0.7 * S, 1.5 * S), Color::Navy, 0.08f * S);
		P.Box(C + FVector2D(-0.42 * S, -0.66 * S), FVector2D(0.54 * S, 1.32 * S), Color::PaperShade, 0.05f * S);
		KartWidgets::Arrow(P, C + FVector2D(0.42 * S + FMath::Sin(Now * 5.0) * 0.06 * S, 0.0), 0.32f * S, true, Color::Hot);
		P.Box(C + FVector2D(-0.05 * S, -0.08 * S), FVector2D(0.35 * S, 0.16 * S), Color::Hot);
	}

	void BowlingPins(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector2D Pin = C + FVector2D(0.18 * S + i * 0.26 * S, -0.1 * S - (i == 1 ? 0.12 * S : 0.0));
			P.Circle(Pin + FVector2D(0.0, -0.42 * S), 0.09f * S, Color::White, Color::Navy, 0.03f * S);
			P.Box(Pin + FVector2D(-0.1 * S, -0.36 * S), FVector2D(0.2 * S, 0.66 * S), Color::White, 0.09f * S, Color::Navy, 0.03f * S);
			P.Box(Pin + FVector2D(-0.1 * S, -0.26 * S), FVector2D(0.2 * S, 0.06 * S), Color::Hot);
		}
		const double Roll = FMath::Fmod(Now * 0.8, 1.0);
		const FVector2D Ball = C + FVector2D(-0.9 * S + Roll * 0.5 * S, 0.28 * S);
		P.Circle(Ball, 0.34f * S, Color::Navy);
		for (int32 Hole = 0; Hole < 3; ++Hole)
		{
			const double A = Now * 4.0 + Hole * 0.6;
			P.Circle(Ball + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 0.16 * S, 0.045f * S, Color::Sun);
		}
		P.Box(C + FVector2D(-0.95 * S, 0.64 * S), FVector2D(1.9 * S, 0.06 * S), Color::Navy, 0.03f * S);
	}

	void Dartboard(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		static const FLinearColor Rings[] = { Color::Navy, Color::White, Color::Hot, Color::White, Color::Mint, Color::Hot };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rings); ++i)
		{
			P.Circle(C, (0.9f - i * 0.15f) * S, Rings[i]);
		}
		const double Wobble = FMath::Sin(Now * 9.0) * 0.03 * S;
		P.Box(C + FVector2D(0.05 * S, -0.08 * S + Wobble), FVector2D(0.7 * S, 0.07 * S), Color::Navy, 0.03f * S);
		P.Polygon({ C + FVector2D(0.62 * S, -0.2 * S + Wobble), C + FVector2D(0.85 * S, -0.2 * S + Wobble), C + FVector2D(0.85 * S, 0.05 * S + Wobble),
			C + FVector2D(0.62 * S, 0.05 * S + Wobble) }, Color::Sun);
	}

	void SlicedFruit(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		const double Part = 0.08 * S * (0.5 + 0.5 * FMath::Sin(Now * 3.0));
		P.Circle(C + FVector2D(-0.1 * S - Part, 0.05 * S), 0.5f * S, Color::Hot, Color::Navy, 0.04f * S);
		P.Circle(C + FVector2D(0.1 * S + Part, -0.05 * S), 0.5f * S, Color::Sun, Color::Navy, 0.04f * S);
		P.Polygon({ C + FVector2D(-0.9 * S, 0.6 * S), C + FVector2D(0.8 * S, -0.7 * S), C + FVector2D(0.9 * S, -0.62 * S), C + FVector2D(-0.82 * S, 0.68 * S) }, Color::White);
	}

	void BlockTower(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		static const FLinearColor Fills[] = { Color::Navy, Color::Sky, Color::Sun, Color::Hot, Color::Mint };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Fills); ++i)
		{
			const double Sway = FMath::Sin(Now * 1.6) * 0.03 * S * i;
			const double W = (1.2 - i * 0.12) * S;
			P.Box(C + FVector2D(-W * 0.5 + Sway + i * 0.03 * S, 0.62 * S - (i + 1) * 0.26 * S), FVector2D(W, 0.24 * S), Fills[i], 0.04f * S, Color::Navy, 0.03f * S);
		}
	}

	void BumperPucks(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		const double Bump = FMath::Abs(FMath::Sin(Now * 2.4)) * 0.12 * S;
		P.Circle(C + FVector2D(0.0, 0.18 * S), 0.92f * S, Color::WithAlpha(Color::Navy, 0.85f), Color::Sky, 0.05f * S);
		P.Circle(C + FVector2D(0.0, 0.18 * S), 0.5f * S, FLinearColor::Transparent, Color::WithAlpha(Color::Sky, 0.5f), 0.03f * S);
		P.Circle(C + FVector2D(-0.36 * S - Bump, 0.05 * S), 0.33f * S, Color::Hot, Color::Navy, 0.05f * S);
		P.Circle(C + FVector2D(0.36 * S + Bump, 0.05 * S), 0.33f * S, Color::Sky, Color::Navy, 0.05f * S);
		P.Circle(C + FVector2D(-0.36 * S - Bump, -0.05 * S), 0.12f * S, Color::White);
		P.Circle(C + FVector2D(0.36 * S + Bump, -0.05 * S), 0.12f * S, Color::White);
		for (int32 i = 0; i < 4; ++i)
		{
			const double A = -0.9 + i * 0.6;
			const FVector2D D(FMath::Sin(A), -FMath::Cos(A));
			P.Lines({ C + D * (0.12 * S + Bump), C + D * (0.34 * S + Bump * 2.0) }, Color::Sun, 0.05f * S);
		}
	}

	void BatAndBall(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		const double Swing = FMath::Sin(Now * 2.4) * 0.25;
		const FVector2D Ball = C + FVector2D(0.42 * S, -0.38 * S);
		for (int32 i = 1; i <= 3; ++i)
		{
			P.Circle(Ball - FVector2D(0.16 * S * i, -0.13 * S * i), (0.15f - i * 0.03f) * S, Color::WithAlpha(Color::Sun, 0.8f - i * 0.2f));
		}
		P.Circle(Ball, 0.2f * S, Color::White, Color::Navy, 0.04f * S);
		P.Lines({ Ball + FVector2D(-0.1 * S, -0.08 * S), Ball + FVector2D(0.02 * S, 0.12 * S) }, Color::Hot, 0.03f * S);
		const FVector2D Knob = C + FVector2D(-0.55 * S, 0.62 * S);
		const double A = -0.95 + Swing;
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		const FVector2D Side(-Dir.Y, Dir.X);
		P.Polygon({ Knob + Side * 0.04 * S, Knob + Dir * 1.15 * S + Side * 0.12 * S, Knob + Dir * 1.22 * S, Knob + Dir * 1.15 * S - Side * 0.12 * S, Knob - Side * 0.04 * S }, Color::Gold);
		P.Circle(Knob, 0.07f * S, Color::Navy);
	}

	void Bobber(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		const double Dip = FMath::Max(0.0, FMath::Sin(Now * 2.4)) * 0.12 * S;
		for (int32 i = 0; i < 3; ++i)
		{
			const double R = (0.45 + 0.22 * i + 0.1 * FMath::Frac(Now * 0.6)) * S;
			P.Lines({ C + FVector2D(-R, 0.45 * S), C + FVector2D(R, 0.45 * S) }, Color::WithAlpha(Color::White, 0.7f - 0.2f * i), 0.05f * S);
		}
		P.Lines({ C + FVector2D(0.0, -0.95 * S), C + FVector2D(0.0, -0.3 * S + Dip) }, Color::White, 0.03f * S);
		P.Circle(C + FVector2D(0.0, 0.05 * S + Dip), 0.42f * S, Color::White, Color::Navy, 0.05f * S);
		P.Polygon({ C + FVector2D(-0.42 * S, 0.05 * S + Dip), C + FVector2D(-0.3 * S, -0.27 * S + Dip), C + FVector2D(0.0, -0.37 * S + Dip),
			C + FVector2D(0.3 * S, -0.27 * S + Dip), C + FVector2D(0.42 * S, 0.05 * S + Dip) }, Color::Hot);
		P.Box(C + FVector2D(-0.04 * S, -0.62 * S + Dip), FVector2D(0.08 * S, 0.3 * S), Color::Navy, 0.03f * S);
	}

	FPaintIcon ForArcadeGame(FName GameId)
	{
		struct FEntry
		{
			const TCHAR* Id;
			FPaintIcon Icon;
		};
		// Catalog ids (Games/ArcadeCatalog.cpp).
		static const FEntry Table[] = {
			{ TEXT("bowling"), &BowlingPins },
			{ TEXT("darts"), &Dartboard },
			{ TEXT("samurai"), &SlicedFruit },
			{ TEXT("stacker"), &BlockTower },
			{ TEXT("sumo"), &BumperPucks },
			{ TEXT("derby"), &BatAndBall },
			{ TEXT("fishing"), &Bobber },
		};
		for (const FEntry& Entry : Table)
		{
			if (GameId == FName(Entry.Id))
			{
				return Entry.Icon;
			}
		}
		return &BlockTower;
	}
}

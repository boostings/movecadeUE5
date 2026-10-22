#pragma once

#include "CoreMinimal.h"
#include "FX/KartFx.h"

/**
 * Bullseye Bash's effect recipes (impactFx.ts and the port's additions): each a labelled table of one emit's look,
 * turned into an FKartFxEmit where it fires. Unset fields keep FKartFxEmit's defaults; the caller adds where, how
 * many, which way and, for the thrower-coloured ones, the colour.
 */
namespace MovecadeKart::Darts::FxRecipes
{
	struct FEmitRecipe
	{
		Fx::EField Field = Fx::EField::Fire;
		/** Cone half-angle, radians. */
		double Spread = 0.5;
		/** m/s, s and m: (min, max). */
		FVector2D Speed = FVector2D(2.0, 2.0);
		FVector2D Life = FVector2D(0.6, 0.6);
		FVector2D Size = FVector2D(0.12, 0.12);
		double SizeEnd = 0.2;
		double Intensity = 1.0;
		double Jitter = 0.0;
		/** sRGB hex, or null when the caller colours it (the thrower's colour, an accent). */
		const TCHAR* Color = nullptr;
		const TCHAR* ColorEnd = nullptr;

		Fx::FKartFxEmit Make(const FVector& Position, int32 Count = 1) const
		{
			Fx::FKartFxEmit Emit;
			Emit.Field = Field;
			Emit.Position = Position;
			Emit.Count = Count;
			Emit.Spread = Spread;
			Emit.Speed = Speed;
			Emit.Life = Life;
			Emit.Size = Size;
			Emit.SizeEnd = SizeEnd;
			Emit.Intensity = Intensity;
			Emit.Jitter = Jitter;
			if (Color)
			{
				Emit.Color = Fx::Hex(Color);
				Emit.ColorEnd = Fx::Hex(ColorEnd ? ColorEnd : Color);
			}
			return Emit;
		}
	};

	/** Dust in the board's light: a slow drift of motes between the oche and the board (a stream, MotesPerSecond). */
	inline const FEmitRecipe DustMotes = { .Field = Fx::EField::Flare, .Spread = UE_DOUBLE_PI, .Speed = FVector2D(0.015, 0.05), .Life = FVector2D(4.0, 7.0),
		.Size = FVector2D(0.006, 0.012), .SizeEnd = 0.6, .Intensity = 0.55, .Jitter = 1.2, .Color = TEXT("ffe2b0"), .ColorEnd = TEXT("ffc98c") };
	constexpr double MotesPerSecond = 6.0;

	/** The newest flight's short glowing tail, in the thrower's colour (a stream, TrailPerSecond). */
	inline const FEmitRecipe Trail = { .Field = Fx::EField::Fire, .Spread = 0.2, .Speed = FVector2D(0.0, 0.05), .Life = FVector2D(0.14, 0.22),
		.Size = FVector2D(0.035, 0.05), .SizeEnd = 0.15, .Intensity = 2.2 };
	constexpr double TrailPerSecond = 140.0;
	/** The tail starts this share of the way from the thrower's colour to white. */
	constexpr float TrailWhiteShare = 0.35f;

	// A landing's impact (impactFx.ts tiers set the counts and sizes; ImpactTierFor).
	inline const FEmitRecipe ImpactDust = { .Field = Fx::EField::Smoke, .Spread = 1.1, .Speed = FVector2D(0.2, 0.7), .Life = FVector2D(0.35, 0.7),
		.Size = FVector2D(0.025, 0.06), .SizeEnd = 1.8, .Intensity = 0.7, .Color = TEXT("#d9c7ae"), .ColorEnd = TEXT("#a8977f") };
	/** Sisal (or, off the board, wood) chips flicked out of the hole, tumbling down. */
	inline const FEmitRecipe SisalChips = { .Field = Fx::EField::Confetti, .Spread = 0.9, .Speed = FVector2D(0.8, 1.9), .Life = FVector2D(0.45, 0.8),
		.Size = FVector2D(0.009, 0.018), .SizeEnd = 0.8, .Intensity = 2.0, .Color = TEXT("#e6d6b8") };
	inline const FEmitRecipe WoodChips = { .Field = Fx::EField::Confetti, .Spread = 0.9, .Speed = FVector2D(0.8, 1.9), .Life = FVector2D(0.45, 0.8),
		.Size = FVector2D(0.009, 0.018), .SizeEnd = 0.8, .Intensity = 2.0, .Color = TEXT("#6b4129") };
	/** The chips fly out and this much up. */
	constexpr double ChipsLift = 0.45;
	/** Sparks in the accent; speed comes from the tier (min is SparkSlowShare of it), they fade to SparkFade. */
	inline const FEmitRecipe Sparks = { .Field = Fx::EField::Sparks, .Spread = 1.25, .Life = FVector2D(0.2, 0.42), .Size = FVector2D(0.01, 0.018),
		.Intensity = 3.0 };
	constexpr double SparkSlowShare = 0.5;
	constexpr float SparkFade = 0.6f;
	/** A ring in the accent: it starts at RingStartShare of the tier's ring size and grows. */
	inline const FEmitRecipe Ring = { .Field = Fx::EField::Ring, .Speed = FVector2D::ZeroVector, .Life = FVector2D(0.45, 0.45), .SizeEnd = 3.3, .Intensity = 2.0 };
	constexpr double RingStartShare = 0.3;
	/** A flash in the accent, FlashSizePerM times the tier's flash. */
	inline const FEmitRecipe Flash = { .Field = Fx::EField::Flare, .Speed = FVector2D::ZeroVector, .Life = FVector2D(0.18, 0.18), .SizeEnd = 0.4, .Intensity = 3.0 };
	constexpr double FlashSizePerM = 2.2;
}

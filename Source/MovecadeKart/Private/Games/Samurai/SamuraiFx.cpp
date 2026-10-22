#include "Games/Samurai/SamuraiFx.h"

#include "Games/Samurai/SamuraiTuning.h"

// One function per emit: its fields (count, spread, speed m/s, life s, size m, colours, intensity) are the recipe,
// named by the FKartFxEmit fields they fill.

namespace MovecadeKart::Samurai
{
	namespace
	{
		using MovecadeKart::Fx::EField;
		using MovecadeKart::Fx::FKartFxEmit;

		/** The two juice arcs leave the cut faces this far out (cm per unit scale). */
		constexpr double JuiceFaceOffsetCm = 30.0;
		/** The explosion is centred this far below the bomb, so its fireball sits on the bomb. */
		constexpr double BombBurstDropCm = 30.0;
		constexpr double BombBurstRadiusMetres = 2.1;

		FKartFxEmit JuiceArc(const FVector& At, const FVector& Normal, double Side, double Scale, bool bPerfect, const FLinearColor& Juice)
		{
			FKartFxEmit Arc;
			Arc.Field = EField::Sparks;
			Arc.Position = At + Normal * Side * Scale * JuiceFaceOffsetCm;
			Arc.Count = bPerfect ? 16 : 10;
			Arc.Direction = (Normal * Side + FVector(0.0, 0.0, 0.9)).GetSafeNormal();
			Arc.Spread = 0.55;
			Arc.Speed = FVector2D(2.2, 5.2);
			Arc.Life = FVector2D(0.35, 0.7);
			Arc.Size = FVector2D(0.025, 0.05);
			Arc.Color = Juice * 1.2f;
			Arc.ColorEnd = Juice * 0.6f;
			Arc.Intensity = 1.8;
			Arc.Jitter = 0.08;
			return Arc;
		}

		FKartFxEmit Spray(const FVector& At, const FLinearColor& Juice)
		{
			FKartFxEmit Emit;
			Emit.Field = EField::Sparks;
			Emit.Position = At;
			Emit.Count = 10;
			Emit.Spread = 1.5;
			Emit.Speed = FVector2D(1.2, 3.4);
			Emit.Life = FVector2D(0.3, 0.55);
			Emit.Size = FVector2D(0.018, 0.035);
			Emit.Color = Juice;
			Emit.ColorEnd = Juice * 0.7f;
			Emit.Intensity = 1.5;
			return Emit;
		}

		FKartFxEmit Pulp(const FVector& At, const FLinearColor& Juice)
		{
			FKartFxEmit Emit;
			Emit.Field = EField::Smoke;
			Emit.Position = At;
			Emit.Count = 6;
			Emit.Spread = 1.0;
			Emit.Speed = FVector2D(0.4, 1.2);
			Emit.Life = FVector2D(0.35, 0.6);
			Emit.Size = FVector2D(0.08, 0.16);
			Emit.SizeEnd = 1.6;
			Emit.Color = Juice;
			Emit.ColorEnd = Juice * 0.5f;
			Emit.Intensity = 0.8;
			return Emit;
		}

		FKartFxEmit Splash(const FVector& At, const FLinearColor& Juice)
		{
			FKartFxEmit Emit;
			Emit.Field = EField::Fire;
			Emit.Position = At;
			Emit.Count = 4;
			Emit.Spread = 1.2;
			Emit.Speed = FVector2D(0.3, 0.9);
			Emit.Life = FVector2D(0.18, 0.3);
			Emit.Size = FVector2D(0.18, 0.28);
			Emit.SizeEnd = 1.8;
			Emit.Color = Juice;
			Emit.ColorEnd = Juice * 0.3f;
			Emit.Intensity = 1.2;
			return Emit;
		}

		/** A perfect cut: a star of light where the blade met the apex. */
		FKartFxEmit Star(const FVector& At)
		{
			FKartFxEmit Emit;
			Emit.Field = EField::Flare;
			Emit.Position = At;
			Emit.Count = 1;
			Emit.Speed = FVector2D::ZeroVector;
			Emit.Life = FVector2D(0.24, 0.24);
			Emit.Size = FVector2D(1.3, 1.3);
			Emit.SizeEnd = 0.2;
			Emit.Color = Palette::HaloLight;
			Emit.ColorEnd = Palette::HaloLight;
			Emit.Intensity = 4.0;
			return Emit;
		}

		/** ... and a ring of glints cooling into the fruit's colour. */
		FKartFxEmit Glints(const FVector& At, const FLinearColor& Juice)
		{
			FKartFxEmit Emit;
			Emit.Field = EField::Flare;
			Emit.Position = At;
			Emit.Count = 6;
			Emit.Spread = 3.1;
			Emit.Speed = FVector2D(1.0, 2.4);
			Emit.Life = FVector2D(0.3, 0.5);
			Emit.Size = FVector2D(0.12, 0.2);
			Emit.SizeEnd = 0.1;
			Emit.Color = Palette::HaloLight;
			Emit.ColorEnd = Juice;
			Emit.Intensity = 3.0;
			return Emit;
		}

		/** The bomb shell's shards and embers, flung toward the camera. */
		FKartFxEmit Shards(const FVector& At)
		{
			FKartFxEmit Emit;
			Emit.Field = EField::Sparks;
			Emit.Position = At;
			Emit.Count = 26;
			Emit.Direction = FVector(-1.0, 0.0, 0.6).GetSafeNormal();
			Emit.Spread = 1.3;
			Emit.Speed = FVector2D(4.0, 9.0);
			Emit.Life = FVector2D(0.4, 0.9);
			Emit.Size = FVector2D(0.03, 0.06);
			Emit.Color = Palette::ShardHot;
			Emit.ColorEnd = Palette::ShardCool;
			Emit.Intensity = 3.0;
			return Emit;
		}
	}

	void AppendCutFx(EVariant Variant, const FVector& At, const FVector& Normal, double Scale, bool bPerfect, TArray<FKartFxEmit>& Out)
	{
		// Each variant's hue from the rules' table, parsed once.
		static const FLinearColor Juices[] = {
			MovecadeKart::Art::Hex(Def(EVariant::Apple).Color), MovecadeKart::Art::Hex(Def(EVariant::Melon).Color),
			MovecadeKart::Art::Hex(Def(EVariant::Lantern).Color), MovecadeKart::Art::Hex(Def(EVariant::Bamboo).Color),
			MovecadeKart::Art::Hex(Def(EVariant::Bomb).Color), MovecadeKart::Art::Hex(Def(EVariant::Scroll).Color),
		};
		const FLinearColor& Juice = Juices[static_cast<int32>(Variant)];
		for (const double Side : { -1.0, 1.0 })
		{
			Out.Add(JuiceArc(At, Normal, Side, Scale, bPerfect, Juice));
		}
		Out.Add(Spray(At, Juice));
		Out.Add(Pulp(At, Juice));
		Out.Add(Splash(At, Juice));
		if (bPerfect)
		{
			Out.Add(Star(At));
			Out.Add(Glints(At, Juice));
		}
	}

	void AppendBombFx(const FVector& At, TArray<FKartFxEmit>& Out)
	{
		MovecadeKart::Fx::ExplosionBurst(At - FVector(0.0, 0.0, BombBurstDropCm), BombBurstRadiusMetres, Out);
		Out.Add(Shards(At));
	}
}

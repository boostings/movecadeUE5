#include "FX/KartFx.h"

#include "Misc/Parse.h"

namespace MovecadeKart::Fx
{
	FColor HexToColor(FStringView Value)
	{
		if (Value.StartsWith(TEXT('#')))
		{
			Value.RightChopInline(1);
		}
		const auto Byte = [Value](int32 At) { return static_cast<uint8>((FParse::HexDigit(Value[At]) << 4) + FParse::HexDigit(Value[At + 1])); };
		switch (Value.Len())
		{
		case 3:
		{
			const auto Doubled = [](int32 Digit) { return static_cast<uint8>((Digit << 4) + Digit); };
			return FColor(Doubled(FParse::HexDigit(Value[0])), Doubled(FParse::HexDigit(Value[1])), Doubled(FParse::HexDigit(Value[2])), 255);
		}
		case 6:
			return FColor(Byte(0), Byte(2), Byte(4), 255);
		case 8:
			return FColor(Byte(0), Byte(2), Byte(4), Byte(6));
		default:
			return FColor(ForceInitToZero);
		}
	}

	FLinearColor Hex(FStringView Value)
	{
		return FLinearColor::FromSRGBColor(HexToColor(Value));
	}

	namespace
	{
		/** A single particle that holds still and changes size and colour over its life (rings and flash stars). */
		FKartFxEmit SingleParticle(EField Field, const FVector& Cm, double LifeSeconds, double SizeMetres, double SizeEnd,
			const FLinearColor& Color, const FLinearColor& ColorEnd, double Intensity, const FVector& InheritCm)
		{
			FKartFxEmit Emit;
			Emit.Field = Field;
			Emit.Position = Cm;
			Emit.Count = 1;
			Emit.Speed = { 0.0, 0.0 };
			Emit.Life = { LifeSeconds, LifeSeconds };
			Emit.Size = { SizeMetres, SizeMetres };
			Emit.SizeEnd = SizeEnd;
			Emit.Color = Color;
			Emit.ColorEnd = ColorEnd;
			Emit.Intensity = Intensity;
			Emit.InheritCm = InheritCm;
			return Emit;
		}
	}

	FKartFxEmit ShockRing(const FVector& Cm, double LifeSeconds, double SizeMetres, double SizeEnd,
		const FLinearColor& Color, const FLinearColor& ColorEnd, double Intensity, const FVector& InheritCm)
	{
		return SingleParticle(EField::Ring, Cm, LifeSeconds, SizeMetres, SizeEnd, Color, ColorEnd, Intensity, InheritCm);
	}

	FKartFxEmit FlashStar(const FVector& Cm, double LifeSeconds, double SizeMetres, double SizeEnd,
		const FLinearColor& Color, const FLinearColor& ColorEnd, double Intensity, const FVector& InheritCm)
	{
		return SingleParticle(EField::Flare, Cm, LifeSeconds, SizeMetres, SizeEnd, Color, ColorEnd, Intensity, InheritCm);
	}

	const FImpactProfile& ImpactProfile(EImpact Kind)
	{
		// kartFxBus.ts:31-37.
		static const FImpactProfile Hit{ 0.34, 0.22, Hex(TEXT("fff1dc")), 1.0 };
		static const FImpactProfile Explosion{ 0.55, 0.32, Hex(TEXT("ffb070")), 1.6 };
		static const FImpactProfile Spin{ 0.14, 0.0, FLinearColor::White, 0.5 };
		static const FImpactProfile Wall{ 0.20, 0.0, FLinearColor::White, 0.6 };
		static const FImpactProfile Bump{ 0.12, 0.0, FLinearColor::White, 0.4 };
		switch (Kind)
		{
		case EImpact::ProjectileHit: return Hit;
		case EImpact::Explosion: return Explosion;
		case EImpact::SpinOut: return Spin;
		case EImpact::Wall: return Wall;
		default: return Bump;
		}
	}

	void ImpactBurst(EImpact Kind, const FVector& GroundCm, double Intensity, TArray<FKartFxEmit>& Out, double RingEndMetres, const FLinearColor& Tint)
	{
		// kartFxBus.ts: scale = clamp(intensity, 0.15, 1); fxProfileFor scales burst by it.
		const double Burst = ImpactProfile(Kind).Burst * FMath::Clamp(Intensity, 0.15, 1.0);
		const bool bHeavy = Kind == EImpact::Explosion || Kind == EImpact::ProjectileHit;
		const FVector At = GroundCm + FVector(0.0, 0.0, 60.0);

		FKartFxEmit Sparks;
		Sparks.Field = EField::Sparks;
		Sparks.Position = At;
		Sparks.Count = FMath::RoundToInt32(34.0 * Burst);
		Sparks.Spread = UE_DOUBLE_PI * 0.7;
		Sparks.Speed = { 5.0, 13.0 };
		Sparks.Life = { 0.25, 0.55 };
		Sparks.Size = { 0.07, 0.13 };
		Sparks.Color = Hex(bHeavy ? TEXT("ffe2a0") : TEXT("fff4dc"));
		Sparks.ColorEnd = Tint.A > 0.f ? Tint : Hex(TEXT("ff5a1f"));
		Sparks.Intensity = 2.4;
		Sparks.Jitter = 0.3;
		Out.Add(Sparks);

		FKartFxEmit Smoke;
		Smoke.Field = EField::Smoke;
		Smoke.Position = At;
		Smoke.Count = FMath::Max(1, FMath::RoundToInt32(8.0 * Burst));
		Smoke.Spread = UE_DOUBLE_PI;
		Smoke.Speed = { 0.8, 2.6 };
		Smoke.Life = { 0.6, 1.0 };
		Smoke.Size = { 0.45, 0.8 };
		Smoke.SizeEnd = 2.2;
		Smoke.Color = Hex(bHeavy ? TEXT("8a8380") : TEXT("ece8e2"));
		Smoke.ColorEnd = Hex(bHeavy ? TEXT("5a5452") : TEXT("c9c4bd"));
		Smoke.Intensity = 1.0;
		Smoke.Jitter = 0.4;
		Out.Add(Smoke);

		if (bHeavy)
		{
			FKartFxEmit Fire;
			Fire.Field = EField::Fire;
			Fire.Position = At;
			Fire.Count = FMath::RoundToInt32(18.0 * Burst);
			Fire.Spread = UE_DOUBLE_PI;
			Fire.Speed = { 2.0, 6.0 };
			Fire.Life = { 0.2, 0.45 };
			Fire.Size = { 0.5, 0.9 };
			Fire.SizeEnd = 1.8;
			Fire.Color = Hex(TEXT("ffd27a"));
			Fire.ColorEnd = Hex(TEXT("ff3d1f"));
			Fire.Intensity = 2.2;
			Fire.Jitter = 0.4;
			Out.Add(Fire);

			// sizeEnd is a multiplier on the 1.2 m start (ParticleField: size * mix(1, sizeEnd, sqrt(t))).
			const double RingEnd = RingEndMetres > 0.0 ? RingEndMetres : (Kind == EImpact::Explosion ? 9.0 : 5.0);
			Out.Add(ShockRing(GroundCm + FVector(0.0, 0.0, 8.0), 0.45, 1.2, RingEnd, Hex(TEXT("ffcf8a")), Hex(TEXT("ff6a2a")), 1.8));
		}
	}

	void ExplosionBurst(const FVector& GroundCm, double RadiusMetres, TArray<FKartFxEmit>& Out)
	{
		const double R = FMath::Max(2.0, RadiusMetres);
		const FVector Core = GroundCm + FVector(0.0, 0.0, 90.0);

		// Frame-one flash: one big white-gold star that pops and is gone.
		Out.Add(FlashStar(Core + FVector(0.0, 0.0, 40.0), 0.22, R * 1.6, 1.5, Hex(TEXT("fff6d8")), Hex(TEXT("ffb347")), 6.0));

		// Bloom core: a few big additive glows under the puffs.
		FKartFxEmit Glow;
		Glow.Field = EField::Fire;
		Glow.Position = Core;
		Glow.Count = 8;
		Glow.Spread = UE_DOUBLE_PI;
		Glow.Speed = { 1.0, 3.0 };
		Glow.Life = { 0.25, 0.4 };
		Glow.Size = { R * 0.35, R * 0.5 };
		Glow.SizeEnd = 1.6;
		Glow.Color = Hex(TEXT("ffe7a0"));
		Glow.ColorEnd = Hex(TEXT("ff5a1f"));
		Glow.Intensity = 3.0;
		Glow.Jitter = R * 0.12;
		Out.Add(Glow);

		// Fireball: chunky toon puffs billowing up and out, white-hot to orange to red as they erode.
		FKartFxEmit Fire;
		Fire.Field = EField::Smoke;
		Fire.Position = Core;
		Fire.Count = 26;
		Fire.Direction = FVector::UpVector;
		Fire.Spread = 1.5;
		Fire.Speed = { R * 0.7, R * 1.7 };
		Fire.Life = { 0.5, 0.85 };
		Fire.Size = { R * 0.4, R * 0.6 };
		Fire.SizeEnd = 2.0;
		Fire.Color = Hex(TEXT("fff4c2"));
		Fire.ColorEnd = Hex(TEXT("ff4a14"));
		Fire.Intensity = 3.6;
		Fire.Jitter = R * 0.12;
		Out.Add(Fire);
		// A second, tighter and hotter core that burns out first.
		Fire.Count = 8;
		Fire.Speed = { R * 0.3, R * 0.8 };
		Fire.Life = { 0.3, 0.45 };
		Fire.Size = { R * 0.5, R * 0.65 };
		Fire.SizeEnd = 1.6;
		Fire.Color = FLinearColor::White;
		Fire.ColorEnd = Hex(TEXT("ffc040"));
		Fire.Intensity = 5.0;
		Out.Add(Fire);

		// Smoke: puffs that start as glowing embers and cool to a dark column that rises and lingers.
		FKartFxEmit Smoke;
		Smoke.Field = EField::Smoke;
		Smoke.Position = Core + FVector(0.0, 0.0, 120.0);
		Smoke.Count = 9;
		Smoke.Direction = FVector::UpVector;
		Smoke.Spread = 0.8;
		Smoke.Speed = { R * 0.5, R * 1.1 };
		Smoke.Life = { 1.3, 2.0 };
		Smoke.Size = { R * 0.2, R * 0.3 };
		Smoke.SizeEnd = 3.0;
		// Overbright at first (the puff material glows above 1), cooling to a lit charcoal grey.
		Smoke.Color = Hex(TEXT("ff7a2a"));
		Smoke.ColorEnd = Hex(TEXT("4a4444"));
		Smoke.Intensity = 1.7;
		Smoke.Jitter = R * 0.14;
		Out.Add(Smoke);

		// Debris: hot streaks flung out and falling.
		FKartFxEmit Debris;
		Debris.Field = EField::Sparks;
		Debris.Position = Core;
		Debris.Count = 46;
		Debris.Direction = FVector::UpVector;
		Debris.Spread = 1.35;
		Debris.Speed = { 9.0, 22.0 };
		Debris.Life = { 0.45, 1.0 };
		Debris.Size = { 0.09, 0.16 };
		Debris.Color = Hex(TEXT("fff0b0"));
		Debris.ColorEnd = Hex(TEXT("ff4a1c"));
		Debris.Intensity = 3.0;
		Debris.Jitter = 0.4;
		Out.Add(Debris);

		// Shock rings: a fast bright one to the blast radius, a slower soft one inside it.
		FKartFxEmit Ring = ShockRing(GroundCm + FVector(0.0, 0.0, 10.0), 0.4, 1.2, R * 2.0 / 1.2, Hex(TEXT("fff0c8")), Hex(TEXT("ff7a2a")), 2.6);
		Out.Add(Ring);
		Ring.Life = { 0.7, 0.7 };
		Ring.SizeEnd = R * 1.3 / 1.2;
		Ring.Intensity = 1.2;
		Out.Add(Ring);
	}

	void Poof(const FVector& Cm, const FLinearColor& Color, double Scale, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Puff;
		Puff.Field = EField::Smoke;
		Puff.Position = Cm;
		Puff.Count = FMath::Max(3, FMath::RoundToInt32(7.0 * Scale));
		Puff.Spread = UE_DOUBLE_PI;
		Puff.Speed = { 0.6 * Scale, 2.0 * Scale };
		Puff.Life = { 0.35, 0.6 };
		Puff.Size = { 0.25 * Scale, 0.4 * Scale };
		Puff.SizeEnd = 2.0;
		Puff.Color = Hex(TEXT("f4f2ee"));
		Puff.ColorEnd = Hex(TEXT("d4d0ca"));
		Puff.Jitter = 0.15 * Scale;
		Out.Add(Puff);

		FKartFxEmit Twinkle;
		Twinkle.Field = EField::Flare;
		Twinkle.Position = Cm;
		Twinkle.Count = FMath::Max(3, FMath::RoundToInt32(8.0 * Scale));
		Twinkle.Spread = UE_DOUBLE_PI;
		Twinkle.Speed = { 1.0, 3.5 };
		Twinkle.Life = { 0.25, 0.5 };
		Twinkle.Size = { 0.2 * Scale, 0.35 * Scale };
		Twinkle.SizeEnd = 0.2;
		Twinkle.Color = FLinearColor::White;
		Twinkle.ColorEnd = Color;
		Twinkle.Intensity = 2.5;
		Twinkle.Jitter = 0.2 * Scale;
		Out.Add(Twinkle);
	}

	void LandingPuff(const FVector& GroundCm, const FVector& InheritCm, double Strength, bool bDirt, TArray<FKartFxEmit>& Out)
	{
		const double S = FMath::Clamp(Strength, 0.0, 1.0);
		FKartFxEmit Puff;
		Puff.Field = EField::Smoke;
		Puff.Position = GroundCm + FVector(0.0, 0.0, 12.0);
		Puff.Count = FMath::RoundToInt32(FMath::Lerp(4.0, 9.0, S));
		// Out sideways along the ground, a little up.
		Puff.Direction = FVector::UpVector;
		Puff.Spread = 1.35;
		Puff.Speed = { 1.0, 2.6 };
		Puff.Life = { 0.35, 0.6 };
		Puff.Size = { 0.22, 0.36 };
		Puff.SizeEnd = 2.2;
		Puff.Color = Hex(bDirt ? TEXT("dcc193") : TEXT("f2f0ec"));
		Puff.ColorEnd = Hex(bDirt ? TEXT("c4a577") : TEXT("d6d2cc"));
		Puff.Jitter = 0.35;
		Puff.InheritCm = InheritCm * 0.5;
		Out.Add(Puff);
	}

	void ConfettiBurst(const FVector& Cm, const FVector& Forward, TArray<FKartFxEmit>& Out)
	{
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			FKartFxEmit Paper;
			Paper.Field = EField::Confetti;
			Paper.Position = Cm + Right * (Side * 180.0);
			Paper.Count = 70;
			Paper.Direction = (FVector::UpVector * 1.4 - Right * Side * 0.5 + Forward * 0.25).GetSafeNormal();
			Paper.Spread = 0.45;
			Paper.Speed = { 7.0, 13.0 };
			Paper.Life = { 2.2, 3.2 };
			Paper.Size = { 0.16, 0.24 };
			Paper.SizeEnd = 0.7;
			Paper.Color = FLinearColor::White;
			Paper.ColorEnd = FLinearColor::White;
			Paper.Intensity = 0.9;
			Paper.Jitter = 0.3;
			Out.Add(Paper);
		}
		FKartFxEmit Twinkle;
		Twinkle.Field = EField::Flare;
		Twinkle.Position = Cm + FVector(0.0, 0.0, 250.0);
		Twinkle.Count = 30;
		Twinkle.Spread = UE_DOUBLE_PI;
		Twinkle.Speed = { 2.0, 6.0 };
		Twinkle.Life = { 0.5, 1.0 };
		Twinkle.Size = { 0.3, 0.6 };
		Twinkle.SizeEnd = 0.2;
		Twinkle.Color = Hex(TEXT("fff6c8"));
		Twinkle.ColorEnd = Hex(TEXT("ff7ad9"));
		Twinkle.Intensity = 3.0;
		Twinkle.Jitter = 1.2;
		Out.Add(Twinkle);
	}

	FDriftLevel DriftLevel(double DriftPower)
	{
		if (DriftPower >= 6.0) return { 3, 6.0, Hex(TEXT("b100ff")) };
		if (DriftPower >= 3.0) return { 2, 3.0, Hex(TEXT("FFA22B")) };
		if (DriftPower >= 1.0) return { 1, 1.0, Hex(TEXT("00ffff")) };
		return { 0, 0.0, FLinearColor::White };
	}

	FLinearColor DriftGradeColor(const FString& Label)
	{
		// Spec 01 §4.3 grades, on the drift-spark palette; APEX is a white-gold flourish on top.
		if (Label.Contains(TEXT("APEX"))) return Hex(TEXT("fff2c0"));
		if (Label.Contains(TEXT("ULTRA"))) return Hex(TEXT("b100ff"));
		if (Label.Contains(TEXT("SUPER"))) return Hex(TEXT("FFA22B"));
		return Hex(TEXT("00ffff"));
	}

	FItemColors ItemColors(EKartItem Item)
	{
		switch (Item)
		{
		case EKartItem::Shot: return { Hex(TEXT("00ff00")), Hex(TEXT("087c36")), Hex(TEXT("00aa00")) };
		case EKartItem::Seeker: return { Hex(TEXT("ff0000")), Hex(TEXT("8a1721")), Hex(TEXT("aa0000")) };
		case EKartItem::SkySeeker: return { Hex(TEXT("0066ff")), Hex(TEXT("174188")), Hex(TEXT("0033aa")) };
		case EKartItem::Banana: return { Hex(TEXT("ff6a13")), Hex(TEXT("f4f4f4")), Hex(TEXT("ff4a00")) }; // the traffic cone
		case EKartItem::Bomb: return { Hex(TEXT("111111")), Hex(TEXT("6d1c19")), Hex(TEXT("ff4400")) };
		// The port's own: a red nitro canister, a rainbow prism, an electric bolt, violet ink, a white toy rocket.
		case EKartItem::Nitro:
		case EKartItem::TripleNitro: return { Hex(TEXT("ff3b2f")), Hex(TEXT("f2f2f2")), Hex(TEXT("ff7a1a")) };
		case EKartItem::Prism: return { Hex(TEXT("ffffff")), Hex(TEXT("ffd84a")), Hex(TEXT("ff7ad9")) };
		case EKartItem::Lightning: return { Hex(TEXT("ffe14a")), Hex(TEXT("fff6c8")), Hex(TEXT("6ab0ff")) };
		case EKartItem::Ink: return { Hex(TEXT("2a1840")), Hex(TEXT("6b3fa0")), Hex(TEXT("8a4dff")) };
		case EKartItem::Rocket: return { Hex(TEXT("f2f4f8")), Hex(TEXT("ff6a13")), Hex(TEXT("ff6a2a")) };
		default: return { FLinearColor::White, FLinearColor::Gray, FLinearColor::Black };
		}
	}

	int32 ParticlesDue(double& Carry, double Rate, double Dt)
	{
		const double Total = Carry + Rate * FMath::Max(0.0, Dt);
		const int32 Count = FMath::FloorToInt32(Total);
		Carry = Total - Count;
		return Count;
	}
}

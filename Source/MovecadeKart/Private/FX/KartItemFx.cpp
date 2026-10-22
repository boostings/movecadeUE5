#include "FX/KartItemFx.h"

namespace MovecadeKart::Fx
{
	FKartFxLight FinishCelebration(const FVector& KartCm, const FVector& Forward, const FVector& InheritCm, TArray<FKartFxEmit>& Out)
	{
		static const FLinearColor Gold = Hex(TEXT("ffd166"));
		static const FLinearColor Amber = Hex(TEXT("ff9f1c"));
		static const FLinearColor Cream = Hex(TEXT("fff6d8"));
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		ConfettiBurst(KartCm + Forward * 300.0 + FVector(0.0, 0.0, 120.0), Forward, Out);
		Out.Add(ShockRing(KartCm + FVector(0.0, 0.0, 8.0), 0.6, 1.5, 6.5, Cream, Gold, 3.0, InheritCm));
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			FKartFxEmit Fountain;
			Fountain.Field = EField::Sparks;
			Fountain.Position = KartCm + Right * (Side * 160.0) + FVector(0.0, 0.0, 40.0);
			Fountain.Count = 40;
			Fountain.Direction = (FVector::UpVector * 2.0 + Right * Side * 0.6).GetSafeNormal();
			Fountain.Spread = 0.35;
			Fountain.Speed = { 9.0, 16.0 };
			Fountain.Life = { 0.7, 1.3 };
			Fountain.Size = { 0.08, 0.14 };
			Fountain.Color = Cream;
			Fountain.ColorEnd = Amber;
			Fountain.Intensity = 4.0;
			Fountain.Jitter = 0.2;
			Fountain.InheritCm = InheritCm;
			Out.Add(Fountain);
		}
		FKartFxEmit Crown;
		Crown.Field = EField::Flare;
		Crown.Position = KartCm + FVector(0.0, 0.0, 220.0);
		Crown.Count = 16;
		Crown.Spread = UE_DOUBLE_PI;
		Crown.Speed = { 1.5, 4.0 };
		Crown.Life = { 0.5, 0.9 };
		Crown.Size = { 0.45, 0.8 };
		Crown.SizeEnd = 0.2;
		Crown.Color = FLinearColor::White;
		Crown.ColorEnd = Gold;
		Crown.Intensity = 3.5;
		Crown.Jitter = 0.8;
		Crown.InheritCm = InheritCm;
		Out.Add(Crown);
		return { KartCm + FVector(0.0, 0.0, 400.0), Gold, 1200.0, 2500.0, 0.7 };
	}

	FKartFxLight ProjectileHitFlash(const FVector& KartCm, double GroundZ, const FItemColors& Colors, const FVector& InheritCm, TArray<FKartFxEmit>& Out)
	{
		Out.Add(FlashStar(KartCm + FVector(0.0, 0.0, 90.0), 0.16, 2.4, 1.4, FLinearColor::White, Colors.Emissive, 3.5, InheritCm));
		FKartFxEmit Shards;
		Shards.Field = EField::Sparks;
		Shards.Position = KartCm + FVector(0.0, 0.0, 70.0);
		Shards.Count = 18;
		Shards.Spread = UE_DOUBLE_PI * 0.75;
		Shards.Speed = { 6.0, 14.0 };
		Shards.Life = { 0.3, 0.6 };
		Shards.Size = { 0.1, 0.18 };
		Shards.Color = FLinearColor::LerpUsingHSV(Colors.Color, FLinearColor::White, 0.4f);
		Shards.ColorEnd = Colors.Inner;
		Shards.Intensity = 3.0;
		Shards.Jitter = 0.3;
		Shards.InheritCm = InheritCm;
		Out.Add(Shards);
		Out.Add(ShockRing(FVector(KartCm.X, KartCm.Y, GroundZ + 12.0), 0.32, 1.0, 4.2,
			FLinearColor::LerpUsingHSV(Colors.Color, FLinearColor::White, 0.3f), Colors.Emissive, 2.4, InheritCm));
		return { KartCm + FVector(0.0, 0.0, 220.0), FLinearColor::LerpUsingHSV(Colors.Color, FLinearColor::White, 0.45f), 650.0, 1200.0, 0.3 };
	}

	FKartFxLight ExplosionLight(const FVector& BlastCentreCm, double RadiusMetres)
	{
		return { BlastCentreCm + FVector(0.0, 0.0, 150.0), Hex(TEXT("ff9a40")), RadiusMetres * 100.0 * 2.5, 60000.0, 0.6 };
	}

	void DizzyStars(const FVector& KartCm, const FVector& KartVelocityCm, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Stars;
		Stars.Field = EField::Flare;
		Stars.Position = KartCm + FVector(0.0, 0.0, 140.0);
		Stars.Count = 10;
		Stars.Spread = 1.2;
		Stars.Speed = { 1.0, 2.5 };
		Stars.Life = { 0.6, 1.0 };
		Stars.Size = { 0.35, 0.55 };
		Stars.SizeEnd = 0.3;
		Stars.Color = Hex(TEXT("fff3a0"));
		Stars.ColorEnd = Hex(TEXT("ffb020"));
		Stars.Intensity = 3.0;
		Stars.Jitter = 0.6;
		Stars.InheritCm = KartVelocityCm * 0.6;
		Out.Add(Stars);
	}

	void BoxCollectBurst(const FVector& BoxCm, const FVector& RingCm, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Twinkle;
		Twinkle.Field = EField::Flare;
		Twinkle.Position = BoxCm;
		Twinkle.Count = 20;
		Twinkle.Spread = UE_DOUBLE_PI;
		Twinkle.Speed = { 2.0, 6.5 };
		Twinkle.Life = { 0.35, 0.7 };
		Twinkle.Size = { 0.35, 0.7 };
		Twinkle.SizeEnd = 0.2;
		Twinkle.Color = Hex(TEXT("c8fbff"));
		Twinkle.ColorEnd = Hex(TEXT("b45cff"));
		Twinkle.Intensity = 3.0;
		Twinkle.Jitter = 0.6;
		Out.Add(Twinkle);
		FKartFxEmit Sparks = Twinkle;
		Sparks.Field = EField::Sparks;
		Sparks.Count = 28;
		Sparks.Speed = { 5.0, 12.0 };
		Sparks.Life = { 0.25, 0.5 };
		Sparks.Size = { 0.06, 0.11 };
		Sparks.Color = Hex(TEXT("e8fdff"));
		Out.Add(Sparks);
		Out.Add(ShockRing(RingCm, 0.4, 1.0, 4.0, Hex(TEXT("7ff4ff")), Hex(TEXT("b45cff")), 2.0));
	}

	void BoxRespawnBurst(const FVector& BoxCm, TArray<FKartFxEmit>& Out)
	{
		Out.Add(ShockRing(BoxCm - FVector(0.0, 0.0, 92.0), 0.45, 2.6, 0.25, Hex(TEXT("c8fbff")), Hex(TEXT("b45cff")), 2.0));
		Poof(BoxCm, Hex(TEXT("5ff0ff")), 1.3, Out);
	}

	const FKartFxEmit& BoxTwinkleStream()
	{
		static const FKartFxEmit Twinkle = []
		{
			FKartFxEmit T;
			T.Field = EField::Flare;
			T.Direction = FVector::UpVector;
			T.Spread = 0.8;
			T.Speed = { 0.3, 0.9 };
			T.Life = { 0.7, 1.3 };
			T.Size = { 0.22, 0.42 };
			T.SizeEnd = 0.3;
			T.Color = Hex(TEXT("d8f9ff"));
			T.ColorEnd = Hex(TEXT("b45cff"));
			T.Intensity = 2.2;
			T.Jitter = 1.25;
			return T;
		}();
		return Twinkle;
	}

	void ThrowPuff(const FVector& Cm, const FLinearColor& Tint, const FVector& ThrowerVelocityCm, TArray<FKartFxEmit>& Out)
	{
		Poof(Cm, Tint, 0.9, Out);
		FKartFxEmit Sparks;
		Sparks.Field = EField::Sparks;
		Sparks.Position = Cm;
		Sparks.Count = 12;
		Sparks.Spread = UE_DOUBLE_PI * 0.7;
		Sparks.Speed = { 3.0, 8.0 };
		Sparks.Life = { 0.15, 0.35 };
		Sparks.Size = { 0.05, 0.09 };
		Sparks.Color = FLinearColor::White;
		Sparks.ColorEnd = Tint;
		Sparks.Intensity = 2.4;
		Sparks.InheritCm = ThrowerVelocityCm * 0.7;
		Out.Add(Sparks);
	}

	void BounceSparks(const FVector& GroundCm, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Sparks;
		Sparks.Field = EField::Sparks;
		Sparks.Position = GroundCm + FVector(0.0, 0.0, 35.0);
		Sparks.Count = 14;
		Sparks.Spread = UE_DOUBLE_PI * 0.8;
		Sparks.Speed = { 4.0, 10.0 };
		Sparks.Life = { 0.15, 0.35 };
		Sparks.Size = { 0.05, 0.09 };
		Sparks.Color = Hex(TEXT("fff4dc"));
		Sparks.ColorEnd = Hex(TEXT("ff5a1f"));
		Sparks.Intensity = 2.4;
		Out.Add(Sparks);
	}

	void LightningStrikeBurst(const FVector& KartCm, TArray<FKartFxEmit>& Out)
	{
		const FItemColors C = ItemColors(EKartItem::Lightning);
		FKartFxEmit Sparks;
		Sparks.Field = EField::Sparks;
		Sparks.Position = KartCm + FVector(0.0, 0.0, 40.0);
		Sparks.Count = 34;
		Sparks.Spread = 1.3;
		Sparks.Speed = { 6.0, 14.0 };
		Sparks.Life = { 0.2, 0.45 };
		Sparks.Size = { 0.07, 0.12 };
		Sparks.Color = Hex(TEXT("f0faff"));
		Sparks.ColorEnd = C.Emissive;
		Sparks.Intensity = 4.0;
		Out.Add(Sparks);
		Out.Add(ShockRing(KartCm + FVector(0.0, 0.0, 8.0), 0.4, 1.2, 4.5, Hex(TEXT("e0f0ff")), C.Emissive, 2.6));
		Out.Add(FlashStar(KartCm + FVector(0.0, 0.0, 80.0), 0.2, 5.0, 1.3, FLinearColor::White, C.Emissive, 5.0));
		ImpactBurst(EImpact::SpinOut, KartCm, 0.6, Out);
	}

	void InkSplash(const FVector& KartCm, const FVector& KartVelocityCm, TArray<FKartFxEmit>& Out)
	{
		const FItemColors C = ItemColors(EKartItem::Ink);
		FKartFxEmit Ink;
		Ink.Field = EField::Smoke;
		Ink.Position = KartCm + FVector(0.0, 0.0, 120.0);
		Ink.Count = 12;
		Ink.Spread = UE_DOUBLE_PI;
		Ink.Speed = { 2.0, 5.0 };
		Ink.Life = { 0.5, 0.9 };
		Ink.Size = { 0.35, 0.6 };
		Ink.SizeEnd = 1.8;
		Ink.Color = C.Color;
		Ink.ColorEnd = Hex(TEXT("120a1e"));
		Ink.Jitter = 0.4;
		Ink.InheritCm = KartVelocityCm * 0.7;
		Out.Add(Ink);
		FKartFxEmit Drops = Ink;
		Drops.Field = EField::Sparks;
		Drops.Count = 24;
		Drops.Speed = { 4.0, 9.0 };
		Drops.Life = { 0.3, 0.6 };
		Drops.Size = { 0.09, 0.14 };
		Drops.Color = C.Inner;
		Drops.ColorEnd = C.Color;
		Drops.Intensity = 1.0;
		Out.Add(Drops);
	}

	void PowerUpBurst(const FVector& KartCm, bool bRocket, const FVector& KartVelocityCm, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Burst;
		Burst.Field = bRocket ? EField::Fire : EField::Flare;
		Burst.Position = KartCm + FVector(0.0, 0.0, 80.0);
		Burst.Count = 26;
		Burst.Spread = UE_DOUBLE_PI;
		Burst.Speed = { 2.0, 7.0 };
		Burst.Life = { 0.3, 0.6 };
		Burst.Size = { 0.35, 0.7 };
		Burst.SizeEnd = bRocket ? 1.6 : 0.3;
		Burst.Color = bRocket ? Hex(TEXT("ffd27a")) : FLinearColor::White;
		Burst.ColorEnd = bRocket ? Hex(TEXT("ff3d1f")) : Hex(TEXT("ff4fd8"));
		Burst.Intensity = 2.8;
		Burst.Jitter = 0.7;
		Burst.InheritCm = KartVelocityCm * 0.8;
		Out.Add(Burst);
		Out.Add(ShockRing(KartCm + FVector(0.0, 0.0, 8.0), 0.45, 1.2, 5.0, Burst.Color, bRocket ? Burst.ColorEnd : Hex(TEXT("7ad8ff")), 2.2,
			KartVelocityCm * 0.8));
	}

	void PopupBurst(const FVector& Cm, const FItemColors& Colors, const FVector& InheritCm, TArray<FKartFxEmit>& Out)
	{
		Poof(Cm, Colors.Emissive, 1.4, Out);
		Out.Add(FlashStar(Cm, 0.25, 2.4, 1.6, FLinearColor::White, Colors.Color, 3.5, InheritCm));
	}

	FKartFxEmit BombFuseSmoke()
	{
		static const FKartFxEmit Smoke = []
		{
			FKartFxEmit S;
			S.Field = EField::Smoke;
			S.Direction = FVector::UpVector;
			S.Spread = 0.5;
			S.Speed = { 0.3, 0.9 };
			S.Life = { 0.35, 0.7 };
			S.Size = { 0.1, 0.16 };
			S.SizeEnd = 2.6;
			S.Color = Hex(TEXT("bab4ae"));
			S.ColorEnd = Hex(TEXT("6a6462"));
			return S;
		}();
		return Smoke;
	}

	FKartFxEmit ProjectileGlowTrail(const FItemColors& Colors, const FVector& VelocityCm, bool bSkySeeker)
	{
		FKartFxEmit Trail;
		Trail.Field = EField::Fire;
		Trail.Direction = -VelocityCm.GetSafeNormal();
		Trail.Spread = 0.25;
		Trail.Speed = { 0.3, 1.0 };
		Trail.Life = bSkySeeker ? FVector2D(0.3, 0.5) : FVector2D(0.2, 0.32);
		Trail.Size = bSkySeeker ? FVector2D(0.5, 0.7) : FVector2D(0.34, 0.48);
		Trail.SizeEnd = 0.15;
		Trail.Color = FLinearColor::LerpUsingHSV(Colors.Color, FLinearColor::White, 0.5f);
		Trail.ColorEnd = Colors.Emissive;
		Trail.Intensity = bSkySeeker ? 2.6 : 2.0;
		Trail.Jitter = 0.04;
		return Trail;
	}

	FKartFxEmit BombFuseSparks(const FVector& VelocityCm)
	{
		static const FLinearColor Hot = Hex(TEXT("fff2b0"));
		static const FLinearColor Ember = Hex(TEXT("ff6a2a"));
		FKartFxEmit Sparks;
		Sparks.Field = EField::Sparks;
		Sparks.InheritCm = VelocityCm * 0.6;
		Sparks.Direction = FVector::UpVector;
		Sparks.Spread = 1.0;
		Sparks.Speed = { 1.5, 3.8 };
		Sparks.Life = { 0.12, 0.3 };
		Sparks.Size = { 0.03, 0.06 };
		Sparks.Color = Hot;
		Sparks.ColorEnd = Ember;
		Sparks.Intensity = 3.2;
		return Sparks;
	}

	FKartFxEmit ProjectileSparks(const FLinearColor& Color, const FVector& VelocityCm)
	{
		static const FLinearColor Hot = Hex(TEXT("eaf3ff"));
		FKartFxEmit Sparks;
		Sparks.Field = EField::Sparks;
		Sparks.InheritCm = VelocityCm * 0.6;
		Sparks.Direction = -VelocityCm.GetSafeNormal();
		Sparks.Spread = 0.6;
		Sparks.Speed = { 2.0, 5.0 };
		Sparks.Life = { 0.2, 0.4 };
		Sparks.Size = { 0.04, 0.08 };
		Sparks.Color = Hot;
		Sparks.ColorEnd = Color;
		Sparks.Intensity = 2.6;
		Sparks.Jitter = 0.2;
		return Sparks;
	}

	double ProjectileSparksRate(bool bSkySeeker)
	{
		return bSkySeeker ? 40.0 : 18.0;
	}
}

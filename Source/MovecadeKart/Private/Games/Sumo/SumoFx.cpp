#include "Games/Sumo/SumoFx.h"

#include "Art/KartMaterials.h"

namespace MovecadeKart::Sumo::SumoFx
{
	using Fx::EField;
	using Fx::FKartFxEmit;

	void HitBurst(const FVector& At, const FLinearColor& A, const FLinearColor& B, double Power, bool bDash, TArray<FKartFxEmit>& Out)
	{
		for (const FLinearColor& Color : { A, B })
		{
			FKartFxEmit Sparks;
			Sparks.Field = EField::Sparks;
			Sparks.Position = At;
			Sparks.Count = FMath::RoundToInt32(6.0 + 22.0 * Power) * (bDash ? 2 : 1) / 2;
			Sparks.Spread = 1.4;
			Sparks.Speed = FVector2D(3.0, 7.0 + 5.0 * Power);
			Sparks.Life = FVector2D(0.25, 0.6);
			Sparks.Size = FVector2D(0.025, 0.05);
			Sparks.Color = FMath::Lerp(Color, FLinearColor::White, 0.55f);
			Sparks.ColorEnd = Color;
			Sparks.Intensity = 3.0;
			Sparks.Jitter = 0.1;
			Out.Add(Sparks);
		}
		FKartFxEmit Flash;
		Flash.Field = EField::Flare;
		Flash.Position = At;
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.14, 0.18);
		Flash.Size = FVector2D(1.2 + 1.4 * Power, 1.2 + 1.4 * Power);
		Flash.SizeEnd = 0.3;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = FMath::Lerp(A, B, 0.5f);
		Flash.Intensity = 2.6;
		Out.Add(Flash);
		if (!bDash && Power <= ShockRingPower)
		{
			return;
		}
		FKartFxEmit Ring;
		Ring.Field = EField::Ring;
		// Flat on the ring's surface under the hit.
		Ring.Position = FVector(At.X, At.Y, 6.0);
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.32, 0.32);
		Ring.Size = FVector2D(1.0, 1.0);
		Ring.SizeEnd = 3.4;
		Ring.Color = FLinearColor::White;
		Ring.ColorEnd = FMath::Lerp(A, B, 0.5f);
		Ring.Intensity = 2.2;
		Out.Add(Ring);
	}

	void DashBurst(const FVector& RingCm, const FVector& PuffCm, double Heading, const FLinearColor& Color, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Ring;
		Ring.Field = EField::Ring;
		Ring.Position = RingCm;
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.25, 0.25);
		Ring.Size = FVector2D(1.4, 1.4);
		Ring.SizeEnd = 2.4;
		Ring.Color = FMath::Lerp(Color, FLinearColor::White, 0.4f);
		Ring.ColorEnd = Color;
		Ring.Intensity = 1.6;
		Out.Add(Ring);
		FKartFxEmit Puff;
		Puff.Field = EField::Smoke;
		Puff.Position = PuffCm;
		Puff.Count = 8;
		Puff.Spread = 1.4;
		Puff.Direction = -FVector(FMath::Cos(Heading), FMath::Sin(Heading), -0.3).GetSafeNormal();
		Puff.Speed = FVector2D(1.0, 2.5);
		Puff.Life = FVector2D(0.35, 0.6);
		Puff.Size = FVector2D(0.18, 0.3);
		Puff.SizeEnd = 1.8;
		Puff.Color = Art::Hex(TEXT("#b9b3d9"));
		Puff.ColorEnd = Art::Hex(TEXT("#3c3760"));
		Puff.Intensity = 0.8;
		Out.Add(Puff);
	}

	void SeaSplash(const FVector& SeaCm, const FLinearColor& Color, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Spray;
		Spray.Field = EField::Sparks;
		Spray.Position = SeaCm;
		Spray.Count = 46;
		Spray.Spread = 0.45;
		Spray.Speed = FVector2D(5.0, 11.0);
		Spray.Life = FVector2D(0.7, 1.3);
		Spray.Size = FVector2D(0.04, 0.08);
		Spray.Color = Art::Hex(TEXT("#e6f2ff"));
		Spray.ColorEnd = Color;
		Spray.Intensity = 2.0;
		Spray.Jitter = 0.5;
		Out.Add(Spray);
		FKartFxEmit Foam;
		Foam.Field = EField::Smoke;
		Foam.Position = SeaCm;
		Foam.Count = 18;
		Foam.Spread = 0.7;
		Foam.Speed = FVector2D(1.5, 4.0);
		Foam.Life = FVector2D(0.8, 1.5);
		Foam.Size = FVector2D(0.5, 0.9);
		Foam.SizeEnd = 2.2;
		Foam.Color = Art::Hex(TEXT("#d7e4ff"));
		Foam.ColorEnd = Art::Hex(TEXT("#4b5d9c"));
		Foam.Intensity = 0.8;
		Foam.Jitter = 0.7;
		Out.Add(Foam);
		FKartFxEmit Ring;
		Ring.Field = EField::Ring;
		Ring.Position = SeaCm;
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.9, 0.9);
		Ring.Size = FVector2D(1.5, 1.5);
		Ring.SizeEnd = 6.0;
		Ring.Color = Art::Hex(TEXT("#cfe3ff"));
		Ring.ColorEnd = Color;
		Ring.Intensity = 1.2;
		Out.Add(Ring);
	}

	void PowerFlash(const FVector& At, const FLinearColor& Color, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Flash;
		Flash.Field = EField::Flare;
		Flash.Position = At;
		Flash.Count = 6;
		Flash.Spread = 1.4;
		Flash.Speed = FVector2D(1.0, 3.0);
		Flash.Life = FVector2D(0.3, 0.6);
		Flash.Size = FVector2D(0.4, 0.9);
		Flash.SizeEnd = 0.2;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = Color;
		Flash.Intensity = 2.2;
		Out.Add(Flash);
	}

	void BandSplash(const FVector& SeaCm, double Scale, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Spray;
		Spray.Field = EField::Sparks;
		Spray.Position = SeaCm;
		Spray.Count = FMath::RoundToInt32(10 * Scale);
		Spray.Spread = 0.5;
		Spray.Speed = FVector2D(3.0, 6.0) * FMath::Sqrt(Scale);
		Spray.Life = FVector2D(0.5, 0.9);
		Spray.Size = FVector2D(0.03, 0.06);
		Spray.Color = Art::Hex(TEXT("#cfe6ff"));
		Spray.ColorEnd = Art::Hex(TEXT("#4b6bd6"));
		Spray.Intensity = 1.6;
		Spray.Jitter = 0.3 * Scale;
		Out.Add(Spray);
		FKartFxEmit Foam;
		Foam.Field = EField::Smoke;
		Foam.Position = SeaCm;
		Foam.Count = FMath::RoundToInt32(6 * Scale);
		Foam.Spread = 0.8;
		Foam.Speed = FVector2D(0.6, 1.6);
		Foam.Life = FVector2D(0.6, 1.0);
		Foam.Size = FVector2D(0.25, 0.45) * Scale;
		Foam.SizeEnd = 1.8;
		Foam.Color = Art::Hex(TEXT("#b8c8f0"));
		Foam.ColorEnd = Art::Hex(TEXT("#3a4a80"));
		Foam.Intensity = 0.7;
		Foam.Jitter = 0.4 * Scale;
		Out.Add(Foam);
	}

	FKartFxEmit DashFire(const FLinearColor& Color, const FVector& Back)
	{
		FKartFxEmit Fire;
		Fire.Field = EField::Fire;
		Fire.Direction = (Back + FVector(0.0, 0.0, 0.4)).GetSafeNormal();
		Fire.Spread = 0.5;
		Fire.Speed = FVector2D(1.0, 2.5);
		Fire.Life = FVector2D(0.2, 0.38);
		Fire.Size = FVector2D(0.35, 0.55);
		Fire.SizeEnd = 0.15;
		Fire.Color = FMath::Lerp(Color, FLinearColor::White, 0.5f);
		Fire.ColorEnd = Color;
		Fire.Intensity = 2.6;
		Fire.Jitter = 0.3;
		return Fire;
	}

	FKartFxEmit SkirtSparks(const FLinearColor& Color, const FVector& Back)
	{
		// Parsed once: the stream is refreshed every frame.
		static const FLinearColor Hot = Art::Hex(TEXT("#fff1c4"));
		FKartFxEmit Sparks;
		Sparks.Field = EField::Sparks;
		Sparks.Direction = (Back + FVector(0.0, 0.0, 0.5)).GetSafeNormal();
		Sparks.Spread = 0.7;
		Sparks.Speed = FVector2D(2.0, 5.0);
		Sparks.Life = FVector2D(0.15, 0.35);
		Sparks.Size = FVector2D(0.02, 0.04);
		Sparks.Color = Hot;
		Sparks.ColorEnd = Color;
		Sparks.Intensity = 2.5;
		Sparks.Jitter = 0.4;
		return Sparks;
	}
}

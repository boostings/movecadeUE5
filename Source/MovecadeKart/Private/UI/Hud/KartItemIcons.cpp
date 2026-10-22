#include "UI/Hud/KartItemIcons.h"

#include "FX/KartFx.h"
#include "UI/KartUIPainter.h"
#include "UI/KartWidgets.h"

using namespace KartUI;
using MovecadeKart::EKartItem;

// Every icon is drawn in icon units: multiples of S (about the icon's width) around C. Each item's shape is a
// table of named unit coordinates below; the functions only paint them.

namespace
{
	using MovecadeKart::Fx::Hex;

	/** A point in icon units. */
	struct FUnit
	{
		double X;
		double Y;
	};

	FVector2D At(const FVector2D& C, const FUnit& U, float S)
	{
		return C + FVector2D(U.X * S, U.Y * S);
	}

	/** The buzz disc (Shot, and the base of Seeker and Sky Seeker) seen from above. */
	namespace DiscArt
	{
		constexpr float Radius = 0.36f;
		constexpr double SpinSpeed = 3.0;
		/** Eight swept teeth round the rim, each a triangle from the rim out and back. */
		constexpr int32 Teeth = 8;
		constexpr double ToothBase = 0.9;
		constexpr double ToothTip = 1.3;
		constexpr double ToothTipSweep = 0.18;
		constexpr double ToothBack = 0.95;
		constexpr double ToothBackSweep = 0.42;
		constexpr float RimWidth = 0.04f;
		constexpr float BodyRadius = 0.8f;
		constexpr float HubRadius = 0.28f;
		constexpr float HubOutline = 0.025f;
		constexpr FUnit ShineOffset{ -0.4, -0.42 };
		constexpr float ShineRadius = 0.16f;
	}

	/** Nitro: a teardrop flame pointing up, orange outside and yellow inside. */
	namespace FlameArt
	{
		constexpr double TipY = -0.48;
		constexpr double CentreY = 0.12;
		constexpr double Radius = 0.3;
		constexpr double ArcFrom = -20.0;
		constexpr double ArcTo = 200.0;
		constexpr int32 ArcSteps = 14;
		constexpr float CoreScale = 0.62f;
	}

	namespace SeekerArt
	{
		/** The reticle's arms on the hub. */
		constexpr double ArmLength = 0.2;
		constexpr float ArmWidth = 0.03f;
	}

	namespace SkySeekerArt
	{
		constexpr int32 Rotors = 4;
		constexpr double RotorDistance = 0.34;
		constexpr float RotorRadius = 0.13f;
		constexpr float RotorOutline = 0.03f;
		constexpr float DiscScale = 0.72f;
	}

	/** Banana: drawn as a traffic cone, an orange cone with two white bands on a dark foot. */
	namespace ConeArt
	{
		constexpr FUnit FootPos{ -0.36, 0.3 };
		constexpr FUnit FootSize{ 0.72, 0.12 };
		constexpr float FootRound = 0.03f;
		constexpr FUnit Tip{ 0.0, -0.46 };
		constexpr FUnit BaseLeft{ -0.27, 0.3 };
		constexpr FUnit BaseRight{ 0.27, 0.3 };
		/** The bands, as fractions of the way from the tip to the base. */
		constexpr float Band1From = 0.32f, Band1To = 0.45f;
		constexpr float Band2From = 0.62f, Band2To = 0.76f;
		constexpr float OutlineWidth = 0.035f;
	}

	/** Triple Nitro: three smaller flames. */
	namespace TripleFlameArt
	{
		constexpr FUnit Left{ -0.24, 0.1 };
		constexpr FUnit Right{ 0.24, 0.1 };
		constexpr FUnit Middle{ 0.0, -0.12 };
		constexpr float SideScale = 0.6f;
		constexpr float MiddleScale = 0.66f;
	}

	/** Prism: a cut gem cycling through the rainbow, its crown lit. */
	namespace PrismArt
	{
		constexpr double HueSpeed = 180.0;
		constexpr uint8 Saturation = 170;
		constexpr FUnit Bottom{ 0.0, 0.46 };
		constexpr FUnit GirdleLeft{ -0.42, -0.1 };
		constexpr FUnit GirdleRight{ 0.42, -0.1 };
		constexpr FUnit TableLeft{ -0.22, -0.3 };
		constexpr FUnit TableRight{ 0.22, -0.3 };
		constexpr FUnit FacetLeft{ -0.1, -0.1 };
		constexpr FUnit FacetRight{ 0.1, -0.1 };
		constexpr float OutlineWidth = 0.035f;
		constexpr float GirdleWidth = 0.025f;
		constexpr float FacetWidth = 0.02f;
	}

	/** Lightning: a bolt in six points, filled as two convex halves. */
	namespace BoltArt
	{
		constexpr FUnit Points[] = { { 0.12, -0.5 }, { -0.28, 0.06 }, { -0.02, 0.06 }, { -0.14, 0.5 }, { 0.3, -0.1 }, { 0.04, -0.1 } };
		constexpr float OutlineWidth = 0.035f;
	}

	/** Ink: a splat of four blobs and a shine. */
	namespace InkArt
	{
		struct FBlob
		{
			FUnit Centre;
			float Radius;
		};
		constexpr FBlob Blobs[] = { { { 0.0, -0.05 }, 0.3f }, { { -0.24, 0.1 }, 0.16f }, { { 0.26, 0.12 }, 0.14f }, { { 0.05, 0.32 }, 0.1f } };
		constexpr FUnit Shine{ -0.1, -0.14 };
		constexpr float ShineRadius = 0.06f;
	}

	/** Bomb: a round bomb with a cap, a curled fuse and a fizzing spark. */
	namespace BombArt
	{
		constexpr FUnit BodyCentre{ 0.0, 0.08 };
		constexpr float BodyRadius = 0.36f;
		constexpr float BodyOutline = 0.03f;
		constexpr FUnit CapPos{ -0.08, -0.36 };
		constexpr FUnit CapSize{ 0.16, 0.12 };
		constexpr float CapRound = 0.03f;
		constexpr FUnit Fuse[] = { { 0.0, -0.36 }, { 0.1, -0.44 }, { 0.2, -0.42 } };
		constexpr float FuseWidth = 0.04f;
		constexpr FUnit Spark{ 0.24, -0.44 };
		constexpr float SparkOuter = 0.12f;
		constexpr float SparkInner = 0.05f;
		constexpr float FizzSpeed = 30.f;
		constexpr double SparkSpin = 200.0;
		constexpr FUnit Shine{ -0.13, -0.02 };
		constexpr float ShineRadius = 0.07f;
	}

	/** Rocket: a body pointing right with a nose cone, a stripe, two fins and a porthole. */
	namespace RocketArt
	{
		constexpr double Length = 0.7;
		constexpr double Girth = 0.36;
		constexpr double Tail = -0.4;
		constexpr double BodyLength = 0.55;
		constexpr float BodyRound = 0.06f;
		constexpr float OutlineWidth = 0.03f;
		constexpr double NoseBase = 0.15;
		constexpr double NoseTip = 0.46;
		constexpr double StripeX = -0.1;
		constexpr double StripeWidth = 0.08;
		/** The fins sweep back from the tail; their tips sit out at this fraction of the girth. */
		constexpr double FinTipX = -0.5;
		constexpr double FinReach = 0.95;
		constexpr double FinRootX = -0.24;
		constexpr FUnit Porthole{ 0.05, 0.0 };
		constexpr float PortholeRadius = 0.075f;
		constexpr float PortholeOutline = 0.025f;
	}

	void Disc(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Body, double Now)
	{
		using namespace DiscArt;
		const float R = S * Radius;
		const double Spin = Now * SpinSpeed;
		for (int32 k = 0; k < Teeth; ++k)
		{
			const double A = Spin + 2.0 * PI * k / static_cast<double>(Teeth);
			const FVector2D Out(FMath::Cos(A), FMath::Sin(A));
			const FVector2D Along(-Out.Y, Out.X);
			P.Polygon({ C + Out * R * ToothBase, C + Out * R * ToothTip + Along * R * ToothTipSweep, C + Out * R * ToothBack + Along * R * ToothBackSweep }, Color::White);
		}
		P.Circle(C, R, Color::White, Color::Night, S * RimWidth);
		P.Circle(C, R * BodyRadius, Body);
		P.Circle(C, R * HubRadius, Color::White, Color::Night, S * HubOutline);
		P.Circle(C + FVector2D(R * ShineOffset.X, R * ShineOffset.Y), R * ShineRadius, Color::WithAlpha(Color::White, 0.7f));
	}

	/** The tip, then Steps + 1 points around an arc: a teardrop when the arc opens towards the tip. */
	TArray<FVector2D, TInlineAllocator<16>> Teardrop(const FVector2D& Tip, const FVector2D& C, double R, double From, double To, int32 Steps)
	{
		TArray<FVector2D, TInlineAllocator<16>> Points;
		Points.Reserve(Steps + 2);
		Points.Add(Tip);
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double A = FMath::DegreesToRadians(FMath::Lerp(From, To, static_cast<double>(i) / Steps));
			Points.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		return Points;
	}

	void Flame(const FKartPainter& P, const FVector2D& C, float S)
	{
		using namespace FlameArt;
		auto Drop = [&](float Scale, const FLinearColor& Fill)
		{
			P.Polygon(Teardrop(C + FVector2D(0.0, S * TipY * Scale), C + FVector2D(0.0, S * CentreY * Scale), S * Radius * Scale, ArcFrom, ArcTo, ArcSteps), Fill);
		};
		static const FLinearColor Outer = Hex(TEXT("ff6a2a"));
		Drop(1.f, Outer);
		Drop(CoreScale, Color::Sun);
	}

	void Seeker(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Tint, double Now)
	{
		using namespace SeekerArt;
		Disc(P, C, S, Tint, Now);
		P.Lines({ C + FVector2D(-S * ArmLength, 0.0), C + FVector2D(S * ArmLength, 0.0) }, Color::Night, S * ArmWidth);
		P.Lines({ C + FVector2D(0.0, -S * ArmLength), C + FVector2D(0.0, S * ArmLength) }, Color::Night, S * ArmWidth);
	}

	void SkySeeker(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Tint, double Now)
	{
		using namespace SkySeekerArt;
		for (int32 k = 0; k < Rotors; ++k)
		{
			const double A = PI * 0.25 + PI * 0.5 * k;
			const FVector2D Rotor = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * S * RotorDistance;
			P.Circle(Rotor, S * RotorRadius, Color::WithAlpha(Color::White, 0.35f), Color::White, S * RotorOutline);
		}
		Disc(P, C, S * DiscScale, Tint, Now);
	}

	void Cone(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Tint)
	{
		using namespace ConeArt;
		P.Box(At(C, FootPos, S), FVector2D(S * FootSize.X, S * FootSize.Y), Color::Night, S * FootRound);
		const FVector2D TipAt = At(C, Tip, S);
		const FVector2D L = At(C, BaseLeft, S), R = At(C, BaseRight, S);
		P.Polygon({ TipAt, R, L }, Tint);
		auto Band = [&](float From, float To)
		{
			P.Polygon({ FMath::Lerp(TipAt, L, From), FMath::Lerp(TipAt, R, From), FMath::Lerp(TipAt, R, To), FMath::Lerp(TipAt, L, To) }, Color::White);
		};
		Band(Band1From, Band1To);
		Band(Band2From, Band2To);
		P.Lines({ TipAt, R, L }, Color::Night, S * OutlineWidth, true);
	}

	void TripleFlame(const FKartPainter& P, const FVector2D& C, float S)
	{
		using namespace TripleFlameArt;
		Flame(P, At(C, Left, S), S * SideScale);
		Flame(P, At(C, Right, S), S * SideScale);
		Flame(P, At(C, Middle, S), S * MiddleScale);
	}

	void Prism(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace PrismArt;
		const FLinearColor Hue = FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(Now * HueSpeed, 256.0)), Saturation, 255);
		const FVector2D BottomAt = At(C, Bottom, S);
		const FVector2D L = At(C, GirdleLeft, S), R = At(C, GirdleRight, S);
		const FVector2D TL = At(C, TableLeft, S), TR = At(C, TableRight, S);
		P.Polygon({ TL, TR, R, BottomAt, L }, Hue);
		P.Polygon({ TL, TR, R, L }, Color::WithAlpha(Color::White, 0.45f));
		P.Lines({ TL, TR, R, BottomAt, L }, Color::Night, S * OutlineWidth, true);
		P.Lines({ L, R }, Color::Night, S * GirdleWidth);
		P.Lines({ At(C, FacetLeft, S), BottomAt, At(C, FacetRight, S) }, Color::WithAlpha(Color::Night, 0.5f), S * FacetWidth);
	}

	void Bolt(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Tint)
	{
		using namespace BoltArt;
		FVector2D Bolt[UE_ARRAY_COUNT(Points)];
		for (int32 i = 0; i < UE_ARRAY_COUNT(Points); ++i)
		{
			Bolt[i] = At(C, Points[i], S);
		}
		// Two convex halves.
		P.Polygon({ Bolt[0], Bolt[1], Bolt[2], Bolt[5] }, Tint);
		P.Polygon({ Bolt[5], Bolt[2], Bolt[3], Bolt[4] }, Tint);
		P.Lines(Bolt, Color::Night, S * OutlineWidth, true);
	}

	void Ink(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Tint)
	{
		using namespace InkArt;
		for (const FBlob& Blob : Blobs)
		{
			P.Circle(At(C, Blob.Centre, S), S * Blob.Radius, Tint);
		}
		P.Circle(At(C, Shine, S), S * ShineRadius, Color::WithAlpha(Color::White, 0.55f));
	}

	void Bomb(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Tint, double Now)
	{
		using namespace BombArt;
		static const FLinearColor FuseColor = Hex(TEXT("c99a5a"));
		P.Circle(At(C, BodyCentre, S), S * BodyRadius, Tint, Color::White, S * BodyOutline);
		P.Box(At(C, CapPos, S), FVector2D(S * CapSize.X, S * CapSize.Y), Color::NavyDim, S * CapRound);
		P.Lines({ At(C, Fuse[0], S), At(C, Fuse[1], S), At(C, Fuse[2], S) }, FuseColor, S * FuseWidth);
		const float Fizz = 0.8f + 0.3f * FMath::Sin(static_cast<float>(Now) * FizzSpeed);
		KartWidgets::Star(P, At(C, Spark, S), S * SparkOuter * Fizz, S * SparkInner, Color::Sun, static_cast<float>(Now * SparkSpin));
		P.Circle(At(C, Shine, S), S * ShineRadius, Color::WithAlpha(Color::White, 0.6f));
	}

	void Rocket(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Tint)
	{
		using namespace RocketArt;
		static const FLinearColor PortholeColor = Hex(TEXT("5fd6ff"));
		const FVector2D Body(S * Length, S * Girth);
		const double Half = Body.Y * 0.5;
		const double FinOut = Body.Y * FinReach;
		P.Box(C + FVector2D(S * Tail, -Half),FVector2D(S * BodyLength, Body.Y), Tint, S * BodyRound, Color::Night, S * OutlineWidth);
		P.Polygon({ C + FVector2D(S * NoseBase, -Half), C + FVector2D(S * NoseTip, 0.0), C + FVector2D(S * NoseBase, Half) }, Tint);
		P.Box(C + FVector2D(S * StripeX, -Half), FVector2D(S * StripeWidth, Body.Y), Color::Hot);
		P.Polygon({ C + FVector2D(S * Tail, -Half), C + FVector2D(S * FinTipX, -FinOut), C + FVector2D(S * FinRootX, -Half) }, Color::Hot);
		P.Polygon({ C + FVector2D(S * Tail, Half), C + FVector2D(S * FinTipX, FinOut), C + FVector2D(S * FinRootX, Half) }, Color::Hot);
		// A porthole, not an eye.
		P.Circle(At(C, Porthole, S), S * PortholeRadius, PortholeColor, Color::Night, S * PortholeOutline);
	}
}

namespace KartItemIcons
{
	FLinearColor Tint(EKartItem Item)
	{
		// Built once: the HUD asks for these every frame.
		static const FLinearColor Shot = Hex(TEXT("37c45a")), Seeker = Hex(TEXT("ec3b4b")), SkySeeker = Hex(TEXT("3b7cf0")),
			Banana = Hex(TEXT("ff7a22")), Nitro = Hex(TEXT("ff8a2a")), Prism = Hex(TEXT("ffe25a")), Lightning = Hex(TEXT("ffd21f")),
			Ink = Hex(TEXT("4a3f7a")), Bomb = Hex(TEXT("2b2b38")), Rocket = Hex(TEXT("f2f4f8"));
		switch (Item)
		{
		case EKartItem::Shot: return Shot;
		case EKartItem::Seeker: return Seeker;
		case EKartItem::SkySeeker: return SkySeeker;
		case EKartItem::Banana: return Banana;
		case EKartItem::Nitro:
		case EKartItem::TripleNitro: return Nitro;
		case EKartItem::Prism: return Prism;
		case EKartItem::Lightning: return Lightning;
		case EKartItem::Ink: return Ink;
		case EKartItem::Bomb: return Bomb;
		case EKartItem::Rocket: return Rocket;
		default: return Color::White;
		}
	}

	void Draw(const FKartPainter& P, EKartItem Item, const FVector2D& C, float S, double Now)
	{
		switch (Item)
		{
		case EKartItem::Shot: Disc(P, C, S, Tint(Item), Now); break;
		case EKartItem::Seeker: Seeker(P, C, S, Tint(Item), Now); break;
		case EKartItem::SkySeeker: SkySeeker(P, C, S, Tint(Item), Now); break;
		case EKartItem::Banana: Cone(P, C, S, Tint(Item)); break;
		case EKartItem::Nitro: Flame(P, C, S); break;
		case EKartItem::TripleNitro: TripleFlame(P, C, S); break;
		case EKartItem::Prism: Prism(P, C, S, Now); break;
		case EKartItem::Lightning: Bolt(P, C, S, Tint(Item)); break;
		case EKartItem::Ink: Ink(P, C, S, Tint(Item)); break;
		case EKartItem::Bomb: Bomb(P, C, S, Tint(Item), Now); break;
		case EKartItem::Rocket: Rocket(P, C, S, Tint(Item)); break;
		default: break;
		}
	}
}

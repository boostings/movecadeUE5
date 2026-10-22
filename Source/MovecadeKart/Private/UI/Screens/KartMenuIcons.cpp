#include "UI/Screens/KartMenuIcons.h"

#include "UI/KartUIPainter.h"
#include "UI/KartWidgets.h"

using namespace KartUI;

// Every icon is drawn in icon units: multiples of S (the Size argument) around C (the Centre). Each icon's
// shape lives in its own table of named unit coordinates below; the functions only paint them.

namespace
{
	/** A point or a size in icon units. */
	struct FUnit
	{
		double X;
		double Y;
	};

	FVector2D Scaled(const FUnit& U, float S)
	{
		return FVector2D(U.X * S, U.Y * S);
	}

	/** Race: a chequered flag on a pole, its columns waving. */
	namespace FlagArt
	{
		constexpr FUnit PolePos{ -0.62, -0.7 };
		constexpr FUnit PoleSize{ 0.08, 1.5 };
		constexpr float PoleRound = 0.04f;
		constexpr int32 Cols = 6;
		constexpr int32 Rows = 4;
		constexpr double Cell = 0.2;
		constexpr FUnit ClothPos{ -0.54, -0.66 };
		/** Cells overlap by half a unit so the chequer has no seams. */
		constexpr double CellOverlap = 0.5;
		constexpr double WaveSpeed = 4.0;
		constexpr double WavePhase = 0.8;
		constexpr double WaveHeight = 0.06;
	}

	/** Party Mode: two phones, and a signal arc pulsing above them. */
	namespace PhonesArt
	{
		constexpr FUnit FirstPos{ -0.62, -0.5 };
		/** The second phone sits right and lower. */
		constexpr FUnit NextOffset{ 0.66, 0.18 };
		constexpr FUnit BodySize{ 0.56, 0.98 };
		constexpr float BodyRound = 0.1f;
		constexpr FUnit ScreenPos{ 0.06, 0.1 };
		constexpr FUnit ScreenSize{ 0.44, 0.7 };
		constexpr float ScreenRound = 0.05f;
		constexpr FUnit ButtonPos{ 0.28, 0.88 };
		constexpr float ButtonRadius = 0.04f;
		constexpr int32 Rings = 3;
		constexpr FUnit RingPos{ -0.1, -0.92 };
		constexpr double RingSize = 0.2;
		/** Each ring grows by this on every side. */
		constexpr double RingGrow = 0.12;
		constexpr float RingRound = 0.2f;
		constexpr float RingWidth = 0.045f;
		constexpr float PulseSpeed = 4.f;
	}

	/** Settings: a turning gear. */
	namespace GearArt
	{
		constexpr int32 Teeth = 8;
		/** Four points per tooth: two out on the tooth, two down in the gap. */
		constexpr int32 Points = Teeth * 4;
		constexpr double ToothRadius = 0.8;
		constexpr double GapRadius = 0.6;
		constexpr float HoleRadius = 0.3f;
		constexpr double SpinSpeed = 0.6;
	}

	/** Credits: a rocking star with a sunny centre. */
	namespace StarArt
	{
		constexpr float Outer = 0.85f;
		constexpr float Inner = 0.38f;
		constexpr float CoreOuter = 0.5f;
		constexpr float CoreInner = 0.22f;
		constexpr double RockSpeed = 1.5;
		constexpr double RockDegrees = 12.0;
	}

	/** Quit: a door with an arrow coming out of it. */
	namespace DoorArt
	{
		constexpr FUnit FramePos{ -0.5, -0.75 };
		constexpr FUnit FrameSize{ 0.7, 1.5 };
		constexpr float FrameRound = 0.08f;
		constexpr FUnit PanelPos{ -0.42, -0.66 };
		constexpr FUnit PanelSize{ 0.54, 1.32 };
		constexpr float PanelRound = 0.05f;
		constexpr double ArrowX = 0.42;
		constexpr float ArrowSize = 0.32f;
		constexpr double ArrowNudge = 0.06;
		constexpr double ArrowNudgeSpeed = 5.0;
		constexpr FUnit ShaftPos{ -0.05, -0.08 };
		constexpr FUnit ShaftSize{ 0.35, 0.16 };
	}

	/** Bowling: a ball rolling into three pins on a lane. */
	namespace BowlingArt
	{
		constexpr int32 Pins = 3;
		constexpr FUnit FirstPin{ 0.18, -0.1 };
		constexpr double PinSpacing = 0.26;
		/** The middle pin stands further back. */
		constexpr double MiddlePinLift = 0.12;
		constexpr FUnit HeadOffset{ 0.0, -0.42 };
		constexpr float HeadRadius = 0.09f;
		constexpr FUnit BodyOffset{ -0.1, -0.36 };
		constexpr FUnit BodySize{ 0.2, 0.66 };
		constexpr float BodyRound = 0.09f;
		constexpr FUnit StripeOffset{ -0.1, -0.26 };
		constexpr FUnit StripeSize{ 0.2, 0.06 };
		constexpr float OutlineWidth = 0.03f;
		constexpr double RollSpeed = 0.8;
		constexpr FUnit BallStart{ -0.9, 0.28 };
		constexpr double RollDistance = 0.5;
		constexpr float BallRadius = 0.34f;
		constexpr int32 Holes = 3;
		constexpr double HoleSpin = 4.0;
		constexpr double HoleSpacing = 0.6;
		constexpr double HoleRing = 0.16;
		constexpr float HoleRadius = 0.045f;
		constexpr FUnit LanePos{ -0.95, 0.64 };
		constexpr FUnit LaneSize{ 1.9, 0.06 };
		constexpr float LaneRound = 0.03f;
	}

	/** Bullseye Bash: a dartboard with a dart wobbling in it. */
	namespace DartsArt
	{
		constexpr float OuterRing = 0.9f;
		constexpr float RingStep = 0.15f;
		constexpr double WobbleSpeed = 9.0;
		constexpr double WobbleHeight = 0.03;
		constexpr FUnit ShaftPos{ 0.05, -0.08 };
		constexpr FUnit ShaftSize{ 0.7, 0.07 };
		constexpr float ShaftRound = 0.03f;
		/** The flight: a square at the shaft's end. */
		constexpr double FlightLeft = 0.62;
		constexpr double FlightRight = 0.85;
		constexpr double FlightTop = -0.2;
		constexpr double FlightBottom = 0.05;
	}

	/** Samurai Slice: a fruit falling apart under a blade stroke. */
	namespace FruitArt
	{
		constexpr double PartMax = 0.08;
		constexpr double PartSpeed = 3.0;
		constexpr FUnit HalfOffset{ 0.1, 0.05 };
		constexpr float HalfRadius = 0.5f;
		constexpr float OutlineWidth = 0.04f;
		/** The blade's stroke, a thin slanted quad. */
		constexpr FUnit Stroke[] = { { -0.9, 0.6 }, { 0.8, -0.7 }, { 0.9, -0.62 }, { -0.82, 0.68 } };
	}

	/** Sky Stacker: a swaying tower of blocks, narrowing as it rises. */
	namespace TowerArt
	{
		constexpr double BaseWidth = 1.2;
		constexpr double WidthStep = 0.12;
		constexpr double Lean = 0.03;
		constexpr double SwaySpeed = 1.6;
		constexpr double Sway = 0.03;
		constexpr double Floor = 0.62;
		constexpr double BlockPitch = 0.26;
		constexpr double BlockHeight = 0.24;
		constexpr float BlockRound = 0.04f;
		constexpr float OutlineWidth = 0.03f;
	}

	/** Sumo Bumpers: two bumper pucks colliding on a ring, sparks between them. */
	namespace PucksArt
	{
		constexpr double BumpSpeed = 2.4;
		constexpr double BumpMax = 0.12;
		constexpr FUnit RingCentre{ 0.0, 0.18 };
		constexpr float RingRadius = 0.92f;
		constexpr float RingWidth = 0.05f;
		constexpr float InnerRingRadius = 0.5f;
		constexpr float InnerRingWidth = 0.03f;
		constexpr double PuckX = 0.36;
		constexpr double PuckY = 0.05;
		constexpr float PuckRadius = 0.33f;
		constexpr float PuckOutline = 0.05f;
		/** The highlight on each puck's top. */
		constexpr double ShineY = -0.05;
		constexpr float ShineRadius = 0.12f;
		constexpr int32 Sparks = 4;
		constexpr double SparkFirstAngle = -0.9;
		constexpr double SparkAngleStep = 0.6;
		constexpr double SparkInner = 0.12;
		constexpr double SparkOuter = 0.34;
		constexpr float SparkWidth = 0.05f;
	}

	/** Home Run Derby: a bat swinging through a ball that streaks away. */
	namespace BatArt
	{
		constexpr double SwingSpeed = 2.4;
		constexpr double SwingRange = 0.25;
		constexpr FUnit BallOffset{ 0.42, -0.38 };
		/** The streak: three fading puffs behind the ball. */
		constexpr int32 Puffs = 3;
		constexpr FUnit PuffStep{ 0.16, -0.13 };
		constexpr float PuffRadius = 0.15f;
		constexpr float PuffShrink = 0.03f;
		constexpr float BallRadius = 0.2f;
		constexpr float BallOutline = 0.04f;
		constexpr FUnit SeamFrom{ -0.1, -0.08 };
		constexpr FUnit SeamTo{ 0.02, 0.12 };
		constexpr float SeamWidth = 0.03f;
		constexpr FUnit Knob{ -0.55, 0.62 };
		constexpr double RestAngle = -0.95;
		constexpr double HandleHalfWidth = 0.04;
		constexpr double BarrelLength = 1.15;
		constexpr double TipLength = 1.22;
		constexpr double BarrelHalfWidth = 0.12;
		constexpr float KnobRadius = 0.07f;
	}

	/** Fishing Frenzy: a bobber dipping on the ripples, its line up out of frame. */
	namespace BobberArt
	{
		constexpr double DipSpeed = 2.4;
		constexpr double DipDepth = 0.12;
		constexpr int32 Ripples = 3;
		constexpr double RippleRadius = 0.45;
		constexpr double RippleStep = 0.22;
		constexpr double RippleGrow = 0.1;
		constexpr double RippleSpeed = 0.6;
		constexpr double RippleY = 0.45;
		constexpr float RippleWidth = 0.05f;
		constexpr double LineTop = -0.95;
		constexpr double LineBottom = -0.3;
		constexpr float LineWidth = 0.03f;
		constexpr double FloatY = 0.05;
		constexpr float FloatRadius = 0.42f;
		constexpr float FloatOutline = 0.05f;
		/** The red cap: the float's top half as a five-point fan. */
		constexpr FUnit Cap[] = { { -0.42, 0.05 }, { -0.3, -0.27 }, { 0.0, -0.37 }, { 0.3, -0.27 }, { 0.42, 0.05 } };
		constexpr FUnit StemPos{ -0.04, -0.62 };
		constexpr FUnit StemSize{ 0.08, 0.3 };
		constexpr float StemRound = 0.03f;
	}
}

namespace KartMenuIcons
{
	void RaceFlag(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace FlagArt;
		P.Box(C + Scaled(PolePos, S), Scaled(PoleSize, S), Color::Navy, PoleRound * S);
		const double CellSize = Cell * S;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Col = 0; Col < Cols; ++Col)
			{
				const double Wave = FMath::Sin(Now * WaveSpeed - Col * WavePhase) * WaveHeight * S * (Col + 1) / Cols;
				const FVector2D Pos(C.X + ClothPos.X * S + Col * CellSize, C.Y + ClothPos.Y * S + Row * CellSize + Wave);
				P.Box(Pos, FVector2D(CellSize + CellOverlap, CellSize + CellOverlap), (Row + Col) % 2 == 0 ? Color::Navy : Color::White);
			}
		}
	}

	void PartyPhones(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace PhonesArt;
		for (int32 i = 0; i < 2; ++i)
		{
			const FVector2D Pos = C + FVector2D(FirstPos.X * S + i * NextOffset.X * S, FirstPos.Y * S + i * NextOffset.Y * S);
			P.Box(Pos, Scaled(BodySize, S), Color::Navy, BodyRound * S);
			P.Box(Pos + Scaled(ScreenPos, S), Scaled(ScreenSize, S), i == 0 ? Color::Sky : Color::Hot, ScreenRound * S);
			P.Circle(Pos + Scaled(ButtonPos, S), ButtonRadius * S, Color::White);
		}
		for (int32 Ring = 0; Ring < Rings; ++Ring)
		{
			const float A = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * PulseSpeed - Ring);
			P.Box(C + FVector2D(RingPos.X * S - Ring * RingGrow * S, RingPos.Y * S - Ring * RingGrow * S),
				FVector2D(RingSize * S + Ring * (2.0 * RingGrow) * S, RingSize * S + Ring * (2.0 * RingGrow) * S),
				FLinearColor::Transparent, RingRound * S, Color::WithAlpha(Color::Navy, A), RingWidth * S);
		}
	}

	void SettingsGear(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace GearArt;
		TArray<FVector2D, TInlineAllocator<Points>> Gear;
		const double Spin = Now * SpinSpeed;
		for (int32 i = 0; i < Points; ++i)
		{
			const double A = Spin + i * UE_DOUBLE_PI * 2.0 / Points;
			const double R = (i % 4 < 2) ? ToothRadius * S : GapRadius * S;
			Gear.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
		}
		P.Polygon(Gear, Color::Navy);
		P.Circle(C, HoleRadius * S, Color::Paper);
	}

	void CreditsStar(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace StarArt;
		const float Rock = static_cast<float>(FMath::Sin(Now * RockSpeed) * RockDegrees);
		KartWidgets::Star(P, C, Outer * S, Inner * S, Color::Navy, Rock);
		KartWidgets::Star(P, C, CoreOuter * S, CoreInner * S, Color::Sun, Rock);
	}

	void QuitDoor(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace DoorArt;
		P.Box(C + Scaled(FramePos, S), Scaled(FrameSize, S), Color::Navy, FrameRound * S);
		P.Box(C + Scaled(PanelPos, S), Scaled(PanelSize, S), Color::PaperShade, PanelRound * S);
		KartWidgets::Arrow(P, C + FVector2D(ArrowX * S + FMath::Sin(Now * ArrowNudgeSpeed) * ArrowNudge * S, 0.0), ArrowSize * S, true, Color::Hot);
		P.Box(C + Scaled(ShaftPos, S), Scaled(ShaftSize, S), Color::Hot);
	}

	void BowlingPins(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace BowlingArt;
		for (int32 i = 0; i < Pins; ++i)
		{
			const FVector2D Pin = C + FVector2D(FirstPin.X * S + i * PinSpacing * S, FirstPin.Y * S - (i == 1 ? MiddlePinLift * S : 0.0));
			P.Circle(Pin + Scaled(HeadOffset, S), HeadRadius * S, Color::White, Color::Navy, OutlineWidth * S);
			P.Box(Pin + Scaled(BodyOffset, S), Scaled(BodySize, S), Color::White, BodyRound * S, Color::Navy, OutlineWidth * S);
			P.Box(Pin + Scaled(StripeOffset, S), Scaled(StripeSize, S), Color::Hot);
		}
		const double Roll = FMath::Fmod(Now * RollSpeed, 1.0);
		const FVector2D Ball = C + FVector2D(BallStart.X * S + Roll * RollDistance * S, BallStart.Y * S);
		P.Circle(Ball, BallRadius * S, Color::Navy);
		for (int32 Hole = 0; Hole < Holes; ++Hole)
		{
			const double A = Now * HoleSpin + Hole * HoleSpacing;
			P.Circle(Ball + FVector2D(FMath::Cos(A), FMath::Sin(A)) * HoleRing * S, HoleRadius * S, Color::Sun);
		}
		P.Box(C + Scaled(LanePos, S), Scaled(LaneSize, S), Color::Navy, LaneRound * S);
	}

	void Dartboard(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace DartsArt;
		static const FLinearColor Rings[] = { Color::Navy, Color::White, Color::Hot, Color::White, Color::Mint, Color::Hot };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rings); ++i)
		{
			P.Circle(C, (OuterRing - i * RingStep) * S, Rings[i]);
		}
		const double Wobble = FMath::Sin(Now * WobbleSpeed) * WobbleHeight * S;
		P.Box(C + FVector2D(ShaftPos.X * S, ShaftPos.Y * S + Wobble), Scaled(ShaftSize, S), Color::Navy, ShaftRound * S);
		P.Polygon({ C + FVector2D(FlightLeft * S, FlightTop * S + Wobble), C + FVector2D(FlightRight * S, FlightTop * S + Wobble),
			C + FVector2D(FlightRight * S, FlightBottom * S + Wobble), C + FVector2D(FlightLeft * S, FlightBottom * S + Wobble) }, Color::Sun);
	}

	void SlicedFruit(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace FruitArt;
		const double Part = PartMax * S * (0.5 + 0.5 * FMath::Sin(Now * PartSpeed));
		P.Circle(C + FVector2D(-HalfOffset.X * S - Part, HalfOffset.Y * S), HalfRadius * S, Color::Hot, Color::Navy, OutlineWidth * S);
		P.Circle(C + FVector2D(HalfOffset.X * S + Part, -HalfOffset.Y * S), HalfRadius * S, Color::Sun, Color::Navy, OutlineWidth * S);
		P.Polygon({ C + Scaled(Stroke[0], S), C + Scaled(Stroke[1], S), C + Scaled(Stroke[2], S), C + Scaled(Stroke[3], S) }, Color::White);
	}

	void BlockTower(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace TowerArt;
		static const FLinearColor Fills[] = { Color::Navy, Color::Sky, Color::Sun, Color::Hot, Color::Mint };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Fills); ++i)
		{
			const double SwayX = FMath::Sin(Now * SwaySpeed) * Sway * S * i;
			const double W = (BaseWidth - i * WidthStep) * S;
			P.Box(C + FVector2D(-W * 0.5 + SwayX + i * Lean * S, Floor * S - (i + 1) * BlockPitch * S), FVector2D(W, BlockHeight * S), Fills[i],
				BlockRound * S, Color::Navy, OutlineWidth * S);
		}
	}

	void BumperPucks(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace PucksArt;
		const double Bump = FMath::Abs(FMath::Sin(Now * BumpSpeed)) * BumpMax * S;
		P.Circle(C + Scaled(RingCentre, S), RingRadius * S, Color::WithAlpha(Color::Navy, 0.85f), Color::Sky, RingWidth * S);
		P.Circle(C + Scaled(RingCentre, S), InnerRingRadius * S, FLinearColor::Transparent, Color::WithAlpha(Color::Sky, 0.5f), InnerRingWidth * S);
		P.Circle(C + FVector2D(-PuckX * S - Bump, PuckY * S), PuckRadius * S, Color::Hot, Color::Navy, PuckOutline * S);
		P.Circle(C + FVector2D(PuckX * S + Bump, PuckY * S), PuckRadius * S, Color::Sky, Color::Navy, PuckOutline * S);
		P.Circle(C + FVector2D(-PuckX * S - Bump, ShineY * S), ShineRadius * S, Color::White);
		P.Circle(C + FVector2D(PuckX * S + Bump, ShineY * S), ShineRadius * S, Color::White);
		for (int32 i = 0; i < Sparks; ++i)
		{
			const double A = SparkFirstAngle + i * SparkAngleStep;
			const FVector2D D(FMath::Sin(A), -FMath::Cos(A));
			P.Lines({ C + D * (SparkInner * S + Bump), C + D * (SparkOuter * S + Bump * 2.0) }, Color::Sun, SparkWidth * S);
		}
	}

	void BatAndBall(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace BatArt;
		const double Swing = FMath::Sin(Now * SwingSpeed) * SwingRange;
		const FVector2D Ball = C + Scaled(BallOffset, S);
		for (int32 i = 1; i <= Puffs; ++i)
		{
			P.Circle(Ball - FVector2D(PuffStep.X * S * i, PuffStep.Y * S * i), (PuffRadius - i * PuffShrink) * S, Color::WithAlpha(Color::Sun, 0.8f - i * 0.2f));
		}
		P.Circle(Ball, BallRadius * S, Color::White, Color::Navy, BallOutline * S);
		P.Lines({ Ball + Scaled(SeamFrom, S), Ball + Scaled(SeamTo, S) }, Color::Hot, SeamWidth * S);
		const FVector2D KnobAt = C + Scaled(Knob, S);
		const double A = RestAngle + Swing;
		const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
		const FVector2D Side(-Dir.Y, Dir.X);
		P.Polygon({ KnobAt + Side * HandleHalfWidth * S, KnobAt + Dir * BarrelLength * S + Side * BarrelHalfWidth * S, KnobAt + Dir * TipLength * S,
			KnobAt + Dir * BarrelLength * S - Side * BarrelHalfWidth * S, KnobAt - Side * HandleHalfWidth * S }, Color::Gold);
		P.Circle(KnobAt, KnobRadius * S, Color::Navy);
	}

	void Bobber(const FKartPainter& P, const FVector2D& C, float S, double Now)
	{
		using namespace BobberArt;
		const double Dip = FMath::Max(0.0, FMath::Sin(Now * DipSpeed)) * DipDepth * S;
		for (int32 i = 0; i < Ripples; ++i)
		{
			const double R = (RippleRadius + RippleStep * i + RippleGrow * FMath::Frac(Now * RippleSpeed)) * S;
			P.Lines({ C + FVector2D(-R, RippleY * S), C + FVector2D(R, RippleY * S) }, Color::WithAlpha(Color::White, 0.7f - 0.2f * i), RippleWidth * S);
		}
		P.Lines({ C + FVector2D(0.0, LineTop * S), C + FVector2D(0.0, LineBottom * S + Dip) }, Color::White, LineWidth * S);
		P.Circle(C + FVector2D(0.0, FloatY * S + Dip), FloatRadius * S, Color::White, Color::Navy, FloatOutline * S);
		TArray<FVector2D, TInlineAllocator<UE_ARRAY_COUNT(Cap)>> CapPoints;
		for (const FUnit& Point : Cap)
		{
			CapPoints.Add(C + FVector2D(Point.X * S, Point.Y * S + Dip));
		}
		P.Polygon(CapPoints, Color::Hot);
		P.Box(C + FVector2D(StemPos.X * S, StemPos.Y * S + Dip), Scaled(StemSize, S), Color::Navy, StemRound * S);
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

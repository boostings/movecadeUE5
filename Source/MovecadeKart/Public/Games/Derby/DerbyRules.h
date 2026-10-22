#pragma once

#include "CoreMinimal.h"

/**
 * Home Run Derby's rules (Docs/Port/10-derby.md): 1-4 batters take turns at the plate. Each turn is ten pitches from
 * a schedule drawn from a seeded RNG (every batter in a round faces the same ten), getting harder as it goes:
 * fastballs, then changeups, then curves. A swing is judged by when its peak lands against the ball's arrival (the
 * timing offset), where it points (aim: pull or opposite field) and how hard it is (strength). That gives the exit
 * speed, the launch and spray angles; the ball then flies with drag and backspin lift on a fixed 10 ms step until it
 * lands, clears the fence or caroms off the wall. Home runs score their distance in feet, moonshots (450 ft and
 * more) half as much again, balls in play a tenth of it. Takes and whiffs are strikes, fouls too until two; three
 * strikes end a turn early. Pure: no UObjects, no clock, no engine RNG (Mulberry32, so tests can be computed by hand).
 */
namespace MovecadeKart::Derby
{
	constexpr int32 PitchesPerTurn = 10;
	constexpr int32 StrikesPerOut = 3;
	/** Release to the front of the plate, metres. */
	constexpr double PitchDistanceM = 17.0;
	/** The pitch slows a little on its way: flight time over the constant-speed time. */
	constexpr double PitchDragFactor = 1.06;
	/** Fair territory: spray within this many degrees of dead centre. */
	constexpr double FoulLineDeg = 45.0;
	/** The fence: 330 ft down the lines, 400 ft to centre, 3 m tall. */
	constexpr double FenceLineM = 100.584;
	constexpr double FenceCentreExtraM = 21.336;
	constexpr double FenceHeightM = 3.0;
	constexpr int32 MoonshotFt = 450;
	/** The flight's step and its forces (per metre: drag and backspin lift scale with speed squared). */
	constexpr double FlightStepS = 0.01;
	constexpr double Gravity = 9.81;
	constexpr double DragK = 0.0050;
	constexpr double LiftK = 0.0016;
	constexpr double ContactHeightM = 1.0;
	constexpr double MaxFlightS = 15.0;
	/** The launch a square, perfectly timed swing gets. */
	constexpr double IdealLaunchDeg = 29.0;
	constexpr double MetresPerFoot = 0.3048;

	/** Mulberry32: a tiny 32-bit generator, the same on every machine and in a test by hand. */
	struct FDerbyRng
	{
		uint32 State = 0;
		explicit FDerbyRng(uint32 Seed = 0) : State(Seed) {}
		uint32 Next();
		/** [0, 1). */
		double Float() { return Next() / 4294967296.0; }
	};

	enum class EPitchType : uint8 { Fastball, Changeup, Curve };

	struct FPitch
	{
		int32 Index = 0;
		EPitchType Type = EPitchType::Fastball;
		double SpeedMph = 90.0;
		/** Release to the front of the plate. */
		double FlightMs = 0.0;
		/** The pitcher's set and windup before the release. */
		double WindupMs = 1500.0;
		/** -1..1: where on the bat an off-time swing tends to catch it (under: a pop-up, over: a grounder). */
		double ContactBias = 0.0;
		/** 0..1: how much it moves on its way (the curve's break, the changeup's fade); presentation only. */
		double Break = 0.0;
		/** The timing windows (ms either side of the arrival): perfect, and the edge of contact. */
		double PerfectMs = 30.0;
		double ContactMs = 165.0;
	};

	MOVECADEKART_API const TCHAR* PitchName(EPitchType Type);
	MOVECADEKART_API uint32 RoundSeed(uint32 MatchSeed, int32 Round);
	/** A turn's pitches: Count from Seed; five draws per pitch whether used or not, so the order never shifts. */
	MOVECADEKART_API TArray<FPitch> PitchSchedule(uint32 Seed, int32 Count = PitchesPerTurn);

	/** What the batter did with one pitch. */
	struct FSwing
	{
		bool bSwung = false;
		/** The swing's peak minus the ball's arrival, ms: negative is early. */
		double TimingMs = 0.0;
		/** -1 pull to left field .. +1 opposite to right field (a right-handed batter). */
		double Aim = 0.0;
		/** 0..1, how hard. */
		double Strength = 0.7;
	};

	enum class EResult : uint8
	{
		/** No swing. */
		Take,
		Whiff,
		Foul,
		/** Fair but in the park: on the grass or off the wall. */
		InPlay,
		HomeRun,
		Moonshot,
	};

	MOVECADEKART_API bool IsHomeRun(EResult Result);
	MOVECADEKART_API const TCHAR* ResultName(EResult Result);

	struct FContact
	{
		/** 0..1: 1 inside the perfect window, 0 at the edge of contact. */
		double Quality = 0.0;
		double ExitMph = 0.0;
		double LaunchDeg = 0.0;
		/** Degrees from dead centre, + toward right field. */
		double SprayDeg = 0.0;
	};

	/** The ball's path in its vertical plane: X along the ground from the plate, Y up, metres, every FlightStepS. */
	struct FFlight
	{
		TArray<FVector2D> Path;
		double DistanceM = 0.0;
		double HangS = 0.0;
		double ApexM = 0.0;
		/** The fence along this spray, and the ball's height as it got there (-1 if it never did). */
		double FenceM = 0.0;
		double HeightAtFenceM = -1.0;
		bool bCleared = false;
		/** In the park at the wall: the ball stops there (Path ends at the wall). */
		bool bOffWall = false;

		/** The ball at time T (seconds from contact), clamped to the flight. */
		FVector2D At(double T) const;
	};

	struct FOutcome
	{
		EResult Result = EResult::Take;
		FContact Contact;
		FFlight Flight;
		int32 DistanceFt = 0;
		int32 Points = 0;
		bool bStrike = false;
		double TimingMs = 0.0;
	};

	MOVECADEKART_API double FenceDistanceM(double SprayDeg);
	/** Quality from the timing offset against a pitch's windows (0 past the edge). */
	MOVECADEKART_API double TimingQuality(double TimingMs, const FPitch& Pitch);
	/** Exit speed, launch and spray for a swing that made contact. */
	MOVECADEKART_API FContact MakeContact(const FPitch& Pitch, const FSwing& Swing);
	/** Flies a ball off the bat to the ground, the fence or the wall. */
	MOVECADEKART_API FFlight SimulateFlight(double ExitMph, double LaunchDeg, double SprayDeg);
	MOVECADEKART_API int32 PointsFor(EResult Result, int32 DistanceFt);
	/** One pitch, judged. StrikesBefore decides whether a foul is a strike (not with two already). */
	MOVECADEKART_API FOutcome ResolvePitch(const FPitch& Pitch, const FSwing& Swing, int32 StrikesBefore = 0);

	// The match -------------------------------------------------------------------------------------------------

	struct FBatter
	{
		FString Id;
		int32 Score = 0;
		int32 HomeRuns = 0;
		int32 Moonshots = 0;
		int32 LongestFt = 0;
		int32 Hits = 0;
		/** Per round, the points of that turn. */
		TArray<int32> RoundScores;
	};

	struct FMatch
	{
		TArray<FBatter> Batters;
		uint32 Seed = 0;
		int32 Rounds = 1;
		int32 Round = 0;
		/** The batter up (index into Batters). */
		int32 Up = 0;
		/** Pitches thrown this turn. */
		int32 Pitch = 0;
		int32 Strikes = 0;
		/** This round's pitches (the same for every batter in it). */
		TArray<FPitch> Schedule;
		bool bComplete = false;

		const FBatter* Batter() const { return bComplete || !Batters.IsValidIndex(Up) ? nullptr : &Batters[Up]; }
		const FPitch* NextPitch() const { return bComplete || !Schedule.IsValidIndex(Pitch) ? nullptr : &Schedule[Pitch]; }
		const FBatter* Find(const FString& Id) const { return Batters.FindByPredicate([&Id](const FBatter& B) { return B.Id == Id; }); }
		int32 PitchesLeft() const { return bComplete ? 0 : PitchesPerTurn - Pitch; }
	};

	struct FRecord
	{
		bool bTurnEnded = false;
		bool bRoundEnded = false;
		bool bMatchComplete = false;
		/** The turn ended on three strikes (before the tenth pitch). */
		bool bStruckOut = false;
	};

	MOVECADEKART_API FMatch CreateMatch(const TArray<FString>& BatterIds, uint32 Seed, int32 Rounds = 1);
	/** Commits the outcome of the pitch up: the score, the count, and the turn order. */
	MOVECADEKART_API FRecord Record(FMatch& Match, const FOutcome& Outcome);
	/** Batter indices best first (score, then home runs, then the longest), ties keep the batting order. */
	MOVECADEKART_API TArray<int32> Ranking(const FMatch& Match);
	/** JS-style rounding: halves up. NaN reads as 0 and the result is clamped to int32 (the cast is undefined past it). */
	inline int32 RoundHalfUp(double X)
	{
		const double Rounded = FMath::FloorToDouble(X + 0.5);
		return FMath::IsNaN(Rounded) ? 0 : static_cast<int32>(FMath::Clamp(Rounded, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32)));
	}
}

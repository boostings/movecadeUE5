#include "Games/Bowling/BowlingSim.h"

namespace MovecadeKart::Bowling
{
	namespace
	{
		double Smoothstep(double Edge0, double Edge1, double X)
		{
			const double T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
			return T * T * (3.0 - 2.0 * T);
		}

		int32 Sign(double V)
		{
			return V < 0.0 ? -1 : (V > 0.0 ? 1 : 0);
		}
	}

	FVector2D PinPosition(int32 Id)
	{
		// Rows of 1, 2, 3, 4 pins, numbered left to right from the head pin. Clamped to the rack: a huge id would
		// overflow the row walk below and never finish.
		constexpr int32 RackPins = 10;
		Id = FMath::Clamp(Id, 1, RackPins);
		int32 Row = 0, First = 1;
		while (Id >= First + Row + 1)
		{
			First += Row + 1;
			++Row;
		}
		const int32 Seat = Id - First;
		return FVector2D((Seat - Row / 2.0) * PinSpacing, Row * PinSpacing * 0.8660254);
	}

	double SpeedForPower(double Power)
	{
		return 7.0 + 2.0 * Power;
	}

	double RotationRps(const FShot& Shot)
	{
		return 11.0 + 5.0 * Shot.Power + (FMath::Min(0.45, FMath::Abs(Shot.Curve)) / 0.45) * 4.0;
	}

	double BallXAt(const FShot& Shot, double Z)
	{
		const double T = FMath::Clamp(Z, 0.0, LaneLength) / LaneLength;
		// The hook stays small on the oil and breaks once the ball reaches the dry back end.
		const double Dry = Smoothstep(OilLength / LaneLength, 1.0, T);
		return Shot.Aim + Shot.Curve * T * (0.12 + 0.88 * T) * (0.18 + 0.82 * Dry);
	}

	TOptional<double> FirstGutter(const FShot& Shot)
	{
		auto Out = [&Shot](double Z) { return FMath::Abs(BallXAt(Shot, Z)) >= GutterCaptureEdge; };
		if (Out(0.0))
		{
			return 0.0;
		}
		double Prev = 0.0;
		for (double Z = 0.05; Z <= LaneLength + 1e-9; Z += 0.05)
		{
			if (Out(Z))
			{
				double Lo = Prev, Hi = Z;
				for (int32 i = 0; i < 18; ++i)
				{
					const double Mid = (Lo + Hi) / 2.0;
					(Out(Mid) ? Hi : Lo) = Mid;
				}
				return Hi;
			}
			Prev = Z;
		}
		return TOptional<double>();
	}

	FShotResult ResolveShot(const FShot& InShot, const TArray<int32>& Standing)
	{
		FShotResult R;
		R.Shot = InShot;
		R.Shot.Loft = 0.0; // presentation only
		const FShot& Shot = R.Shot;
		R.Speed = SpeedForPower(Shot.Power);
		R.Rps = RotationRps(Shot);
		const TOptional<double> Gutter = FirstGutter(Shot);
		const double PathEnd = Gutter ? *Gutter : LaneLength;
		for (double Z = 0.0; Z < PathEnd; Z += PathSampleStep)
		{
			R.Path.Add(FVector2D(BallXAt(Shot, Z), Z));
		}
		R.Path.Add(FVector2D(BallXAt(Shot, PathEnd), PathEnd));
		R.EndX = R.Path.Last().X;
		if (Gutter)
		{
			R.bGutter = true;
			R.GutterAt = *Gutter;
			R.GutterSide = BallXAt(Shot, *Gutter) < 0.0 ? -1 : 1;
			return R;
		}

		// The rack sees the ball on one line, at its deck position.
		const double BallX = BallXAt(Shot, LaneLength);
		const double PowerCarry = 0.18 + 0.82 * Shot.Power;
		TMap<int32, double> Energy, Direct;
		for (const int32 Id : Standing)
		{
			const double Frac = FMath::Max(0.0, 1.0 - FMath::Abs(BallX - PinPosition(Id).X) / BallContactReach);
			const double D = FMath::Pow(Frac, 1.18) * PowerCarry;
			Direct.Add(Id, D);
			Energy.Add(Id, D);
		}

		// Waves: every pin over the topple energy goes down at once and passes energy to standing neighbours.
		TSet<int32> Down;
		while (true)
		{
			TArray<int32> Toppling;
			for (const int32 Id : Standing)
			{
				if (!Down.Contains(Id) && Energy[Id] >= PinToppleEnergy)
				{
					Toppling.Add(Id);
				}
			}
			if (Toppling.Num() == 0)
			{
				break;
			}
			for (const int32 Id : Toppling)
			{
				Down.Add(Id);
			}
			for (const int32 From : Toppling)
			{
				const double Carried = Energy[From];
				for (const int32 To : Standing)
				{
					if (Down.Contains(To))
					{
						continue;
					}
					const double Gap = FVector2D::Distance(PinPosition(From), PinPosition(To));
					if (Gap > PinTransferReach)
					{
						continue;
					}
					const double Pass = Carried * PinTransferCoefficient * FMath::Max(0.0, 1.0 - Gap / PinTransferReach) * (0.82 + 0.18 * Shot.Power);
					Energy[To] = FMath::Min(MaxPinEnergy, Energy[To] + Pass);
				}
			}
		}

		for (const int32 Id : Standing)
		{
			const double E = Energy[Id];
			FPinImpact& Impact = R.Impacts.AddDefaulted_GetRef();
			Impact.Id = Id;
			Impact.Energy = E;
			Impact.DirectEnergy = Direct[Id];
			Impact.Slide = FMath::Min(1.0, E / (PinToppleEnergy * 1.35));
			const int32 Away = Sign(PinPosition(Id).X - BallX);
			Impact.ScatterSign = Away != 0 ? Away : (FMath::Sin(Id * 12.9898 + Shot.Aim * 78.233 + Shot.Curve * 37.719) < 0.0 ? -1 : 1);
			if (Down.Contains(Id))
			{
				R.Felled.Add(Id);
			}
		}
		R.Felled.Sort();
		return R;
	}

	bool IsSplit(int32 StandingBefore, const TArray<int32>& Remaining)
	{
		if (StandingBefore != 10 || Remaining.Num() < 2 || Remaining.Num() >= 10 || Remaining.Contains(1))
		{
			return false;
		}
		for (int32 a = 0; a < Remaining.Num(); ++a)
		{
			for (int32 b = a + 1; b < Remaining.Num(); ++b)
			{
				if (FVector2D::Distance(PinPosition(Remaining[a]), PinPosition(Remaining[b])) > SplitGap)
				{
					return true;
				}
			}
		}
		return false;
	}

	double RollSeconds(double Speed)
	{
		return FMath::Max(1.9, LaneLength / FMath::Clamp(Speed, 7.0, 9.0));
	}
}

#include "Input/DeviceMotionSim.h"

namespace MovecadeKart::Input
{
	FTimedDeviceScript::FTimedDeviceScript(TFunction<FDeviceMotion(double)> InMotion, TArray<FPress> InPresses, double InPeriod)
		: Motion(MoveTemp(InMotion))
		, Presses(MoveTemp(InPresses))
		, Period(InPeriod)
	{
		Presses.StableSort([](const FPress& A, const FPress& B) { return A.At < B.At; });
	}

	FDeviceMotion FTimedDeviceScript::Step(double T, TFunctionRef<void(EDeviceButton, bool)> Press)
	{
		// Presses fire in order as their time comes; with a period, the list repeats.
		while (Presses.IsValidIndex(NextPress) && Presses[NextPress].At + Cycle * Period <= T)
		{
			Press(Presses[NextPress].Button, Presses[NextPress].bDown);
			++NextPress;
			if (NextPress == Presses.Num() && Period > 0.0)
			{
				NextPress = 0;
				++Cycle;
			}
		}
		FDeviceMotion M = Motion(T);
		M.TimeSeconds = T;
		return M;
	}

	namespace FDeviceGestures
	{
		namespace
		{
			const FVector3d EarthGravity(0.0, 0.0, -StandardGravity);

			FDeviceMotion AtAttitude(const FQuat& Attitude, EScreenOrientation Orientation)
			{
				FDeviceMotion M;
				M.Attitude = Attitude;
				M.Orientation = Orientation;
				M.GravityMs2 = FVector3d(Attitude.UnrotateVector(FVector(EarthGravity)));
				return M;
			}
		}

		FDeviceMotion Upright(EScreenOrientation Orientation)
		{
			// Screen to the face: the top edge up (beta 90), turned in the screen plane for a landscape hold.
			FQuat Attitude(FVector(1.0, 0.0, 0.0), UE_DOUBLE_HALF_PI);
			if (Orientation == EScreenOrientation::LandscapeLeft)
			{
				Attitude = Attitude * FQuat(FVector(0.0, 0.0, 1.0), UE_DOUBLE_HALF_PI);
			}
			else if (Orientation == EScreenOrientation::LandscapeRight)
			{
				Attitude = Attitude * FQuat(FVector(0.0, 0.0, 1.0), -UE_DOUBLE_HALF_PI);
			}
			else if (Orientation == EScreenOrientation::PortraitUpsideDown)
			{
				Attitude = Attitude * FQuat(FVector(0.0, 0.0, 1.0), UE_DOUBLE_PI);
			}
			return AtAttitude(Attitude, Orientation);
		}

		FDeviceMotion Flat()
		{
			return AtAttitude(FQuat::Identity, EScreenOrientation::Portrait);
		}

		FDeviceMotion Posed(const FDeviceMotion& Base, const FQuat& Rotation, const FVector3d& GyroDegPerSec, const FVector3d& AccelMs2)
		{
			FDeviceMotion M = AtAttitude((Base.Attitude * Rotation).GetNormalized(), Base.Orientation);
			M.GyroDegPerSec = GyroDegPerSec;
			M.AccelMs2 = AccelMs2;
			M.TimeSeconds = Base.TimeSeconds;
			return M;
		}

		FDeviceMotion Wheel(double Degrees)
		{
			// Clockwise as the player sees it: about the screen normal (+z, toward the face), negative.
			return Posed(Upright(EScreenOrientation::LandscapeLeft), FQuat(FVector(0.0, 0.0, 1.0), -FMath::DegreesToRadians(Degrees)),
				FVector3d::ZeroVector, FVector3d::ZeroVector);
		}

		FQuat Blade(double Pitch, double Yaw)
		{
			return FQuat(FVector(0.0, 1.0, 0.0), Yaw) * FQuat(FVector(1.0, 0.0, 0.0), Pitch);
		}
	}

	// Recordings -------------------------------------------------------------------------------------------------

	namespace
	{
		constexpr int32 MotionFields = 15;
		constexpr int32 ButtonFields = 4;
		constexpr int32 TouchFields = 6;
		constexpr int32 MaxButton = static_cast<int32>(EDeviceButton::Item);

		FString Vec(const FVector& V)
		{
			return FString::Printf(TEXT("%.9g,%.9g,%.9g"), V.X, V.Y, V.Z);
		}

		bool ReadNumber(const FString& Field, double& Out)
		{
			if (!FCString::IsNumeric(*Field))
			{
				return false;
			}
			Out = FCString::Atod(*Field);
			return FMath::IsFinite(Out);
		}

		bool ReadVector(const TArray<FString>& Fields, int32 First, FVector& Out)
		{
			double X = 0.0, Y = 0.0, Z = 0.0;
			if (!ReadNumber(Fields[First], X) || !ReadNumber(Fields[First + 1], Y) || !ReadNumber(Fields[First + 2], Z))
			{
				return false;
			}
			Out = FVector(X, Y, Z);
			return true;
		}

		bool ReadLine(const TArray<FString>& F, FDeviceRecordedEvent& E)
		{
			if (F.Num() < 2 || !ReadNumber(F[1], E.Time))
			{
				return false;
			}
			double N = 0.0;
			if (F[0] == TEXT("m") && F.Num() == MotionFields)
			{
				E.Kind = FDeviceRecordedEvent::EKind::Motion;
				E.Reading.TimeSeconds = E.Time;
				if (!ReadVector(F, 2, E.Reading.Tilt) || !ReadVector(F, 5, E.Reading.RotationRate) || !ReadVector(F, 8, E.Reading.Gravity)
					|| !ReadVector(F, 11, E.Reading.Acceleration) || !ReadNumber(F[14], N) || N < 0.0 || N > 3.0)
				{
					return false;
				}
				E.Reading.Orientation = static_cast<EScreenOrientation>(static_cast<uint8>(N));
				return true;
			}
			if (F[0] == TEXT("b") && F.Num() == ButtonFields)
			{
				double Down = 0.0;
				if (!ReadNumber(F[2], N) || N < 0.0 || N > MaxButton || !ReadNumber(F[3], Down))
				{
					return false;
				}
				E.Kind = FDeviceRecordedEvent::EKind::Button;
				E.Button = static_cast<EDeviceButton>(static_cast<uint8>(N));
				E.bDown = Down != 0.0;
				return true;
			}
			if (F[0] == TEXT("t") && F.Num() == TouchFields)
			{
				double Pointer = 0.0, X = 0.0, Y = 0.0;
				if (!ReadNumber(F[2], N) || N < 0.0 || N > 2.0 || !ReadNumber(F[3], Pointer) || !ReadNumber(F[4], X) || !ReadNumber(F[5], Y))
				{
					return false;
				}
				E.Kind = FDeviceRecordedEvent::EKind::Touch;
				E.Touch.Phase = static_cast<ETouchPhase>(static_cast<uint8>(N));
				E.Touch.Pointer = static_cast<int32>(Pointer);
				E.Touch.Position = FVector2D(X, Y);
				E.Touch.Time = E.Time;
				return true;
			}
			return false;
		}
	}

	FString FDeviceRecording::ToText() const
	{
		FString Out = TEXT("# Movecade device motion recording (Input/DeviceMotionSim.h)\n");
		for (const FDeviceRecordedEvent& E : Events)
		{
			switch (E.Kind)
			{
			case FDeviceRecordedEvent::EKind::Motion:
				Out += FString::Printf(TEXT("m,%.6f,%s,%s,%s,%s,%d\n"), E.Time, *Vec(E.Reading.Tilt), *Vec(E.Reading.RotationRate),
					*Vec(E.Reading.Gravity), *Vec(E.Reading.Acceleration), static_cast<int32>(E.Reading.Orientation));
				break;
			case FDeviceRecordedEvent::EKind::Button:
				Out += FString::Printf(TEXT("b,%.6f,%d,%d\n"), E.Time, static_cast<int32>(E.Button), E.bDown ? 1 : 0);
				break;
			case FDeviceRecordedEvent::EKind::Touch:
				Out += FString::Printf(TEXT("t,%.6f,%d,%d,%.6f,%.6f\n"), E.Time, static_cast<int32>(E.Touch.Phase), E.Touch.Pointer,
					E.Touch.Position.X, E.Touch.Position.Y);
				break;
			}
		}
		return Out;
	}

	bool FDeviceRecording::FromText(const FString& Text, FDeviceRecording& Out)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		FDeviceRecording Parsed;
		for (const FString& Raw : Lines)
		{
			const FString Line = Raw.TrimStartAndEnd();
			if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
			{
				continue;
			}
			TArray<FString> Fields;
			Line.ParseIntoArray(Fields, TEXT(","), /*CullEmpty*/ false);
			FDeviceRecordedEvent& E = Parsed.Events.AddDefaulted_GetRef();
			if (!ReadLine(Fields, E))
			{
				return false;
			}
		}
		Out = MoveTemp(Parsed);
		return true;
	}
}

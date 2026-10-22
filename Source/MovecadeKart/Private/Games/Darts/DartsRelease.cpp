#include "Games/Darts/DartsRelease.h"

#include "Dom/JsonObject.h"

namespace MovecadeKart::Darts
{
	TOptional<FDartGesture> GestureFromRelease(const FJsonObject& Data)
	{
		double Power = 0.0;
		if (!Data.TryGetNumberField(TEXT("power"), Power))
		{
			return {};
		}
		auto Field = [&Data](const TCHAR* Key, double Fallback)
		{
			double Value = 0.0;
			return Data.TryGetNumberField(Key, Value) ? Value : Fallback;
		};
		FDartGesture Gesture;
		Gesture.AimX = Field(TEXT("aimX"), 0.0);
		Gesture.AimY = Field(TEXT("aimY"), 0.0);
		Gesture.Power = Power;
		Gesture.SpinDegPerSec = Field(TEXT("spinDegPerSec"), 0.0);
		Gesture.RollDegPerSec = Field(TEXT("rollDegPerSec"), 0.0);
		Gesture.Confidence = Field(TEXT("confidence"), 0.5);
		Gesture.Steadiness = Field(TEXT("steadiness"), 0.5);
		Gesture.PeakMs2 = Field(TEXT("peakMs2"), 0.0);
		Gesture.DurationMs = Field(TEXT("durationMs"), 0.0);
		return Gesture;
	}

	FDartGesture BoundRelease(const FDartGesture& Gesture)
	{
		auto Bound = [](double V, double Fallback, double Lo, double Hi) { return FMath::Clamp(FMath::IsFinite(V) ? V : Fallback, Lo, Hi); };
		FDartGesture G = Gesture;
		G.AimX = Bound(G.AimX, 0.0, -1.5, 1.5);
		G.AimY = Bound(G.AimY, 0.0, -1.5, 1.5);
		G.Power = Bound(G.Power, 0.0, 0.0, 1.0);
		G.SpinDegPerSec = Bound(G.SpinDegPerSec, 0.0, -2000.0, 2000.0);
		G.RollDegPerSec = Bound(G.RollDegPerSec, 0.0, -2000.0, 2000.0);
		G.Confidence = Bound(G.Confidence, 0.5, 0.0, 1.0);
		G.Steadiness = Bound(G.Steadiness, 0.5, 0.0, 1.0);
		G.PeakMs2 = Bound(G.PeakMs2, 0.0, 0.0, 120.0);
		G.DurationMs = Bound(G.DurationMs, 0.0, 0.0, 1000.0);
		return G;
	}
}

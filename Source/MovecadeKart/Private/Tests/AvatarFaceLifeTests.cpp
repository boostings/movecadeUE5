#include "Avatar/AvatarFaceLife.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Avatar;

namespace
{
	constexpr float Frame = 1.f / 60.f;

	/** Blinks begun over Seconds of a scheduler stepped at 60 Hz, and the kinds seen. */
	struct FBlinkRun
	{
		int32 Blinks = 0;
		float MaxClosure = 0.f;
		TSet<EBlinkKind> Kinds;
		TArray<float> Trace;
	};

	FBlinkRun RunBlinks(int32 Seed, float Seconds, float Focus, float Excite, float Drowsy)
	{
		FBlinkScheduler Blinks(Seed);
		FBlinkRun Run;
		float Last = 0.f;
		for (float T = 0.f; T < Seconds; T += Frame)
		{
			const float Closure = Blinks.Step(Frame, Focus, Excite, Drowsy, false);
			if (Closure > 0.f && Last == 0.f)
			{
				++Run.Blinks;
				Run.Kinds.Add(Blinks.Current());
			}
			Run.MaxClosure = FMath::Max(Run.MaxClosure, Closure);
			Run.Trace.Add(Closure);
			Last = Closure;
		}
		return Run;
	}

	float AngleBetween(const FVector& A, const FVector& B)
	{
		return FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal())), -1.f, 1.f));
	}

	FVector Yawed(float Radians)
	{
		return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarBlinkShapeTest, "MovecadeKart.Avatar.FaceLife.BlinkShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarBlinkShapeTest::RunTest(const FString& Parameters)
{
	for (int32 Kind = 0; Kind < static_cast<int32>(EBlinkKind::Count); ++Kind)
	{
		const FBlinkShape Shape = BlinkShape(static_cast<EBlinkKind>(Kind));
		TestEqual(FString::Printf(TEXT("kind %d: open before"), Kind), Shape.At(-0.01f), 0.f);
		TestEqual(FString::Printf(TEXT("kind %d: starts open"), Kind), Shape.At(0.f), 0.f);
		TestTrue(FString::Printf(TEXT("kind %d: reaches its depth"), Kind), FMath::IsNearlyEqual(Shape.At(Shape.Close + Shape.Hold * 0.5f), Shape.Depth, 1e-3f));
		TestEqual(FString::Printf(TEXT("kind %d: open after"), Kind), Shape.At(Shape.Duration()), 0.f);
		TestTrue(FString::Printf(TEXT("kind %d: closes faster than it opens"), Kind), Shape.Close < Shape.Open);
		float Last = 0.f;
		bool bMonotonic = true;
		for (float T = 0.f; T <= Shape.Close; T += 0.005f)
		{
			bMonotonic &= Shape.At(T) >= Last - 1e-5f;
			Last = Shape.At(T);
		}
		TestTrue(FString::Printf(TEXT("kind %d: the lids only come down while closing"), Kind), bMonotonic);
	}
	TestTrue(TEXT("a half blink stays half open"), BlinkShape(EBlinkKind::Half).Depth < 0.7f);
	TestTrue(TEXT("a slow blink is slow"), BlinkShape(EBlinkKind::Slow).Duration() > 2.f * BlinkShape(EBlinkKind::Full).Duration());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarBlinkSchedulerTest, "MovecadeKart.Avatar.FaceLife.BlinkScheduler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarBlinkSchedulerTest::RunTest(const FString& Parameters)
{
	// A natural rate: a blink every few seconds, never stuck shut, with variety.
	const FBlinkRun Calm = RunBlinks(7, 300.f, 0.f, 0.f, 0.f);
	TestTrue(FString::Printf(TEXT("blinks every 1.5-7 s (%d in 300 s)"), Calm.Blinks), Calm.Blinks >= 300 / 7 && Calm.Blinks <= 300 / 1.5f);
	TestTrue(TEXT("closure within 0..1"), Calm.MaxClosure <= 1.f && Calm.MaxClosure > 0.99f);
	TestTrue(TEXT("several kinds of blink"), Calm.Kinds.Num() >= 3);
	int32 ShutFrames = 0, LongestShut = 0;
	for (const float Closure : Calm.Trace)
	{
		ShutFrames = Closure > 0.5f ? ShutFrames + 1 : 0;
		LongestShut = FMath::Max(LongestShut, ShutFrames);
	}
	TestTrue(TEXT("never shut for long"), LongestShut * Frame < 0.35f);

	// Seeded: the same seed replays, another differs (so a line-up never blinks in step).
	TestTrue(TEXT("deterministic"), RunBlinks(7, 30.f, 0.f, 0.f, 0.f).Trace == RunBlinks(7, 30.f, 0.f, 0.f, 0.f).Trace);
	TestFalse(TEXT("seeds differ"), RunBlinks(7, 30.f, 0.f, 0.f, 0.f).Trace == RunBlinks(8, 30.f, 0.f, 0.f, 0.f).Trace);

	// Concentration blinks less, excitement more; a drowsy face blinks slow.
	const FBlinkRun Focused = RunBlinks(11, 600.f, 1.f, 0.f, 0.f);
	const FBlinkRun Excited = RunBlinks(11, 600.f, 0.f, 1.f, 0.f);
	TestTrue(FString::Printf(TEXT("focus stretches the gaps (%d vs %d)"), Focused.Blinks, Excited.Blinks), Focused.Blinks < Excited.Blinks);
	FBlinkScheduler Drowsy(3);
	int32 Slow = 0, Total = 0;
	float Last = 0.f;
	for (float T = 0.f; T < 300.f; T += Frame)
	{
		const float Closure = Drowsy.Step(Frame, 0.f, 0.f, 1.f, false);
		if (Closure > 0.f && Last == 0.f)
		{
			++Total;
			Slow += Drowsy.Current() == EBlinkKind::Slow ? 1 : 0;
		}
		Last = Closure;
	}
	TestTrue(FString::Printf(TEXT("drowsy blinks are mostly slow (%d of %d)"), Slow, Total), Total > 0 && Slow * 2 > Total);

	// A big eye jump brings a blink soon after the last one has settled.
	FBlinkScheduler Jumpy(5);
	for (float T = 0.f; T < 4.f; T += Frame)
	{
		Jumpy.Step(Frame, 0.f, 0.f, 0.f, false);
	}
	int32 FromSaccade = 0;
	for (int32 Try = 0; Try < 20; ++Try)
	{
		for (float T = 0.f; T < 1.f; T += Frame)
		{
			Jumpy.Step(Frame, 0.f, 0.f, 0.f, false);
		}
		const float Before = Jumpy.Age();
		Jumpy.Step(Frame, 0.f, 0.f, 0.f, true);
		FromSaccade += Jumpy.Age() < Before ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("saccades bring blinks (%d of 20)"), FromSaccade), FromSaccade >= 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarSaccadeTest, "MovecadeKart.Avatar.FaceLife.Saccades",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarSaccadeTest::RunTest(const FString& Parameters)
{
	// A far target: one fast saccade, flagged as a big jump on the frame it starts, landing within a tenth of a second.
	FSaccadeScheduler Eyes(1);
	bool bSaccade = false;
	for (float T = 0.f; T < 0.3f; T += Frame)
	{
		Eyes.Step(Frame, FVector::ForwardVector, 0.f, bSaccade);
	}
	const FVector Target = Yawed(FMath::DegreesToRadians(30.f));
	Eyes.Step(Frame, Target, 0.f, bSaccade);
	TestTrue(TEXT("a 30 degree jump is a big saccade"), bSaccade);
	Eyes.Step(Frame, Target, 0.f, bSaccade);
	TestTrue(TEXT("the eyes leave fast"), AngleBetween(Eyes.Direction(), FVector::ForwardVector) > FMath::DegreesToRadians(8.f));
	float Landed = -1.f;
	for (float T = 2.f * Frame; T < 0.5f; T += Frame)
	{
		Eyes.Step(Frame, Target, 0.f, bSaccade);
		if (Landed < 0.f && AngleBetween(Eyes.Direction(), Target) < FMath::DegreesToRadians(2.f))
		{
			Landed = T;
		}
	}
	TestTrue(FString::Printf(TEXT("lands in a tenth of a second (%.3f s)"), Landed), Landed >= 0.f && Landed <= 0.12f);

	// A small, slow move is pursued smoothly: no saccade, and the eyes follow it.
	bool bAnySaccade = false;
	FVector Moving = Target;
	for (float T = 0.f; T < 1.f; T += Frame)
	{
		Moving = Yawed(FMath::DegreesToRadians(30.f + 5.f * T));
		Eyes.Step(Frame, Moving, 0.f, bSaccade);
		bAnySaccade |= bSaccade;
	}
	TestFalse(TEXT("pursuit without big saccades"), bAnySaccade);
	TestTrue(TEXT("pursuit keeps up"), AngleBetween(Eyes.Direction(), Moving) < FMath::DegreesToRadians(2.5f));

	// Fixating: micro-saccades keep the eyes alive, always within a couple of degrees.
	FSaccadeScheduler Still(2);
	float MaxOff = 0.f;
	TSet<int32> Distinct;
	for (float T = 0.f; T < 10.f; T += Frame)
	{
		const FVector Dir = Still.Step(Frame, FVector::ForwardVector, 0.f, bSaccade);
		MaxOff = FMath::Max(MaxOff, AngleBetween(Dir, FVector::ForwardVector));
		Distinct.Add(FMath::RoundToInt(Dir.Y * 1e5));
	}
	TestTrue(FString::Printf(TEXT("micro-saccades stay small (%.2f deg)"), FMath::RadiansToDegrees(MaxOff)), MaxOff < FMath::DegreesToRadians(2.f));
	TestTrue(TEXT("micro-saccades move the eyes"), Distinct.Num() > 3);

	// Idle: the eyes glance aside on their own now and then.
	FSaccadeScheduler Idle(3);
	MaxOff = 0.f;
	for (float T = 0.f; T < 30.f; T += Frame)
	{
		MaxOff = FMath::Max(MaxOff, AngleBetween(Idle.Step(Frame, FVector::ForwardVector, 1.f, bSaccade), FVector::ForwardVector));
	}
	TestTrue(TEXT("idle eyes glance aside"), MaxOff > FMath::DegreesToRadians(5.f));

	// The eyes' range: a target behind the shoulder is clamped to the ellipse.
	const FVector2f Clamped = EyeAngles(ClampEyes(Yawed(FMath::DegreesToRadians(80.f)), 0.5f, 0.3f));
	TestTrue(TEXT("yaw clamped"), FMath::IsNearlyEqual(Clamped.X, 0.5f, 1e-3f) && FMath::Abs(Clamped.Y) < 1e-3f);
	const FVector2f Free = EyeAngles(ClampEyes(Yawed(0.2f), 0.5f, 0.3f));
	TestTrue(TEXT("inside the range untouched"), FMath::IsNearlyEqual(Free.X, 0.2f, 1e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarFaceSecondaryTest, "MovecadeKart.Avatar.FaceLife.Secondary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarFaceSecondaryTest::RunTest(const FString& Parameters)
{
	using namespace DriverFace;
	auto At = [](const float (&W)[MorphCount], EMorph Morph) { return W[static_cast<int32>(Morph)]; };
	float Rest[MorphCount], Out[MorphCount];
	Mix(FMood(), Rest);
	float Lid = 0.f;

	// At rest the face settles on the mood's own weights (give or take a breath).
	FFaceLife Face(4);
	FFaceLifeInput In;
	for (float T = 0.f; T < 2.f; T += Frame)
	{
		In.Time = T;
		Face.Step(Frame, Rest, In, Out, Lid);
	}
	float Worst = 0.f;
	for (int32 i = 0; i < MorphCount; ++i)
	{
		Worst = FMath::Max(Worst, FMath::Abs(Out[i] - FMath::Max(Rest[i], i == static_cast<int32>(EMorph::CheekSquint) ? 0.65f * Rest[0] : 0.f)));
	}
	TestTrue(FString::Printf(TEXT("settles on the resting face (off by %.3f)"), Worst), Worst < 0.04f);
	TestEqual(TEXT("level eyes: lids open"), Lid, 0.f);

	// A big smile: the cheeks follow it, a beat behind, and the eyes narrow.
	float Grin[MorphCount];
	FMemory::Memcpy(Grin, Rest, sizeof(Grin));
	Grin[static_cast<int32>(EMorph::Smile)] = 1.f;
	Face.Step(Frame * 3.f, Grin, In, Out, Lid);
	TestTrue(TEXT("cheeks lag the smile"), At(Out, EMorph::CheekSquint) < At(Out, EMorph::Smile));
	for (float T = 0.f; T < 1.5f; T += Frame)
	{
		Face.Step(Frame, Grin, In, Out, Lid);
	}
	TestTrue(TEXT("cheeks lift with the smile"), At(Out, EMorph::CheekSquint) > 0.55f);
	TestTrue(TEXT("a broad smile narrows the eyes"), At(Out, EMorph::EyeSquint) > 0.08f);

	// Looking down lowers the lids; looking up widens the eyes.
	In.EyePitch = -0.4f;
	Face.Step(Frame, Rest, In, Out, Lid);
	TestTrue(TEXT("looking down lowers the lids"), Lid > 0.3f);
	In.EyePitch = 0.35f;
	for (float T = 0.f; T < 0.5f; T += Frame)
	{
		Face.Step(Frame, Rest, In, Out, Lid);
	}
	TestTrue(TEXT("looking up widens the eyes"), At(Out, EMorph::EyeWide) > 0.2f && Lid == 0.f);
	In.EyePitch = 0.f;

	// A greeting flashes the brows, which settle again.
	FFaceLife Greeter(5);
	FFaceLifeInput Hello;
	Greeter.Step(Frame, Rest, Hello, Out, Lid);
	Hello.bGreeting = true;
	Greeter.Step(Frame, Rest, Hello, Out, Lid);
	Hello.bGreeting = false;
	float Peak = 0.f;
	for (float T = 0.f; T < 0.4f; T += Frame)
	{
		Greeter.Step(Frame, Rest, Hello, Out, Lid);
		Peak = FMath::Max(Peak, At(Out, EMorph::BrowOuterUp));
	}
	TestTrue(TEXT("the brows flash"), Peak > 0.25f);
	for (float T = 0.f; T < 1.5f; T += Frame)
	{
		Greeter.Step(Frame, Rest, Hello, Out, Lid);
	}
	TestTrue(TEXT("and settle"), At(Out, EMorph::BrowOuterUp) < 0.05f);

	// Effort and cheer keep every weight in range while the jaw works.
	FFaceLife Busy(6);
	FFaceLifeInput Strain;
	Strain.Effort = 1.f;
	Strain.Cheer = 1.f;
	float JawMin = 1.f, JawMax = 0.f;
	bool bInRange = true;
	for (float T = 0.f; T < 3.f; T += Frame)
	{
		Strain.Time = T;
		Busy.Step(Frame, Rest, Strain, Out, Lid);
		for (const float W : Out)
		{
			bInRange &= W >= 0.f && W <= 1.f;
		}
		JawMin = FMath::Min(JawMin, At(Out, EMorph::JawOpen));
		JawMax = FMath::Max(JawMax, At(Out, EMorph::JawOpen));
	}
	TestTrue(TEXT("weights stay in 0..1"), bInRange);
	TestTrue(TEXT("the jaw works"), JawMax - JawMin > 0.05f);
	return true;
}

#endif

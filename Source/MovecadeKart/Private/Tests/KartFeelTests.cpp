#include "Feel/KartFeel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	/** Runs the feel for Seconds at Fps with a constant input (Mutate can vary it per frame). */
	template <typename FMutate>
	FKartFeelPose Run(FKartFeel& Feel, FKartFeelInput In, double Seconds, double Fps, FMutate&& Mutate)
	{
		const int32 Frames = FMath::RoundToInt(Seconds * Fps);
		In.DeltaSeconds = 1.0 / Fps;
		FKartFeelPose Pose;
		for (int32 i = 0; i < Frames; ++i)
		{
			Mutate(In, i);
			Pose = Feel.Update(In);
		}
		return Pose;
	}

	FKartFeelPose Run(FKartFeel& Feel, const FKartFeelInput& In, double Seconds, double Fps = 60.0)
	{
		return Run(Feel, In, Seconds, Fps, [](FKartFeelInput&, int32) {});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFeelSpringTest, "MovecadeKart.Feel.Spring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartFeelSpringTest::RunTest(const FString& Parameters)
{
	// Settles on the target, and the same motion at 30 and 240 fps (substepped integration).
	auto Settle = [](double Fps)
	{
		FFeelSpring Spring;
		for (int32 i = 0; i < FMath::RoundToInt(Fps * 0.5); ++i)
		{
			Spring.Step(1.0, 3.0, 0.5, 1.0 / Fps);
		}
		return Spring.Value;
	};
	TestTrue(TEXT("frame-rate independent"), FMath::IsNearlyEqual(Settle(30.0), Settle(240.0), 0.02));

	FFeelSpring Spring;
	for (int32 i = 0; i < 600; ++i)
	{
		Spring.Step(1.0, 3.0, 0.5, 1.0 / 60.0);
	}
	TestTrue(TEXT("settles on the target"), FMath::IsNearlyEqual(Spring.Value, 1.0, 1e-4));

	// Underdamped springs overshoot (that's the bounce); critical ones don't.
	double Peak = 0.0;
	FFeelSpring Bouncy;
	FFeelSpring Critical;
	double CriticalPeak = 0.0;
	for (int32 i = 0; i < 120; ++i)
	{
		Bouncy.Step(1.0, 3.0, 0.3, 1.0 / 60.0);
		Critical.Step(1.0, 3.0, 1.0, 1.0 / 60.0);
		Peak = FMath::Max(Peak, Bouncy.Value);
		CriticalPeak = FMath::Max(CriticalPeak, Critical.Value);
	}
	TestTrue(TEXT("underdamped overshoots"), Peak > 1.1);
	TestTrue(TEXT("critical doesn't"), CriticalPeak <= 1.0 + 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFeelChassisTest, "MovecadeKart.Feel.Chassis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartFeelChassisTest::RunTest(const FString& Parameters)
{
	FKartFeelInput Rest;
	{
		FKartFeel Feel;
		const FKartFeelPose Pose = Run(Feel, Rest, 2.0);
		TestTrue(TEXT("at rest: level"), FMath::IsNearlyZero(Pose.ChassisPitch) && FMath::IsNearlyZero(Pose.ChassisRoll));
		TestTrue(TEXT("at rest: no squash"), FMath::IsNearlyEqual(Pose.SquashUp, 1.0) && FMath::IsNearlyEqual(Pose.StretchForward, 1.0));
	}
	{
		// Throttle from a standstill (+12 m/s^2) lifts the nose; braking dips it.
		FKartFeel Feel;
		const FKartFeelPose Accel = Run(Feel, Rest, 0.4, 60.0, [](FKartFeelInput& In, int32) { In.Speed += 12.0 * In.DeltaSeconds; });
		TestTrue(TEXT("throttle: nose up"), Accel.ChassisPitch > 1.0);
		const FKartFeelPose Brake = Run(Feel, Rest, 0.4, 60.0, [](FKartFeelInput& In, int32 Frame) { In.Speed = FMath::Max(0.0, 30.0 - Frame); });
		TestTrue(TEXT("brake: nose down"), Brake.ChassisPitch < -1.0);
	}
	{
		// A left turn at speed (yaw rate +1 rad/s at 30 m/s is ~3 g) rolls the body to the right, and the
		// camera banks into the turn (left side down: negative UE roll).
		FKartFeel Feel;
		FKartFeelInput Turn;
		Turn.Speed = 30.0;
		const FKartFeelPose Pose = Run(Feel, Turn, 1.0, 60.0, [](FKartFeelInput& In, int32) { In.Yaw += 1.0 * In.DeltaSeconds; });
		TestTrue(TEXT("left turn: body rolls right"), Pose.ChassisRoll > 2.0);
		TestTrue(TEXT("left turn: camera banks left"), Pose.CameraRoll < -1.0);
		TestTrue(TEXT("roll is capped"), Pose.ChassisRoll <= Feel.Tuning.MaxRoll * 1.5);
	}
	{
		// Landing a hop squashes the kart, then it springs back.
		FKartFeel Feel;
		FKartFeelInput Hop;
		Hop.Speed = 25.0;
		double MinSquash = 1.0;
		Run(Feel, Hop, 0.6, 60.0, [&](FKartFeelInput& In, int32 Frame)
		{
			const double T = Frame / 60.0;
			In.HopOffset = T < 0.25 ? 0.3 * FMath::Sin(UE_DOUBLE_PI * T / 0.25) : 0.0;
			MinSquash = FMath::Min(MinSquash, Feel.Pose().SquashUp);
		});
		TestTrue(TEXT("landing squashes"), MinSquash < 0.97);
		const FKartFeelPose After = Run(Feel, Hop, 2.0);
		TestTrue(TEXT("and recovers"), FMath::IsNearlyEqual(After.SquashUp, 1.0, 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFeelCameraTest, "MovecadeKart.Feel.Camera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartFeelCameraTest::RunTest(const FString& Parameters)
{
	{
		// An ultra boost kicks the FOV and pulls the camera back; both settle once it's over.
		FKartFeel Feel;
		FKartFeelInput Boost;
		Boost.Speed = 40.0;
		Boost.BoostMultiplier = 1.55;
		Boost.BoostTimeRemaining = 1.6;
		const FKartFeelPose During = Run(Feel, Boost, 0.8);
		TestTrue(TEXT("boost: FOV kick"), During.CameraFovKick > 5.0);
		TestTrue(TEXT("boost: pull back"), During.CameraPullBack > 0.4);
		Boost.BoostTimeRemaining = 0.0;
		const FKartFeelPose After = Run(Feel, Boost, 3.0);
		TestTrue(TEXT("after: FOV back"), FMath::Abs(After.CameraFovKick) < 0.1);
	}
	{
		// Drifting left swings the eye out to the right and turns the view into the corner (left).
		FKartFeel Feel;
		FKartFeelInput Drift;
		Drift.Speed = 30.0;
		Drift.DriftDirection = 1.4;
		Drift.DriftPower = 5.0;
		const FKartFeelPose Pose = Run(Feel, Drift, 1.5);
		TestTrue(TEXT("drift left: eye right"), Pose.CameraSide > 0.3);
		TestTrue(TEXT("drift left: view left"), Pose.CameraYaw < -3.0);
	}
	{
		// A teleport snaps every spring to rest instead of springing across the map.
		FKartFeel Feel;
		FKartFeelInput In;
		In.Speed = 30.0;
		In.DriftDirection = -1.4;
		Run(Feel, In, 1.0);
		In.TeleportCount = 1;
		In.DriftDirection = 0.0;
		In.Speed = 0.0;
		In.DeltaSeconds = 1.0 / 60.0;
		const FKartFeelPose Snapped = Feel.Update(In);
		TestTrue(TEXT("teleport resets"), FMath::IsNearlyZero(Snapped.CameraSide) && FMath::IsNearlyZero(Snapped.ChassisPitch));
	}
	{
		// -KartWebFeel: everything stays at rest.
		FKartFeel Feel;
		Feel.bEnabled = false;
		FKartFeelInput Boost;
		Boost.Speed = 40.0;
		Boost.BoostMultiplier = 1.55;
		Boost.BoostTimeRemaining = 1.6;
		const FKartFeelPose Pose = Run(Feel, Boost, 1.0);
		TestTrue(TEXT("web feel: no kick"), FMath::IsNearlyZero(Pose.CameraFovKick) && FMath::IsNearlyZero(Pose.CameraRumble));
	}
	return true;
}

#endif

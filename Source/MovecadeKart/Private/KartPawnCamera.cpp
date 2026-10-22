// AKartPawn's camera: the chase rig with the feel's offsets and the shake, the countdown crane, the finish orbit,
// look behind, the trailer and close-up orbit shots, and the per-viewport field of view.
#include "KartPawn.h"

#include "Camera/CameraComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "KartCoords.h"
#include "KartDebugFlags.h"
#include "KartMovementComponent.h"
#include "KartPawnTuning.h"
#include "KartPlayerController.h"

using namespace MovecadeKart;

namespace
{
	/** Frames longer than this follow as if they were this long (the web caps its frame dt at 1/30 s). */
	constexpr double MaxFollowFrame = 1.0 / 30.0;
	/** A respawn drops the camera in from this much further back (m) and higher (m). */
	constexpr double RespawnDropBack = 3.0;
	constexpr double RespawnDropUp = 5.0;
	/** kartFxBus.ts: the shake envelope decays at 6.5/s; roll shake is this share of the positional one. */
	constexpr double ShakeDecay = 6.5;
	constexpr double ShakeRollScale = 0.06;
	constexpr double ShakeVerticalScale = 0.7;
	/** The feel's camera dip (m) also lowers the look point, by 0.4 of it (in cm per metre). */
	constexpr double DipLookCmPerMetre = 40.0;

	/** Countdown crane: from 15 m back, 8 m to the side and 10 m up, easing down onto the chase view, the FOV opening
	 *  by 12 degrees as it lands. */
	constexpr double SwoopBack = 15.0;
	constexpr double SwoopSide = 8.0;
	constexpr double SwoopHeight = 10.0;
	constexpr double SwoopEase = 2.5;
	constexpr double SwoopFovDrop = 12.0;

	/** Finish: blends in over 0.9 s, swings 148 degrees round to the front three-quarter over 2.6 s, then orbits at
	 *  9 deg/s, 6 m out (7 m in split-screen), 1.2 m up, 10 degrees tighter. */
	constexpr double FinishBlendSeconds = 0.9;
	constexpr double FinishSwingSeconds = 2.6;
	constexpr double FinishSwingDegrees = 148.0;
	constexpr double FinishOrbitRate = 9.0;
	constexpr double FinishOrbitCm = 600.0;
	constexpr double FinishOrbitSplitCm = 700.0;
	constexpr double FinishFovDrop = 10.0;

	/** Look behind: from 3.6 m ahead of the kart, low, back down the road 9 m. */
	constexpr double LookBehindAhead = 3.6;
	constexpr double LookBehindBack = 9.0;
	constexpr double LookBehindEyeScale = 0.6;
	constexpr double LookBehindLookScale = 0.45;

	/** -KartCinematic: a hard cut every CinematicShotSeconds through the chase view and tracking shots around the kart
	 *  (yaw from ahead, distance m, height m, orbit deg/s; distance <= 0 is the chase view). */
	constexpr double CinematicShotSeconds = 4.5;
	const FVector4 CinematicShots[] = {
		FVector4(0, -1, 0, 0),       // chase
		FVector4(35, 5.0, 0.9, 0),   // low front three-quarter
		FVector4(95, 6.5, 1.4, 0),   // side tracking
		FVector4(0, 6.0, 2.2, 14),   // slow orbit
		FVector4(180, 13.0, 5.5, 0), // high and wide, behind
		FVector4(150, 3.4, 0.55, 0), // low over the rear wheel
	};
	constexpr float CinematicFov = 62.f;
	constexpr float CloseCamFov = 50.f;
}

void AKartPawn::UpdateCamera(float DeltaSeconds)
{
	FVector Eye;
	FRotator View;
	FollowChase(DeltaSeconds, Eye, View);
	const double FovOffset = ApplyTransitions(Eye, View);
	// Look behind: from just ahead of the kart, back down the road (the kart stays in the foreground).
	if (const AKartPlayerController* Player = Cast<AKartPlayerController>(GetController()); Player && Player->IsLookingBack())
	{
		const double Yaw = Movement->RenderYaw();
		const FVector2D Fwd(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
		const FVector2D Pos = Movement->RenderPosition();
		Eye = Coords::ToUE(Pos + Fwd * LookBehindAhead, ChaseCamera.EyeHeight * LookBehindEyeScale);
		View = (Coords::ToUE(Pos - Fwd * LookBehindBack, ChaseCamera.LookHeight * LookBehindLookScale) - Eye).Rotation();
	}
	if (ApplyOrbitShot())
	{
		return;
	}
	Camera->SetWorldLocationAndRotation(Eye, View);
	ApplyFieldOfView(FovOffset);
}

void AKartPawn::FollowChase(float DeltaSeconds, FVector& OutEye, FRotator& OutView)
{
	const FKartChaseCameraTuning& Rig = ChaseCamera;
	const FKartState& S = Movement->SimState();
	const double Speed = FMath::Abs(S.Speed);
	const FKartFeelPose& Juice = Feel.Pose();
	const double Chase = (bSplitScreenCamera ? Rig.SplitChaseDistance : Rig.ChaseDistance + FMath::Min(Rig.ChaseSpeedMax, Speed * Rig.ChasePerSpeed)) + Juice.CameraPullBack;
	const double Ahead = bSplitScreenCamera ? Rig.SplitLookAhead : Rig.LookAhead + FMath::Min(Rig.LookAheadSpeedMax, Speed * Rig.LookAheadPerSpeed);
	const double Yaw = Movement->RenderYaw();
	const FVector2D Fwd(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
	const FVector2D Pos = Movement->RenderPosition();
	const FVector EyeTarget = Coords::ToUE(Pos - Fwd * Chase, Rig.EyeHeight);
	const FVector LookTarget = Coords::ToUE(Pos + Fwd * Ahead, Rig.LookHeight);

	if (S.TeleportCount != CameraTeleports && !(CameraPos.IsZero() && CameraLook.IsZero()))
	{
		// A respawn: rather than whip across the track to the new spot, start lifted above it and settle in as
		// the kart pops back.
		CameraTeleports = S.TeleportCount;
		CameraPos = Coords::ToUE(Pos - Fwd * (Chase + RespawnDropBack), Rig.EyeHeight + RespawnDropUp);
		CameraLook = Coords::ToUE(Pos, 0.5);
		RespawnPopStart = GetWorld()->GetTimeSeconds();
	}
	if (CameraPos.IsZero() && CameraLook.IsZero())
	{
		CameraPos = EyeTarget;
		CameraLook = LookTarget;
	}
	else
	{
		const double Alpha = FMath::Min(1.0, Rig.FollowRate * FMath::Min(static_cast<double>(DeltaSeconds), MaxFollowFrame));
		CameraPos = FMath::Lerp(CameraPos, EyeTarget, Alpha);
		CameraLook = FMath::Lerp(CameraLook, LookTarget, Alpha);
	}
	// Feel offsets (drift swing, landing dip, corner bank) and the kartFxBus.ts shake ride on top of the follow,
	// never accumulated into it: the springs already smooth them.
	const FVector Right = Coords::DirToUE(FVector(FMath::Cos(Yaw), 0.0, -FMath::Sin(Yaw))); // the kart's right on the ground
	FVector Eye = CameraPos + Right * Juice.CameraSide * 100.0 + FVector(0, 0, Juice.CameraDip * 100.0);
	FRotator View = (CameraLook + FVector(0, 0, Juice.CameraDip * DipLookCmPerMetre) - Eye).Rotation();
	View.Yaw += Juice.CameraYaw;
	View.Roll += Juice.CameraRoll;
	const double Now = GetWorld()->GetTimeSeconds();
	const double Amp = ShakePeak * FMath::Exp(-ShakeDecay * (Now - ShakeStart)) + Juice.CameraRumble;
	if (Amp > 0.001)
	{
		// Two incommensurate sines per axis, so the shake never visibly repeats.
		const FRotationMatrix Axes(View);
		const double X = (FMath::Sin(Now * 47.3) * 0.6 + FMath::Sin(Now * 83.1 + 1.3) * 0.4) * Amp;
		const double Y = (FMath::Sin(Now * 53.7 + 2.1) * 0.6 + FMath::Sin(Now * 91.9 + 0.4) * 0.4) * Amp * ShakeVerticalScale;
		const double Roll = (FMath::Sin(Now * 39.1 + 0.7) * 0.6 + FMath::Sin(Now * 71.3 + 2.9) * 0.4) * Amp * ShakeRollScale;
		Eye += Axes.GetUnitAxis(EAxis::Y) * X * 100.0 + Axes.GetUnitAxis(EAxis::Z) * Y * 100.0;
		View.Roll += FMath::RadiansToDegrees(Roll);
	}
	OutEye = Eye;
	OutView = View;
}

double AKartPawn::ApplyTransitions(FVector& Eye, FRotator& View)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const double Yaw = Movement->RenderYaw();
	const FVector2D Fwd(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
	const FVector2D Pos = Movement->RenderPosition();
	double FovOffset = 0.0;
	// Countdown swoop: from high behind and to the side of the grid, a crane down onto the chase view.
	if (Now - SwoopStart < SwoopSeconds)
	{
		const FVector2D RightXZ(FMath::Cos(Yaw), -FMath::Sin(Yaw));
		const double T = FMath::InterpEaseInOut(0.0, 1.0, FMath::Clamp((Now - SwoopStart) / SwoopSeconds, 0.0, 1.0), SwoopEase);
		const FVector Kart = Coords::ToUE(Pos, 1.0);
		const FVector High = Coords::ToUE(Pos - Fwd * SwoopBack + RightXZ * SwoopSide, SwoopHeight);
		Eye = FMath::Lerp(High, Eye, T);
		View = FQuat::Slerp((Kart - Eye).ToOrientationQuat(), View.Quaternion(), T).Rotator();
		FovOffset -= SwoopFovDrop * (1.0 - T);
	}
	// Finish: the camera swings from behind round to the driver's face (front three-quarter) and keeps a slow orbit
	// until the results take over.
	if (Movement->SimState().bFinished)
	{
		if (FinishCamStart < 0.0)
		{
			FinishCamStart = Now;
		}
		const double T = Now - FinishCamStart;
		const double In = FMath::SmoothStep(0.0, 1.0, T / FinishBlendSeconds);
		const double Swing = FMath::SmoothStep(0.0, 1.0, T / FinishSwingSeconds);
		const double Angle = 180.0 - FinishSwingDegrees * Swing - FinishOrbitRate * FMath::Max(0.0, T - FinishSwingSeconds);
		const FVector Centre = VehicleRoot->GetComponentLocation() + FVector(0, 0, 80);
		const FVector Around = FRotator(0, GetActorRotation().Yaw + Angle, 0).Vector();
		const FVector OrbitEye = Centre + Around * (bSplitScreenCamera ? FinishOrbitSplitCm : FinishOrbitCm) + FVector(0, 0, 120);
		Eye = FMath::Lerp(Eye, OrbitEye, In);
		View = FQuat::Slerp(View.Quaternion(), (Centre + FVector(0, 0, 25) - OrbitEye).ToOrientationQuat(), In).Rotator();
		FovOffset -= FinishFovDrop * In;
	}
	return FovOffset;
}

bool AKartPawn::ApplyOrbitShot()
{
	const FKartDebugFlags& Flags = FKartDebugFlags::Get();
	const double Now = GetWorld()->GetTimeSeconds();
	FVector4 Orbit = Flags.CloseCam;
	if (Flags.bCinematic)
	{
		Orbit = CinematicShots[FMath::FloorToInt32(Now / CinematicShotSeconds) % UE_ARRAY_COUNT(CinematicShots)];
	}
	if (Orbit.Y <= 0.0)
	{
		return false;
	}
	// Rigid to the kart's heading (not the drifting body), so tracking shots stay smooth through drifts.
	const FVector Centre = VehicleRoot->GetComponentLocation() + FVector(0, 0, 50);
	const FVector Around = FRotator(0, GetActorRotation().Yaw + Orbit.X + Orbit.W * Now, 0).Vector();
	const FVector Eye = Centre + Around * Orbit.Y * 100.0 + FVector(0, 0, Orbit.Z * 100.0);
	Camera->SetWorldLocationAndRotation(Eye, (Centre - Eye).Rotation());
	Camera->SetFieldOfView(Flags.bCinematic ? CinematicFov : CloseCamFov);
	return true;
}

void AKartPawn::ApplyFieldOfView(double FovOffset)
{
	// Vertical FOV, converted to UE's horizontal FOV for this player's part of the screen.
	const FKartChaseCameraTuning& Rig = ChaseCamera;
	const double Speed = FMath::Abs(Movement->SimState().Speed);
	const double VerticalFov = (bSplitScreenCamera ? Rig.SplitFov : Rig.Fov + FMath::Min(Rig.FovSpeedMax, Speed * Rig.FovPerSpeed))
		+ Feel.Pose().CameraFovKick * (bSplitScreenCamera ? Rig.SplitFovKickScale : 1.0) + FovOffset;
	double Aspect = 16.0 / 9.0;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		int32 X = 0, Y = 0;
		PC->GetViewportSize(X, Y);
		const ULocalPlayer* Local = PC->GetLocalPlayer();
		const FVector2D Part = Local ? FVector2D(Local->Size) : FVector2D(1.0, 1.0);
		if (X > 0 && Y > 0 && Part.X > 0.0 && Part.Y > 0.0)
		{
			Aspect = (X * Part.X) / (Y * Part.Y);
		}
	}
	const double HalfV = FMath::DegreesToRadians(VerticalFov * 0.5);
	Camera->SetFieldOfView(static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfV) * Aspect))));
}

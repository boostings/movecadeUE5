#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"

DECLARE_LOG_CATEGORY_EXTERN(LogKartPawn, Log, All);

/** AKartPawn's vehicle frame, driver seating and presentation constants, shared by its source files. */
namespace MovecadeKart::Pawn
{
	/**
	 * The vehicles' model frame (the imported meshes, Tools/ue/import_karts.py): glTF (x, y, z) -> (x, z, y) * 100,
	 * so the model's forward is +Y, its up +Z, and the wheels' contact patch is 49 cm below the origin on every
	 * vehicle (hub height = wheel radius, Tools/art/.cache/out/mounts.json).
	 */
	constexpr double RoadBelowOrigin = 49.0;
	/** The avatar rig stands 2.56 units tall; the web seats it at 0.9 scale (Kart.jsx:649-679). */
	constexpr double DriverScale = 0.9;
	constexpr double DriverHeight = 256.0;
	/** The name plate floats this far over the driver's head. */
	constexpr double NamePlateClearance = 30.0;
	/** MESH/HARBOR_KART_RADIUS 1.15 m. */
	constexpr float KartRadiusCm = 115.f;

	/** How a vehicle seats its driver and which of its parts exist (vehicles.ts VEHICLE_MOUNT_SPECS). */
	struct FVehicleMount
	{
		const TCHAR* Name;
		/** The rig root (the driver's feet), model frame: vehicles.ts KART_SEAT or the GLB `seat` node. */
		FVector Seat;
		const TCHAR* GripNode;
		bool bStraddle;
		bool bGripPaint;
		bool bBooster;
		bool bTrim;
		/** Front slots 0/1 steer; the moto's empty slots have no mesh. */
		const TCHAR* WheelNodes[4];
	};

	/** By EKartVehicle. The kart's trim (bumper, lamps, splitter, dampers, roundels) comes from Tools/art/detail. */
	inline const FVehicleMount& MountFor(EKartVehicle Vehicle)
	{
		static const FVehicleMount Mounts[] = {
			{ TEXT("Kart"), FVector(0, -15, -92.8), TEXT("d_wheel"), false, true, true, true, { TEXT("wheel_0"), TEXT("wheel_1"), TEXT("wheel_2"), TEXT("wheel_3") } },
			{ TEXT("Moto"), FVector(0, -20, -64), TEXT("handlebar"), true, false, false, true, { TEXT("wheel_0"), nullptr, TEXT("wheel_2"), nullptr } },
			{ TEXT("Coupe"), FVector(0, -15, -92.8), TEXT("d_wheel"), false, false, false, true, { TEXT("wheel_0"), TEXT("wheel_1"), TEXT("wheel_2"), TEXT("wheel_3") } },
			{ TEXT("Truck"), FVector(0, -15, -3.8), TEXT("d_wheel"), false, false, false, true, { TEXT("wheel_0"), TEXT("wheel_1"), TEXT("wheel_2"), TEXT("wheel_3") } },
		};
		return Mounts[static_cast<int32>(Vehicle)];
	}

	/** Every d_wheel rim is tilted -1.134 rad about the lateral axis (the GLB node rotation): its disc normal, toward the driver. */
	inline const FVector& RimNormal()
	{
		static const FVector Normal(0.0, -FMath::Sin(1.134), FMath::Cos(1.134));
		return Normal;
	}

	/** Where a rotation about Axis by +Angle carries Probe: used to pick signs without trusting handedness. */
	inline double SignToward(const FVector& Axis, const FVector& Probe, const FVector& Toward)
	{
		return FVector::DotProduct(FQuat(Axis, 0.1).RotateVector(Probe), Toward) >= FVector::DotProduct(Probe, Toward) ? 1.0 : -1.0;
	}

	/** The rim's turn for full lock: drivingGrip.ts rim = damp(clamp(yRot * 64, +-1) * 1.1, 20). */
	constexpr double SteerToRim = 64.0;
}

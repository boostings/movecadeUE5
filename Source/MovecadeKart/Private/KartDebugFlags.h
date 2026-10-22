#pragma once

#include "CoreMinimal.h"

/**
 * The kart runtime's command-line hooks for tests, look checks and captures (README.md, "Command-line switches"),
 * parsed once per process. None of them reach the sim: they change presentation, pick the track, or film it.
 * Development hooks read as off in Shipping, and so do the forced vehicle and outfit, which would skip the store.
 * Race flow switches (-KartBots, -KartSkipIntro, -KartAutoDrive, -KartShots) belong to AKartGameMode.
 */
struct FKartDebugFlags
{
	/** -KartTrack=<id>: race another exported track on this level (local play). */
	FString Track;
	/** -KartWebFeel: the web build's presentation, without the chassis springs and camera juice (FKartFeel). */
	bool bWebFeel = false;
	/** -KartNoLife: no ATrackLife (grass, crowds, butterflies, fireworks), for performance comparisons. */
	bool bNoLife = false;

	/** -KartCinematic: trailer coverage, hard cuts between the chase view and tracking shots round the kart. */
	bool bCinematic = false;
	/** -KartCloseCam=<yaw deg>,<distance m>,<height m>[,<deg/s>]: an orbit shot of each kart (yaw 0 = straight ahead
	 *  looking back at it). Y (the distance) <= 0 means off. */
	FVector4 CloseCam = FVector4(0.0, -1.0, 0.0, 0.0);
	/** -KartDebugGrip: draws the driver's hand targets on the wheel. */
	bool bDebugGrip = false;
	/** -KartVehicle=<kart|moto|coupe|truck> and -KartOutfit=<id>: every kart wears it (art checks). */
	TOptional<FString> Vehicle;
	TOptional<FString> Outfit;

	/** -KartTrackCam=x,y,z,lookX,lookY,lookZ (three.js metres, before the track's rebase): holds the first view on a
	 *  fixed camera for look checks. */
	bool bTrackCam = false;
	FVector TrackCamFrom = FVector::ZeroVector;
	FVector TrackCamLookAt = FVector::ZeroVector;
	/** -KartLookUE="key=value,...": overrides the numbers in the track JSON's look.ue for quick look passes. */
	TMap<FString, double> LookOverrides;
	/** -KartSkyRealtime: the sky light captures every frame again (comparisons with the one-off capture). */
	bool bSkyRealtime = false;
	/** -KartLifeDemo (with -KartTrackCam): a phantom kart circles the camera's look-at point, so stills show the
	 *  grass trampling and the butterflies scattering. */
	bool bLifeDemo = false;
	/** -KartFieldDebug: paints the trample field on the grass (red: flat; blue: inside the field). */
	bool bFieldDebug = false;
	/** -KartFxShowcase[=beat]: a cosmetic tour of the item effects on seat 0, with screenshots. The beat (lower case)
	 *  is empty for the whole tour, one of the beats, or "items" for every item mesh in a row. */
	bool bFxShowcase = false;
	FString FxShowcaseBeat;
	/** -KartStepTrace: the race logs a fingerprint of every kart's sim state and inputs once a second of race time,
	 *  so two runs can be compared step for step (Tools/ue/race_repeat.ps1). */
	bool bStepTrace = false;

	/** The process command line's flags, parsed on first use. */
	static const FKartDebugFlags& Get();
	/** Parses any command line (Get uses FCommandLine::Get()). */
	static FKartDebugFlags Parse(const TCHAR* CommandLine);
};

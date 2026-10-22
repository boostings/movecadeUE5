#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KartCaptureSubsystem.generated.h"

/** What -KartCapture asks for: where the frames go, and which stretch of world time to write. */
struct MOVECADEKART_API FKartCaptureSettings
{
	FString Folder;
	/** World time (s) of the first frame, and how long to capture. */
	double From = 0.0;
	double For = 0.0;
	/** -KartCaptureNoUI leaves the HUD out (trailer shots). */
	bool bShowUI = true;

	/** Reads -KartCapture=<folder> -KartCaptureFrom=<s> -KartCaptureFor=<s> -KartCaptureNoUI, with the defaults filled in. */
	static FKartCaptureSettings Parse(const TCHAR* CommandLine);
	/** The frame at world time Now is in the captured stretch. */
	bool Covers(double Now) const { return Now >= From && Now < From + For; }
	/** <Folder>/frame_NNNNN.png. */
	FString FramePath(int32 Frame) const;
};

/**
 * Frame capture for trailers and gameplay video: with -KartCapture=<folder>, every rendered frame between
 * -KartCaptureFrom=<s> and -KartCaptureFrom + -KartCaptureFor=<s> (world time) is written as
 * <folder>/frame_NNNNN.png, with the UI unless -KartCaptureNoUI.
 *
 * Run it with the engine's fixed time step (-benchmark -fps=60) so each frame is exactly 1/60 s of game time,
 * however slowly the frames are rendered and saved; Tools/video/capture.ps1 does that and encodes the MP4.
 */
UCLASS()
class MOVECADEKART_API UKartCaptureSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	FKartCaptureSettings Settings;
	int32 Frame = 0;
};

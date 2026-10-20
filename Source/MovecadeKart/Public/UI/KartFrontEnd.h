#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KartLook.h"
#include "Sim/KartTrack.h"
#include "KartFrontEnd.generated.h"

class ACameraActor;
class AKartPawn;
class APlayerController;
class SKartScreenStack;
enum class EKartMenuEntry : uint8;

/** Where the front end's camera looks. */
enum class EKartFrontEndShot : uint8
{
	/** A low flyover along the racing line (the attract loop behind the title). */
	Flyover,
	/** A slow high orbit of the whole track, behind the menus. */
	Orbit,
	/** Framing the showroom karts on the grid (racers screen). */
	Showroom,
};

/**
 * The front end, staged inside a track level: the track is the backdrop (camera flyover / orbit), the
 * showroom karts on the grid preview each player's picks, and the screens (title, main menu, race setup,
 * settings, credits) run in an SKartScreenStack. AKartGameMode spawns it when the session says "menus".
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API AKartFrontEnd : public AActor
{
	GENERATED_BODY()

public:
	AKartFrontEnd();

	static AKartFrontEnd* Open(UWorld* World, TSharedPtr<const MovecadeKart::FTrack> Track);
	static AKartFrontEnd* Find(const UWorld* World);

	virtual void Tick(float DeltaSeconds) override;

	void SetShot(EKartFrontEndShot Shot);
	/** Shows a kart per entry on the grid, wearing that look (empty hides them). */
	void SetShowroom(const TArray<FKartLook>& Looks);
	const MovecadeKart::FTrack* GetTrack() const { return Track.Get(); }

	// Flow (the screens call these) ---------------------------------------------------------------------
	/** Saves the setup and travels to its track; the level builds the race. */
	void StartRace();
	void StartParty();
	void QuitGame();

	// -KartUiShots ------------------------------------------------------------------------------------
	/** Shows the screen for a tour step and captures it once settled; false if Step isn't a menu screen. */
	bool TourShow(FName Step, double SecondsInStep);
	void TourStartRace();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void BuildScreens(EKartMenuEntry Entry);
	void PaintBackdrop(const class FKartPainter& P, const struct FKartScreenFrame& Frame) const;
	void UpdateCamera(float DeltaSeconds);
	FTransform ShotTransform(EKartFrontEndShot Shot, double Time) const;
	APlayerController* GetPlayer() const;

	TSharedPtr<const MovecadeKart::FTrack> Track;
	TSharedPtr<SKartScreenStack> Screens;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TArray<TObjectPtr<AKartPawn>> ShowroomKarts;

	EKartFrontEndShot Shot = EKartFrontEndShot::Flyover;
	EKartFrontEndShot PreviousShot = EKartFrontEndShot::Flyover;
	double ShotChangedAt = -100.0;
	double Clock = 0.0;
	bool bStarting = false;
	bool bTourRaceStarted = false;
};

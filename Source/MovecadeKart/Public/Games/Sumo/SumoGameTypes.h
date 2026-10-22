#pragma once

#include "CoreMinimal.h"
#include "Games/Sumo/SumoRules.h"
#include "KartLook.h"
#include "SumoGameTypes.generated.h"

class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/** The match's stages around the rules' own phases. */
UENUM()
enum class ESumoStage : uint8
{
	/** The intro card and the camera's sweep in from the sea; the rules hold. */
	Intro,
	Match,
	/** The champion's showcase; the rules coast the pucks to a stop underneath. */
	Finished,
};

/** Who drives a puck. */
UENUM()
enum class ESumoPilot : uint8
{
	/** The left stick or WASD roll; A, Space or a click dashes. */
	Local,
	/** A phone's tilt rolls (its attitude in sensor frames); the pad's A button dashes. */
	Phone,
	Bot,
};

/** One wrestler, as ASumoGameMode seats them for ASumoGame::Setup. */
USTRUCT()
struct FSumoSeat
{
	GENERATED_BODY()

	/** The room's player id, "seat-N" locally, "house-1" for the room's sparring partner. */
	FString Id;
	FString Name;
	/** Filled by ASumoGame::Setup: the arena's slot colour (the posts, trims and HUD all use it). */
	FLinearColor Color = FLinearColor::White;
	ESumoPilot Kind = ESumoPilot::Bot;
	/** Bots: 0 sloppy .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/** A word that pops over the action (or the screen's middle): KNOCKOUT!, SHIELD!, a round's winner. */
struct FSumoCallout
{
	FString Text;
	FString Sub;
	FLinearColor Color = FLinearColor::White;
	/** World position to float over, or zero for the screen's middle. */
	FVector World = FVector::ZeroVector;
	bool bCentre = false;
	bool bBig = false;
	/** ASumoGame's clock when it popped, and how long it stays up (s). */
	double At = 0.0;
	double Duration = 1.4;
};

/** A line of the knockout feed: who put whom in the sea. */
struct FSumoFeedLine
{
	/** INDEX_NONE for a plain ring out. */
	int32 Killer = INDEX_NONE;
	int32 Victim = INDEX_NONE;
	double At = 0.0;
};

// ---- ASumoGame's own bookkeeping. USTRUCTs so the GC sees every component and material they point at.

/** One puck's components: the body tilts and squashes under its root, the rider stands on the deck. */
USTRUCT()
struct FSumoPuckParts
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	/** The lean and squash pivot under Root (the shield stays upright on Root). */
	UPROPERTY()
	TObjectPtr<USceneComponent> Tilt;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Shield;
	/** The heavy power-up's iron ring. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Weight;
	UPROPERTY()
	TObjectPtr<UArcadeAvatarComponent> Rider;
	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> Trail;
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> Sparks;
	/** Per puck (their parameters change every frame): the shell's emissive, the trim's dash-ready glow, the shield. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Paint;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Trim;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ShieldMat;
};

/** A pooled power-up pickup: matched to the rules' power-ups by id, it pops in and shrinks away when taken. */
USTRUCT()
struct FSumoPowerParts
{
	GENERATED_BODY()

	/** The rules' power-up id shown, or INDEX_NONE while free. */
	int32 Id = INDEX_NONE;
	double BornAt = 0.0;
	/** When the rules dropped it (taken or expired), or < 0 while it is live. */
	double GoneAt = -1.0;
	MovecadeKart::Sumo::EPower Kind = MovecadeKart::Sumo::EPower::None;
	FVector2D Pos = FVector2D::ZeroVector;

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;
	/** One icon per power (heavy, shield, turbo); only the live kind's shows. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Icons[3];
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Halo;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HaloMat;
	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;
};

/** "Long ago" for presentation moments (s on ASumoGame's clock): nothing is still playing out from it. */
constexpr double SumoLongAgo = -10.0;

/** One puck's driver state and its presentation history (no UObjects). */
struct FSumoPilot
{
	ESumoPilot Kind = ESumoPilot::Bot;
	MovecadeKart::Sumo::FBotBrain Brain;
	// Local: the smoothed keys and stick, a queued press.
	FVector2D LocalTilt = FVector2D::ZeroVector;
	bool bDashQueued = false;
	// Phone: the latest tilt and when it came (ASumoGame's clock).
	FVector2D PhoneTilt = FVector2D::ZeroVector;
	/** Never heard from: a phone seat starts quiet until its first frame. */
	double PhoneAt = -100.0;
	// Presentation: the previous step (to interpolate and lean), the smoothed lean, the hit and dash moments.
	FVector2D PrevPos = FVector2D::ZeroVector;
	FVector2D PrevVel = FVector2D::ZeroVector;
	FVector2D AccelSmooth = FVector2D::ZeroVector;
	double Yaw = 0.0;
	double Squash = 0.0;
	double HitAt = SumoLongAgo;
	FVector2D HitDir = FVector2D::ZeroVector;
	double DashAt = SumoLongAgo;
	double ReadyFlashAt = SumoLongAgo;
	bool bWasReady = true;
	double CheerAt = SumoLongAgo;
	/** Deg/s of the fall's spin, picked at the ring out. */
	double FallSpin = 0.0;
};

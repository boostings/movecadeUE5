#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FX/KartFx.h"
#include "Games/ArcadeGame.h"
#include "Games/GameAudio.h"
#include "Games/Samurai/SamuraiBlade.h"
#include "Games/Samurai/SamuraiRules.h"
#include "Games/Samurai/SamuraiTypes.h"
#include "ProceduralMeshComponent.h"
#include "SamuraiGame.generated.h"

class ACameraActor;
class APlayerController;
class ASamuraiDojo;
class SSamuraiHud;
class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

namespace MovecadeKart::Net { struct FSensorFrame; }

/**
 * One round of Samurai Slice in the dojo (Docs/Port/08-samurai.md): 1-4 swordsmen, each with a lane of fruit, bombs
 * and scrolls. The rules (SamuraiRules) and every blade (SamuraiBlade) step together on a fixed 60 Hz clock: each
 * step samples each lane's katana (a phone's attitude, the mouse or pad's aim, a bot's planned sweep) and, while
 * the sweep is meant, resolves whatever the blade passed through since the last step (M2). Presentation follows:
 * the targets spin on their arcs with the bomb's telegraph and the halo of a precise moment, cuts leave halves and
 * juice, bombs blow, the katana leaves a trail, the swordsman follows the blade, the camera punches and hit-stops.
 *
 * ASamuraiGameMode spawns and owns it, feeds it phone frames in Party Mode and listens to its callbacks; SSamuraiHud
 * paints from its getters. The implementation is split by job: SamuraiGame.cpp (setup and the round's flow),
 * SamuraiGameBlades.cpp (input, blades, bots, contact), SamuraiGamePresentation.cpp (targets, halves, swordsmen,
 * trails, juice), SamuraiGameCamera.cpp and SamuraiGamePractice.cpp. Its numbers live in SamuraiTuning.h.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ASamuraiGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ASamuraiGame();

	void Setup(ASamuraiDojo* InDojo, const TArray<FSamuraiSeat>& InSeats, APlayerController* InViewer,
		uint32 Seed = MovecadeKart::Samurai::DefaultSeed, const FSamuraiDebugFlags& InDebugFlags = FSamuraiDebugFlags());
	/** A phone's sensor frame for a seat (by player id): its attitude aims the blade, its sweep is the intent. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	/** A fresh round with the same seats and the next seed. */
	void Rematch();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** When the intro's title card gives way to the practice (or the match). */
	static constexpr double TitleSeconds = 3.2;

	/** Web world (metres) to UE cm. */
	static FVector ToWorld(const FVector& Web);

	// ---- For the HUD ----
	const MovecadeKart::Samurai::FMatch& GetMatch() const { return Match; }
	ESamuraiStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetFinishedAge() const { return Stage == ESamuraiStage::Finished ? GetStageAge() : -1.0; }
	/** The round's clock (slow motion scales it). */
	double GetClock() const { return Clock; }
	const TArray<FSamuraiSeat>& GetSeats() const { return Seats; }
	const FSamuraiSeat* SeatOf(const FString& Id) const;
	bool HasLocal() const;
	/** Lanes the screen treatment follows: the humans (lane 0 when all lanes are bots). */
	bool IsFeatured(int32 LaneIndex) const;
	/** Whose camera the HUD projects through. */
	APlayerController* GetViewer() const { return Viewer; }
	const TArray<FSamuraiNote>& GetNotes() const { return Notes; }
	/** The centre banner (FOCUS!, 10x COMBO!) and when it rose; empty when none. */
	const FText& GetBanner() const { return Banner; }
	double GetBannerAge() const { return Clock - BannerAt; }
	FLinearColor GetBannerColor() const { return BannerColor; }
	/** How much of the screen's Focus treatment is on (0..1). */
	float GetFocusMood() const { return FocusMood; }
	/** The practice moment: its state, how long it has run, the apple's place (UE cm). */
	ESamuraiPractice GetPractice() const { return Practice; }
	double GetPracticeAge() const { return Clock - PracticeAt; }
	FVector GetPracticeWorld() const { return ToWorld(PracticeWeb); }

	// ---- Callbacks (the mode's room glue) ----
	TFunction<void()> OnMatchStart;
	/** Every new effect but slashes (hits, combos, bombs, misses): the phones' toasts. */
	TFunction<void(const MovecadeKart::Samurai::FEffect&)> OnEffect;
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	TFunction<void()> OnRematch;

	// ---- IArcadeGame (SArcadeOverlay: pause, results, play again, next game) ----
	virtual FString GetArcadeId() const override { return TEXT("samurai"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	/** One swordsman's blade and everything that follows it. */
	struct FLane
	{
		FString PlayerId;
		ESwordKind Kind = ESwordKind::Bot;
		double LaneX = 0.0;
		/** The blade now and at the previous step (web metres). */
		MovecadeKart::Samurai::FBlade Blade;
		MovecadeKart::Samurai::FBlade Previous;
		/** Is this sweep meant (the intent gate, or a bot's planned cut)? */
		bool bIntent = false;
		MovecadeKart::Samurai::FBladeIntent Intent;
		/** Local: where the tip is aimed. */
		FVector TipGoal = FVector::ZeroVector;
		/** Phone: the latest attitude and when it came. */
		FVector PhoneDirection = FVector(0.0, 1.0, 0.0);
		double PhoneAt = -10.0;
		bool bPhoneIntent = false;
		/** Bot: the planned sweep, on the rules' clock. */
		FVector SweepFrom = FVector::ZeroVector;
		FVector SweepTo = FVector::ZeroVector;
		double SweepAt = -10.0;
		double SweepSeconds = 0.18;
		double NextThinkAt = 0.0;
		/** Presentation: the trail's samples (inner, tip; UE cm). */
		TArray<TPair<FVector, FVector>> Trail;
		double TrailAt = -10.0;
		bool bTrailShown = false;
		double LastCutAt = -10.0;
		/** The cut's follow-through: which way the tip was going (-1 left, 1 right) and how hard (0..1). */
		double CutSide = 1.0;
		double CutPower = 0.0;
		/** How fast the blade is moving, smoothed (0..1): the swordsman's eagerness. */
		double Energy = 0.0;
		/** Feedback the rules don't give: a sweep too slow to cut, a sweep that touched nothing. */
		double HintAt = -10.0;
		bool bSweepHit = false;
		bool bWasIntent = false;
		bool bWasFocus = false;
	};

	/** One flying half of a cut target, in the ring over PiecePool. */
	struct FPiece
	{
		bool bLive = false;
		FVector Origin = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		/** Which way this half parts (UE). */
		FVector Normal = FVector::ZeroVector;
		double Scale = 1.0;
		double BornAt = 0.0;
		double SpinSign = 1.0;
	};

	/** A pooled target component, held by one target id from launch to removal (dressed once). */
	struct FTargetSlot
	{
		int32 TargetId = INDEX_NONE;
		FQuat Spin = FQuat::Identity;
		/** The UpdateTargets pass that last saw this target: an older stamp means it's gone. */
		uint32 Stamp = 0;
		double LastProgress = 0.0;
		MovecadeKart::Samurai::EKind Kind = MovecadeKart::Samurai::EKind::Fruit;
		int32 Lane = 0;
		FVector LastAt = FVector::ZeroVector;
	};

	// ---- Setup and flow (SamuraiGame.cpp) ----
	void SetupLanes();
	void SetupProps();
	void SetupTargetPools();
	void SetupTrailTopology(TArray<int32>& OutTriangles);
	void SetupSwordsmen(const TArray<int32>& TrailTriangles);
	void SetupCameraAndHud();
	UStaticMeshComponent* MakePropComponent(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows = true);
	void SetStage(ESamuraiStage Next);
	/** Advances both clocks by a frame; returns the round's (slow-motion scaled) step. */
	float AdvanceClocks(float DeltaSeconds, float& OutRealDt);
	void TickIntro(float Dt, bool bSkip);
	void TickMatch(float Dt);
	void FinishRound();
	void WatchFocus();
	void SampleAllBlades(double DtSeconds);
	MovecadeKart::Samurai::FMatch NewMatch(uint32 Seed) const;
	bool IsHuman(int32 LaneIndex) const;
	int32 LaneCount() const { return Lanes.Num(); }

	// ---- Input, blades, bots and contact (SamuraiGameBlades.cpp) ----
	/** Moves the local tips from the mouse, sticks and keys; true when skip (Space, Enter, pad A) was pressed. */
	bool ReadLocalInput(float RealDt);
	void SampleBlade(FLane& Lane, int32 Index, double DtSeconds);
	FVector AimLocal(FLane& Lane, double DtSeconds, bool& bOutIntent);
	FVector AimPhone(const FLane& Lane, bool& bOutIntent) const;
	FVector AimBot(FLane& Lane, int32 Index, bool& bOutIntent);
	void PlanBot(FLane& Lane, const FSamuraiSeat& Seat);
	void StepFixed(double DtMs, double DtSeconds);
	void ResolveContacts(FLane& Lane);
	void PublishNewEffects();
	void FeedbackForMisses(FLane& Lane, int32 Index);
	bool LaneCalloutActive(int32 LaneIndex) const;
	/** The plane a lane's last sweep cut along, as its normal (UE); zero when the blade barely moved. */
	FVector CutNormal(int32 LaneIndex) const;
	FVector KamaeDirection(double LaneX) const;

	// ---- Presentation (SamuraiGamePresentation.cpp) ----
	void UpdatePresentation(float Dt, float RealDt);
	void PresentEffect(const MovecadeKart::Samurai::FEffect& Effect);
	void PresentCut(const MovecadeKart::Samurai::FEffect& Effect);
	void PresentBomb(const MovecadeKart::Samurai::FEffect& Effect, bool bFeatured);
	/** Both halves of a cut target, turned so they part along the lane's last sweep, and the juice. */
	void SplitTarget(const MovecadeKart::Samurai::FTarget& Target, const FVector& At, const FQuat& Spin, int32 LaneIndex, bool bPerfect);
	void SpawnPiece(const MovecadeKart::Samurai::FTarget& Target, const FVector& At, const FQuat& Rotation, bool bPlus);
	/** Puts a variant's mesh (whole or a half) and its materials on a pooled component. */
	void Dress(UStaticMeshComponent* Component, MovecadeKart::Samurai::EVariant Variant, int32 Part) const;
	void Note(const FVector& World, const FText& Label, const FLinearColor& Color, double Life = 0.9, float Scale = 1.f);
	void Shout(const FText& Text, const FLinearColor& Color);
	void SlowMo(double Seconds);
	void Flash(const FLinearColor& Color, double Power, double Fringe);
	void UpdateMood(float RealDt);
	void UpdateTargets(float Dt);
	/** The target's pooled component (dressed on first use); INDEX_NONE when the pool is full. */
	int32 AcquireSlot(const MovecadeKart::Samurai::FTarget& Target);
	/** A bomb's telegraph or a halo for the target, from the next free marker in its pool. */
	void PlaceMarker(const MovecadeKart::Samurai::FTarget& Target, const FVector& At, double Scale, int32& Telegraphs, int32& Halos);
	void ReleaseSlot(int32 Slot);
	void UpdatePieces();
	void UpdateSwordsmen();
	void PoseSwordsman(const FLane& Lane, UArcadeAvatarComponent& Avatar) const;
	void PlaceKatana(const FLane& Lane, UStaticMeshComponent& Katana) const;
	/** The swordsman's cheer and slump now: the lane's latest combo or bomb, then the result. */
	void Reactions(const FLane& Lane, float& OutCheer, float& OutSlump) const;
	void UpdateTrails();
	void ResetPresentation();

	// ---- Camera (SamuraiGameCamera.cpp) ----
	void UpdateCamera(float Dt);
	double ViewportAspect() const;

	// ---- Practice (SamuraiGamePractice.cpp) ----
	/** Hangs the practice apple in the local lane (or marks the practice done when there is no local swordsman). */
	void BeginPractice();
	void UpdatePractice(float Dt);
	void CutPracticeApple(FLane& Lane, const FVector& At, double Age);
	void EndPractice();

	UPROPERTY()
	TObjectPtr<ASamuraiDojo> Dojo;

	UPROPERTY()
	TObjectPtr<APlayerController> Viewer;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TArray<TObjectPtr<UArcadeAvatarComponent>> Avatars;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Katanas;

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> Trails;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TargetPool;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TelegraphPool;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> HaloPool;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PiecePool;

	/** Whole, plus and minus meshes per variant (index variant * PartCount + part). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> TargetMeshes;

	/** Every variant's slot materials, back to back (MaterialStart says where each variant's run begins). */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> TargetMaterials;

	/** Each lane's trail, white-hot in its seat's colour. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> TrailMaterials;

	/** The practice apple (onboarding) and its halo. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> PracticeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> PracticeHalo;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> TelegraphMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HaloMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FocusHaloMaterial;

	/** The material each halo wears now (set only on change). */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> HaloWorn;

	// Seats and the rules.
	TArray<FSamuraiSeat> Seats;
	TArray<FLane> Lanes;
	MovecadeKart::Samurai::FMatch Match;
	uint32 MatchSeed = MovecadeKart::Samurai::DefaultSeed;
	FSamuraiDebugFlags DebugFlags;
	FRandomStream BotRng;
	/** Effect ids are monotonic (the rules' NextId): everything above this is new. */
	int32 LastSeenEffectId = 0;
	TSet<int32> HitScratch;

	// Pools.
	TArray<int32> MaterialStart;
	/** Target id to its slot in TargetPool (and Slots). */
	TMap<int32, int32> TargetSlots;
	TArray<FTargetSlot> Slots;
	uint32 SlotStamp = 0;
	/** A ring over PiecePool: slot i is Pieces[i]. */
	TArray<FPiece> Pieces;
	int32 NextPiece = 0;
	/** The trail ribbons' fixed topology, reused every frame. */
	TArray<FVector> TrailVerts;
	TArray<FVector> TrailNormals;
	TArray<FVector2D> TrailUVs;
	TArray<FLinearColor> TrailColors;
	TArray<FProcMeshTangent> TrailTangents;
	/** Reused for every burst of particles. */
	TArray<MovecadeKart::Fx::FKartFxEmit> FxScratch;
	MovecadeKart::Games::FCuePlayer Cues;

	// Clocks and flow.
	ESamuraiStage Stage = ESamuraiStage::Intro;
	double Clock = 0.0;
	double StageAt = 0.0;
	double Accumulator = 0.0;
	bool bPaused = false;
	/** Unscaled seconds (slow motion scales Clock, not this). */
	double RealClock = 0.0;

	// Juice.
	/** Hit-stop: the visuals hold until then while the rules run on. */
	double FrozenUntil = -10.0;
	double Shake = 0.0;
	double Punch = 0.0;
	double SlowAt = -10.0;
	double SlowSeconds = 0.5;
	double FlashAt = -10.0;
	double FlashPower = 0.0;
	double FringePower = 0.0;
	FLinearColor FlashColor = FLinearColor::White;
	float FocusMood = 0.f;
	double FovKick = 0.0;
	TArray<FSamuraiNote> Notes;
	FText Banner;
	FLinearColor BannerColor = FLinearColor::White;
	double BannerAt = -10.0;

	// The practice apple.
	ESamuraiPractice Practice = ESamuraiPractice::None;
	bool bPracticeDone = false;
	double PracticeAt = 0.0;
	int32 PracticeLane = INDEX_NONE;
	FVector PracticeWeb = FVector::ZeroVector;
	FQuat PracticeSpin = FQuat::Identity;

	// The camera, smoothed (web metres and degrees).
	FVector CameraPosition = FVector::ZeroVector;
	FVector CameraLook = FVector::ZeroVector;
	double CameraFov = 45.0;
	bool bCameraPlaced = false;

	TSharedPtr<SSamuraiHud> Hud;
};

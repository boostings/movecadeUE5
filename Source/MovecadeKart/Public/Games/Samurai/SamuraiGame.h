#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeGame.h"
#include "Games/GameAudio.h"
#include "Games/Samurai/SamuraiBlade.h"
#include "Games/Samurai/SamuraiRules.h"
#include "KartLook.h"
#include "ProceduralMeshComponent.h"
#include "SamuraiGame.generated.h"

class ASamuraiDojo;
class ACameraActor;
class APlayerController;
class SSamuraiHud;
class UArcadeAvatarComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

namespace MovecadeKart::Net { struct FSensorFrame; }

UENUM()
enum class ESamuraiStage : uint8
{
	/** The intro card and the camera's sweep onto the dojo; the round clock holds. */
	Intro,
	Match,
	Finished,
};

UENUM()
enum class ESwordKind : uint8
{
	/** The mouse or the right stick moves the blade's tip (M9). */
	Local,
	/** A phone is the hilt: its attitude is the blade, its sweep the intent. */
	Phone,
	Bot,
};

/** A presentation-only note floating over the dojo (never from the rules): coaching and the misses they don't call. */
struct FSamuraiNote
{
	FVector World = FVector::ZeroVector;
	FText Label;
	FLinearColor Color = FLinearColor::White;
	double BornAt = 0.0;
	double Life = 0.9;
	float Scale = 1.f;
};

/** The onboarding moment in the intro: one practice apple hangs in the local swordsman's lane until it's cut. */
enum class ESamuraiPractice : uint8
{
	None,
	Waiting,
	TooSlow,
	Cut,
};

/** The viewer's projection, built once per HUD paint (rather than per projected point). */
struct FSamuraiView
{
	FMatrix ViewProjection = FMatrix::Identity;
	FIntRect Rect;
	FVector2D ViewportSize = FVector2D::ZeroVector;
	bool bValid = false;

	/** World (UE cm) to 0..1 of the viewport; false behind the camera. */
	bool Project(const FVector& World, FVector2D& Out01) const;
};

USTRUCT()
struct FSamuraiSeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	ESwordKind Kind = ESwordKind::Bot;
	/** Bots: 0 wild .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/**
 * One round of Samurai Slice in the dojo (Docs/Port/08-samurai.md): 1-4 swordsmen, each with a lane of fruit, bombs
 * and scrolls. The rules (SamuraiRules) and every blade (SamuraiBlade) step together on a fixed 60 Hz clock: each
 * step samples each lane's katana (a phone's attitude, the mouse or pad's aim, a bot's planned sweep) and, while
 * the sweep is meant, resolves whatever the blade passed through since the last step (M2). Presentation follows:
 * the targets spin on their arcs with the bomb's telegraph and the halo of a precise moment, cuts leave halves and
 * juice, bombs blow, the katana leaves a trail, the swordsman follows the blade, the camera punches and hit-stops.
 */
UCLASS(NotPlaceable)
class MOVECADEKART_API ASamuraiGame : public AActor, public IArcadeGame
{
	GENERATED_BODY()

public:
	ASamuraiGame();

	void Setup(ASamuraiDojo* InDojo, const TArray<FSamuraiSeat>& InSeats, APlayerController* InViewer, uint32 Seed = MovecadeKart::Samurai::DefaultSeed);
	/** A phone's sensor frame for a seat (by player id): its attitude aims the blade, its sweep is the intent. */
	void FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame);
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// For the HUD ---------------------------------------------------------------------------------------------
	const MovecadeKart::Samurai::FMatch& GetMatch() const { return Match; }
	ESamuraiStage GetStage() const { return Stage; }
	double GetStageAge() const { return Clock - StageAt; }
	double GetClock() const { return Clock; }
	const TArray<FSamuraiSeat>& GetSeats() const { return Seats; }
	const FSamuraiSeat* SeatOf(const FString& Id) const;
	bool ScreenOf(const FVector& World, FVector2D& Out01) const;
	/** The viewer's projection now (one GetProjectionData for a whole paint). */
	FSamuraiView MakeView() const;
	/** Web world to UE cm. */
	static FVector W(const FVector& Web);
	double GetFinishedAge() const { return Stage == ESamuraiStage::Finished ? GetStageAge() : -1.0; }
	bool HasLocal() const;
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
	FVector GetPracticeWorld() const { return W(PracticeWeb); }
	/** When the intro's title card gives way to the practice (or the match). */
	static constexpr double TitleSeconds = 3.2;
	/** Lanes the screen treatment follows: the humans (all lanes' bots when there are none, lane 0). */
	bool IsFeatured(int32 LaneIndex) const;

	TFunction<void()> OnMatchStart;
	/** Every new effect but slashes (hits, combos, bombs, misses): the phones' toasts. */
	TFunction<void(const MovecadeKart::Samurai::FEffect&)> OnEffect;
	TFunction<void()> OnFinished;
	TFunction<void()> OnQuit;
	TFunction<void()> OnRematch;

	void Rematch();

	// IArcadeGame (SArcadeOverlay: pause, results, play again, next game) -----------------------------------
	virtual FString GetArcadeId() const override { return TEXT("samurai"); }
	virtual double GetArcadeFinishedAge() const override { return GetFinishedAge(); }
	virtual void SetArcadePaused(bool bInPaused) override { bPaused = bInPaused; }
	virtual bool IsArcadePaused() const override { return bPaused; }
	virtual void ArcadeRestart() override;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const override;

private:
	struct FLane
	{
		FString PlayerId;
		ESwordKind Kind = ESwordKind::Bot;
		double LaneX = 0.0;
		/** The blade now and at the previous step (web metres). */
		MovecadeKart::Samurai::FBlade Blade;
		MovecadeKart::Samurai::FBlade Previous;
		bool bHasPrevious = false;
		/** Is this sweep meant (the intent gate, or a bot's planned cut)? */
		bool bIntent = false;
		MovecadeKart::Samurai::FBladeIntent Intent;
		// Local: where the tip is aimed.
		FVector TipGoal = FVector::ZeroVector;
		// Phone: the latest attitude and when it came.
		FVector PhoneDir = FVector(0.0, 1.0, 0.0);
		double PhoneAt = -10.0;
		bool bPhoneIntent = false;
		// Bot: the planned sweep.
		FVector SweepFrom = FVector::ZeroVector;
		FVector SweepTo = FVector::ZeroVector;
		double SweepAt = -10.0;
		double SweepSeconds = 0.18;
		double NextThinkAt = 0.0;
		// Presentation.
		TArray<TPair<FVector, FVector>> Trail;
		double TrailAt = -10.0;
		bool bTrailShown = false;
		double LastCutAt = -10.0;
		/** The cut's follow-through: which way the tip was going (-1 left, 1 right) and how hard (0..1). */
		double CutSide = 1.0;
		double CutPower = 0.0;
		double Energy = 0.0;
		// Feedback the rules don't give: a sweep too slow to cut, a sweep that touched nothing.
		double HintAt = -10.0;
		bool bSweepHit = false;
		bool bWasIntent = false;
		bool bWasFocus = false;
	};

	struct FPiece
	{
		MovecadeKart::Samurai::EVariant Variant = MovecadeKart::Samurai::EVariant::Apple;
		bool bPlus = true;
		bool bLive = false;
		FVector Origin = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
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
		uint32 Stamp = 0;
		double LastProgress = 0.0;
		MovecadeKart::Samurai::EKind Kind = MovecadeKart::Samurai::EKind::Fruit;
		int32 Lane = 0;
		FVector LastAt = FVector::ZeroVector;
	};

	void SetStage(ESamuraiStage Next);
	void StepFixed(double DtMs, double DtSeconds);
	void SampleBlade(FLane& Lane, int32 Index, double DtSeconds);
	void PlanBot(FLane& Lane, const FSamuraiSeat& Seat);
	void PresentEffect(const MovecadeKart::Samurai::FEffect& Effect);
	void UpdateTargets(float Dt);
	void UpdatePieces();
	void UpdateSwordsmen(float Dt);
	void UpdateTrails();
	void UpdateCamera(float Dt);
	void SpawnPiece(const MovecadeKart::Samurai::FTarget& Target, const FVector& At, const FQuat& Rotation, bool bPlus);
	/** The juice, pulp and light of a cut through a target at At (UE), parting along Normal (UE). */
	void CutFx(MovecadeKart::Samurai::EVariant Variant, const FVector& At, const FVector& Normal, double Scale, bool bPerfect);
	/** The plane a lane's last sweep cut along, as its normal (UE); zero when the blade barely moved. */
	FVector CutNormal(int32 LaneIndex) const;
	void Note(const FVector& World, const FText& Label, const FLinearColor& Color, double Life = 0.9, float Scale = 1.f);
	void Shout(const FText& Text, const FLinearColor& Color);
	void SlowMo(double Seconds = 0.5);
	void Flash(const FLinearColor& Color, double Power, double Fringe);
	void UpdatePractice(float Dt);
	void UpdateMood(float RealDt);
	void FeedbackForMisses(FLane& Lane, int32 Index);
	bool IsHuman(int32 LaneIndex) const;
	void ReleaseSlot(int32 Slot);
	void ResetPresentation();
	FVector KamaeDirection(double LaneX) const;
	int32 LaneCount() const { return Lanes.Num(); }

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

	/** Whole, plus and minus meshes per variant (index variant * 3 + part). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> TargetMeshes;

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

	TArray<FSamuraiSeat> Seats;
	TArray<FLane> Lanes;
	MovecadeKart::Samurai::FMatch Match;
	uint32 MatchSeed = MovecadeKart::Samurai::DefaultSeed;
	TArray<int32> MaterialStart;
	/** Target id to its slot in TargetPool (and Slots). */
	TMap<int32, int32> TargetSlots;
	TArray<FTargetSlot> Slots;
	uint32 SlotStamp = 0;
	/** The material each halo wears now (set only on change). */
	TArray<TObjectPtr<UMaterialInterface>> HaloWorn;
	/** Effect ids are monotonic (the rules' NextId): everything above this is new. */
	int32 LastSeenEffectId = 0;
	TSet<int32> HitScratch;
	/** A ring over PiecePool: slot i is Pieces[i]. */
	TArray<FPiece> Pieces;
	int32 NextPiece = 0;
	// The trail ribbons' fixed topology, reused every frame.
	TArray<FVector> TrailVerts;
	TArray<FVector> TrailNormals;
	TArray<FVector2D> TrailUVs;
	TArray<FLinearColor> TrailColors;
	TArray<FProcMeshTangent> TrailTangents;
	FRandomStream BotRng;
	MovecadeKart::Games::FCuePlayer Cues;

	ESamuraiStage Stage = ESamuraiStage::Intro;
	double Clock = 0.0;
	double StageAt = 0.0;
	double Accumulator = 0.0;
	bool bPaused = false;
	/** Hit-stop: the visuals hold until then while the rules run on. */
	double FrozenUntil = -10.0;
	double Shake = 0.0;
	double Punch = 0.0;
	FVector2D MouseTip = FVector2D::ZeroVector;

	/** Unscaled seconds (slow motion scales Clock, not this). */
	double RealClock = 0.0;
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
	ESamuraiPractice Practice = ESamuraiPractice::None;
	bool bPracticeDone = false;
	bool bPracticeDemo = false;
	double PracticeAt = 0.0;
	int32 PracticeLane = INDEX_NONE;
	FVector PracticeWeb = FVector::ZeroVector;
	FQuat PracticeSpin = FQuat::Identity;

	FVector CamPos = FVector::ZeroVector;
	FVector CamLook = FVector::ZeroVector;
	double CamFov = 45.0;
	bool bCamPlaced = false;

	TSharedPtr<SSamuraiHud> Hud;
};

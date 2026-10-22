#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Feel/KartFeel.h"
#include "KartDriverExpression.h"
#include "Avatar/AvatarDressing.h"
#include "KartLook.h"
#include "KartPawn.generated.h"

class UCameraComponent;
class USphereComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UKartMovementComponent;
class UKartAudioComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Item-driven presentation for one kart (set by AKartItemPresenter each frame). */
struct FKartItemVisual
{
	double SpinDegrees = 0.0;
	double PopCm = 0.0;
	double TumbleDegrees = 0.0;
	double Scale = 1.0;
	bool bAura = false;
	bool bRocket = false;
};

/**
 * A kart: a sphere root that only carries the sim pose, a visual body with the web build's
 * presentation layers (slip yaw, body yaw, lean, hop), and the chase camera (Docs/Port/01 §9, §10).
 * The body is the shipped vehicle art (kart.glb or a moto/coupe/truck mount) with separate wheels that
 * steer and spin, a skinned avatar4 driver holding the wheel, and a name plate (SetLook).
 */
UCLASS()
class MOVECADEKART_API AKartPawn : public APawn
{
	GENERATED_BODY()

public:
	AKartPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	UKartMovementComponent* GetKartMovement() const { return Movement; }
	UKartAudioComponent* GetKartAudio() const { return Audio; }
	/** Snap the chase camera behind the kart (after spawn or respawn). */
	void SnapCamera();
	/** Repaints the current vehicle (and a jersey that follows the paint). */
	void SetPaint(const FLinearColor& Color);
	/** Vehicle, paint, driver and name plate. Safe to call again at any time. */
	void SetLook(const FKartLook& InLook);
	const FKartLook& GetLook() const { return Look; }
	/** Split-screen rig: fixed 6/8 m offsets and a 75° vertical FOV (SplitScreenRenderer); otherwise the single-view speed rig. */
	void SetSplitScreenCamera(bool bSplit) { bSplitScreenCamera = bSplit; }
	void SetItemVisual(const FKartItemVisual& InVisual) { ItemVisual = InVisual; }
	/** The driver's right hand leaves the wheel to throw or fire (drivingExpression.ts). */
	void PlayItemThrow();
	/** This kart's finishing place (1 = winner): the podium cheers, the rest slump. 0 = not finished. */
	void SetFinishPlace(int32 Place) { FinishPlace = Place; }
	/** 0..1 revving on the grid during the countdown. */
	void SetGridRev(float Amount) { GridRev = Amount; }
	/** The countdown's opening shot: the camera cranes down from high over the grid onto the chase view. */
	void PlayCountdownSwoop(double Seconds);
	/** GO: a punch of FOV and pull-back. */
	void PlayRaceStart();
	/** Camera shake (kartFxBus.ts): peak in metres, decaying at 6.5/s; a new kick keeps the larger envelope. */
	void AddShake(double PeakMetres);

	UPROPERTY(EditAnywhere, Category = "Kart")
	FLinearColor PaintColor = FLinearColor(0.871f, 0.084f, 0.270f); // #f0508e, the v2 "pink" paint

protected:
	virtual void BeginPlay() override;

private:
	void UpdateVisual(float DeltaSeconds);
	void UpdateDriver(float DeltaSeconds);
	/** The driver's acting on top of the grip pose: moods, glances, whiplash. Fills the anim input. */
	void UpdateDriverMood(float DeltaSeconds, struct FKartDriverPoseInput& In);
	/** Blends and writes the face morphs from the moods. */
	void UpdateFace(float DeltaSeconds);
	void UpdateNamePlate();
	void UpdateCamera(float DeltaSeconds);
	void UpdateFeel(float DeltaSeconds);
	/** Propagates the slip/body/chassis pivots' directly set transforms once (see UpdateVisual). */
	void PoseVehicle();
	void ApplyVehicle();
	void ApplyDriver();
	void ApplyColours();

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USphereComponent> Root;

	/** Slip yaw (kartRef). */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USceneComponent> SlipPivot;

	/** Drift and body yaw, lean and hop (groupRef). */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USceneComponent> BodyPivot;

	/** The vehicle's model frame: GLB units x100, model forward (+Y) turned to the kart's +X, origin 49 cm up (wheels on the road). */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USceneComponent> VehicleRoot;

	/** The sprung body: pitch, roll, heave and squash from FKartFeel. The wheels stay on VehicleRoot, on the road. */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USceneComponent> ChassisPivot;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> Trim;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> Booster;

	/** The steering wheel (or handlebar) turns about this pivot. */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USceneComponent> GripPivot;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> Grip;

	/** Hub pivots (steer + spin), 0/1 front. */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TArray<TObjectPtr<USceneComponent>> WheelPivots;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TArray<TObjectPtr<UStaticMeshComponent>> Wheels;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USkeletalMeshComponent> Driver;

	/** A paid outfit over the driver (leader-posed to it). */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<USkeletalMeshComponent> Outfit;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UTextRenderComponent> NamePlate;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> NamePlateBack;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UKartMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UKartAudioComponent> Audio;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;

	/** The driver's face, body, outfit and tints from the look. */
	MovecadeKart::Avatar::FDressing Dressing;

	FKartLook Look;
	bool bLookSet = false;
	bool bVehicleBuilt = false;
	EKartVehicle BuiltVehicle = EKartVehicle::Kart;

	/** Steering wheel frame in VehicleRoot space: centre, normal toward the driver, radius (cm). */
	FVector GripCentre = FVector::ZeroVector;
	FVector GripNormal = FVector::UpVector;
	double GripRadius = 0.0;
	bool bHandlebar = false;
	bool bStraddle = false;
	TArray<FVector> WheelHubs;
	double WheelSpin = 0.0;
	double RimTurn = 0.0;
	double NextBlink = 2.0;
	/** 0..1, eases in once this kart has finished. */
	double Cheer = 0.0;
	double ItemThrowStart = -1000.0;
	int32 FinishPlace = 0;
	float GridRev = 0.f;
	uint32 SeenDriftBoosts = 0;

	// Camera transitions (UpdateCamera).
	double SwoopStart = -1000.0;
	double SwoopSeconds = 2.4;
	/** World time the finish orbit began, or < 0 before this kart finishes. */
	double FinishCamStart = -1.0;
	/** The teleport the camera last saw; a new one drops the camera in from above instead of whipping across. */
	uint32 CameraTeleports = 0;
	/** World time of the last respawn: the kart pops back in. */
	double RespawnPopStart = -1000.0;
	/** The lightning shrink, eased. */
	double ShrinkScale = 1.0;

	/** The driver's moods, eased (see UpdateDriverMood). */
	struct FDriverMood
	{
		double Drift = 0.0;
		double Boost = 0.0;
		double Rev = 0.0;
		double Spin = 0.0;
		double Slump = 0.0;
		double LookWeight = 0.0;
		FVector LookAt = FVector::ZeroVector;
		TWeakObjectPtr<AKartPawn> LookTarget;
		double LookUntil = 0.0;
		float Face[MovecadeKart::DriverFace::MorphCount] = {};
		/** No rival in range: the next search for one (karts are searched a few times a second, not every frame). */
		double NextLookSearch = 0.0;
	} Mood;

	/** What was last written to the paint and the face morphs, so unchanged values aren't pushed every frame. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SentPaintMaterial;
	FLinearColor SentPaint = FLinearColor::Transparent;
	TWeakObjectPtr<UObject> SentMorphMesh;
	float SentFace[MovecadeKart::DriverFace::MorphCount] = {};
	float SentBlink = -1.f;
	/** Whiplash springs, kicked by impacts: torso thrown back, and sideways. */
	MovecadeKart::FFeelSpring JoltBack;
	MovecadeKart::FFeelSpring JoltSide;
	double BlinkStart = -10.0;

	bool bSplitScreenCamera = false;
	FKartItemVisual ItemVisual;
	double ShakePeak = 0.0;
	double ShakeStart = -1000.0;

	/** Chassis and camera juice on top of the sim's presentation layers (-KartWebFeel turns it off). */
	MovecadeKart::FKartFeel Feel;

	uint32 SeenBarrierEvents = 0;
	uint32 SeenBumpEvents = 0;
	bool bDebugGrip = false;
	/** -KartCinematic: trailer camera coverage instead of the chase rig. */
	bool bCinematic = false;
	/** -KartCloseCam: yaw, distance and height of an orbit shot, and its rate; Y <= 0 means off. */
	FVector4 CloseCam = FVector4(0, -1, 0, 0);

	FVector CameraPos = FVector::ZeroVector;
	FVector CameraLook = FVector::ZeroVector;
	int32 LoggedLaps = 0;
	double LastLapTime = 0.0;
};

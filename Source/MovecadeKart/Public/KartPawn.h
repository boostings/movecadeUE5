#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Feel/KartFeel.h"
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
	/** Camera shake (kartFxBus.ts): peak in metres, decaying at 6.5/s; a new kick keeps the larger envelope. */
	void AddShake(double PeakMetres);

	UPROPERTY(EditAnywhere, Category = "Kart")
	FLinearColor PaintColor = FLinearColor(0.871f, 0.084f, 0.270f); // #f0508e, the v2 "pink" paint

protected:
	virtual void BeginPlay() override;

private:
	void UpdateVisual(float DeltaSeconds);
	void UpdateDriver(float DeltaSeconds);
	void UpdateNamePlate();
	void UpdateCamera(float DeltaSeconds);
	void UpdateFeel(float DeltaSeconds);
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

	/** Prism aura (yellow sphere, MultiplayerKart.tsx:1697-1713). */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> Aura;

	/** Rocket shell over the kart (laid along the kart, not across it as on the web). */
	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UStaticMeshComponent> RocketShell;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UKartMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere, Category = "Kart")
	TObjectPtr<UKartAudioComponent> Audio;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AuraMaterial;

	/** Driver and outfit slots tinted from the look (skin, hair, jersey, ...), by slot name. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> DriverTints;

	UPROPERTY()
	TMap<FName, TObjectPtr<UMaterialInstanceDynamic>> OutfitTints;

	FKartLook Look;
	bool bLookSet = false;
	bool bVehicleBuilt = false;
	EKartVehicle BuiltVehicle = EKartVehicle::Kart;
	FString BuiltDriver;
	EKartOutfit BuiltOutfit = EKartOutfit::Classic;
	bool bRebuildOutfit = true;

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
	double BlinkStart = -10.0;

	bool bSplitScreenCamera = false;
	FKartItemVisual ItemVisual;
	double ShakePeak = 0.0;
	double ShakeStart = -1000.0;

	/** Chassis and camera juice on top of the sim's presentation layers (-KartWebFeel turns it off). */
	MovecadeKart::FKartFeel Feel;

	FVector CameraPos = FVector::ZeroVector;
	FVector CameraLook = FVector::ZeroVector;
	int32 LoggedLaps = 0;
	double LastLapTime = 0.0;
};

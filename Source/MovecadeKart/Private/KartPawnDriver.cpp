// AKartPawn's driver: the hands on the wheel, the item throw, blinks, and the acting on top (moods for drifts,
// boosts, revs, spins, wins and losses, glances at rivals, whiplash) that UKartDriverAnimInstance poses.
#include "KartPawn.h"

#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "KartDebugFlags.h"
#include "KartDriverAnimInstance.h"
#include "KartMovementComponent.h"
#include "KartPawnTuning.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Pawn;

namespace
{
	/** The grip point sits just outside the rim's tube; the anim instance then backs the wrist off along the arm so
	 *  the hand, which reaches past the wrist, wraps the rim. */
	constexpr double WristClearance = 2.0;
	constexpr double HandlebarGripInset = 6.0;
	/** The hands sit this far in front of the rim's plane. */
	constexpr double RimHandLift = 2.0;

	/** Anim inputs: speed (SU) for "moving", lean (rad) for full lateral g, hop (m) for a full bounce. */
	constexpr double MovingFullSpeed = 45.0;
	constexpr double LeanToLateralG = 4.0;
	constexpr double HopToBounce = 2.0;
	/** The podium (places 1-3) cheers at the finish; everyone else slumps. */
	constexpr int32 PodiumPlaces = 3;
	constexpr double CheerRate = 3.0;
	/** Item throw: the hand comes up in RaiseSeconds, holds HoldSeconds, and is back on the wheel over LowerSeconds. */
	constexpr double ThrowRaiseSeconds = 0.12;
	constexpr double ThrowHoldUntil = 0.32;
	constexpr double ThrowLowerSeconds = 0.3;

	/** blink.ts: 65 ms close, 20 ms hold, 135 ms reopen, every 2-5 s. */
	constexpr double BlinkCloseMs = 65.0;
	constexpr double BlinkShutUntilMs = 85.0;
	constexpr double BlinkOpenMs = 135.0;
	constexpr double BlinkMinGap = 2.0;
	constexpr double BlinkMaxGap = 5.0;

	/** Mood ease rates (1/s). */
	constexpr double DriftMoodRate = 6.0;
	constexpr double BoostMoodRate = 7.0;
	constexpr double RevMoodRate = 8.0;
	constexpr double SpinMoodRate = 9.0;
	constexpr double SlumpMoodRate = 2.5;
	constexpr double LookWeightRate = 4.0;
	constexpr float LookAtRate = 6.f;
	constexpr float FaceRate = 12.f;
	/** Whiplash springs: stiffness and damping. */
	constexpr double JoltStiffness = 3.2;
	constexpr double JoltDamping = 0.32;
	/** Drift lean: 0.6 at once, up to 1 with a full charge (drift power 10). */
	constexpr double DriftMoodBase = 0.6;
	constexpr double DriftMoodCharge = 0.4;
	constexpr double DriftFullPower = 10.0;

	/** Glances: at a kart within range (wider while waiting on the grid) and alongside rather than ahead or behind,
	 *  held at least LookHoldSeconds; with nobody in range the world is scanned every LookScanSeconds. */
	constexpr double LookRangeCm = 900.0;
	constexpr double LookRangeWaitingCm = 1400.0;
	constexpr double LookAlongsideDot = 0.7;
	constexpr double LookHoldSeconds = 1.2;
	constexpr double LookScanSeconds = 0.25;
	/** Below this speed (SU) the driver counts as waiting. */
	constexpr double WaitingSpeed = 1.0;
	/** The rival's head is about this high over its origin. */
	constexpr double LookHeadHeightCm = 120.0;
	constexpr double LookWeightDriving = 0.55;
	constexpr double LookWeightWaiting = 0.8;
	/** Whiplash reads on the face as shock at this gain. */
	constexpr double JoltShockGain = 1.6;
}

void AKartPawn::UpdateDriver(float DeltaSeconds)
{
	UKartDriverAnimInstance* Anim = Cast<UKartDriverAnimInstance>(Driver->GetAnimInstance());
	if (!Anim || !Driver->GetSkeletalMeshAsset())
	{
		return;
	}
	const FKartState& S = Movement->SimState();
	FKartDriverPoseInput& In = Anim->Input;
	In.Steer = static_cast<float>(FMath::Clamp(-S.SteerVisual * SteerToRim, -1.0, 1.0));
	In.Moving = static_cast<float>(FMath::Clamp(FMath::Abs(S.Speed) / MovingFullSpeed, 0.0, 1.0));
	In.LateralG = static_cast<float>(FMath::Clamp(-S.Lean * LeanToLateralG, -1.0, 1.0));
	In.Bounce = static_cast<float>(FMath::Clamp(S.HopOffset * HopToBounce, -1.0, 1.0));
	In.Time = GetWorld()->GetTimeSeconds();
	In.bStraddle = bStraddle;
	// Place 0 is not known yet, so it celebrates.
	const bool bPodium = FinishPlace <= PodiumPlaces;
	Cheer = FMath::FInterpTo(Cheer, S.bFinished && bPodium ? 1.0 : 0.0, DeltaSeconds, CheerRate);
	In.Cheer = static_cast<float>(Cheer);
	UpdateDriverMood(DeltaSeconds, In);
	const double SinceThrow = GetWorld()->GetTimeSeconds() - ItemThrowStart;
	const double Raise = SinceThrow < ThrowRaiseSeconds ? SinceThrow / ThrowRaiseSeconds
		: SinceThrow < ThrowHoldUntil ? 1.0 : 1.0 - (SinceThrow - ThrowHoldUntil) / ThrowLowerSeconds;
	In.ItemRaise = static_cast<float>(FMath::SmoothStep(0.0, 1.0, FMath::Clamp(Raise, 0.0, 1.0)));
	UpdateGripTargets(In);
	UpdateBlink();
	UpdateFace(DeltaSeconds);
}

void AKartPawn::UpdateGripTargets(FKartDriverPoseInput& In) const
{
	// From where the rim (or bar) actually is this frame, into the driver's component space.
	const FTransform GripFrame = GripPivot->GetComponentTransform();
	const FTransform Vehicle = ChassisPivot->GetComponentTransform();
	const FVector DriverRight = Vehicle.TransformVectorNoScale(-FVector::XAxisVector); // model right is -X
	const FVector DriverBack = Vehicle.TransformVectorNoScale(-FVector::YAxisVector);
	FVector Left, Right;
	if (bHandlebar)
	{
		const FVector Side = GripFrame.TransformVectorNoScale(-FVector::XAxisVector);
		const double Reach = GripRadius - HandlebarGripInset;
		Left = GripFrame.GetLocation() - Side * Reach + DriverBack * WristClearance;
		Right = GripFrame.GetLocation() + Side * Reach + DriverBack * WristClearance;
	}
	else
	{
		const FVector Normal = Vehicle.TransformVectorNoScale(GripNormal);
		const FVector Side = FVector::VectorPlaneProject(DriverRight, Normal).GetSafeNormal();
		const FQuat Turn = GripFrame.GetRotation() * Vehicle.GetRotation().Inverse();
		const double Reach = GripRadius + WristClearance;
		Left = GripFrame.GetLocation() + Turn.RotateVector(-Side) * Reach + Normal * RimHandLift;
		Right = GripFrame.GetLocation() + Turn.RotateVector(Side) * Reach + Normal * RimHandLift;
	}
	if (FKartDebugFlags::Get().bDebugGrip)
	{
		DrawDebugSphere(GetWorld(), Left, 3.f, 8, FColor::Green);
		DrawDebugSphere(GetWorld(), Right, 3.f, 8, FColor::Red);
		DrawDebugSphere(GetWorld(), GripFrame.GetLocation(), 3.f, 8, FColor::Blue);
	}
	const FTransform& Component = Driver->GetComponentTransform();
	In.LeftHand = Component.InverseTransformPosition(Left);
	In.RightHand = Component.InverseTransformPosition(Right);
	In.bHasGrip = GripRadius > 0.0;
}

void AKartPawn::UpdateBlink()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextBlink)
	{
		BlinkStart = Now;
		NextBlink = Now + FMath::FRandRange(BlinkMinGap, BlinkMaxGap);
	}
	const double T = (Now - BlinkStart) * 1000.0;
	const double Blink = T < BlinkCloseMs ? T / BlinkCloseMs
		: T < BlinkShutUntilMs ? 1.0
		: T < BlinkShutUntilMs + BlinkOpenMs ? 1.0 - (T - BlinkShutUntilMs) / BlinkOpenMs : 0.0;
	// Morphs are written only when they change (a new driver mesh starts from zero, so it is written afresh).
	if (SentMorphMesh.Get() != Driver->GetSkeletalMeshAsset())
	{
		SentMorphMesh = Driver->GetSkeletalMeshAsset();
		SentBlink = -1.f;
		for (float& W : SentFace)
		{
			W = -1.f;
		}
	}
	static const FName EyeBlink(TEXT("eyeBlink"));
	const float BlinkWeight = static_cast<float>(FMath::Clamp(Blink, 0.0, 1.0));
	if (BlinkWeight != SentBlink)
	{
		Driver->SetMorphTarget(EyeBlink, BlinkWeight);
		SentBlink = BlinkWeight;
	}
}

void AKartPawn::UpdateDriverMood(float DeltaSeconds, FKartDriverPoseInput& In)
{
	const FKartState& S = Movement->SimState();
	auto Ease = [DeltaSeconds](double& Value, double Target, double Rate) { Value = FMath::FInterpTo(Value, Target, DeltaSeconds, Rate); };

	// Drift (+ = drifting right in UE terms; the sim's +1 is left), scaled by the charge.
	const double DriftTarget = S.DriftDirection != 0.0
		? -FMath::Sign(S.DriftDirection) * (DriftMoodBase + DriftMoodCharge * FMath::Clamp(S.DriftPower / DriftFullPower, 0.0, 1.0))
		: 0.0;
	Ease(Mood.Drift, DriftTarget, DriftMoodRate);
	Ease(Mood.Boost, S.BoostTimeRemaining > 0.0 && S.BoostMultiplier > 1.01 ? 1.0 : 0.0, BoostMoodRate);
	Ease(Mood.Rev, GridRev, RevMoodRate);
	Ease(Mood.Spin, FMath::Abs(ItemVisual.SpinDegrees) > 1.0 || FMath::Abs(ItemVisual.TumbleDegrees) > 1.0 ? 1.0 : 0.0, SpinMoodRate);
	Ease(Mood.Slump, S.bFinished && FinishPlace > PodiumPlaces ? 1.0 : 0.0, SlumpMoodRate);
	JoltBack.Step(0.0, JoltStiffness, JoltDamping, DeltaSeconds);
	JoltSide.Step(0.0, JoltStiffness, JoltDamping, DeltaSeconds);
	const bool bWaiting = FMath::Abs(S.Speed) < WaitingSpeed;
	UpdateGlance(DeltaSeconds, bWaiting);
	const bool bLooking = Mood.LookTarget.IsValid() && Mood.Spin < 0.5 && Cheer < 0.5;
	Ease(Mood.LookWeight, bLooking ? (bWaiting ? LookWeightWaiting : LookWeightDriving) : 0.0, LookWeightRate);

	In.DriftSide = static_cast<float>(Mood.Drift);
	In.Boost = static_cast<float>(Mood.Boost);
	In.Rev = static_cast<float>(Mood.Rev);
	In.Spin = static_cast<float>(Mood.Spin);
	In.Slump = static_cast<float>(Mood.Slump);
	In.JoltBack = static_cast<float>(FMath::Clamp(JoltBack.Value, -1.0, 1.0));
	In.JoltSide = static_cast<float>(FMath::Clamp(JoltSide.Value, -1.0, 1.0));
	In.LookWeight = static_cast<float>(Mood.LookWeight);
	In.LookAt = Mood.LookWeight > 0.01 ? Driver->GetComponentTransform().InverseTransformPosition(Mood.LookAt) : FVector::ZeroVector;
}

void AKartPawn::UpdateGlance(float DeltaSeconds, bool bWaiting)
{
	// Glance at a rival running alongside (or anyone close while waiting on the grid); hold a glance at least
	// LookHoldSeconds so the head doesn't flick between karts.
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector Me = GetActorLocation();
	const FVector Ahead = GetActorForwardVector();
	AKartPawn* Rival = Mood.LookTarget.Get();
	auto Interesting = [&](const AKartPawn* Other)
	{
		const FVector To = Other->GetActorLocation() - Me;
		const double Range = bWaiting ? LookRangeWaitingCm : LookRangeCm;
		return To.SizeSquared() < Range * Range && (bWaiting || FMath::Abs(FVector::DotProduct(To.GetSafeNormal(), Ahead)) < LookAlongsideDot);
	};
	// The world scan runs at most every LookScanSeconds while nobody is in range, rather than every frame for every kart.
	if (Now > Mood.LookUntil && (!Rival || !Interesting(Rival)))
	{
		Rival = nullptr;
		double Best = TNumericLimits<double>::Max();
		for (TActorIterator<AKartPawn> It(GetWorld()); It; ++It)
		{
			if (*It != this && Interesting(*It) && FVector::DistSquared(It->GetActorLocation(), Me) < Best)
			{
				Best = FVector::DistSquared(It->GetActorLocation(), Me);
				Rival = *It;
			}
		}
		Mood.LookTarget = Rival;
		Mood.LookUntil = Now + (Rival ? LookHoldSeconds : LookScanSeconds);
	}
	if (Rival)
	{
		Mood.LookAt = FMath::VInterpTo(Mood.LookAt.IsNearlyZero() ? Rival->GetActorLocation() : Mood.LookAt,
			Rival->GetActorLocation() + FVector(0, 0, LookHeadHeightCm), DeltaSeconds, LookAtRate);
	}
}

void AKartPawn::UpdateFace(float DeltaSeconds)
{
	using namespace MovecadeKart::DriverFace;
	FMood Face;
	Face.Drift = static_cast<float>(FMath::Abs(Mood.Drift));
	Face.Boost = static_cast<float>(Mood.Boost);
	Face.Rev = static_cast<float>(Mood.Rev);
	Face.Shock = static_cast<float>(FMath::Max(Mood.Spin, FMath::Clamp(FMath::Max(FMath::Abs(JoltBack.Value), FMath::Abs(JoltSide.Value)) * JoltShockGain, 0.0, 1.0)));
	Face.Cheer = static_cast<float>(Cheer);
	Face.Slump = static_cast<float>(Mood.Slump);
	float Target[MorphCount];
	Mix(Face, Target);
	for (int32 i = 0; i < MorphCount; ++i)
	{
		// Faces change fast but not in a single frame.
		Mood.Face[i] = FMath::FInterpTo(Mood.Face[i], Target[i], DeltaSeconds, FaceRate);
		if (FMath::Abs(Mood.Face[i] - SentFace[i]) > 1e-3f || (Mood.Face[i] == 0.f && SentFace[i] != 0.f))
		{
			Driver->SetMorphTarget(MorphName(static_cast<EMorph>(i)), Mood.Face[i]);
			SentFace[i] = Mood.Face[i];
		}
	}
}

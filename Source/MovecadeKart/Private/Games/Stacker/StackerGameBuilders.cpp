// AStackerGame: the builders: each one's lift and piston beside the lane, the avatar's pose and the block in hand.

#include "Games/Stacker/StackerGame.h"

#include "Art/KartMaterialCache.h"
#include "Avatar/ArcadeAvatar.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StackerScene.h"

using namespace MovecadeKart::Stacker;
using namespace MovecadeKart::Stacker::Scene;

namespace
{
	/** Four builders share the deck: a little smaller then, so the lanes stay readable. */
	constexpr float CrowdedAvatarScale = 0.78f;
	constexpr float AvatarScale = 0.84f;
	constexpr int32 CrowdedLanes = 4;
	/** UArcadeAvatarComponent's rig is authored at this scale: offsets in its space scale by AvatarScale over it. */
	constexpr double RigScale = 0.64;
	/** The avatar's arm, web units: turns the hands' reach across into the rig's units. */
	constexpr double ArmLength = 0.525;
	/** Where the eyes are above the avatar's feet (cm, at rig scale), for the look at the cursor. */
	constexpr double EyeHeightCm = 95.0;

	/** The lift beside the lane (web units): its platform just under the builder's feet, the piston under that. */
	struct FLiftShape
	{
		static constexpr double PlatformDrop = 0.09;
		static constexpr double PlatformDeep = 1.4;
		static constexpr double PlatformWide = 1.9;
		static constexpr double PlatformThick = 0.12;
		static constexpr double PistonDrop = 0.1;
		static constexpr double PistonWidth = 0.22;
		static constexpr double MinPistonHeight = 0.02;
	};

	/** presentation.ts: the throw over the cooldown after a drop: reach, brace, recoil (shares of the cooldown). */
	struct FThrow
	{
		static constexpr double ReachShare = 0.4;
		static constexpr double BraceShare = 0.7;
		static constexpr double RecoilStart = 0.3;
		static constexpr double RecoilShare = 0.7;
	};

	/**
	 * How hard the builder works (the crouch): a base, the block far out along its sweep, the strain of the lean, the
	 * brace of a throw, a recovering tower; and how the body turns after the block and away from the lean.
	 */
	struct FEffort
	{
		static constexpr double Base = 0.18;
		static constexpr double PerSweep = 0.08;
		static constexpr double PerStrain = 0.32;
		static constexpr double PerBrace = 0.5;
		static constexpr double Recovering = 0.25;
		static constexpr double Max = 0.95;
		/** The lean counts up to 1.5 times the limit. */
		static constexpr double MaxBalance = 1.5;
		static constexpr double MinSweepHalfWidth = 0.1;
		static constexpr double LateralPerCursor = 0.3;
		static constexpr double LateralPerLean = 0.7;
		/** The turn follows at this rate (per second). */
		static constexpr double LateralRate = 14.0;
	};

	/** The holding pose (UArcadeAvatarComponent pose units): both hands out over the sweep with the block. */
	struct FHoldPose
	{
		static constexpr double HandSidePerCursor = 0.42;
		static constexpr double MaxHandSide = 0.6;
		static constexpr double HandSideScale = 0.4;
		static constexpr double HandSpread = 0.28;
		static constexpr double Forward = 0.62;
		static constexpr double ReachForward = 0.18;
		static constexpr double RecoilBack = 0.08;
		static constexpr double Up = -0.05;
		static constexpr double ReachDown = 0.19;
		static constexpr double RecoilUp = 0.1;
		static constexpr double LeanDown = 0.08;
		static constexpr double CrouchBase = 0.12;
		static constexpr double CrouchPerEffort = 0.3;
		static constexpr double LeanBase = 0.1;
		static constexpr double LeanPerReach = 0.2;
		static constexpr double LeanPerBrace = 0.08;
		static constexpr double LeanPerStrain = 0.1;
		static constexpr double LeanPerStagger = 0.35;
		static constexpr double TwistPerLateral = 0.6;
		static constexpr float LookWeight = 0.85f;
	};

	/** A topple shoves the builder back off balance (web units) and twists them toward the tower (degrees). */
	struct FStaggerMotion
	{
		static constexpr double Shove = 0.12;
		static constexpr double ShoveRate = 9.0;
		static constexpr double Rise = 0.18;
		static constexpr double TwistDeg = 12.0;
		static constexpr double TwistRate = 7.0;
	};

	/** The next block between the hands: a small thick slice of the top's width, placed in the rig's space (cm). */
	struct FHeldBlockShape
	{
		static constexpr double MaxWidth = 0.5;
		static constexpr double WidthShare = 0.16;
		static constexpr double ForwardCm = 46.0;
		static constexpr double SideCm = 40.0;
		static constexpr double HeightCm = 92.0;
		static constexpr double UpCm = 50.0;
		static constexpr double Deep = 0.24;
		static constexpr double WidthScale = 2.2;
		static constexpr double Thick = 0.16;
	};

	/** A cheer or a stagger past this takes the hands off the block (and hides the held block). */
	constexpr float HandsOffCheer = 0.3f;
	constexpr double HandsOffStagger = 0.3;

	/** finishFirst / finishLast: how the winner, the last place and everyone between end the round. */
	constexpr float WinnerCheer = 0.9f;
	constexpr float LastSlump = 0.6f;
	constexpr float MiddleSlump = 0.25f;
	constexpr float EliminatedSlump = 0.7f;

	/** A reaction's rise and fall by its age (s): Peak * smoothstep(0, RiseEnd) * smoothstep(FallStart, FallEnd). */
	struct FEnvelope
	{
		double Peak;
		double RiseEnd;
		double FallStart;
		double FallEnd;

		double At(double Age) const
		{
			return Peak * Smooth(0.0, RiseEnd, Age) * Smooth(FallStart, FallEnd, Age);
		}
	};

	/** StackerAvatar.tsx: joy on a perfect (more on a streak) and a complete tower, a wince on a miss, a stagger on a topple. */
	constexpr FEnvelope PerfectCheer{ 0.45, 0.1, 0.7, 0.5 };
	constexpr FEnvelope StreakCheer{ 0.75, 0.1, 0.7, 0.5 };
	constexpr int32 StreakCombo = 3;
	constexpr FEnvelope CompleteCheer{ 1.0, 0.12, 1.6, 1.2 };
	constexpr FEnvelope MissSlump{ 0.6, 0.12, 0.95, 0.6 };
	constexpr FEnvelope TopoutStagger{ 1.0, 0.1, 1.4, 0.9 };
	constexpr double TopoutSlumpPerStagger = 0.9;

	struct FReaction
	{
		float Cheer = 0.f;
		float Slump = 0.f;
		double Stagger = 0.0;
	};

	FReaction ReactionTo(EEffectKind Kind, double Age, int32 Combo)
	{
		FReaction Out;
		switch (Kind)
		{
		case EEffectKind::Perfect:
			Out.Cheer = static_cast<float>((Combo >= StreakCombo ? StreakCheer : PerfectCheer).At(Age));
			break;
		case EEffectKind::Complete:
			Out.Cheer = static_cast<float>(CompleteCheer.At(Age));
			break;
		case EEffectKind::Miss:
			Out.Slump = static_cast<float>(MissSlump.At(Age));
			break;
		case EEffectKind::Topout:
			Out.Stagger = TopoutStagger.At(Age);
			Out.Slump = static_cast<float>(TopoutSlumpPerStagger * Out.Stagger);
			break;
		default:
			break;
		}
		return Out;
	}

	/** The showcase: the winner celebrates, the last place hangs its head, everyone else a little. */
	FReaction AtRoundEnd(FReaction React, bool bWin, bool bLast)
	{
		React.Cheer = FMath::Max(React.Cheer, bWin ? WinnerCheer : 0.f);
		React.Slump = FMath::Max(React.Slump, bLast ? LastSlump : (bWin ? 0.f : MiddleSlump));
		return React;
	}

	/** Where a throw is (0..1 each) at an age since the drop. */
	struct FThrowPhase
	{
		double Reach = 0.0;
		double Brace = 0.0;
		double Recoil = 0.0;
	};

	FThrowPhase ThrowAt(double AgeMs)
	{
		const double T = FMath::Clamp(AgeMs / DropCooldownMs, 0.0, 1.0);
		FThrowPhase Out;
		Out.Reach = FMath::Sin(FMath::Min(1.0, T / FThrow::ReachShare) * PI);
		Out.Brace = FMath::Sin(FMath::Min(1.0, T / FThrow::BraceShare) * PI);
		Out.Recoil = FMath::Sin(FMath::Clamp((T - FThrow::RecoilStart) / FThrow::RecoilShare, 0.0, 1.0) * PI);
		return Out;
	}
}

void AStackerGame::BuildBuilderPieces(int32 TowerIndex)
{
	using MovecadeKart::Art::Hex;
	UStaticMeshComponent* Lift = MakeMesh(TEXT("Lift"), CubeMesh);
	Lift->SetMaterial(0, Materials.Plain(this, Hex(TEXT("#d39a32")), 0.5, 0.3));
	Lift->SetVisibility(true);
	Lifts.Add(Lift);
	UStaticMeshComponent* Piston = MakeMesh(TEXT("Piston"), CylinderMesh);
	Piston->SetMaterial(0, Materials.Plain(this, Hex(TEXT("#c9ccd2")), 0.25, 0.95));
	Piston->SetVisibility(true);
	Pistons.Add(Piston);
	UArcadeAvatarComponent* Avatar = NewObject<UArcadeAvatarComponent>(this, MakeUniqueObjectName(this, UArcadeAvatarComponent::StaticClass(), TEXT("Builder")));
	Avatar->AvatarScale = Lanes.Num() >= CrowdedLanes ? CrowdedAvatarScale : AvatarScale;
	Avatar->SetupAttachment(RootComponent);
	Avatar->RegisterComponent();
	Avatar->SetLook(Seats[TowerIndex].Look);
	Avatars.Add(Avatar);
}

void AStackerGame::UpdateBuilders(float Dt)
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		UpdateBuilder(i, Dt);
	}
}

FVector AStackerGame::PlaceLift(int32 TowerIndex)
{
	using L = FLiftShape;
	const FLane& Lane = Lanes[TowerIndex];
	// The lift rides with the work rows: (height - scroll) rows up, smoothed by the scroll itself. The showcase
	// brings everyone back down to the deck.
	const double LiftY = FMath::Max(0.0, Match.Towers[TowerIndex].Height() - Lane.Scroll) * RowHeight * (Stage == EStackerStage::Finished ? 0.0 : 1.0);
	// Beside the lane rather than square in front of it (the web's spot hides a lone tower), turned toward it.
	const FVector Base(Lane.LaneX - BuilderSide, FloorY, BuilderZ);
	Lifts[TowerIndex]->SetWorldTransform(FTransform(FRotator(0.0, BuilderYawDeg, 0.0), W(Base + FVector(0.0, LiftY - L::PlatformDrop, 0.0)),
		FVector(L::PlatformDeep, L::PlatformWide, L::PlatformThick)));
	Pistons[TowerIndex]->SetWorldTransform(FTransform(FRotator::ZeroRotator, W(Base + FVector(0.0, LiftY / 2.0 - L::PistonDrop, 0.0)),
		FVector(L::PistonWidth, L::PistonWidth, FMath::Max(L::MinPistonHeight, LiftY))));
	return Base + FVector(0.0, LiftY, 0.0);
}

void AStackerGame::UpdateBuilder(int32 TowerIndex, float Dt)
{
	using E = FEffort;
	FLane& Lane = Lanes[TowerIndex];
	const FTower& Tower = Match.Towers[TowerIndex];
	const FVector Feet = PlaceLift(TowerIndex);
	const double Age = FMath::Max(0.0, Match.ElapsedMs - Tower.LastDropAtMs);
	const FThrowPhase Throw = ThrowAt(Age);
	const bool bRecovering = Tower.Balance.RecoveryMs > 0.0;
	// The strain of holding the lean, and how far the block is out along its sweep, drive the crouch and the twist.
	const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
	const double BalX = FMath::Clamp(Tower.Balance.LeanX / Limit, -E::MaxBalance, E::MaxBalance);
	const double BalY = FMath::Clamp(Tower.Balance.LeanY / Limit, -E::MaxBalance, E::MaxBalance);
	const double Strain = FMath::Min(1.0, FMath::Sqrt(BalX * BalX + BalY * BalY));
	const double Sweep = FMath::Abs(Tower.CursorCenter) / FMath::Max(E::MinSweepHalfWidth, SweepTrackHalfWidth(Tower.Height()));
	const double Effort = FMath::Min(E::Max, E::Base + Sweep * E::PerSweep + Strain * E::PerStrain + Throw.Brace * E::PerBrace + (bRecovering ? E::Recovering : 0.0));
	const double Lateral = Tower.CursorCenter / TrackHalfWidth * E::LateralPerCursor - BalX * E::LateralPerLean;
	Lane.LateralSmooth += (Lateral - Lane.LateralSmooth) * Approach(E::LateralRate, Dt);

	const double ReactionAge = Clock - Lane.ReactionAt;
	FReaction React = ReactionTo(Lane.Reaction, ReactionAge, Tower.Combo);
	if (Tower.bEliminated)
	{
		React.Slump = FMath::Max(React.Slump, EliminatedSlump);
	}
	if (Stage == EStackerStage::Finished)
	{
		const TArray<int32>& Order = StandingOrder.Get();
		const bool bWin = Match.WinnerIds.Contains(Tower.PlayerId);
		const bool bLast = Order.Num() > 1 && Order.Last() == TowerIndex && !bWin;
		React = AtRoundEnd(React, bWin, bLast);
	}
	// The topple shoves the builder back off balance and twists them toward the tower.
	using St = FStaggerMotion;
	const double Stagger = React.Stagger;
	UArcadeAvatarComponent* Avatar = Avatars[TowerIndex];
	const FVector Shove = W(FVector(-FMath::Sign(Tower.Balance.LeanX + 1e-6) * St::Shove * Stagger * FMath::Sin(ReactionAge * St::ShoveRate), 0.0, St::Rise * Stagger));
	Avatar->SetWorldLocationAndRotation(W(Feet) + Shove, FRotator(0.0, BuilderYawDeg + St::TwistDeg * Stagger * FMath::Sin(ReactionAge * St::TwistRate), 0.0));

	// The block held out in both hands, over the sweep: the hands follow the cursor and push on the drop.
	using H = FHoldPose;
	FArcadePoseInput& Pose = Avatar->Pose();
	const double HandSide = FMath::Clamp(Tower.CursorCenter * H::HandSidePerCursor, -H::MaxHandSide, H::MaxHandSide);
	const double Fwd = H::Forward + H::ReachForward * Throw.Reach - H::RecoilBack * Throw.Recoil;
	const double Up = H::Up - H::ReachDown * Throw.Reach + H::RecoilUp * Throw.Recoil - H::LeanDown * FMath::Abs(BalY);
	Pose.Hand[0] = FVector(Fwd, HandSide / ArmLength * H::HandSideScale + H::HandSpread, Up);
	Pose.Hand[1] = FVector(Fwd, HandSide / ArmLength * H::HandSideScale - H::HandSpread, Up);
	const bool bHandsOff = React.Cheer > HandsOffCheer || Stagger > HandsOffStagger;
	Pose.HandWeight[0] = Pose.HandWeight[1] = bHandsOff ? 0.f : 1.f;
	Pose.Crouch = static_cast<float>(H::CrouchBase + H::CrouchPerEffort * Effort);
	Pose.Lean = static_cast<float>(H::LeanBase + H::LeanPerReach * Throw.Reach + H::LeanPerBrace * Throw.Brace + H::LeanPerStrain * Strain - H::LeanPerStagger * Stagger);
	Pose.Twist = static_cast<float>(-Lane.LateralSmooth * H::TwistPerLateral);
	const FVector CursorAt = W(FVector(Lane.LaneX + Tower.CursorCenter, (Tower.Top().Index + CursorRowsAbove - Lane.Scroll) * RowHeight, 0.0));
	Pose.Look = Avatar->GetComponentTransform().InverseTransformVectorNoScale(CursorAt - Avatar->GetComponentLocation() - FVector(0.0, 0.0, EyeHeightCm)).GetSafeNormal();
	Pose.LookWeight = H::LookWeight;
	Pose.Cheer = React.Cheer;
	Pose.Slump = React.Slump;
	Pose.Idle = 1.f;

	// The next block between the hands, hidden while it reloads, while the tower recovers or while they cheer.
	const bool bHold = Stage == EStackerStage::Match && !Tower.IsDone() && Age >= DropCooldownMs && !bRecovering && React.Cheer <= HandsOffCheer;
	UpdateHeldBlock(TowerIndex, bHold, FVector(Fwd, HandSide, Up));
}

void AStackerGame::UpdateHeldBlock(int32 TowerIndex, bool bHold, const FVector& Hands)
{
	using B = FHeldBlockShape;
	UStaticMeshComponent* Held = HeldBlocks[TowerIndex];
	if (Held->IsVisible() != bHold)
	{
		Held->SetVisibility(bHold);
	}
	if (!bHold)
	{
		return;
	}
	const UArcadeAvatarComponent* Avatar = Avatars[TowerIndex];
	const double Width = FMath::Min(B::MaxWidth, FMath::Max(MinWidth, Match.Towers[TowerIndex].Top().Width) * B::WidthShare);
	const FTransform Body = Avatar->GetComponentTransform();
	const double Scale = Avatar->AvatarScale / RigScale;
	// Hands: X forward, Y the hands' side, Z up (pose units).
	const FVector Local = FVector(Hands.X * B::ForwardCm, Hands.Y * B::SideCm, B::HeightCm + Hands.Z * B::UpCm) * Scale;
	Held->SetWorldTransform(FTransform(Body.GetRotation(), Body.GetLocation() + Body.GetRotation().RotateVector(Local), FVector(B::Deep, Width * B::WidthScale, B::Thick / BlockSize.Y)));
}

// ASumoGame's per-frame pucks (placement, lean, squash, dash glow, shield, FX streams), their riders' poses, and the
// power-up pickups. Everything here only presents the rules' state; nothing feeds back into the simulation.

#include "Games/Sumo/SumoGame.h"

#include "Avatar/ArcadeAvatar.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Sumo/SumoArena.h"
#include "Games/Sumo/SumoFx.h"
#include "Games/Sumo/SumoPresentation.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Sumo;
using namespace MovecadeKart::Sumo::Presentation;
namespace Fx = MovecadeKart::Fx;

namespace
{
	// Facing: the rider steers toward the tilt, or turns with the roll.
	constexpr double SteerTiltSq = 0.05;
	constexpr double SteerSpeedSq = 0.5;
	constexpr double YawRate = 7.0;
	// The lean into the acceleration (deg per m/s^2, smoothed), and the wobble after a hit.
	constexpr double AccelClamp = 40.0;
	constexpr double AccelSmoothRate = 10.0;
	constexpr double LeanPerAccel = 0.35;
	constexpr double MaxLeanDeg = 12.0;
	constexpr double WobbleSeconds = 0.6;
	constexpr double WobbleFrequency = 32.0;
	constexpr double WobbleDecay = 6.0;
	constexpr double WobbleDeg = 9.0;
	// Squash and stretch after a hit: it rings at SquashFrequency and fades at SquashDecay.
	constexpr double SquashDecay = 7.0;
	constexpr double SquashFrequency = 26.0;
	constexpr double SquashWiden = 0.16;
	constexpr double SquashFlatten = 0.24;
	constexpr double HeavyScale = 1.1;
	// The idle bob on the swell (cm) and the fall's tumble (deg at the bottom of the fall).
	constexpr double BobRate = 3.0;
	constexpr double BobPhasePerSlot = 1.3;
	constexpr double BobCm = 1.5;
	constexpr double TumbleDeg = 70.0;
	constexpr double TumbleRollShare = 0.4;

	// The dash's readiness on the trim, the shell and the light.
	constexpr double ReadyFlashSeconds = 0.35;
	constexpr float TrimDimGlow = 1.2f;
	constexpr float TrimReadyGlow = 8.f;
	constexpr double TrimReloadShare = 0.3;
	constexpr double ReadyFlashWhite = 6.0;
	constexpr double PaintEmissive = 0.06;
	constexpr double PaintFlashEmissive = 0.5;
	constexpr double PaintDashEmissive = 0.35;
	constexpr double PuckLightLumens = 260.0;

	// The shield bubble: a slow spin and a shimmer, blinking in its last moments.
	constexpr double ShieldEndingMs = 1500.0;
	constexpr double ShieldBlinkRate = 20.0;
	constexpr double ShieldBlinkLow = 0.4;
	constexpr double ShieldOpacity = 0.16;
	constexpr double ShieldShimmer = 0.06;
	constexpr double ShieldShimmerRate = 6.0;
	constexpr double ShieldSpinDegPerSecond = 40.0;

	// FX streams (particles per second): the dash's fire, a turbo's pilot flame, the skirt's sparks at speed.
	constexpr double DashFireRate = 160.0;
	constexpr double TurboFireRate = 30.0;
	constexpr double TurboFireSpeed = 2.0;
	constexpr double SkirtSparksRate = 70.0;
	constexpr double SkirtSparksSpeed = 9.0;

	// The rider: a sumo crouch, palms out in a dash (tsuppari), a cheer, a slump.
	constexpr double PushSeconds = 0.45;
	constexpr double CheerSeconds = 1.6;
	constexpr double CheerInSeconds = 0.15;
	constexpr double CheerOutFrom = 1.1;
	// Pose weights (0..1 of each pose axis): the crouch deepens with drive and weight and rises into a push, the lean
	// follows drive, push and the puck's own pitch, the twist follows its roll and sways a little.
	constexpr double CrouchBase = 0.42;
	constexpr double CrouchDrive = 0.18;
	constexpr double CrouchHeavy = 0.15;
	constexpr double CrouchPush = 0.2;
	constexpr double LeanBase = 0.12;
	constexpr double LeanDrive = 0.22;
	constexpr double LeanPush = 0.45;
	constexpr double LeanPerPitchDeg = 30.0;
	constexpr double LeanPitchMax = 0.25;
	constexpr double TwistPerRollDeg = 40.0;
	constexpr double TwistRollMax = 0.3;
	constexpr double TwistSway = 0.06;
	constexpr double TwistSwayRate = 2.0;
	constexpr double BalanceBase = 0.3;
	constexpr double BalanceSway = 0.15;
	constexpr double BalanceSwayRate = 3.1;
	constexpr double BalanceReach = 0.1;
	constexpr float FallCheer = 0.85f;
	constexpr float FallCrouch = 0.1f;
	constexpr float FallLean = -0.2f;
	constexpr float LoserSlump = 0.6f;
	/** Past this much cheer the arms go up (the hands let go of their targets). */
	constexpr float ArmsUpCheer = 0.3f;
	constexpr double HopRate = 9.0;
	constexpr double HopHeight = 0.12;
	const FVector IdleHand(0.25, 0.75, -0.05);
	const FVector PushHand(0.98, 0.12, 0.12);
	const FVector RestLook(1.0, 0.0, -0.15);
	constexpr float RestLookWeight = 0.6f;
	constexpr float RivalLookWeight = 0.75f;
	/** Rival heads are looked at from the rider's chest (cm over its root), at this height over the ring. */
	constexpr double RivalHeadZ = 120.0;
	constexpr double RiderChestZ = 70.0;

	// Pickups: pop in with an overshoot, bob, spin, shrink away when gone.
	constexpr double PickupInSeconds = 0.4;
	constexpr float PickupOvershoot = 2.2f;
	constexpr double PickupOutSeconds = 0.25;
	constexpr double PickupBobRate = 2.6;
	constexpr double PickupBobCm = 12.0;
	constexpr double IconSpinDegPerSecond = 120.0;
	constexpr float HaloGlow = 2.f;
	constexpr double HaloOpacity = 0.35;
	constexpr double HaloPulse = 0.25;
	constexpr double HaloPulseRate = 5.0;
}

void ASumoGame::UpdatePucks(float Dt, double Alpha)
{
	const bool bRoundOver = Match.Phase == EPhase::RoundOver || Match.Phase == EPhase::MatchOver;
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		UpdatePuck(i, Dt, Alpha, bRoundOver);
	}
}

void ASumoGame::UpdatePuck(int32 Slot, float Dt, double Alpha, bool bRoundOver)
{
	const FPuck& Puck = Match.Pucks[Slot];
	FSumoPuckParts& Pt = Parts[Slot];
	const bool bVisible = !Puck.bOut;
	if (Pt.Root->IsVisible() != bVisible)
	{
		Pt.Root->SetVisibility(bVisible, true);
	}
	if (!bVisible)
	{
		if (Pt.Trail)
		{
			UKartFxSubsystem::UpdateStream(Pt.Trail, Fx::FKartFxEmit(), 0.0);
		}
		if (Pt.Sparks)
		{
			UKartFxSubsystem::UpdateStream(Pt.Sparks, Fx::FKartFxEmit(), 0.0);
		}
		return;
	}
	const FVector2D Lean = PlacePuck(Slot, Dt, Alpha);
	UpdateDashGlow(Slot);
	UpdateShield(Slot);
	UpdateStreams(Slot);
	PoseRider(Slot, Lean, bRoundOver);
}

FVector2D ASumoGame::PlacePuck(int32 Slot, float Dt, double Alpha)
{
	FSumoPilot& Pilot = Pilots[Slot];
	const FSumoPuckParts& Pt = Parts[Slot];
	const FPuck& Puck = Match.Pucks[Slot];
	// Height: on the ring, or the fall into the sea (gravity eased to meet the splash).
	const double FallT = Puck.bFalling ? FMath::Clamp(Puck.FallMs / FallMs, 0.0, 1.0) : (Puck.bOut ? 1.0 : 0.0);
	const double Z = ASumoArena::WaterZ * FallT * FallT;

	const FVector2D Want = Puck.Input.Tilt.SizeSquared() > SteerTiltSq ? Puck.Input.Tilt : (Puck.Vel.SizeSquared() > SteerSpeedSq ? Puck.Vel : FVector2D::ZeroVector);
	if (!Want.IsZero() && Puck.bStanding)
	{
		const double Target = FMath::RadiansToDegrees(FMath::Atan2(Want.Y, Want.X));
		Pilot.Yaw += FMath::FindDeltaAngleDegrees(Pilot.Yaw, Target) * (1.0 - FMath::Exp(-YawRate * Dt));
	}
	if (Puck.bFalling)
	{
		Pilot.Yaw += Pilot.FallSpin * Dt;
	}

	// The lean into the acceleration (the change in velocity per step, smoothed), plus a wobble after a hit.
	const FVector2D Change = Dt > 0.f ? (Puck.Vel - Pilot.PrevVel) / StepSeconds : FVector2D::ZeroVector;
	const FVector2D Clamped(FMath::Clamp(Change.X, -AccelClamp, AccelClamp), FMath::Clamp(Change.Y, -AccelClamp, AccelClamp));
	Pilot.AccelSmooth += (Clamped - Pilot.AccelSmooth) * (1.0 - FMath::Exp(-AccelSmoothRate * Dt));
	const double HitAge = Clock - Pilot.HitAt;
	const double Wobble = HitAge < WobbleSeconds ? FMath::Sin(HitAge * WobbleFrequency) * FMath::Exp(-HitAge * WobbleDecay) * WobbleDeg : 0.0;
	FVector2D LeanV = Pilot.AccelSmooth * LeanPerAccel + Pilot.HitDir * Wobble;
	if (LeanV.Size() > MaxLeanDeg)
	{
		LeanV = LeanV.GetSafeNormal() * MaxLeanDeg;
	}
	Pilot.Squash *= FMath::Exp(-SquashDecay * Dt);
	const double SquashNow = Pilot.Squash * FMath::Cos(HitAge * SquashFrequency);
	const double Bob = FMath::Sin(Clock * BobRate + Slot * BobPhasePerSlot) * BobCm;
	Pt.Root->SetWorldLocationAndRotation(Cm(DrawnPos(Slot, Alpha), Z + Bob), FRotator(0.0, Pilot.Yaw, 0.0));

	// The lean is in world terms; the tilt pivot takes it in the puck's frame: pitch forward/back, roll left/right.
	const double YawRad = FMath::DegreesToRadians(Pilot.Yaw);
	const double Forward = LeanV.X * FMath::Cos(YawRad) + LeanV.Y * FMath::Sin(YawRad);
	const double Side = -LeanV.X * FMath::Sin(YawRad) + LeanV.Y * FMath::Cos(YawRad);
	const double Tumble = Puck.bFalling ? FallT * TumbleDeg : 0.0;
	Pt.Tilt->SetRelativeRotation(FRotator(-Forward - Tumble, 0.0, Side + Tumble * TumbleRollShare));
	const double Wide = (Puck.Power == EPower::Heavy ? HeavyScale : 1.0) * PuckScale;
	Pt.Tilt->SetRelativeScale3D(FVector(Wide * (1.0 + SquashWiden * SquashNow), Wide * (1.0 + SquashWiden * SquashNow), PuckScale * (1.0 - SquashFlatten * SquashNow)));
	return FVector2D(Forward, Side);
}

void ASumoGame::UpdateDashGlow(int32 Slot)
{
	// The trim says when the dash is ready: dim while it reloads, a flash when it comes back.
	FSumoPilot& Pilot = Pilots[Slot];
	const FSumoPuckParts& Pt = Parts[Slot];
	const FPuck& Puck = Match.Pucks[Slot];
	const FLinearColor C = ColorOf(Slot);
	const bool bReady = Puck.DashCooldownMs <= 0.0;
	if (bReady && !Pilot.bWasReady)
	{
		Pilot.ReadyFlashAt = Clock;
	}
	Pilot.bWasReady = bReady;
	const double Flash = FMath::Max(0.0, 1.0 - (Clock - Pilot.ReadyFlashAt) / ReadyFlashSeconds);
	const double Cool = Puck.DashCooldownMs > 0.0 ? 1.0 - Puck.DashCooldownMs / DashCooldownMs : 1.0;
	const bool bDashing = Puck.DashMs > 0.0;
	Pt.Trim->SetVectorParameterValue(TEXT("Color"),
		FMath::Lerp(C * TrimDimGlow, C * TrimReadyGlow, static_cast<float>(bReady ? 1.0 : TrimReloadShare * Cool)) + FLinearColor::White * static_cast<float>(ReadyFlashWhite * Flash));
	Pt.Paint->SetScalarParameterValue(TEXT("Emissive"), static_cast<float>(PaintEmissive + PaintFlashEmissive * Flash + (bDashing ? PaintDashEmissive : 0.0)));
	Pt.Weight->SetVisibility(Puck.Power == EPower::Heavy);
	Pt.Light->SetIntensity(static_cast<float>(PuckLightLumens * (1.0 + 2.0 * Flash + (bDashing ? 2.0 : 0.0))));
}

void ASumoGame::UpdateShield(int32 Slot) const
{
	const FSumoPuckParts& Pt = Parts[Slot];
	if (!Pt.Shield)
	{
		return;
	}
	const FPuck& Puck = Match.Pucks[Slot];
	const bool bShield = Puck.Power == EPower::Shield;
	Pt.Shield->SetVisibility(bShield);
	if (!bShield || !Pt.ShieldMat)
	{
		return;
	}
	const double Ending = Puck.PowerMs < ShieldEndingMs ? (FMath::Sin(Clock * ShieldBlinkRate) > 0.0 ? ShieldBlinkLow : 1.0) : 1.0;
	Pt.ShieldMat->SetScalarParameterValue(TEXT("Opacity"), static_cast<float>((ShieldOpacity + ShieldShimmer * FMath::Sin(Clock * ShieldShimmerRate)) * Ending));
	Pt.Shield->SetRelativeRotation(FRotator(0.0, Clock * ShieldSpinDegPerSecond, 0.0));
}

void ASumoGame::UpdateStreams(int32 Slot) const
{
	const FSumoPuckParts& Pt = Parts[Slot];
	const FPuck& Puck = Match.Pucks[Slot];
	const FLinearColor C = ColorOf(Slot);
	const double Speed = Puck.Vel.Size();
	const FVector Back = -FVector(Puck.Vel.X, Puck.Vel.Y, 0.0).GetSafeNormal();
	const bool bDashing = Puck.DashMs > 0.0;
	if (Pt.Trail)
	{
		// The dash's fire trail; turbo keeps a small flame lit while rolling.
		const double Rate = bDashing ? DashFireRate : (Puck.Power == EPower::Turbo && Speed > TurboFireSpeed ? TurboFireRate : 0.0);
		UKartFxSubsystem::UpdateStream(Pt.Trail, SumoFx::DashFire(C, Back), Puck.bStanding ? Rate : 0.0);
	}
	if (Pt.Sparks)
	{
		UKartFxSubsystem::UpdateStream(Pt.Sparks, SumoFx::SkirtSparks(C, Back), Puck.bStanding && (bDashing || Speed > SkirtSparksSpeed) ? SkirtSparksRate : 0.0);
	}
}

void ASumoGame::PoseRider(int32 Slot, const FVector2D& Lean, bool bRoundOver) const
{
	// A sumo crouch, leaning into the roll, both palms out in a dash (tsuppari), arms flung up as it falls, a cheer
	// for a won round and a slump for the losers at the end.
	const FSumoPilot& Pilot = Pilots[Slot];
	const FPuck& Puck = Match.Pucks[Slot];
	UArcadeAvatarComponent* Rider = Parts[Slot].Rider;
	FArcadePoseInput& Pose = Rider->Pose();
	const double Forward = Lean.X;
	const double Side = Lean.Y;
	const double DashAge = Clock - Pilot.DashAt;
	const double Push = DashAge < PushSeconds ? FMath::Sin(FMath::Clamp(DashAge / PushSeconds, 0.0, 1.0) * PI) : 0.0;
	const double Drive = FMath::Clamp(Puck.Input.Tilt.Size(), 0.0, 1.0);
	Pose.Crouch = static_cast<float>(CrouchBase + CrouchDrive * Drive + (Puck.Power == EPower::Heavy ? CrouchHeavy : 0.0) - CrouchPush * Push);
	Pose.Lean = static_cast<float>(LeanBase + LeanDrive * Drive + LeanPush * Push + FMath::Clamp(Forward / LeanPerPitchDeg, -LeanPitchMax, LeanPitchMax));
	Pose.Twist = static_cast<float>(FMath::Clamp(-Side / TwistPerRollDeg, -TwistRollMax, TwistRollMax) + TwistSway * FMath::Sin(Clock * TwistSwayRate + Slot));
	// The idle hands sway out for balance; a dash drives both forward.
	const double Balance = BalanceBase + BalanceSway * FMath::Sin(Clock * BalanceSwayRate + Slot);
	Pose.Hand[0] = FMath::Lerp(FVector(IdleHand.X, -IdleHand.Y, IdleHand.Z + BalanceReach * Balance), FVector(PushHand.X, -PushHand.Y, PushHand.Z), Push);
	Pose.Hand[1] = FMath::Lerp(FVector(IdleHand.X, IdleHand.Y, IdleHand.Z + BalanceReach * (1.0 - Balance)), PushHand, Push);
	Pose.HandWeight[0] = Pose.HandWeight[1] = 1.f;
	Pose.Look = RestLook;
	Pose.LookWeight = RestLookWeight;
	Pose.Idle = 1.f;

	float Cheer = 0.f;
	float Slump = 0.f;
	if (Puck.bFalling)
	{
		Cheer = FallCheer;
		Pose.Crouch = FallCrouch;
		Pose.Lean = FallLean;
	}
	const double CheerAge = Clock - Pilot.CheerAt;
	if (CheerAge < CheerSeconds)
	{
		Cheer = FMath::Max(Cheer, static_cast<float>(SmoothStep(0.0, CheerInSeconds, CheerAge) * SmoothStep(CheerSeconds, CheerOutFrom, CheerAge)));
	}
	if (bRoundOver)
	{
		const bool bWinner = Match.Phase == EPhase::MatchOver ? Slot == Match.MatchWinner : Slot == Match.RoundWinner;
		if (bWinner)
		{
			Cheer = 1.f;
		}
		else if (Puck.bStanding || Match.Phase == EPhase::MatchOver)
		{
			Slump = LoserSlump;
		}
	}
	Pose.Cheer = Cheer;
	Pose.Slump = Slump;
	if (Cheer > ArmsUpCheer)
	{
		// Arms up and, for a winner on the ring, a little hop.
		Pose.HandWeight[0] = Pose.HandWeight[1] = 0.f;
		Pose.Lift = static_cast<float>(bRoundOver && !Puck.bFalling ? FMath::Max(0.0, FMath::Sin(Clock * HopRate)) * HopHeight : 0.0);
	}
	else
	{
		Pose.Lift = 0.f;
	}

	// Look at the nearest rival still on the ring.
	double Best = 1e9;
	for (const FPuck& Rival : Match.Pucks)
	{
		if (Rival.Slot == Slot || !Rival.bStanding)
		{
			continue;
		}
		const double DistSq = FVector2D::DistSquared(Rival.Pos, Puck.Pos);
		if (DistSq < Best)
		{
			Best = DistSq;
			const FVector To = Cm(Rival.Pos, RivalHeadZ) - Rider->GetComponentLocation() - FVector(0.0, 0.0, RiderChestZ);
			Pose.Look = Rider->GetComponentTransform().InverseTransformVectorNoScale(To).GetSafeNormal();
			Pose.LookWeight = RivalLookWeight;
		}
	}
}

void ASumoGame::MatchPowerPool()
{
	// Match the pool to the rules' power-ups by id: a taken or expired one shrinks away, then frees its slot.
	for (FSumoPowerParts& P : Powers)
	{
		if (P.Id != INDEX_NONE && P.GoneAt < 0.0 && !Match.PowerUps.ContainsByPredicate([&P](const FPowerUp& U) { return U.Id == P.Id; }))
		{
			P.GoneAt = Clock;
		}
		if (P.GoneAt >= 0.0 && Clock - P.GoneAt > PickupOutSeconds)
		{
			P.Id = INDEX_NONE;
			P.GoneAt = -1.0;
		}
	}
	for (const FPowerUp& U : Match.PowerUps)
	{
		if (Powers.ContainsByPredicate([&U](const FSumoPowerParts& P) { return P.Id == U.Id; }))
		{
			continue;
		}
		if (FSumoPowerParts* Free = Powers.FindByPredicate([](const FSumoPowerParts& P) { return P.Id == INDEX_NONE; }))
		{
			Free->Id = U.Id;
			Free->Kind = U.Kind;
			Free->Pos = U.Pos;
			Free->BornAt = Clock;
			Free->GoneAt = -1.0;
		}
	}
}

void ASumoGame::UpdatePowerUps()
{
	MatchPowerPool();
	for (FSumoPowerParts& P : Powers)
	{
		const bool bOn = P.Id != INDEX_NONE;
		const int32 Icon = static_cast<int32>(P.Kind) - 1;
		for (int32 i = 0; i < UE_ARRAY_COUNT(P.Icons); ++i)
		{
			P.Icons[i]->SetVisibility(bOn && i == Icon);
		}
		if (P.Halo)
		{
			P.Halo->SetVisibility(bOn);
		}
		P.Light->SetVisibility(bOn);
		if (!bOn)
		{
			continue;
		}
		const double Age = Clock - P.BornAt;
		const double In = KartUI::Ease::OutBack(static_cast<float>(FMath::Clamp(Age / PickupInSeconds, 0.0, 1.0)), PickupOvershoot);
		const double Out = P.GoneAt >= 0.0 ? FMath::Clamp(1.0 - (Clock - P.GoneAt) / PickupOutSeconds, 0.0, 1.0) : 1.0;
		const double Scale = FMath::Max(0.01, In * Out);
		const FLinearColor C = PowerColor(P.Kind);
		P.Root->SetWorldLocation(Cm(P.Pos, FMath::Sin(Clock * PickupBobRate + P.Id) * PickupBobCm));
		P.Root->SetWorldScale3D(FVector(Scale));
		if (UStaticMeshComponent* Shown = P.Icons[FMath::Clamp(Icon, 0, 2)])
		{
			Shown->SetRelativeRotation(FRotator(0.0, Clock * IconSpinDegPerSecond, 0.0));
		}
		if (P.HaloMat)
		{
			P.HaloMat->SetVectorParameterValue(TEXT("Color"), C * HaloGlow);
			P.HaloMat->SetScalarParameterValue(TEXT("Opacity"), static_cast<float>(HaloOpacity + HaloPulse * FMath::Sin(Clock * HaloPulseRate)));
		}
		P.Light->SetLightColor(C);
	}
}

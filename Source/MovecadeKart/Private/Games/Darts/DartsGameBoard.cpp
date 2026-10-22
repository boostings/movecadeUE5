// ADartsGame: the darts in the board and in the air, the flight's trail, the board's wobble and bounty wedge, the
// reticle, the chalkboard and the pub's murmur.

#include "Games/Darts/DartsGame.h"

#include "Art/KartMaterials.h"
#include "Components/StaticMeshComponent.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Darts/DartsGameTuning.h"
#include "Games/Darts/DartsPub.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"

using namespace MovecadeKart::Darts;
using MovecadeKart::Darts::Tuning::ToWorld;
namespace Fx = MovecadeKart::Fx;

namespace
{
	// The flight (DartsScene.tsx writeFlightPosition): web metres.
	/** Where the dart leaves the hand: the thrower's line + 0.28, at shoulder height. */
	constexpr double LaunchX = ADartsPub::ThrowerX + 0.28;
	constexpr double LaunchY = ADartsPub::FloorY + 1.55;
	constexpr double ThrowDistance = 4.3;
	/** The tip sits a centimetre into the sisal. */
	constexpr double StuckZ = -0.012;
	/** The arc's sideways swing per unit of curve, and its rise for a soft (power 0) throw. */
	constexpr double ArcSideM = 0.9;
	constexpr double ArcRiseM = 1.1;
	/** A stuck dart's resting angles (radians): tilted up, turned a touch left. */
	constexpr double StuckPitch = 0.14;
	constexpr double StuckYaw = -0.08;

	// The stuck dart's quiver after it lands (ms; DartsScene.tsx stuck darts).
	constexpr double QuiverPeriodMs = 38.0;
	constexpr double QuiverDecayMs = 260.0;
	constexpr double QuiverAttackMs = 70.0;
	/** It settles this much deeper into the sisal over its first SettleMs. */
	constexpr double SettleDepthM = 0.002;
	constexpr double SettleMs = 420.0;

	/** A finished visit lingers on the board this long after the board passes (unless a dart lands first). */
	constexpr double HoldOverSeconds = 1.6;

	/** The trail streams behind the newest flight until it is nearly in, a step behind the dart. */
	constexpr double TrailUntilProgress = 0.97;
	constexpr double TrailLag = 0.035;
	constexpr double TrailRate = 140.0;

	// The board's shiver (DartsScene.tsx board wobble): the target decays, the wobble chases it.
	constexpr double WobbleDecay = 2.8;
	constexpr double WobbleFollow = 28.0;
	/** Peak tilt (radians) about the board's normal and across it, at full wobble, and their rates (rad/s). */
	constexpr double WobbleRollRadians = 0.016;
	constexpr double WobblePitchRadians = 0.009;
	constexpr double WobbleRollRate = 18.0;
	constexpr double WobblePitchRate = 11.0;

	/** The bounty wedge pulses at this rate (rad/s). */
	constexpr double BountyPulseRate = 4.0;

	// The reticle: it hides for a beat after a landing, breathes, tightens with the power, glows brighter when gripped.
	constexpr double ReticleHideAfterLandingSeconds = 0.25;
	constexpr double ReticleBreath = 0.08;
	constexpr double ReticleBreathRate = 3.0;
	constexpr double ReticleGripTighten = 0.25;
	constexpr float ReticleGlow = 2.2f;
	constexpr float ReticleGripGlow = 3.5f;

	/** The pub murmurs: chatter every this many seconds after the first (sound.ts). */
	constexpr double ChatterEverySeconds = 9.0;
	/** The chalkboard fits names to this many letters. */
	constexpr int32 ChalkNameLetters = 10;

	const FName NameColor(TEXT("Color"));
	const FName CueChatter(TEXT("darts.pub-chatter"));
}

FVector ADartsGame::FlightWeb(const FDartFlight& Flight, double T) const
{
	// DartsScene.tsx writeFlightPosition: a scripted arc to the decided point, no physics.
	T = FMath::Clamp(T, 0.0, 1.0);
	const double Travel = 1.0 - FMath::Pow(1.0 - T, 3.0);
	const double Arc = FMath::Sin(UE_DOUBLE_PI * Tuning::SmootherStep(T));
	return FVector(
		LaunchX * (1.0 - Travel) + Flight.X * ADartsPub::BoardRadius * Travel + Flight.Curve * ArcSideM * Arc,
		LaunchY * (1.0 - Travel) + (ADartsPub::BoardY + Flight.Y * ADartsPub::BoardRadius) * Travel + (1.0 - Flight.Power) * ArcRiseM * Arc,
		ThrowDistance * (1.0 - Travel) + StuckZ * Travel);
}

void ADartsGame::PlaceDart(UStaticMeshComponent* Dart, const FVector& TipWeb, double Pitch, double Yaw)
{
	// The web's Euler (-pi/2 + pitch, yaw, 0, YXZ) on a +Y dart is UE pitch +pitch, yaw -yaw on a +X dart.
	Dart->SetWorldLocationAndRotation(ToWorld(TipWeb), FRotator(FMath::RadiansToDegrees(Pitch), -FMath::RadiansToDegrees(Yaw), 0.0));
	Dart->SetVisibility(true);
}

void ADartsGame::SetFlightColor(UStaticMeshComponent* Dart, int32 Lane)
{
	if (FlightMaterials.IsValidIndex(Lane))
	{
		// SetMaterial early-outs on the same material.
		Dart->SetMaterial(ADartsPub::DartFlightSlot, FlightMaterials[Lane]);
	}
}

void ADartsGame::UpdateDarts()
{
	int32 Used = 0;
	StickVisibleVisit(Used);
	FlyDarts(Used);
	for (int32 i = Used; i < Darts.Num(); ++i)
	{
		Darts[i]->SetVisibility(false);
	}
}

void ADartsGame::StickDart(const FDartLanding& Landing, int32& Used)
{
	if (Used >= Darts.Num())
	{
		return;
	}
	const double Age = Landing.AgeMs;
	const double Bounce = FMath::Sin(Age / QuiverPeriodMs) * FMath::Exp(-Age / QuiverDecayMs) * (1.0 - FMath::Exp(-Age / QuiverAttackMs));
	const FVector Tip(Landing.X * ADartsPub::BoardRadius, ADartsPub::BoardY + Landing.Y * ADartsPub::BoardRadius + Bounce * 0.08,
		StuckZ + Bounce * 0.008 + FMath::Min(1.0, Age / SettleMs) * SettleDepthM);
	UStaticMeshComponent* Dart = Darts[Used++];
	SetFlightColor(Dart, Landing.LaneIndex);
	// A stuck dart shadows the board under the board spot; one in flight doesn't bother.
	Dart->SetCastShadow(true);
	PlaceDart(Dart, Tip, StuckPitch + Bounce * 0.035, StuckYaw + Bounce * 0.025);
}

void ADartsGame::StickVisibleVisit(int32& Used)
{
	// Only the current visit's darts are on the board (DartsScene.tsx:1369-1392).
	const FDartsPlayer* Visitor = Match.Players.IsValidIndex(Match.CurrentPlayerIndex) ? &Match.Players[Match.CurrentPlayerIndex] : nullptr;
	if (!Visitor || Stage == EDartsStage::Intro)
	{
		return;
	}
	const int32 Visit = (Visitor->DartsThrown - Match.TurnDarts) / DartsPerRound;
	const bool bEnded = Match.Phase == EMatchPhase::Ended;
	auto IsCurrent = [this, Visitor, Visit, bEnded](const FDartLanding& L)
	{
		const bool bThisVisit = bEnded ? L.Round == Visitor->Round - 1 : L.Round == Visit;
		return bThisVisit && !Match.Flights.ContainsByPredicate([&L](const FDartFlight& F) { return F.Landing.Id == L.Id; });
	};
	if (Visitor->Id != ShownVisitor)
	{
		// The board passed: the finished visit lingers until the next dart lands or a beat goes by.
		// The third dart lands in the same step that passes the board, so read the visit from the match.
		HeldOverPlayer = Match.Players.IndexOfByPredicate([this](const FDartsPlayer& P) { return P.Id == ShownVisitor; });
		if (Match.Players.IsValidIndex(HeldOverPlayer) && Match.Players[HeldOverPlayer].DartsThrown > 0)
		{
			HeldOverVisit = (Match.Players[HeldOverPlayer].DartsThrown - 1) / DartsPerRound;
		}
		else
		{
			HeldOverPlayer = INDEX_NONE;
		}
		HeldOverUntil = Clock + HoldOverSeconds;
		ShownVisitor = Visitor->Id;
	}
	const bool bAnyCurrent = Visitor->Landings.ContainsByPredicate(IsCurrent);
	if (!bAnyCurrent && Clock < HeldOverUntil && Match.Players.IsValidIndex(HeldOverPlayer))
	{
		for (const FDartLanding& L : Match.Players[HeldOverPlayer].Landings)
		{
			if (L.Round == HeldOverVisit)
			{
				StickDart(L, Used);
			}
		}
		return;
	}
	for (const FDartLanding& L : Visitor->Landings)
	{
		if (IsCurrent(L))
		{
			StickDart(L, Used);
		}
	}
}

void ADartsGame::FlyDarts(int32& Used)
{
	for (const FDartFlight& Flight : Match.Flights)
	{
		if (Used >= Darts.Num())
		{
			break;
		}
		// Nose up early (more for a soft throw), the curve's yaw unwinding, both easing to the stuck angles.
		const double T = Flight.Progress;
		const double Ease = Tuning::SmootherStep(T);
		const double Pitch = FMath::Lerp(0.18 + (1.0 - Flight.Power) * 0.18 * FMath::Cos(UE_DOUBLE_PI * T), StuckPitch, Ease);
		const double Yaw = FMath::Lerp(Flight.Curve * 0.4 * (1.0 - T), StuckYaw, Ease);
		UStaticMeshComponent* Dart = Darts[Used++];
		SetFlightColor(Dart, Flight.LaneIndex);
		Dart->SetCastShadow(false);
		PlaceDart(Dart, FlightWeb(Flight, T), Pitch, Yaw);
	}
}

void ADartsGame::UpdateTrail()
{
	if (!Trail)
	{
		return;
	}
	// The newest flight streams a short glowing tail in its thrower's colour.
	const FDartFlight* Flight = Match.Flights.Num() > 0 ? &Match.Flights.Last() : nullptr;
	Fx::FKartFxEmit Tail;
	Tail.Field = Fx::EField::Fire;
	double Rate = 0.0;
	if (Flight && Flight->Progress < TrailUntilProgress)
	{
		const FDartsSeat* Seat = Seats.IsValidIndex(Flight->LaneIndex) ? &Seats[Flight->LaneIndex] : nullptr;
		const FVector Pos = ToWorld(FlightWeb(*Flight, FMath::Max(0.0, Flight->Progress - TrailLag)));
		Trail->SetWorldLocation(Pos);
		Tail.Position = Pos;
		Tail.Spread = 0.2;
		Tail.Speed = FVector2D(0.0, 0.05);
		Tail.Life = FVector2D(0.14, 0.22);
		Tail.Size = FVector2D(0.035, 0.05);
		Tail.SizeEnd = 0.15;
		Tail.Color = Seat ? FMath::Lerp(Seat->Color, FLinearColor::White, 0.35f) : FLinearColor::White;
		Tail.ColorEnd = Seat ? Seat->Color : FLinearColor::White;
		Tail.Intensity = 2.2;
		Rate = TrailRate;
	}
	UKartFxSubsystem::UpdateStream(Trail, Tail, Rate);
}

void ADartsGame::UpdateBoard(float Dt)
{
	// The board shivers when a dart lands (DartsScene.tsx board wobble).
	WobbleTarget *= FMath::Exp(-WobbleDecay * Dt);
	Wobble += (WobbleTarget - Wobble) * (1.0 - FMath::Exp(-WobbleFollow * Dt));
	const double Phase = WobbleSeed * 0.001;
	const double Roll = FMath::Sin(Phase + Clock * WobbleRollRate) * Wobble * WobbleRollRadians;
	const double Tilt = FMath::Cos(Phase + Clock * WobblePitchRate) * Wobble * WobblePitchRadians;
	if (USceneComponent* Pivot = Pub->GetBoardPivot())
	{
		Pivot->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(Tilt), 0.0, FMath::RadiansToDegrees(Roll)));
	}
	// The called shot: the visit's bounty wedge, pulsing, with its outline brighter.
	const FDartsPlayer* Active = Match.ActivePlayer();
	if (Stage != EDartsStage::Match || !Active)
	{
		Pub->HideBounty();
		return;
	}
	const int32 Visit = FMath::Min((Active->DartsThrown - Match.TurnDarts) / DartsPerRound, RoundCount - 1);
	const float Pulse = static_cast<float>(0.5 + 0.5 * FMath::Sin(Clock * BountyPulseRate));
	Pub->ShowBounty(Tuning::SectorIndexOfValue(BountyForRound(Visit)), Pulse);
}

void ADartsGame::UpdateReticle()
{
	FVector2D Aim;
	const bool bShow = GetReticle(Aim) && Clock - LastLandingAt > ReticleHideAfterLandingSeconds;
	Reticle->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}
	const FDartsSeat* Thrower = GetThrower();
	// The ring breathes; while the dart is gripped it tightens with the power and turns mint in the sweet spot.
	const bool bSweet = Local.bGripped && Tuning::IsSweet(Local.Power);
	const double Breath = (1.0 + ReticleBreath * FMath::Sin(Clock * ReticleBreathRate)) * (Local.bGripped ? 1.0 - ReticleGripTighten * Local.Power : 1.0);
	Reticle->SetWorldTransform(FTransform(FRotator::ZeroRotator, ToWorld(ADartsPub::BoardPoint(Aim.X * AimReach, Aim.Y * AimReach, 0.03)), FVector(Breath)));
	static const FLinearColor SweetMint = MovecadeKart::Art::Hex(TEXT("#3ee6a8"));
	const FLinearColor Color = bSweet ? SweetMint : (Thrower ? Thrower->Color : FLinearColor::White);
	ReticleMaterial->SetVectorParameterValue(NameColor, Color * ((Local.bGripped ? ReticleGripGlow : ReticleGlow) * ADartsPub::GlowGain));
}

void ADartsGame::UpdateChalk()
{
	// The chalkboard re-chalks only when a score moves (SetText rebuilds the text's render state).
	uint32 Key = 0;
	for (const FDartsPlayer& P : Match.Players)
	{
		Key = HashCombine(Key, ::GetTypeHash(P.Score));
	}
	if (bChalked && Key == ChalkKey)
	{
		return;
	}
	bChalked = true;
	ChalkKey = Key;
	TArray<FString> Chalk;
	for (const FDartsPlayer* P : Leaderboard(Match))
	{
		Chalk.Add(FString::Printf(TEXT("%s  %d"), *P->Name.Left(ChalkNameLetters).ToUpper(), P->Score));
	}
	Pub->SetChalk(Chalk);
}

void ADartsGame::UpdateAmbience()
{
	// The pub murmurs: chatter at 2.5 s (NextChatterAt's start), then every 9 s (sound.ts).
	if (Clock >= NextChatterAt)
	{
		Cues.Play(this, CueChatter);
		NextChatterAt = Clock + ChatterEverySeconds;
	}
}

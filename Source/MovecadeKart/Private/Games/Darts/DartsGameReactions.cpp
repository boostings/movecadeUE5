// ADartsGame: what a landing sets off (the cues, the camera punch and flash, the crowd, the impact effects, the
// visit card) and the rounds' and the finish's confetti.

#include "Games/Darts/DartsGame.h"

#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Darts/DartsFxRecipes.h"
#include "Games/Darts/DartsGameTuning.h"
#include "Games/Darts/DartsPub.h"
#include "TimerManager.h"

using namespace MovecadeKart::Darts;
using MovecadeKart::Darts::Tuning::ToWorld;
namespace Fx = MovecadeKart::Fx;

#define LOCTEXT_NAMESPACE "Darts"

namespace
{
	const FName CueThud(TEXT("darts.thud"));
	const FName CueMiss(TEXT("darts.miss"));
	const FName CueBull(TEXT("darts.bull"));
	const FName CueWire(TEXT("darts.wire"));
	const FName CueBounty(TEXT("darts.bounty"));
	const FName CueRound(TEXT("darts.round"));
	const FName CueChatter(TEXT("darts.pub-chatter"));

	/** The board's shiver on a landing: a miss knocks it a little, a scoring dart by the effect's intensity. */
	constexpr double MissWobble = 0.3;
	constexpr double HitWobble = 0.72;

	/** A landing at least this big (a bounty or the bull) cheers, confetti and, with -DartsShotBig, a screenshot. */
	constexpr double CheerKick = 0.6;
	/** The bull's kick (the biggest): the loudest cheer, the most confetti. */
	constexpr double BullKick = 1.0;
	/** The cheer: the pub's murmur lifted (louder, quicker) on the crowd's own player. */
	constexpr float CheerVolumeBull = 3.2f;
	constexpr float CheerVolumeBounty = 2.4f;
	constexpr float CheerPitch = 1.18f;
	/** -DartsShotBig grabs the frame this long after the hit (the punch and the flash at their peak). */
	constexpr float BigHitShotDelaySeconds = 0.07f;

	// Confetti counts (ConfettiBurst's own counts scale against FullConfetti).
	constexpr double FullConfetti = 48.0;
	constexpr double BullConfetti = 22.0;
	constexpr double BountyConfetti = 12.0;
	constexpr double BigVisitConfetti = 21.0;
	constexpr double MaxVisitConfetti = 40.0;
	/** The paper is lit: in the pub's dim exposure it needs lifting or it reads as dark scraps. */
	constexpr float ConfettiBrightness = 3.5f;

	// sound.ts mixes: volume, pitch.
	/** The thud climbs in pitch with the points, up to a treble 20's worth. */
	constexpr double ThudPitch = 0.92;
	constexpr double ThudPitchRise = 0.18;
	constexpr double ThudFullPoints = 60.0;
	constexpr float OuterBullVolume = 0.55f;
	constexpr float OuterBullPitch = 0.75f;
	constexpr float TrebleWireVolume = 1.f;
	constexpr float DoubleWireVolume = 0.8f;
	/** The round's sting: louder and higher for a ton or a bonus. */
	constexpr float RoundVolume = 0.7f;
	constexpr float BigRoundVolume = 1.f;
	constexpr float BigRoundPitch = 1.12f;

	/** A big hit's flash: warm white, or half way from a bounty thrower's colour to white. */
	constexpr const TCHAR* FlashWarmWhite = TEXT("fff2c4");
	constexpr float FlashLaneWhiteShare = 0.5f;
	/** The impact's effects sit this far off the face (metres), blowing out toward the oche. */
	constexpr double ImpactLift = 0.03;
	/** The confetti bursts above the board (board radii up, metres out), thrown out into the room and a little up. */
	constexpr double ConfettiBoardY = 1.5;
	constexpr double ConfettiLift = 0.3;
	constexpr double ConfettiRise = 0.4;

	/** The checkout board's calls, scaled to this game's bounty-doubled scores. */
	constexpr int32 MaxVisit = 180;
	constexpr int32 TonForty = 140;
	constexpr int32 Ton = 100;

	/** What a ring sets off: the camera's punch, the screen's flash (impactFx.ts / DartsScene's big hits). */
	struct FPunch
	{
		double Kick = 0.0;
		float Flash = 0.f;
	};

	FPunch PunchForRing(const FDartLanding& Landing)
	{
		if (Landing.Hit.Ring == ERing::Bull)
		{
			return { 1.0, 0.55f };
		}
		if (Landing.bBounty)
		{
			return { 0.65, 0.3f };
		}
		if (Landing.Hit.Ring == ERing::OuterBull)
		{
			return { 0.5, 0.16f };
		}
		if (Landing.Hit.Ring == ERing::Triple)
		{
			return { 0.3, 0.f };
		}
		return {};
	}

	/** impactFx.ts tiers: dust, sparks, spark speed, ring (m), flash; the port adds sisal chips knocked out. */
	struct FImpactTier
	{
		int32 Dust = 3;
		int32 Sparks = 0;
		double Speed = 0.0;
		double Ring = 0.0;
		double Flash = 0.0;
		int32 Chips = 3;
	};

	FImpactTier ImpactTierFor(const FDartLanding& Landing)
	{
		switch (Landing.Hit.Ring)
		{
		case ERing::Bull: return { 12, 40, 3.6, 0.55, 0.28, 10 };
		case ERing::OuterBull: return { 10, 24, 3.0, 0.36, 0.16, 8 };
		case ERing::Triple: return { 9, 18, 2.6, 0.30, 0.12, 7 };
		case ERing::Double: return { 9, 12, 2.2, 0.26, 0.08, 7 };
		case ERing::Single: return { 8, 0, 0.0, 0.0, 0.0, 5 };
		default:
			// A miss in the surround kicks up more than one in the wood.
			return Landing.Hit.Radius <= CatchRadius ? FImpactTier{ 5, 0, 0.0, 0.0, 0.0, 6 } : FImpactTier{ 3, 0, 0.0, 0.0, 0.0, 6 };
		}
	}

	/** A bounty hit sparks at least like a treble (impactFx.ts bounty overrides), in the thrower's colour. */
	struct FBountyLift
	{
		int32 MinSparks = 14;
		double SparkGain = 1.3;
		double MinSpeed = 2.4;
		double MinRing = 0.32;
		double FlashAdd = 0.08;
		double MaxFlash = 0.32;
	};
	const FBountyLift BountyLift;

	void LiftForBounty(FImpactTier& Tier)
	{
		const FBountyLift& L = BountyLift;
		Tier.Sparks = FMath::Max(L.MinSparks, FMath::RoundToInt32(Tier.Sparks * L.SparkGain));
		Tier.Speed = FMath::Max(L.MinSpeed, Tier.Speed);
		Tier.Ring = FMath::Max(L.MinRing, Tier.Ring);
		Tier.Flash = FMath::Min(L.MaxFlash, Tier.Flash + L.FlashAdd);
	}

	FText VisitCall(const FDartsVisitSummary& Visit, int32 Bulls)
	{
		if (Visit.Bounties >= DartsPerRound)
		{
			return LOCTEXT("CallHatTrick", "BOUNTY HAT-TRICK!");
		}
		if (Bulls >= DartsPerRound)
		{
			return LOCTEXT("CallBulls", "THREE IN THE BULL!");
		}
		if (Visit.Total >= MaxVisit)
		{
			return LOCTEXT("Call180", "ONE HUNDRED AND EIGHTY!");
		}
		if (Visit.Total >= TonForty)
		{
			return LOCTEXT("Call140", "TON-FORTY!");
		}
		if (Visit.Total >= Ton)
		{
			return LOCTEXT("CallTon", "TON!");
		}
		if (Visit.Total == 0)
		{
			return LOCTEXT("CallBlank", "BLANK VISIT");
		}
		return FText::Format(LOCTEXT("CallVisit", "VISIT {0}"), Visit.Round + 1);
	}
}

void ADartsGame::OnArrival(const FDartsEffect& Effect)
{
	switch (Effect.Kind)
	{
	case EEffectKind::Round:
		// The engine announces the round as the third dart leaves the hand; the visit card pays it off when it sticks.
		PendingBonus.Add(Effect.PlayerId, Effect.ScoreDelta);
		return;
	case EEffectKind::Finished:
	{
		const FDartsSeat* Seat = SeatOf(Effect.PlayerId);
		Celebrate(FullConfetti, Seat ? Seat->Color : FLinearColor::White);
		return;
	}
	default:
		break;
	}
	// A landing: find it (by position and player) among the player's darts, newest first.
	const FDartsPlayer* Player = Match.FindPlayer(Effect.PlayerId);
	const int32 Index = Player ? Player->Landings.FindLastByPredicate([&Effect](const FDartLanding& L) { return L.X == Effect.X && L.Y == Effect.Y; })
		: INDEX_NONE;
	const FDartLanding* Landing = Index != INDEX_NONE ? &Player->Landings[Index] : nullptr;
	if (!Landing)
	{
		return;
	}
	LastLanding = *Landing;
	LastLandingAt = Clock;
	// The next dart in the board retires the visit card.
	if (LastVisit.At > 0.0 && Clock > LastVisit.At && LastVisit.EndAt > Clock)
	{
		LastVisit.EndAt = Clock;
	}
	WobbleSeed = Landing->Id;
	WobbleTarget = FMath::Max(WobbleTarget, Landing->ScoreDelta == 0 ? MissWobble : HitWobble * Effect.Intensity);
	PlayLandingCues(*Landing);
	const FDartsSeat* Seat = SeatOf(*Player);
	const double Kick = PunchFor(*Landing, Seat);
	if (Kick >= CheerKick)
	{
		ScheduleBigHitShot();
		CrowdCues.Play(this, CueChatter, Kick >= BullKick ? CheerVolumeBull : CheerVolumeBounty, CheerPitch);
		Celebrate(Kick >= BullKick ? BullConfetti : BountyConfetti, Seat ? Seat->Color : FLinearColor::White);
	}
	ImpactFx(*Landing);
	if (Landing->DartIndex == DartsPerRound - 1)
	{
		FinishVisit(*Player, *Landing);
	}
}

void ADartsGame::PlayLandingCues(const FDartLanding& Landing)
{
	// sound.ts: the thud, then the bull, the wire's ring on trebles and doubles, the bounty's sting.
	if (Landing.ScoreDelta == 0)
	{
		Cues.Play(this, CueMiss);
		return;
	}
	// A heavier dart lands deeper: the thud's pitch climbs with the points (to a treble 20).
	Cues.Play(this, CueThud, 1.f, static_cast<float>(ThudPitch + ThudPitchRise * FMath::Min(1.0, Landing.ScoreDelta / ThudFullPoints)));
	switch (Landing.Hit.Ring)
	{
	case ERing::Bull: Cues.Play(this, CueBull); break;
	case ERing::OuterBull: Cues.Play(this, CueBull, OuterBullVolume, OuterBullPitch); break;
	case ERing::Triple: Cues.Play(this, CueWire, TrebleWireVolume); break;
	case ERing::Double: Cues.Play(this, CueWire, DoubleWireVolume); break;
	default: break;
	}
	if (Landing.bBounty)
	{
		Cues.Play(this, CueBounty);
	}
}

double ADartsGame::PunchFor(const FDartLanding& Landing, const FDartsSeat* Seat)
{
	// The big ones land with a punch: the camera kicks in and the screen flashes (a bounty in the thrower's colour).
	const FPunch Hit = PunchForRing(Landing);
	Punch = FMath::Max(Punch, Hit.Kick);
	if (Hit.Flash > 0.f)
	{
		FlashAt = Clock;
		FlashStrength = Hit.Flash;
		const bool bLaneTint = Landing.Hit.Ring != ERing::Bull && Landing.bBounty && Seat;
		FlashColor = bLaneTint ? FMath::Lerp(Seat->Color, FLinearColor::White, FlashLaneWhiteShare) : Fx::Hex(FlashWarmWhite);
	}
	return Hit.Kick;
}

void ADartsGame::ScheduleBigHitShot()
{
#if !UE_BUILD_SHIPPING
	if (!bShotOnBigHit || !Viewer)
	{
		return;
	}
	GetWorldTimerManager().SetTimer(BigHitShotTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (Viewer)
		{
			Viewer->ConsoleCommand(TEXT("Shot showui"));
		}
	}), BigHitShotDelaySeconds, /*bLoop*/ false);
#endif
}

void ADartsGame::FinishVisit(const FDartsPlayer& Player, const FDartLanding& Last)
{
	FDartsVisitSummary Visit;
	Visit.PlayerId = Player.Id;
	Visit.Round = Last.Round;
	int32 Bulls = 0;
	for (const FDartLanding& L : Player.Landings)
	{
		if (L.Round != Last.Round)
		{
			continue;
		}
		Visit.Labels.Add(L.ScoreDelta == 0 ? FString(TEXT("MISS")) : L.Hit.Label);
		Visit.Points.Add(L.ScoreDelta);
		Visit.Total += L.ScoreDelta;
		Visit.Bounties += L.bBounty ? 1 : 0;
		Bulls += L.Hit.Ring == ERing::Bull || L.Hit.Ring == ERing::OuterBull ? 1 : 0;
	}
	Visit.Bonus = PendingBonus.FindRef(Player.Id);
	PendingBonus.Remove(Player.Id);
	Visit.Call = VisitCall(Visit, Bulls);
	Visit.At = Clock;
	LastVisit = MoveTemp(Visit);
	const bool bBig = LastVisit.Total >= Ton || LastVisit.Bonus > 0;
	Cues.Play(this, CueRound, bBig ? BigRoundVolume : RoundVolume, bBig ? BigRoundPitch : 1.f);
	if (bBig)
	{
		const FDartsSeat* Seat = SeatOf(Player);
		Celebrate(LastVisit.Total >= MaxVisit ? MaxVisitConfetti : BigVisitConfetti, Seat ? Seat->Color : FLinearColor::White);
	}
}

void ADartsGame::ImpactFx(const FDartLanding& Landing)
{
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	if (!FxSys)
	{
		return;
	}
	FImpactTier Tier = ImpactTierFor(Landing);
	const bool bBull = Landing.Hit.Ring == ERing::Bull || Landing.Hit.Ring == ERing::OuterBull;
	FLinearColor Accent = bBull ? Fx::Hex(TEXT("#ffd166")) : FLinearColor::White;
	if (Landing.bBounty)
	{
		LiftForBounty(Tier);
		const FDartsSeat* Seat = SeatOf(Landing.PlayerId);
		Accent = Seat ? Seat->Color : Accent;
	}
	const FVector At = ToWorld(ADartsPub::BoardPoint(Landing.X, Landing.Y, ImpactLift));
	// Out of the board, toward the oche.
	const FVector Out(-1.0, 0.0, 0.0);
	const bool bWood = Landing.Hit.Ring == ERing::Miss && Landing.Hit.Radius > CatchRadius;
	{
		Fx::FKartFxEmit Dust = FxRecipes::ImpactDust.Make(At, Tier.Dust);
		Dust.Direction = Out;
		FxSys->Emit(Dust);
	}
	{
		Fx::FKartFxEmit Chips = (bWood ? FxRecipes::WoodChips : FxRecipes::SisalChips).Make(At, Tier.Chips);
		Chips.Direction = (Out + FVector(0.0, 0.0, FxRecipes::ChipsLift)).GetSafeNormal();
		FxSys->Emit(Chips);
	}
	if (Tier.Sparks > 0)
	{
		Fx::FKartFxEmit Sparks = FxRecipes::Sparks.Make(At, Tier.Sparks);
		Sparks.Direction = Out;
		Sparks.Speed = FVector2D(Tier.Speed * FxRecipes::SparkSlowShare, Tier.Speed);
		Sparks.Color = Accent;
		Sparks.ColorEnd = Accent * FxRecipes::SparkFade;
		FxSys->Emit(Sparks);
	}
	if (Tier.Ring > 0.0)
	{
		Fx::FKartFxEmit Ring = FxRecipes::Ring.Make(At);
		Ring.Direction = Out;
		Ring.Size = FVector2D(Tier.Ring * FxRecipes::RingStartShare, Tier.Ring * FxRecipes::RingStartShare);
		Ring.Color = Accent;
		Ring.ColorEnd = Accent;
		FxSys->Emit(Ring);
	}
	if (Tier.Flash > 0.0)
	{
		Fx::FKartFxEmit Flash = FxRecipes::Flash.Make(At);
		Flash.Size = FVector2D(Tier.Flash * FxRecipes::FlashSizePerM, Tier.Flash * FxRecipes::FlashSizePerM);
		Flash.Color = Accent;
		Flash.ColorEnd = Accent;
		FxSys->Emit(Flash);
	}
}

void ADartsGame::Celebrate(double Confetti, const FLinearColor& Color)
{
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	if (!FxSys)
	{
		return;
	}
	// Above the board, thrown out into the room.
	TArray<Fx::FKartFxEmit> Out;
	Fx::ConfettiBurst(ToWorld(ADartsPub::BoardPoint(0.0, ConfettiBoardY, ConfettiLift)), FVector(-1.0, 0.0, ConfettiRise).GetSafeNormal(), Out);
	for (Fx::FKartFxEmit& Emit : Out)
	{
		Emit.Count = FMath::Max(1, FMath::RoundToInt32(Emit.Count * Confetti / FullConfetti));
		if (Emit.Field == Fx::EField::Confetti)
		{
			Emit.Intensity *= ConfettiBrightness;
		}
	}
	FxSys->Emit(Out);
}

#undef LOCTEXT_NAMESPACE

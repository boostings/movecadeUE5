// AStackerGame: what each new rules effect sounds and looks like (cues, particles, camera shake, reactions).

#include "Games/Stacker/StackerGame.h"

#include "Art/KartMaterials.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Stacker/StackerRooftop.h"
#include "StackerScene.h"

using namespace MovecadeKart::Stacker;
namespace Fx = MovecadeKart::Fx;

namespace
{
	/** The drop cue's pitch rises 2% a row, up to this row. */
	constexpr int32 DropPitchRows = 24;
	constexpr float DropPitchPerRow = 0.02f;
	/** The perfect cue climbs a semitone (of 12 to the octave) per combo step, up to an octave. */
	constexpr int32 PerfectComboSteps = 12;
	/** How hard each kind kicks the camera (scaled by the effect's intensity where it has one). */
	constexpr double LandPunchBase = 0.2;
	constexpr double LandPunchPerIntensity = 0.3;
	constexpr double PerfectPunchBase = 0.45;
	constexpr double PerfectPunchPerIntensity = 0.75;
	constexpr double BigPunch = 1.2;
	constexpr double ShakePerPunch = 0.45;
	constexpr double MaxShake = 0.6;
	/** The tower's shudder: hard for a topple, a jolt for a miss. */
	constexpr double TopoutShudder = 1.0;
	constexpr double MissShudder = 0.35;
	/** At (or within a hair of) full intensity a perfect earns the white sparks and the brighter ring. */
	constexpr float FullIntensity = 0.99f;

	/**
	 * One particle burst's fixed shape: field, offset from the effect's point (UE cm), count, cone, speed (m/s), life
	 * (s), size (m) and end size, intensity and jitter (m). Colours and anything that depends on the effect are set
	 * where the recipe is used.
	 */
	struct FEmitRecipe
	{
		Fx::EField Field;
		double OffsetX, OffsetY, OffsetZ;
		int32 Count;
		double Spread;
		double SpeedMin, SpeedMax;
		double LifeMin, LifeMax;
		double SizeMin, SizeMax;
		double SizeEnd;
		double Intensity;
		double Jitter;
	};

	Fx::FKartFxEmit Emit(const FEmitRecipe& R, const FVector& At)
	{
		Fx::FKartFxEmit E;
		E.Field = R.Field;
		E.Position = At + FVector(R.OffsetX, R.OffsetY, R.OffsetZ);
		E.Count = R.Count;
		E.Spread = R.Spread;
		E.Speed = FVector2D(R.SpeedMin, R.SpeedMax);
		E.Life = FVector2D(R.LifeMin, R.LifeMax);
		E.Size = FVector2D(R.SizeMin, R.SizeMax);
		E.SizeEnd = R.SizeEnd;
		E.Intensity = R.Intensity;
		E.Jitter = R.Jitter;
		return E;
	}

	// StackerScene.tsx's bursts. A count or size of 0 here is set per effect.
	/** A puff of grit squeezed out of the joint, blown back and up. */
	constexpr FEmitRecipe LandingGrit{ Fx::EField::Smoke, -60.0, 0.0, -18.0, 6, 1.4, 0.4, 1.0, 0.35, 0.6, 0.06, 0.12, 1.8, 0.8, 0.35 };
	const FVector GritDirection(-0.4, 0.0, 1.0);
	/** A perfect: round(8 + 16i) sparks (three times that for a complete tower), ten more in white at full intensity. */
	constexpr FEmitRecipe PerfectSparks{ Fx::EField::Sparks, -80.0, 0.0, 20.0, 0, 1.2, 2.0, 4.5, 0.35, 0.7, 0.02, 0.04, 0.2, 2.2, 0.3 };
	constexpr int32 WhiteSparks = 10;
	constexpr double WhiteSparkIntensity = 2.8;
	/** Twinkles along the seam: the sparkle that says "clean" (one more per combo step, up to three). */
	constexpr FEmitRecipe SeamTwinkle{ Fx::EField::Flare, -80.0, 0.0, 22.0, 4, 0.6, 0.2, 0.7, 0.35, 0.65, 0.1, 0.18, 0.1, 2.6, 0.3 };
	constexpr double TwinkleJitterPerWidth = 0.45;
	/** The flash at the seam, and the ring around the top (wider with the block, brighter at full intensity). */
	constexpr FEmitRecipe PerfectFlash{ Fx::EField::Flare, -80.0, 0.0, 0.0, 1, 0.5, 0.0, 0.0, 0.22, 0.22, 1.4, 1.4, 0.4, 2.4, 0.0 };
	constexpr FEmitRecipe PerfectRing{ Fx::EField::Ring, 0.0, 0.0, 22.0, 1, 0.5, 0.0, 0.0, 0.45, 0.45, 1.0, 1.0, 2.6, 1.4, 0.0 };
	constexpr double RingSizePerWidth = 0.3;
	constexpr double BrightRingIntensity = 2.2;
	/** A complete tower's confetti, well above the top. */
	constexpr FEmitRecipe CompleteConfetti{ Fx::EField::Confetti, 0.0, 0.0, 120.0, 60, 0.9, 3.0, 6.0, 1.4, 2.2, 0.06, 0.1, 0.2, 1.0, 0.0 };
	/** A cloud of dust off the tower for a miss, a bigger one for a topple. */
	constexpr FEmitRecipe FailureDust{ Fx::EField::Smoke, 0.0, 0.0, 0.0, 22, 1.3, 0.6, 1.8, 0.5, 0.9, 0.12, 0.22, 2.0, 0.9, 0.4 };
	constexpr int32 TopoutDust = 34;
	/** The spark and seam colours mix the lane's colour this far toward white. */
	constexpr float SparkWhiteMix = 0.5f;

	void PerfectBurst(const FVector& At, const FEffect& E, const FTower& Tower, const FLinearColor& Color, TArray<Fx::FKartFxEmit>& Out)
	{
		const float P = static_cast<float>(E.Intensity);
		const bool bComplete = E.Kind == EEffectKind::Complete;
		const FLinearColor Pale = FMath::Lerp(Color, FLinearColor::White, SparkWhiteMix);
		Fx::FKartFxEmit Sparks = Emit(PerfectSparks, At);
		Sparks.Count = FMath::RoundToInt32(8.f + 16.f * P) * (bComplete ? 3 : 1);
		Sparks.Color = Pale;
		Sparks.ColorEnd = Color;
		Out.Add(Sparks);
		Fx::FKartFxEmit Twinkle = Emit(SeamTwinkle, At);
		Twinkle.Count = SeamTwinkle.Count + FMath::Min(Tower.Combo, 3);
		Twinkle.Color = FLinearColor::White;
		Twinkle.ColorEnd = Pale;
		Twinkle.Jitter = FMath::Max(SeamTwinkle.Jitter, Tower.Top().Width * TwinkleJitterPerWidth);
		Out.Add(Twinkle);
		if (P >= FullIntensity)
		{
			Fx::FKartFxEmit White = Sparks;
			White.Count = WhiteSparks;
			White.Color = White.ColorEnd = FLinearColor::White;
			White.Intensity = WhiteSparkIntensity;
			Out.Add(White);
		}
		Fx::FKartFxEmit Flash = Emit(PerfectFlash, At);
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = Color;
		Out.Add(Flash);
		Fx::FKartFxEmit Ring = Emit(PerfectRing, At);
		const double RingSize = PerfectRing.SizeMin + RingSizePerWidth * Tower.Top().Width;
		Ring.Size = FVector2D(RingSize, RingSize);
		Ring.Color = FLinearColor::White;
		Ring.ColorEnd = Color;
		Ring.Intensity = P >= FullIntensity || bComplete ? BrightRingIntensity : PerfectRing.Intensity;
		Out.Add(Ring);
		if (bComplete)
		{
			Out.Add(Emit(CompleteConfetti, At));
		}
	}

	Fx::FKartFxEmit LandingGritAt(const FVector& At)
	{
		Fx::FKartFxEmit Grit = Emit(LandingGrit, At);
		Grit.Direction = GritDirection.GetSafeNormal();
		Grit.Color = MovecadeKart::Art::Hex(TEXT("#e8ddcc"));
		Grit.ColorEnd = MovecadeKart::Art::Hex(TEXT("#a89c8a"));
		return Grit;
	}

	Fx::FKartFxEmit FailureDustAt(const FVector& At, bool bTopout)
	{
		Fx::FKartFxEmit Dust = Emit(FailureDust, At);
		Dust.Count = bTopout ? TopoutDust : FailureDust.Count;
		Dust.Color = MovecadeKart::Art::Hex(TEXT("#cfc6b8"));
		Dust.ColorEnd = MovecadeKart::Art::Hex(TEXT("#8c857a"));
		return Dust;
	}
}

void AStackerGame::PresentNewEffects()
{
	// Effect ids only grow (the rules' one counter), so "new" is "above the last one presented".
	int32 Newest = LastSeenEffectId;
	for (const FEffect& E : Match.Effects)
	{
		if (E.Id > LastSeenEffectId)
		{
			Newest = FMath::Max(Newest, E.Id);
			PresentEffect(E);
			if (OnEffect)
			{
				OnEffect(E);
			}
		}
	}
	LastSeenEffectId = Newest;
}

void AStackerGame::PresentEffect(const FEffect& E)
{
	const int32 i = TowerOfLane(E.LaneIndex);
	if (i == INDEX_NONE)
	{
		return;
	}
	const FTower& Tower = Match.Towers[i];
	FLane& Lane = Lanes[i];
	Lane.Reaction = E.Kind;
	Lane.ReactionAt = Clock;
	if (Lane.Kind == EBuilderKind::Local && (E.Kind == EEffectKind::Land || E.Kind == EEffectKind::Perfect))
	{
		++CoachLandings;
	}
	const float P = static_cast<float>(E.Intensity);
	const FLinearColor Color = AStackerRooftop::LaneColor(Tower.LaneIndex);
	const double Y = (E.Row - Lane.Scroll) * Scene::RowHeight + Scene::RowHeight / 2.0;
	const FVector At = W(FVector(Lane.LaneX + Tower.Top().Center + BalanceDisplacement(Tower.Balance.LeanX), Y, 0.0));
	const float DropPitch = 1.f + FMath::Min(DropPitchRows, E.Row) * DropPitchPerRow;
	TArray<Fx::FKartFxEmit> Out;
	double Punch = 0.0;
	switch (E.Kind)
	{
	case EEffectKind::Land:
		Cues.Play(this, TEXT("stacker.drop"), 0.7f + 0.3f * P, DropPitch);
		Punch = LandPunchBase + LandPunchPerIntensity * P;
		Out.Add(LandingGritAt(At));
		break;
	case EEffectKind::Perfect:
	case EEffectKind::Complete:
	{
		Lane.PerfectAt = Clock;
		Cues.Play(this, TEXT("stacker.drop"), 1.f, DropPitch);
		const int32 Step = FMath::Clamp(Tower.Combo - 1, 0, PerfectComboSteps);
		Cues.Play(this, TEXT("stacker.perfect"), 0.7f + 0.3f * Step / 12.f, FMath::Pow(2.f, Step / 12.f));
		Punch = E.Kind == EEffectKind::Complete ? BigPunch : PerfectPunchBase + PerfectPunchPerIntensity * P;
		PerfectBurst(At, E, Tower, Color, Out);
		break;
	}
	case EEffectKind::Miss:
	case EEffectKind::Topout:
		Cues.Play(this, TEXT("stacker.miss"));
		Punch = BigPunch;
		Lane.Topple = FMath::Max(Lane.Topple, E.Kind == EEffectKind::Topout ? TopoutShudder : MissShudder);
		Out.Add(FailureDustAt(At, E.Kind == EEffectKind::Topout));
		break;
	}
	Shake = FMath::Max(Shake, FMath::Min(MaxShake, Punch * ShakePerPunch));
	UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this);
	if (FxSystem && Out.Num() > 0)
	{
		FxSystem->Emit(Out);
	}
}

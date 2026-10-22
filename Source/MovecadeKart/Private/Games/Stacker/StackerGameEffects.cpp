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

	/** A puff of grit squeezed out of the joint. */
	Fx::FKartFxEmit LandingGrit(const FVector& At)
	{
		Fx::FKartFxEmit Grit;
		Grit.Field = Fx::EField::Smoke;
		Grit.Position = At + FVector(-60.0, 0.0, -18.0);
		Grit.Count = 6;
		Grit.Direction = FVector(-0.4, 0.0, 1.0).GetSafeNormal();
		Grit.Spread = 1.4;
		Grit.Speed = FVector2D(0.4, 1.0);
		Grit.Life = FVector2D(0.35, 0.6);
		Grit.Size = FVector2D(0.06, 0.12);
		Grit.SizeEnd = 1.8;
		Grit.Color = MovecadeKart::Art::Hex(TEXT("#e8ddcc"));
		Grit.ColorEnd = MovecadeKart::Art::Hex(TEXT("#a89c8a"));
		Grit.Intensity = 0.8;
		Grit.Jitter = 0.35;
		return Grit;
	}

	/**
	 * StackerScene.tsx's perfect: round(8 + 16i) sparks (three times that for a complete tower), ten more in white at
	 * full intensity, twinkles along the seam, a flash and a ring; confetti for a complete tower.
	 */
	void PerfectBurst(const FVector& At, const FEffect& E, const FTower& Tower, const FLinearColor& Color, TArray<Fx::FKartFxEmit>& Out)
	{
		const float P = static_cast<float>(E.Intensity);
		const bool bComplete = E.Kind == EEffectKind::Complete;
		Fx::FKartFxEmit Sparks;
		Sparks.Field = Fx::EField::Sparks;
		Sparks.Position = At + FVector(-80.0, 0.0, 20.0);
		Sparks.Count = FMath::RoundToInt32(8.f + 16.f * P) * (bComplete ? 3 : 1);
		Sparks.Spread = 1.2;
		Sparks.Speed = FVector2D(2.0, 4.5);
		Sparks.Life = FVector2D(0.35, 0.7);
		Sparks.Size = FVector2D(0.02, 0.04);
		Sparks.Color = FMath::Lerp(Color, FLinearColor::White, 0.5f);
		Sparks.ColorEnd = Color;
		Sparks.Intensity = 2.2;
		Sparks.Jitter = 0.3;
		Out.Add(Sparks);
		// Twinkles along the seam: the sparkle that says "clean".
		Fx::FKartFxEmit Twinkle;
		Twinkle.Field = Fx::EField::Flare;
		Twinkle.Position = At + FVector(-80.0, 0.0, 22.0);
		Twinkle.Count = 4 + FMath::Min(Tower.Combo, 3);
		Twinkle.Spread = 0.6;
		Twinkle.Speed = FVector2D(0.2, 0.7);
		Twinkle.Life = FVector2D(0.35, 0.65);
		Twinkle.Size = FVector2D(0.1, 0.18);
		Twinkle.SizeEnd = 0.1;
		Twinkle.Color = FLinearColor::White;
		Twinkle.ColorEnd = FMath::Lerp(Color, FLinearColor::White, 0.5f);
		Twinkle.Intensity = 2.6;
		Twinkle.Jitter = FMath::Max(0.3, Tower.Top().Width * 0.45);
		Out.Add(Twinkle);
		if (P >= FullIntensity)
		{
			Fx::FKartFxEmit White = Sparks;
			White.Count = 10;
			White.Color = White.ColorEnd = FLinearColor::White;
			White.Intensity = 2.8;
			Out.Add(White);
		}
		Fx::FKartFxEmit Flash;
		Flash.Field = Fx::EField::Flare;
		Flash.Position = At + FVector(-80.0, 0.0, 0.0);
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.22, 0.22);
		Flash.Size = FVector2D(1.4, 1.4);
		Flash.SizeEnd = 0.4;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = Color;
		Flash.Intensity = 2.4;
		Out.Add(Flash);
		Fx::FKartFxEmit Ring;
		Ring.Field = Fx::EField::Ring;
		Ring.Position = At + FVector(0.0, 0.0, 22.0);
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.45, 0.45);
		Ring.Size = FVector2D(1.0 + 0.3 * Tower.Top().Width, 1.0 + 0.3 * Tower.Top().Width);
		Ring.SizeEnd = 2.6;
		Ring.Color = FLinearColor::White;
		Ring.ColorEnd = Color;
		Ring.Intensity = P >= FullIntensity || bComplete ? 2.2 : 1.4;
		Out.Add(Ring);
		if (bComplete)
		{
			Fx::FKartFxEmit Confetti;
			Confetti.Field = Fx::EField::Confetti;
			Confetti.Position = At + FVector(0.0, 0.0, 120.0);
			Confetti.Count = 60;
			Confetti.Spread = 0.9;
			Confetti.Speed = FVector2D(3.0, 6.0);
			Confetti.Life = FVector2D(1.4, 2.2);
			Confetti.Size = FVector2D(0.06, 0.1);
			Out.Add(Confetti);
		}
	}

	/** A cloud of dust off the tower for a miss, a bigger one for a topple. */
	Fx::FKartFxEmit FailureDust(const FVector& At, bool bTopout)
	{
		Fx::FKartFxEmit Dust;
		Dust.Field = Fx::EField::Smoke;
		Dust.Position = At;
		Dust.Count = bTopout ? 34 : 22;
		Dust.Spread = 1.3;
		Dust.Speed = FVector2D(0.6, 1.8);
		Dust.Life = FVector2D(0.5, 0.9);
		Dust.Size = FVector2D(0.12, 0.22);
		Dust.SizeEnd = 2.0;
		Dust.Color = MovecadeKart::Art::Hex(TEXT("#cfc6b8"));
		Dust.ColorEnd = MovecadeKart::Art::Hex(TEXT("#8c857a"));
		Dust.Intensity = 0.9;
		Dust.Jitter = 0.4;
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
		Out.Add(LandingGrit(At));
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
		Out.Add(FailureDust(At, E.Kind == EEffectKind::Topout));
		break;
	}
	Shake = FMath::Max(Shake, FMath::Min(MaxShake, Punch * ShakePerPunch));
	UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this);
	if (FxSystem && Out.Num() > 0)
	{
		FxSystem->Emit(Out);
	}
}

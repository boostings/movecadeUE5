// AFishingGame's effects: each new rules effect (cast, bite, run, snap, catch, record, ...) becomes sound, splashes,
// sparkle, camera shake and the moments the pose keys off.
#include "Art/KartMaterials.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingGameInternal.h"
#include "Games/Fishing/FishingLake.h"

using namespace MovecadeKart::Fishing;
namespace Fx = MovecadeKart::Fx;

namespace
{
	/** The bobber's kick (cm/s, downward) when the lure lands, on a nibble and on the bite. */
	constexpr double SplashKick = -190.0;
	constexpr double NibbleKick = -95.0;
	constexpr double BiteKick = -240.0;
	/** Camera shake (0..1) on the hook, a snap, and a catch (more for a heavy one). */
	constexpr double HookShake = 0.25;
	constexpr double SnapShake = 0.6;
	constexpr double CatchShake = 0.15;
	constexpr double HeavyCatchShake = 0.4;
	/** A catch this heavy (g) sounds and shakes the heaviest. */
	constexpr float HeavyCatchG = 12000.f;
	/** A strong fish (Strength 0.5+) leaps on 65% of its runs, a beat after the run starts (s). */
	constexpr double LeapMinStrength = 0.5;
	constexpr double LeapOdds = 0.65;
	constexpr double LeapDelaySeconds = 0.45;
	/** A sloppy bot hooks a nibble (too early) up to this often (scaled by 1 - Skill). */
	constexpr double BotNervousHookOdds = 0.3;
	/** The hardest fighters (Strength 1+) run with a deeper sound. */
	constexpr double StrongRunStrength = 1.0;

	void Emit(const UObject* Context, const TArray<Fx::FKartFxEmit>& Emits)
	{
		if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(Context))
		{
			FxSys->Emit(Emits);
		}
	}

	/**
	 * The effects' particle recipes: each returns one emit, ready to send. Size is 0..1, from a nibble's ripple up to
	 * a fish landing; the splash's droplets, spray and ring all grow with it.
	 */
	namespace FxRecipe
	{
		Fx::FKartFxEmit SplashDrops(const FVector& At, double Size)
		{
			Fx::FKartFxEmit Drops;
			Drops.Field = Fx::EField::Sparks;
			Drops.Position = At + FVector(0.0, 0.0, 4.0);
			Drops.Count = FMath::RoundToInt32(8.0 + 18.0 * Size);
			Drops.Spread = 0.55;
			Drops.Speed = FVector2D(1.2, 2.6) * (0.6 + 0.6 * Size);
			Drops.Life = FVector2D(0.35, 0.7);
			Drops.Size = FVector2D(0.015, 0.03);
			Drops.Color = FLinearColor(0.85f, 0.95f, 1.f);
			Drops.ColorEnd = FLinearColor(0.5f, 0.75f, 0.85f);
			Drops.Intensity = 1.4;
			Drops.Jitter = 0.08 * Size;
			return Drops;
		}

		Fx::FKartFxEmit SplashSpray(const FVector& At, double Size)
		{
			Fx::FKartFxEmit Spray;
			Spray.Field = Fx::EField::Smoke;
			Spray.Position = At + FVector(0.0, 0.0, 6.0);
			Spray.Count = FMath::RoundToInt32(4.0 + 8.0 * Size);
			Spray.Spread = 0.5;
			Spray.Speed = FVector2D(0.4, 1.3) * (0.6 + 0.5 * Size);
			Spray.Life = FVector2D(0.35, 0.6);
			Spray.Size = FVector2D(0.06, 0.12) * (0.7 + 0.6 * Size);
			Spray.SizeEnd = 1.6;
			Spray.Color = FLinearColor(0.95f, 0.98f, 1.f);
			Spray.ColorEnd = FLinearColor(0.75f, 0.85f, 0.9f);
			Spray.Intensity = 1.0;
			Spray.Jitter = 0.06 * Size;
			return Spray;
		}

		/** A ring spreading on the water. */
		Fx::FKartFxEmit SplashRing(const FVector& At, double Size)
		{
			Fx::FKartFxEmit Ring;
			Ring.Field = Fx::EField::Ring;
			Ring.Position = At + FVector(0.0, 0.0, 1.0);
			Ring.Speed = FVector2D::ZeroVector;
			Ring.Life = FVector2D(0.8, 0.8);
			Ring.Size = FVector2D(0.25, 0.25) * (0.6 + Size);
			Ring.SizeEnd = 4.0;
			Ring.Color = FLinearColor(0.9f, 0.95f, 1.f);
			Ring.ColorEnd = FLinearColor(0.6f, 0.8f, 0.9f);
			Ring.Intensity = 0.8;
			return Ring;
		}

		/** A snapped line: a white flash at the rod tip, going red as it fades. */
		Fx::FKartFxEmit SnapFlash(const FVector& At)
		{
			Fx::FKartFxEmit Flash;
			Flash.Field = Fx::EField::Flare;
			Flash.Position = At;
			Flash.Speed = FVector2D::ZeroVector;
			Flash.Life = FVector2D(0.18, 0.18);
			Flash.Size = FVector2D(0.6, 0.6);
			Flash.SizeEnd = 0.2;
			Flash.Color = FLinearColor::White;
			Flash.ColorEnd = FLinearColor(1.f, 0.4f, 0.3f);
			Flash.Intensity = 2.5;
			return Flash;
		}

		/** A new record: confetti bursting over the angler's head. */
		Fx::FKartFxEmit RecordConfetti(const FVector& Head)
		{
			Fx::FKartFxEmit Confetti;
			Confetti.Field = Fx::EField::Confetti;
			Confetti.Position = Head + FVector(0.0, 0.0, 60.0);
			Confetti.Count = 70;
			Confetti.Spread = 0.9;
			Confetti.Speed = FVector2D(3.0, 6.0);
			Confetti.Life = FVector2D(1.4, 2.2);
			Confetti.Size = FVector2D(0.05, 0.09);
			return Confetti;
		}

		/** A new record: gold glints off the fish held up. */
		Fx::FKartFxEmit RecordGlint(const FVector& Fish)
		{
			Fx::FKartFxEmit Glint;
			Glint.Field = Fx::EField::Flare;
			Glint.Position = Fish;
			Glint.Count = 6;
			Glint.Spread = 1.2;
			Glint.Speed = FVector2D(0.3, 0.8);
			Glint.Life = FVector2D(0.5, 0.9);
			Glint.Size = FVector2D(0.25, 0.45);
			Glint.Color = MovecadeKart::Art::Hex(TEXT("#fff0b0"));
			Glint.ColorEnd = MovecadeKart::Art::Hex(TEXT("#ffb020"));
			Glint.Intensity = 3.0;
			return Glint;
		}
	}
}

void AFishingGame::PresentEffect(const FEffect& E)
{
	const int32 i = E.LaneIndex;
	if (!Lanes.IsValidIndex(i))
	{
		return;
	}
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	const int32 N = LaneCount();
	Lane.Show.Reaction = E.Kind;
	Lane.Show.ReactionAt = Clock;
	const FVector Water = AFishingLake::WaterPoint(i, N, E.DistanceM, A.State == EAnglerState::Fighting ? A.Fight.Lateral : 0.0);
	const FSpecies* Sp = Species().IsValidIndex(E.Species) ? &Species()[E.Species] : nullptr;
	switch (E.Kind)
	{
	case EEffectKind::Cast:
		Lane.Show.CastFrom = Lane.Show.Tip;
		Cues.Play(this, TEXT("fishing.cast"), 0.7f + 0.3f * static_cast<float>(A.CastPower), 0.85f + 0.3f * static_cast<float>(A.CastPower));
		break;
	case EEffectKind::Splash:
		Lane.Show.BobV = SplashKick;
		Splash(Water, 0.5);
		// Farther casts land with a deeper plop.
		Cues.Play(this, TEXT("fishing.plop"), 0.9f, 1.15f - 0.3f * static_cast<float>(E.DistanceM / MaxCastM));
		break;
	case EEffectKind::Nibble:
		Lane.Show.BobV = NibbleKick;
		Splash(Water, 0.08);
		Cues.Play(this, TEXT("fishing.nibble"), 0.6f, 1.3f);
		// A sloppy bot strikes at the nibble (too early) now and then.
		if (Lane.Kind == EAnglerKind::Bot && Seats.IsValidIndex(i) && BotRng.FRand() < (1.0 - Seats[i].Skill) * BotNervousHookOdds)
		{
			Hook(Match, A.PlayerId);
		}
		break;
	case EEffectKind::Bite:
		Lane.Show.BobV = BiteKick;
		Splash(Water, 0.35);
		Cues.Play(this, TEXT("fishing.bite"));
		break;
	case EEffectKind::Hooked:
		Lane.Show.HookedAt = Clock;
		Splash(Water, 0.7);
		Cues.Play(this, TEXT("fishing.hook"));
		Shake = FMath::Max(Shake, HookShake);
		break;
	case EEffectKind::TooEarly:
		Cues.Play(this, TEXT("fishing.early"), 0.7f, 1.2f);
		break;
	case EEffectKind::Burst:
		Splash(Water, 0.9);
		Cues.Play(this, TEXT("fishing.burst"), 0.8f, Sp && Sp->Strength >= StrongRunStrength ? 0.8f : 1.1f);
		if (Sp && Sp->Strength >= LeapMinStrength && FxRng.FRand() < LeapOdds)
		{
			Lane.Show.JumpAt = Clock + LeapDelaySeconds;
		}
		break;
	case EEffectKind::Snap:
		Cues.Play(this, TEXT("fishing.snap"));
		Shake = FMath::Max(Shake, SnapShake);
		SnapFlash(Lane.Show.Tip);
		Splash(Water, 0.6);
		break;
	case EEffectKind::Escape:
	case EEffectKind::StolenBait:
		Splash(Water, 0.4);
		Cues.Play(this, TEXT("fishing.escape"));
		break;
	case EEffectKind::Catch:
	case EEffectKind::Junk:
	{
		const bool bJunk = E.Kind == EEffectKind::Junk;
		Splash(AFishingLake::WaterPoint(i, N, LandM, 0.0), 1.0);
		const float Big = FMath::Clamp(static_cast<float>(E.WeightG) / HeavyCatchG, 0.f, 1.f);
		Cues.Play(this, bJunk ? TEXT("fishing.junk") : TEXT("fishing.catch"), 0.8f + 0.2f * Big, bJunk ? 0.8f : 1.1f - 0.25f * Big);
		Shake = FMath::Max(Shake, CatchShake + HeavyCatchShake * Big);
		break;
	}
	case EEffectKind::Record:
		Cues.Play(this, TEXT("fishing.record"));
		RecordBurst(i);
		break;
	default:
		break;
	}
}

void AFishingGame::Splash(const FVector& At, double Size, bool bRing)
{
	// Droplets and a puff of spray; on open water a ring too.
	TArray<Fx::FKartFxEmit> Out;
	Out.Add(FxRecipe::SplashDrops(At, Size));
	Out.Add(FxRecipe::SplashSpray(At, Size));
	if (bRing)
	{
		Out.Add(FxRecipe::SplashRing(At, Size));
	}
	Emit(this, Out);
}

void AFishingGame::SnapFlash(const FVector& At)
{
	Emit(this, { FxRecipe::SnapFlash(At) });
}

void AFishingGame::RecordBurst(int32 i)
{
	Emit(this, { FxRecipe::RecordConfetti(HeadOf(i)), FxRecipe::RecordGlint(HoldPoint(i)) });
}

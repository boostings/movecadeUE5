// ASumoGame's presentation of the rules' events: FX, sound, camera shake and hit-stop, callouts and the knockout feed.

#include "Games/Sumo/SumoGame.h"

#include "FX/KartFxSubsystem.h"
#include "Games/Sumo/SumoArena.h"
#include "Games/Sumo/SumoFx.h"
#include "Games/Sumo/SumoPresentation.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Sumo;
using namespace MovecadeKart::Sumo::Presentation;
namespace Fx = MovecadeKart::Fx;

namespace
{
	/** Events happen at the pucks' centre height; a dash's ring and dust start at the skirt; a pickup's flash at the icon (cm). */
	constexpr double EventZ = 40.0;
	constexpr double DashRingZ = 8.0;
	constexpr double DashPuffZ = 10.0;
	constexpr double PowerFlashZ = 90.0;
	/** A splash sits just over the sea's surface, cm. */
	constexpr double SplashOverWaterZ = 10.0;
	/** Callouts float this high over the puck they are about, cm. */
	constexpr double SmashCalloutZ = 150.0;
	constexpr double RingOutCalloutZ = 160.0;
	constexpr double PuckCalloutZ = 190.0;
	constexpr double RoundWinConfettiZ = 220.0;
	/** A dash hit this hard (closing m/s) earns a SMASH! */
	constexpr double SmashStrength = 9.0;
	/** A shield pop always reads as at least this hard a hit. */
	constexpr double ShieldBlockMinStrength = 10.0;
	/** Hits normalise to 0..1 power over this closing speed (m/s), at least the floor. */
	constexpr double ImpactFullStrength = 14.0;
	constexpr double ImpactMinPower = 0.15;
	/** The squash a hit leaves on both pucks, over this closing speed. */
	constexpr double SquashFullStrength = 12.0;
	/** Hit-stop: the frame holds this long on a dash hit, shorter on a hard bump, not at all on a nudge. */
	constexpr double DashHitStopSeconds = 0.085;
	constexpr double BumpHitStopSeconds = 0.05;
	constexpr double BumpHitStopPower = 0.5;
	/** A crash sounds over the bump on a dash or a hit this hard. */
	constexpr double CrashPower = 0.55;
	/** Camera shake floors for a fall and a band breaking away. */
	constexpr double RingOutShake = 0.5;
	constexpr double ShrinkShake = 0.4;
	/** A fall spins the puck this fast (deg/s), either way. */
	constexpr float FallSpinMin = 240.f;
	constexpr float FallSpinMax = 520.f;
	/** Callouts kept at once (the oldest goes first). */
	constexpr int32 MaxCallouts = 12;
	/** How long the HAKKE-YOI! and the round's result stay up. */
	constexpr double FightCalloutSeconds = 1.1;
	constexpr double RoundOverCalloutSeconds = 3.0;

	const FName CueBump(TEXT("sumo.bump"));
	const FName CueCrash(TEXT("sumo.crash"));
	const FName CueBlock(TEXT("sumo.block"));
	const FName CueDash(TEXT("sumo.dash"));
	const FName CueFall(TEXT("sumo.fall"));
	const FName CueKnockout(TEXT("sumo.ko"));
	const FName CueSplash(TEXT("sumo.splash"));
	const FName CueSpawn(TEXT("sumo.spawn"));
	const FName CuePower(TEXT("sumo.power"));
	const FName CueWarn(TEXT("sumo.warn"));
	const FName CueCrumble(TEXT("sumo.crumble"));
	const FName CueWin(TEXT("sumo.win"));
	const FName CueDraw(TEXT("sumo.draw"));
	const FName CueRound(TEXT("sumo.round"));
	const FName CueFight(TEXT("lobby.go"));
}

void ASumoGame::EmitFx(const TArray<Fx::FKartFxEmit>& Emits) const
{
	if (Emits.Num() == 0)
	{
		return;
	}
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		FxSys->Emit(Emits);
	}
}

void ASumoGame::Callout(const FString& Text, const FLinearColor& Color, const FVector& World, bool bBig, const FString& Sub, double Duration)
{
	FSumoCallout& C = Callouts.AddDefaulted_GetRef();
	C.Text = Text;
	C.Sub = Sub;
	C.Color = Color;
	C.World = World;
	C.bCentre = World.IsZero();
	C.bBig = bBig;
	C.At = Clock;
	C.Duration = Duration;
	if (Callouts.Num() > MaxCallouts)
	{
		Callouts.RemoveAt(0);
	}
}

void ASumoGame::Present(const FRingEvent& E)
{
	switch (E.Kind)
	{
	case EEventKind::RoundStart:
		PresentRoundStart(E);
		break;
	case EEventKind::Fight:
		Cues.Play(this, CueFight);
		Callout(TEXT("HAKKE-YOI!"), KartUI::Color::Sun, FVector::ZeroVector, true, FString(), FightCalloutSeconds);
		break;
	case EEventKind::Bump:
	case EEventKind::DashHit:
		PresentHit(E);
		break;
	case EEventKind::ShieldBlock:
		PresentShieldBlock(E);
		break;
	case EEventKind::Dash:
		PresentDash(E);
		break;
	case EEventKind::RingOut:
		PresentRingOut(E);
		break;
	case EEventKind::Splash:
		PresentSplash(E);
		break;
	case EEventKind::PowerSpawn:
		Cues.Play(this, CueSpawn);
		break;
	case EEventKind::PowerPickup:
		PresentPowerPickup(E);
		break;
	case EEventKind::ShrinkWarning:
		Cues.Play(this, CueWarn);
		// Strength is the radius the band leaves: none left means the core itself goes.
		Callout(E.Strength > 0.0 ? TEXT("THE RING SHRINKS!") : TEXT("THE CORE IS GOING!"), KartUI::Color::Hot, FVector::ZeroVector, false, FString(), 1.6);
		break;
	case EEventKind::Shrink:
		Cues.Play(this, CueCrumble);
		Shake = FMath::Max(Shake, ShrinkShake);
		break;
	case EEventKind::RoundWin:
		PresentRoundWin(E);
		break;
	case EEventKind::RoundDraw:
		RoundOverAt = Clock;
		Cues.Play(this, CueDraw);
		Callout(TEXT("NO CONTEST"), KartUI::Color::White, FVector::ZeroVector, true, TEXT("everyone fell: replay the round"), RoundOverCalloutSeconds);
		break;
	case EEventKind::PowerExpire:
	case EEventKind::MatchOver:
		// The pool notices a gone power-up itself; the match's end is the stage change.
		break;
	}
}

void ASumoGame::PresentRoundStart(const FRingEvent& E)
{
	if (Arena)
	{
		Arena->SetRing(0, false, 0.0);
	}
	Feed.Reset();
	LastCountdown = 0;
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		Pilots[i].PrevPos = Match.Pucks[i].Pos;
		Pilots[i].FallSpin = 0.0;
	}
	// The first round's start is the stage change (SetStage); later ones sound and prompt here.
	if (E.Round > 1)
	{
		Cues.Play(this, CueRound);
		if (OnRoundStart)
		{
			OnRoundStart(E.Round);
		}
	}
}

void ASumoGame::PresentHit(const FRingEvent& E)
{
	if (!Match.Pucks.IsValidIndex(E.A) || !Match.Pucks.IsValidIndex(E.B))
	{
		return;
	}
	const FVector At = Cm(E.Pos, EventZ);
	Impact(At, ColorOf(E.A), ColorOf(E.B), E.Strength, E.Kind == EEventKind::DashHit);
	const FVector2D Dir = (Match.Pucks[E.B].Pos - Match.Pucks[E.A].Pos).GetSafeNormal();
	Pilots[E.A].HitAt = Pilots[E.B].HitAt = Clock;
	Pilots[E.A].HitDir = -Dir;
	Pilots[E.B].HitDir = Dir;
	const double Squash = FMath::Clamp(E.Strength / SquashFullStrength, 0.1, 1.0);
	Pilots[E.A].Squash = FMath::Max(Pilots[E.A].Squash, Squash);
	Pilots[E.B].Squash = FMath::Max(Pilots[E.B].Squash, Squash);
	if (E.Kind == EEventKind::DashHit && E.Strength > SmashStrength)
	{
		Callout(TEXT("SMASH!"), KartUI::Color::Sun, At + FVector(0.0, 0.0, SmashCalloutZ), false, FString(), 0.8);
	}
}

void ASumoGame::PresentShieldBlock(const FRingEvent& E)
{
	if (!Match.Pucks.IsValidIndex(E.A))
	{
		return;
	}
	const FLinearColor Shield = PowerColor(EPower::Shield);
	const FLinearColor Attacker = Match.Pucks.IsValidIndex(E.B) ? ColorOf(E.B) : FLinearColor::White;
	Impact(Cm(E.Pos, EventZ), Shield, Attacker, FMath::Max(ShieldBlockMinStrength, E.Strength), true);
	Cues.Play(this, CueBlock);
	Callout(TEXT("BLOCKED!"), Shield, PuckWorld(E.A) + FVector(0.0, 0.0, PuckCalloutZ), false, FString(), 0.9);
}

void ASumoGame::PresentDash(const FRingEvent& E)
{
	if (!Match.Pucks.IsValidIndex(E.A))
	{
		return;
	}
	Pilots[E.A].DashAt = Clock;
	Cues.Play(this, CueDash, 0.8f, 0.95f + 0.15f * FMath::FRand());
	// A dash's Strength is its heading (radians).
	TArray<Fx::FKartFxEmit> Out;
	SumoFx::DashBurst(Cm(E.Pos, DashRingZ), Cm(E.Pos, DashPuffZ), E.Strength, ColorOf(E.A), Out);
	EmitFx(Out);
}

void ASumoGame::PresentRingOut(const FRingEvent& E)
{
	if (!Match.Pucks.IsValidIndex(E.A))
	{
		return;
	}
	Pilots[E.A].FallSpin = (FMath::FRand() < 0.5f ? -1.0 : 1.0) * FMath::FRandRange(FallSpinMin, FallSpinMax);
	Cues.Play(this, CueFall);
	const FString& Victim = Match.Pucks[E.A].Name;
	const FVector Over = PuckWorld(E.A) + FVector(0.0, 0.0, RingOutCalloutZ);
	FSumoFeedLine& Line = Feed.AddDefaulted_GetRef();
	Line.Victim = E.A;
	Line.At = Clock;
	if (Match.Pucks.IsValidIndex(E.B))
	{
		Callout(TEXT("KNOCKOUT!"), ColorOf(E.B), Over, true, FString::Printf(TEXT("%s  >  %s"), *Match.Pucks[E.B].Name, *Victim), 1.6);
		Line.Killer = E.B;
		Pilots[E.B].CheerAt = Clock;
		Cues.Play(this, CueKnockout);
	}
	else
	{
		Callout(TEXT("RING OUT!"), ColorOf(E.A), Over, false, Victim, 1.4);
	}
	Shake = FMath::Max(Shake, RingOutShake);
}

void ASumoGame::PresentSplash(const FRingEvent& E)
{
	if (!Match.Pucks.IsValidIndex(E.A) || !Arena)
	{
		return;
	}
	Cues.Play(this, CueSplash);
	TArray<Fx::FKartFxEmit> Out;
	SumoFx::SeaSplash(FVector(E.Pos.X * 100.0, E.Pos.Y * 100.0, ASumoArena::WaterZ + SplashOverWaterZ), ColorOf(E.A), Out);
	EmitFx(Out);
}

void ASumoGame::PresentPowerPickup(const FRingEvent& E)
{
	if (!Match.Pucks.IsValidIndex(E.A))
	{
		return;
	}
	Cues.Play(this, CuePower);
	Callout(PowerCallout(E.Power), PowerColor(E.Power), PuckWorld(E.A) + FVector(0.0, 0.0, PuckCalloutZ), false, FString(), 1.0);
	TArray<Fx::FKartFxEmit> Out;
	SumoFx::PowerFlash(Cm(E.Pos, PowerFlashZ), PowerColor(E.Power), Out);
	EmitFx(Out);
}

void ASumoGame::PresentRoundWin(const FRingEvent& E)
{
	if (!Match.Pucks.IsValidIndex(E.A))
	{
		return;
	}
	RoundOverAt = Clock;
	Pilots[E.A].CheerAt = Clock;
	Cues.Play(this, CueWin);
	const FPuck& Winner = Match.Pucks[E.A];
	const bool bMatch = Winner.RoundWins >= WinsForMatch;
	Callout(FString::Printf(TEXT("%s TAKES ROUND %d"), *Winner.Name.ToUpper(), E.Round), ColorOf(E.A), FVector::ZeroVector, true,
		bMatch ? FString(TEXT("and the match!")) : FString::Printf(TEXT("%d of %d round wins"), Winner.RoundWins, WinsForMatch), RoundOverCalloutSeconds);
	TArray<Fx::FKartFxEmit> Out;
	Fx::ConfettiBurst(PuckWorld(E.A) + FVector(0.0, 0.0, RoundWinConfettiZ), FVector::ForwardVector, Out);
	EmitFx(Out);
}

void ASumoGame::Impact(const FVector& At, const FLinearColor& A, const FLinearColor& B, double Strength, bool bDash)
{
	const double Power = FMath::Clamp(Strength / ImpactFullStrength, ImpactMinPower, 1.0);
	TArray<Fx::FKartFxEmit> Out;
	SumoFx::HitBurst(At, A, B, Power, bDash, Out);
	EmitFx(Out);
	Shake = FMath::Max(Shake, 0.25 + 0.75 * Power * (bDash ? 1.0 : 0.7));
	// Hit-stop: the frame holds for a beat on a real hit (the rules simply wait, so nothing drifts).
	HitStop = FMath::Max(HitStop, bDash ? DashHitStopSeconds : (Power > BumpHitStopPower ? BumpHitStopSeconds : 0.0));
	const float Gain = static_cast<float>(Power);
	if (bDash || Power > CrashPower)
	{
		Cues.Play(this, CueCrash, 0.6f + 0.4f * Gain, 0.9f + 0.25f * FMath::FRand());
	}
	Cues.Play(this, CueBump, 0.5f + 0.5f * Gain, 0.8f + 0.5f * (1.f - Gain) + 0.1f * FMath::FRand());
}

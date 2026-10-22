// ABowlingGame: the show around a roll. The ball's comet trail, the impact at the rack, the celebrations (confetti,
// spark fountains, fireworks, the screen flash, the alley's light show, the crowd) and the camera jolt.
#include "Games/Bowling/BowlingGame.h"

#include "BowlingGameTuning.h"
#include "Components/StaticMeshComponent.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Bowling/BowlingAlley.h"

using namespace MovecadeKart::Bowling;
using namespace MovecadeKart::Bowling::Tuning;
namespace Fx = MovecadeKart::Fx;

namespace
{
	const FName CueCrowd(TEXT("darts.pub-chatter"));
	/** The fanfare stings for the big moments. */
	const FName CueSparkle(TEXT("stacker.perfect"));
	const FName CueGo(TEXT("lobby.go"));

	/** The screen flash fades out over this long. */
	constexpr double FlashSeconds = 0.6;
	/** The light show eases in over this long and out over the last FadeOut of its run. */
	constexpr double ShowFadeInSeconds = 0.08;
	constexpr double ShowFadeOutSeconds = 0.6;
	/** Confetti is paper in a dark room: lifted so it reads as colour, not as dark flecks across the lens. */
	constexpr double ConfettiGlow = 3.0;
	constexpr double ConfettiScale = 0.75;

	/** The ball's comet: a glow in the bowler's colour, and sparkles that kick up harder the faster it goes. */
	constexpr double TrailRate = 120.0;
	constexpr double SparkleRate = 14.0;
	constexpr double SparkleRatePerHeat = 26.0;
	/** A ball reads hot from 7 m/s and fully hot 2 m/s later. */
	constexpr double HotFromSpeed = 7.0;
	constexpr double HotSpeedRange = 2.0;
	/** The trail hugs the lane just under the ball's centre (UE cm). */
	constexpr double TrailDropCm = 10.0;

	/** The impact's camera jolt (web units): a baseline plus more for a bigger share of the rack. */
	constexpr double ImpactShake = 0.02;
	constexpr double ImpactShakePerShare = 0.06;

	FVector DeckCentre()
	{
		return LaneToWorld(0.0, 1.0, ABowlingAlley::DeckZ - 0.8);
	}

	/** Spark fountains off the tops of the kickbacks either side of the deck; Height scales how high they throw. */
	void AddFountains(TArray<Fx::FKartFxEmit>& Out, const FLinearColor& Color, int32 Count, double Height)
	{
		for (const double Side : { -1.775, 1.775 })
		{
			Fx::FKartFxEmit Sparks;
			Sparks.Field = Fx::EField::Sparks;
			Sparks.Position = LaneToWorld(Side, 1.15, ABowlingAlley::DeckZ - 0.4);
			Sparks.Count = Count;
			Sparks.Direction = FVector::UpVector;
			Sparks.Spread = 0.28;
			Sparks.Speed = { 5.0 * Height, 8.5 * Height };
			Sparks.Life = { 0.7, 1.2 };
			Sparks.Size = { 0.05, 0.09 };
			Sparks.Color = Color;
			Sparks.ColorEnd = FLinearColor(1.f, 0.35f, 0.1f);
			Sparks.Intensity = 6.0;
			Sparks.Jitter = 0.15;
			Out.Add(Sparks);
		}
	}

	/** Three bursts over the deck: pink, gold, cyan. */
	void AddFireworks(TArray<Fx::FKartFxEmit>& Out)
	{
		for (int32 i = 0; i < 3; ++i)
		{
			Fx::FKartFxEmit Burst;
			Burst.Field = Fx::EField::Sparks;
			Burst.Position = LaneToWorld(-1.6 + 1.6 * i, 3.6 + 0.3 * (i % 2), ABowlingAlley::DeckZ + 0.8);
			Burst.Count = 60;
			Burst.Spread = UE_DOUBLE_PI;
			Burst.Speed = { 3.0, 6.5 };
			Burst.Life = { 0.8, 1.3 };
			Burst.Size = { 0.05, 0.08 };
			Burst.Color = i == 1 ? Fx::Hex(TEXT("fff2a8")) : (i == 0 ? Fx::Hex(TEXT("ff3fb4")) : Fx::Hex(TEXT("38e1ff")));
			Burst.ColorEnd = Fx::Hex(TEXT("ff7a2a"));
			Burst.Intensity = 7.0;
			Out.Add(Burst);
		}
	}

	/** Wood chips off the struck pins, kicked up and down the lane; Share is the part of the rack felled. */
	Fx::FKartFxEmit PinChips(const FVector& Hit, double Share)
	{
		Fx::FKartFxEmit Chips;
		Chips.Field = Fx::EField::Sparks;
		Chips.Position = Hit + FVector(0.0, 0.0, 40.0);
		Chips.Count = 12 + FMath::RoundToInt32(36.0 * Share);
		Chips.Direction = (FVector::UpVector + FVector::ForwardVector * 0.6).GetSafeNormal();
		Chips.Spread = 1.1;
		Chips.Speed = { 2.5, 5.0 + 3.0 * Share };
		Chips.Life = { 0.35, 0.7 };
		Chips.Size = { 0.05, 0.1 };
		Chips.Color = Fx::Hex(TEXT("fff6e0"));
		Chips.ColorEnd = Fx::Hex(TEXT("ffb35a"));
		Chips.Intensity = 5.0;
		return Chips;
	}

	/** A shock ring across the deck from where the ball meets the rack. */
	Fx::FKartFxEmit ShockRing(double EndX, double Share)
	{
		Fx::FKartFxEmit Ring;
		Ring.Field = Fx::EField::Ring;
		Ring.Position = LaneToWorld(EndX * ABowlingAlley::VisualPinScale, 0.05, ABowlingAlley::DeckZ - 0.5);
		Ring.Count = 1;
		Ring.Speed = { 0.0, 0.0 };
		Ring.Life = { 0.45, 0.45 };
		Ring.Size = { 0.4, 0.4 };
		Ring.SizeEnd = 6.0 + 4.0 * Share;
		Ring.Color = Fx::Hex(TEXT("bfe9ff"));
		Ring.ColorEnd = Fx::Hex(TEXT("6d8bff"));
		Ring.Intensity = 2.0 * Share;
		return Ring;
	}
}

// ---- Celebrations -----------------------------------------------------------------------------------------------

void ABowlingGame::Celebrate(const FLinearColor& Mine)
{
	TArray<Fx::FKartFxEmit> Out;
	switch (LastBeat)
	{
	case EBowlingBeat::Turkey:
		CelebrateTurkey(Out);
		break;
	case EBowlingBeat::Strike:
		CelebrateStrike(Mine, Out);
		break;
	case EBowlingBeat::Spare:
	case EBowlingBeat::SplitConvert:
		CelebrateSpare(Mine, Out);
		break;
	case EBowlingBeat::Gutter:
		// A sympathetic groan from the house.
		Crowd.Play(this, CueCrowd, 1.6f, 0.72f);
		break;
	default:
		break;
	}
	for (Fx::FKartFxEmit& Emit : Out)
	{
		if (Emit.Field == Fx::EField::Confetti)
		{
			Emit.Intensity *= ConfettiGlow;
			Emit.Size *= ConfettiScale;
		}
	}
	if (UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this); FxSystem && Out.Num() > 0)
	{
		FxSystem->Emit(Out);
	}
}

void ABowlingGame::CelebrateTurkey(TArray<Fx::FKartFxEmit>& Out)
{
	Fx::ConfettiBurst(DeckCentre(), FVector::ForwardVector, Out);
	Fx::ConfettiBurst(LaneToWorld(0.0, 2.2, ABowlingAlley::DeckZ + 0.2), FVector::ForwardVector, Out);
	AddFountains(Out, Fx::Hex(TEXT("ffd34d")), 70, 1.2);
	AddFireworks(Out);
	Flash(FLinearColor(1.f, 0.82f, 0.3f), 0.75);
	// Past energy 1 the show strobes (ABowlingAlley::SetShow), tinting toward gold.
	StartShow(1.4, 3.2, FLinearColor(1.f, 0.75f, 0.2f));
	Crowd.Play(this, CueCrowd, 4.5f, 1.0f);
	CrowdHigh.Play(this, CueCrowd, 3.5f, 1.3f);
	Cues.Play(this, CueGo, 1.2f, 0.85f);
	Cues.Play(this, CueSparkle, 1.0f, 1.25f);
	Shake(0.08);
}

void ABowlingGame::CelebrateStrike(const FLinearColor& Mine, TArray<Fx::FKartFxEmit>& Out)
{
	Fx::ConfettiBurst(DeckCentre(), FVector::ForwardVector, Out);
	AddFountains(Out, Fx::Hex(TEXT("fff2c0")), 45, 1.0);
	Flash(FLinearColor::White, 0.55);
	StartShow(1.0, 2.4, Mine);
	Crowd.Play(this, CueCrowd, 3.6f, 1.0f);
	CrowdHigh.Play(this, CueCrowd, 2.4f, 1.22f);
	Cues.Play(this, CueSparkle, 0.8f, 1.1f);
	Shake(0.05);
}

void ABowlingGame::CelebrateSpare(const FLinearColor& Mine, TArray<Fx::FKartFxEmit>& Out)
{
	const bool bConverted = LastBeat == EBowlingBeat::SplitConvert;
	// Half the strike's confetti; a converted split adds the fountains.
	Fx::ConfettiBurst(DeckCentre(), FVector::ForwardVector, Out);
	for (Fx::FKartFxEmit& Emit : Out)
	{
		Emit.Count = FMath::Max(1, Emit.Count / 2);
	}
	if (bConverted)
	{
		AddFountains(Out, Fx::Hex(TEXT("7dffcf")), 40, 1.0);
	}
	Flash(FLinearColor(0.5f, 1.f, 0.8f), 0.3);
	StartShow(bConverted ? 1.0 : 0.55, 1.5, Mine);
	Crowd.Play(this, CueCrowd, bConverted ? 3.4f : 2.4f, 1.08f);
}

void ABowlingGame::Flash(const FLinearColor& Color, double Strength)
{
	FlashColor = Color;
	FlashStrength = Strength;
	FlashAt = Clock;
}

void ABowlingGame::StartShow(double Energy, double Seconds, const FLinearColor& Tint)
{
	ShowAt = Clock;
	ShowEnergy = Energy;
	ShowSeconds = Seconds;
	ShowTint = Tint;
}

float ABowlingGame::GetFlash(FLinearColor& OutColor) const
{
	OutColor = FlashColor;
	const double Age = Clock - FlashAt;
	if (Age < 0.0 || Age > FlashSeconds)
	{
		return 0.f;
	}
	return static_cast<float>(FlashStrength * FMath::Square(1.0 - Age / FlashSeconds));
}

void ABowlingGame::UpdateShow()
{
	if (!Alley)
	{
		return;
	}
	const double Age = Clock - ShowAt;
	if (Age < 0.0 || Age > ShowSeconds)
	{
		Alley->SetShow(0.0, ShowTint, 0.0);
		return;
	}
	const double Envelope = Smooth(0.0, ShowFadeInSeconds, Age) * (1.0 - Smooth(ShowSeconds - ShowFadeOutSeconds, ShowSeconds, Age));
	Alley->SetShow(ShowEnergy * Envelope, ShowTint, Age);
}

// ---- The roll ---------------------------------------------------------------------------------------------------

void ABowlingGame::EmitImpact()
{
	// fxPlan.ts: sparks, chips and dust by the share of the rack felled, a shock ring across the deck.
	if (Shot.bGutter)
	{
		return;
	}
	const double Share = Standing.Num() > 0 ? static_cast<double>(Shot.Felled.Num()) / Standing.Num() : 0.0;
	const FVector Hit = LaneToWorld(Shot.EndX * ABowlingAlley::VisualPinScale, 0.0, ABowlingAlley::DeckZ - 0.2);
	if (UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this))
	{
		TArray<Fx::FKartFxEmit> Out;
		Fx::ImpactBurst(Fx::EImpact::Bump, Hit, 0.3 + 0.7 * Share, Out);
		// No kart dust on a polished deck: only the sparks of the hit.
		Out.RemoveAll([](const Fx::FKartFxEmit& Emit) { return Emit.Field == Fx::EField::Smoke; });
		if (Shot.Felled.Num() > 0)
		{
			Out.Add(PinChips(Hit, Share));
			Out.Add(ShockRing(Shot.EndX, Share));
		}
		FxSystem->Emit(Out);
	}
	Shake(ImpactShake + ImpactShakePerShare * Share);
}

void ABowlingGame::UpdateTrail(float Dt)
{
	// Off in the gutter: the comet only burns on the lane.
	if (Phase != EBowlingPhase::Rolling || !bHasShot)
	{
		return;
	}
	const double T = FMath::Clamp(GetPhaseAge() / RollSeconds, 0.0, 1.0);
	if (Shot.bGutter && T * LaneLength > Shot.GutterAt)
	{
		return;
	}
	UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this);
	const FBowlerSeat* Bowler = GetBowler();
	if (!FxSystem || !Bowler)
	{
		return;
	}
	const FVector At = Ball->GetComponentLocation() - FVector(0.0, 0.0, TrailDropCm);
	const double Hot = FMath::Clamp((Shot.Speed - HotFromSpeed) / HotSpeedRange, 0.0, 1.0);
	Fx::FKartFxEmit Glow;
	Glow.Field = Fx::EField::Fire;
	Glow.Position = At;
	Glow.Count = Fx::ParticlesDue(TrailCarry, TrailRate, Dt);
	Glow.Direction = FVector::UpVector;
	Glow.Spread = 0.6;
	Glow.Speed = { 0.05, 0.25 };
	Glow.Life = { 0.3, 0.5 };
	Glow.Size = { 0.24, 0.34 };
	Glow.SizeEnd = 0.05;
	Glow.Color = Bowler->Color;
	Glow.ColorEnd = Bowler->Color * 0.4f;
	Glow.Intensity = 4.0 + 3.0 * Hot;
	Glow.Jitter = 0.04;
	if (Glow.Count > 0)
	{
		FxSystem->Emit(Glow);
	}
	Fx::FKartFxEmit Sparkle;
	Sparkle.Field = Fx::EField::Flare;
	Sparkle.Position = At;
	Sparkle.Count = Fx::ParticlesDue(SparkleCarry, SparkleRate + SparkleRatePerHeat * Hot, Dt);
	Sparkle.Spread = 1.2;
	Sparkle.Speed = { 0.4, 1.4 };
	Sparkle.Life = { 0.25, 0.5 };
	Sparkle.Size = { 0.08, 0.16 };
	Sparkle.SizeEnd = 0.1;
	Sparkle.Color = FLinearColor::White;
	Sparkle.ColorEnd = Bowler->Color;
	Sparkle.Intensity = 3.0;
	Sparkle.Jitter = 0.1;
	if (Sparkle.Count > 0)
	{
		FxSystem->Emit(Sparkle);
	}
}

void ABowlingGame::Shake(double Metres)
{
	// A new hit only ever raises what is still shaking.
	const double Left = ShakeMetres * FMath::Max(0.0, 1.0 - (Clock - ShakeAt) / ShakeSeconds);
	if (Metres > Left)
	{
		ShakeMetres = Metres;
		ShakeAt = Clock;
	}
}

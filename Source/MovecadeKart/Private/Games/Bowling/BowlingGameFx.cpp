// ABowlingGame: the show around a roll. The ball's comet trail, the impact at the rack, the celebrations (confetti,
// spark fountains, fireworks, the screen flash, the alley's light show, the crowd) and the camera jolt.
//
// Every effect is a recipe: the emitters' fixed shape is data below, and each use fills in only what varies (where,
// how many, what colour). The celebrations are one table row per beat, applied in a fixed order.
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

	/** fxPlan.ts: the impact burst's strength and the camera jolt (web units), each a baseline plus the rack's share. */
	constexpr double ImpactBurst = 0.3;
	constexpr double ImpactBurstPerShare = 0.7;
	constexpr double ImpactShake = 0.02;
	constexpr double ImpactShakePerShare = 0.06;

	// Where the effects sit on the deck (web units).
	constexpr double KickbackX = 1.775;
	constexpr double FountainY = 1.15;
	constexpr double FountainZ = ABowlingAlley::DeckZ - 0.4;
	const FVector DeckConfettiAt(0.0, 1.0, ABowlingAlley::DeckZ - 0.8);
	const FVector HighConfettiAt(0.0, 2.2, ABowlingAlley::DeckZ + 0.2);
	constexpr double ImpactZ = ABowlingAlley::DeckZ - 0.2;
	constexpr double RingY = 0.05;
	constexpr double RingZ = ABowlingAlley::DeckZ - 0.5;
	/** Wood chips leave from this high above the hit (UE cm). */
	constexpr double ChipLiftCm = 40.0;

	// ---- Emitter recipes: the fixed shape of each effect.

	/** A spark fountain off a kickback top; Speed is scaled by the celebration's height. */
	Fx::FKartFxEmit FountainRecipe()
	{
		return { .Field = Fx::EField::Sparks, .Direction = FVector(0.0, 0.0, 1.0), .Spread = 0.28, .Speed = FVector2D(5.0, 8.5),
			.Life = FVector2D(0.7, 1.2), .Size = FVector2D(0.05, 0.09), .ColorEnd = FLinearColor(1.f, 0.35f, 0.1f), .Intensity = 6.0, .Jitter = 0.15 };
	}

	/** A firework burst over the deck; the colour is per burst. */
	Fx::FKartFxEmit FireworkRecipe()
	{
		return { .Field = Fx::EField::Sparks, .Count = 60, .Spread = UE_DOUBLE_PI, .Speed = FVector2D(3.0, 6.5), .Life = FVector2D(0.8, 1.3),
			.Size = FVector2D(0.05, 0.08), .ColorEnd = Fx::Hex(TEXT("ff7a2a")), .Intensity = 7.0 };
	}

	/** Wood chips off the struck pins, kicked up and down the lane; the count and top speed grow with the share felled. */
	Fx::FKartFxEmit ChipsRecipe()
	{
		return { .Field = Fx::EField::Sparks, .Direction = FVector(0.6, 0.0, 1.0).GetSafeNormal(), .Spread = 1.1, .Life = FVector2D(0.35, 0.7),
			.Size = FVector2D(0.05, 0.1), .Color = Fx::Hex(TEXT("fff6e0")), .ColorEnd = Fx::Hex(TEXT("ffb35a")), .Intensity = 5.0 };
	}

	/** A shock ring across the deck from the hit; its reach and brightness grow with the share felled. */
	Fx::FKartFxEmit RingRecipe()
	{
		return { .Field = Fx::EField::Ring, .Count = 1, .Speed = FVector2D(0.0, 0.0), .Life = FVector2D(0.45, 0.45), .Size = FVector2D(0.4, 0.4),
			.Color = Fx::Hex(TEXT("bfe9ff")), .ColorEnd = Fx::Hex(TEXT("6d8bff")) };
	}

	/** The comet's glow, in the bowler's colour. */
	Fx::FKartFxEmit TrailGlowRecipe()
	{
		return { .Field = Fx::EField::Fire, .Direction = FVector(0.0, 0.0, 1.0), .Spread = 0.6, .Speed = FVector2D(0.05, 0.25), .Life = FVector2D(0.3, 0.5),
			.Size = FVector2D(0.24, 0.34), .SizeEnd = 0.05, .Jitter = 0.04 };
	}

	/** The comet's sparkles, white fading to the bowler's colour. */
	Fx::FKartFxEmit TrailSparkleRecipe()
	{
		return { .Field = Fx::EField::Flare, .Spread = 1.2, .Speed = FVector2D(0.4, 1.4), .Life = FVector2D(0.25, 0.5), .Size = FVector2D(0.08, 0.16),
			.SizeEnd = 0.1, .Color = FLinearColor::White, .Intensity = 3.0, .Jitter = 0.1 };
	}

}

/** One beat's celebration: what the house throws, flashes, lights and plays. A row per beat, below. */
struct FBowlingCelebration
{
	/** Confetti over the deck; a turkey adds a second, higher burst. Half the paper on a spare. */
	bool bHighConfetti = false;
	bool bHalfConfetti = false;
	/** Kickback fountains, when Fountains > 0. */
	const TCHAR* FountainColor = nullptr;
	int32 Fountains = 0;
	double FountainHeight = 1.0;
	bool bFireworks = false;
	FLinearColor FlashColor = FLinearColor::White;
	double Flash = 0.0;
	/** The alley's light show; a gold tint (a turkey) instead of the thrower's colour. */
	double ShowEnergy = 0.0;
	double ShowSeconds = 0.0;
	bool bGoldShow = false;
	/** The crowd, and its second, higher layer (0: none); the stings (0: none). */
	float Crowd = 0.f, CrowdPitch = 1.f;
	float CrowdHigh = 0.f, CrowdHighPitch = 1.f;
	float Go = 0.f, GoPitch = 1.f;
	float Sparkle = 0.f, SparklePitch = 1.f;
	/** The camera jolt (web units; 0: none). */
	double Shake = 0.0;
};

namespace
{
	// ---- Celebrations: one row per beat.

	const FBowlingCelebration& TurkeyCelebration()
	{
		static const FBowlingCelebration Row{ .bHighConfetti = true, .FountainColor = TEXT("ffd34d"), .Fountains = 70, .FountainHeight = 1.2, .bFireworks = true,
			.FlashColor = FLinearColor(1.f, 0.82f, 0.3f), .Flash = 0.75, .ShowEnergy = 1.4, .ShowSeconds = 3.2, .bGoldShow = true,
			.Crowd = 4.5f, .CrowdPitch = 1.0f, .CrowdHigh = 3.5f, .CrowdHighPitch = 1.3f, .Go = 1.2f, .GoPitch = 0.85f, .Sparkle = 1.0f, .SparklePitch = 1.25f,
			.Shake = 0.08 };
		return Row;
	}

	const FBowlingCelebration& StrikeCelebration()
	{
		static const FBowlingCelebration Row{ .FountainColor = TEXT("fff2c0"), .Fountains = 45, .FountainHeight = 1.0, .FlashColor = FLinearColor::White, .Flash = 0.55,
			.ShowEnergy = 1.0, .ShowSeconds = 2.4, .Crowd = 3.6f, .CrowdPitch = 1.0f, .CrowdHigh = 2.4f, .CrowdHighPitch = 1.22f,
			.Sparkle = 0.8f, .SparklePitch = 1.1f, .Shake = 0.05 };
		return Row;
	}

	const FBowlingCelebration& SpareCelebration()
	{
		static const FBowlingCelebration Row{ .bHalfConfetti = true, .FlashColor = FLinearColor(0.5f, 1.f, 0.8f), .Flash = 0.3, .ShowEnergy = 0.55, .ShowSeconds = 1.5,
			.Crowd = 2.4f, .CrowdPitch = 1.08f };
		return Row;
	}

	const FBowlingCelebration& SplitConvertCelebration()
	{
		static const FBowlingCelebration Row{ .bHalfConfetti = true, .FountainColor = TEXT("7dffcf"), .Fountains = 40, .FountainHeight = 1.0,
			.FlashColor = FLinearColor(0.5f, 1.f, 0.8f), .Flash = 0.3, .ShowEnergy = 1.0, .ShowSeconds = 1.5, .Crowd = 3.4f, .CrowdPitch = 1.08f };
		return Row;
	}

	/** The beat's celebration, or null for the beats the house lets pass. */
	const FBowlingCelebration* CelebrationFor(EBowlingBeat Beat)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey: return &TurkeyCelebration();
		case EBowlingBeat::Strike: return &StrikeCelebration();
		case EBowlingBeat::Spare: return &SpareCelebration();
		case EBowlingBeat::SplitConvert: return &SplitConvertCelebration();
		default: return nullptr;
		}
	}

	/** A gold tint for the turkey's show. */
	const FLinearColor TurkeyShowTint(1.f, 0.75f, 0.2f);
	/** A gutter draws a sympathetic groan from the house. */
	constexpr float GutterGroan = 1.6f;
	constexpr float GutterGroanPitch = 0.72f;

	void AddConfetti(const FBowlingCelebration& Row, TArray<Fx::FKartFxEmit>& Out)
	{
		Fx::ConfettiBurst(LaneToWorld(DeckConfettiAt.X, DeckConfettiAt.Y, DeckConfettiAt.Z), FVector::ForwardVector, Out);
		if (Row.bHighConfetti)
		{
			Fx::ConfettiBurst(LaneToWorld(HighConfettiAt.X, HighConfettiAt.Y, HighConfettiAt.Z), FVector::ForwardVector, Out);
		}
		if (Row.bHalfConfetti)
		{
			for (Fx::FKartFxEmit& Emit : Out)
			{
				Emit.Count = FMath::Max(1, Emit.Count / 2);
			}
		}
	}

	void AddFountains(const FBowlingCelebration& Row, TArray<Fx::FKartFxEmit>& Out)
	{
		for (const double Side : { -KickbackX, KickbackX })
		{
			Fx::FKartFxEmit Sparks = FountainRecipe();
			Sparks.Position = LaneToWorld(Side, FountainY, FountainZ);
			Sparks.Count = Row.Fountains;
			Sparks.Speed = { Sparks.Speed.X * Row.FountainHeight, Sparks.Speed.Y * Row.FountainHeight };
			Sparks.Color = Fx::Hex(Row.FountainColor);
			Out.Add(Sparks);
		}
	}

	/** Three bursts over the deck: pink, gold, cyan. */
	void AddFireworks(TArray<Fx::FKartFxEmit>& Out)
	{
		const TCHAR* const Colors[] = { TEXT("ff3fb4"), TEXT("fff2a8"), TEXT("38e1ff") };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Colors); ++i)
		{
			Fx::FKartFxEmit Burst = FireworkRecipe();
			Burst.Position = LaneToWorld(-1.6 + 1.6 * i, 3.6 + 0.3 * (i % 2), ABowlingAlley::DeckZ + 0.8);
			Burst.Color = Fx::Hex(Colors[i]);
			Out.Add(Burst);
		}
	}
}

// ---- Celebrations -----------------------------------------------------------------------------------------------

void ABowlingGame::Celebrate(const FLinearColor& Mine)
{
	if (LastBeat == EBowlingBeat::Gutter)
	{
		Crowd.Play(this, CueCrowd, GutterGroan, GutterGroanPitch);
		return;
	}
	const FBowlingCelebration* Row = CelebrationFor(LastBeat);
	if (!Row)
	{
		return;
	}
	TArray<Fx::FKartFxEmit> Out;
	AddConfetti(*Row, Out);
	if (Row->Fountains > 0)
	{
		AddFountains(*Row, Out);
	}
	if (Row->bFireworks)
	{
		AddFireworks(Out);
	}
	for (Fx::FKartFxEmit& Emit : Out)
	{
		if (Emit.Field == Fx::EField::Confetti)
		{
			Emit.Intensity *= ConfettiGlow;
			Emit.Size *= ConfettiScale;
		}
	}
	if (UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this))
	{
		FxSystem->Emit(Out);
	}
	Flash(Row->FlashColor, Row->Flash);
	StartShow(Row->ShowEnergy, Row->ShowSeconds, Row->bGoldShow ? TurkeyShowTint : Mine);
	PlayCelebrationCues(*Row);
	if (Row->Shake > 0.0)
	{
		Shake(Row->Shake);
	}
}

void ABowlingGame::PlayCelebrationCues(const FBowlingCelebration& Row)
{
	Crowd.Play(this, CueCrowd, Row.Crowd, Row.CrowdPitch);
	if (Row.CrowdHigh > 0.f)
	{
		CrowdHigh.Play(this, CueCrowd, Row.CrowdHigh, Row.CrowdHighPitch);
	}
	if (Row.Go > 0.f)
	{
		Cues.Play(this, CueGo, Row.Go, Row.GoPitch);
	}
	if (Row.Sparkle > 0.f)
	{
		Cues.Play(this, CueSparkle, Row.Sparkle, Row.SparklePitch);
	}
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
	// fxPlan.ts: sparks and chips by the share of the rack felled, a shock ring across the deck.
	if (Shot.bGutter)
	{
		return;
	}
	const double Share = Standing.Num() > 0 ? static_cast<double>(Shot.Felled.Num()) / Standing.Num() : 0.0;
	const double HitX = Shot.EndX * ABowlingAlley::VisualPinScale;
	const FVector Hit = LaneToWorld(HitX, 0.0, ImpactZ);
	if (UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this))
	{
		TArray<Fx::FKartFxEmit> Out;
		Fx::ImpactBurst(Fx::EImpact::Bump, Hit, ImpactBurst + ImpactBurstPerShare * Share, Out);
		// No kart dust on a polished deck: only the sparks of the hit.
		Out.RemoveAll([](const Fx::FKartFxEmit& Emit) { return Emit.Field == Fx::EField::Smoke; });
		if (Shot.Felled.Num() > 0)
		{
			Fx::FKartFxEmit Chips = ChipsRecipe();
			Chips.Position = Hit + FVector(0.0, 0.0, ChipLiftCm);
			Chips.Count = 12 + FMath::RoundToInt32(36.0 * Share);
			Chips.Speed = { 2.5, 5.0 + 3.0 * Share };
			Out.Add(Chips);
			Fx::FKartFxEmit Ring = RingRecipe();
			Ring.Position = LaneToWorld(HitX, RingY, RingZ);
			Ring.SizeEnd = 6.0 + 4.0 * Share;
			Ring.Intensity = 2.0 * Share;
			Out.Add(Ring);
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
	Fx::FKartFxEmit Glow = TrailGlowRecipe();
	Glow.Position = At;
	Glow.Count = Fx::ParticlesDue(TrailCarry, TrailRate, Dt);
	Glow.Color = Bowler->Color;
	Glow.ColorEnd = Bowler->Color * 0.4f;
	Glow.Intensity = 4.0 + 3.0 * Hot;
	if (Glow.Count > 0)
	{
		FxSystem->Emit(Glow);
	}
	Fx::FKartFxEmit Sparkle = TrailSparkleRecipe();
	Sparkle.Position = At;
	Sparkle.Count = Fx::ParticlesDue(SparkleCarry, SparkleRate + SparkleRatePerHeat * Hot, Dt);
	Sparkle.ColorEnd = Bowler->Color;
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

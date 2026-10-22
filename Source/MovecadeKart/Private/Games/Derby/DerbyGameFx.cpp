// ADerbyGame's moments: the crack of contact (sparks, a flash, a ring for a barrel), where the ball comes down (dust,
// the wall, a home run's fireworks and roar), the fireworks queue, and the centre-field scoreboard's lines.

#include "Games/Derby/DerbyGame.h"

#include "Art/KartMaterials.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Derby/DerbyBallpark.h"
#include "Games/Derby/DerbyStage.h"

using namespace MovecadeKart::Derby;
using namespace MovecadeKart::Derby::Stage;
namespace Fx = MovecadeKart::Fx;

namespace
{
	/** Contact: a hit this hard (or a home run) is a big one for the crowd; past RingQuality it rings. */
	constexpr double LoudExitMph = 95.0;
	constexpr float RingQuality = 0.85f;
	/** A home run this square also gets the sweet-spot cue. */
	constexpr float SweetQuality = 0.9f;
	/** The launch the contact sparks fly along, clamped to something the camera sees (degrees). */
	constexpr double SparkMinLaunchDeg = -10.0;
	constexpr double SparkMaxLaunchDeg = 60.0;
	/** The roar for each landing (0..1, ADerbyBallpark::Roar). */
	constexpr float HomeRunRoar = 1.f;
	constexpr float WallRoar = 0.6f;
	constexpr float InPlayRoar = 0.3f;
	constexpr float FoulRoar = 0.25f;
	constexpr float BigContactRoar = 0.7f;
	constexpr float ContactRoar = 0.35f;
	/** Fireworks: bursts per home run, the stagger between them and the random spread over their spots (cm). */
	constexpr int32 MoonshotFireworks = 10;
	constexpr int32 HomeRunFireworks = 5;
	constexpr double FireworkStagger = 0.32;
	constexpr float FireworkStaggerJitter = 0.15f;
	constexpr float FireworkSpreadCm = 1500.f;
	constexpr float FireworkLowCm = -600.f;
	constexpr float FireworkHighCm = 1200.f;
	/** The firework palette (each burst fades from one hue into the next). */
	const TCHAR* const FireworkHues[] = { TEXT("#ff4f5e"), TEXT("#ffd34f"), TEXT("#4fd6ff"), TEXT("#b47bff"), TEXT("#5cff9d"), TEXT("#ff9a3c") };
	constexpr int32 NumFireworkHues = UE_ARRAY_COUNT(FireworkHues);
	const TCHAR* const ContactGold = TEXT("#ffd34f");

	FLinearColor FireworkColor(int32 Index)
	{
		return MovecadeKart::Art::Hex(FireworkHues[((Index % NumFireworkHues) + NumFireworkHues) % NumFireworkHues]);
	}

	/** The crack: sparks along the hit, a white flash, and a ring for a barrelled ball. */
	void EmitContact(UKartFxSubsystem& FxSys, const FVector& At, const FVector& Dir, float Quality)
	{
		const FLinearColor Gold = MovecadeKart::Art::Hex(ContactGold);
		Fx::FKartFxEmit Sparks;
		Sparks.Field = Fx::EField::Sparks;
		Sparks.Position = At;
		Sparks.Direction = Dir;
		Sparks.Count = FMath::RoundToInt32(10.f + 26.f * Quality);
		Sparks.Spread = 0.7;
		Sparks.Speed = FVector2D(3.0, 8.0);
		Sparks.Life = FVector2D(0.2, 0.45);
		Sparks.Size = FVector2D(0.02, 0.04);
		Sparks.Color = FLinearColor::White;
		Sparks.ColorEnd = Gold;
		Sparks.Intensity = 3.0;
		FxSys.Emit(Sparks);
		Fx::FKartFxEmit Flash;
		Flash.Field = Fx::EField::Flare;
		Flash.Position = At;
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.14, 0.14);
		Flash.Size = FVector2D(0.6 + 0.9 * Quality, 0.6 + 0.9 * Quality);
		Flash.SizeEnd = 0.3;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = Gold;
		Flash.Intensity = 3.0;
		FxSys.Emit(Flash);
		if (Quality <= RingQuality)
		{
			return;
		}
		Fx::FKartFxEmit Ring;
		Ring.Field = Fx::EField::Ring;
		Ring.Position = At;
		Ring.Direction = Dir;
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.3, 0.3);
		Ring.Size = FVector2D(0.5, 0.5);
		Ring.SizeEnd = 3.0;
		Ring.Color = FLinearColor::White;
		Ring.ColorEnd = Gold;
		Ring.Intensity = 2.0;
		FxSys.Emit(Ring);
	}

	/** A ball in play kicks up the infield dirt where it lands. */
	Fx::FKartFxEmit LandingDust(const FVector& At)
	{
		Fx::FKartFxEmit Dust;
		Dust.Field = Fx::EField::Smoke;
		Dust.Position = At;
		Dust.Count = 14;
		Dust.Spread = 1.2;
		Dust.Speed = FVector2D(0.6, 1.6);
		Dust.Life = FVector2D(0.6, 1.0);
		Dust.Size = FVector2D(0.4, 0.7);
		Dust.SizeEnd = 2.0;
		Dust.Color = MovecadeKart::Art::Hex(TEXT("#b08a63"));
		Dust.ColorEnd = MovecadeKart::Art::Hex(TEXT("#6d5a44"));
		Dust.Intensity = 0.6;
		return Dust;
	}

	/** One firework: a coloured burst, a white flash, and slow glitter that fades into the next hues. */
	void EmitFirework(UKartFxSubsystem& FxSys, const FVector& At, int32 Hue)
	{
		Fx::FKartFxEmit Burst;
		Burst.Field = Fx::EField::Sparks;
		Burst.Position = At;
		Burst.Count = 140;
		Burst.Spread = PI;
		Burst.Speed = FVector2D(12.0, 19.0);
		Burst.Life = FVector2D(1.1, 1.9);
		Burst.Size = FVector2D(1.0, 1.6);
		Burst.SizeEnd = 0.25;
		Burst.Color = FMath::Lerp(FireworkColor(Hue), FLinearColor::White, 0.35f);
		Burst.ColorEnd = FireworkColor(Hue + 1);
		Burst.Intensity = 14.0;
		Burst.Jitter = 0.6;
		FxSys.Emit(Burst);
		Fx::FKartFxEmit Flash;
		Flash.Field = Fx::EField::Flare;
		Flash.Position = At;
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.3, 0.3);
		Flash.Size = FVector2D(26.0, 26.0);
		Flash.SizeEnd = 0.3;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = FireworkColor(Hue);
		Flash.Intensity = 5.0;
		FxSys.Emit(Flash);
		Fx::FKartFxEmit Glitter;
		Glitter.Field = Fx::EField::Flare;
		Glitter.Position = At;
		Glitter.Count = 40;
		Glitter.Spread = PI;
		Glitter.Speed = FVector2D(4.0, 8.0);
		Glitter.Life = FVector2D(1.4, 2.4);
		Glitter.Size = FVector2D(1.6, 2.6);
		Glitter.SizeEnd = 0.1;
		Glitter.Color = FLinearColor::White;
		Glitter.ColorEnd = FireworkColor(Hue + 2);
		Glitter.Intensity = 8.0;
		Glitter.Jitter = 2.0;
		FxSys.Emit(Glitter);
	}
}

void ADerbyGame::ContactBurst(const FOutcome& Hit)
{
	const float Quality = static_cast<float>(Hit.Contact.Quality);
	const bool bBig = IsHomeRun(Hit.Result) || (Hit.Result == EResult::InPlay && Hit.Contact.ExitMph > LoudExitMph);
	Cues.Play(this, Cue::Crack, 0.6f + 0.5f * Quality, 0.85f + 0.35f * Quality);
	if (Hit.Result == EResult::Moonshot || (IsHomeRun(Hit.Result) && Quality > SweetQuality))
	{
		Cues.Play(this, Cue::Sweet, 0.9f, 1.f);
	}
	Shake = FMath::Max(Shake, 0.25 + 0.5 * Quality);
	if (Park)
	{
		Park->Roar(bBig ? BigContactRoar : ContactRoar);
	}
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	if (!FxSys)
	{
		return;
	}
	const double Spray = FMath::DegreesToRadians(Hit.Contact.SprayDeg);
	const double Launch = FMath::DegreesToRadians(FMath::Clamp(Hit.Contact.LaunchDeg, SparkMinLaunchDeg, SparkMaxLaunchDeg));
	const FVector Dir = FVector(FMath::Cos(Spray), FMath::Sin(Spray), FMath::Tan(Launch)).GetSafeNormal();
	EmitContact(*FxSys, ToCm(PlateM), Dir, Quality);
}

void ADerbyGame::Land()
{
	bLanded = true;
	const FVector At = HitBall(Outcome.Flight.HangS);
	switch (Outcome.Result)
	{
	case EResult::Moonshot:
		QueueFireworks(MoonshotFireworks, 0.0);
		Cues.Play(this, Cue::Homer, 1.1f, 1.08f);
		Cues.Play(this, Cue::Moonshot);
		if (Park)
		{
			Park->Roar(HomeRunRoar);
		}
		break;
	case EResult::HomeRun:
		QueueFireworks(HomeRunFireworks, 0.0);
		Cues.Play(this, Cue::Homer);
		if (Park)
		{
			Park->Roar(HomeRunRoar);
		}
		break;
	case EResult::InPlay:
		Cues.Play(this, Outcome.Flight.bOffWall ? Cue::Wall : Cue::Land);
		if (Park)
		{
			Park->Roar(Outcome.Flight.bOffWall ? WallRoar : InPlayRoar);
		}
		if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
		{
			FxSys->Emit(LandingDust(At));
		}
		break;
	default:
		Cues.Play(this, Cue::Foul);
		if (Park)
		{
			Park->Roar(FoulRoar);
		}
		break;
	}
}

void ADerbyGame::QueueFireworks(int32 Bursts, double Delay)
{
	if (!Park)
	{
		return;
	}
	TArray<FVector> Spots;
	Park->FireworkSpots(Spots);
	for (int32 i = 0; i < Bursts && Spots.Num() > 0; ++i)
	{
		// Keep each draw's expression as it is: the stream's order makes a seeded run's fireworks repeatable.
		const FVector Jitter(BotRng.FRandRange(-FireworkSpreadCm, FireworkSpreadCm), BotRng.FRandRange(-FireworkSpreadCm, FireworkSpreadCm), BotRng.FRandRange(FireworkLowCm, FireworkHighCm));
		PendingFireworks.Add(TPair<double, FVector>(Clock + Delay + i * FireworkStagger + BotRng.FRandRange(0.f, FireworkStaggerJitter), Spots[(i * 3 + Rematches) % Spots.Num()] + Jitter));
	}
}

void ADerbyGame::LaunchDueFireworks()
{
	for (int32 i = PendingFireworks.Num() - 1; i >= 0; --i)
	{
		if (Clock < PendingFireworks[i].Key)
		{
			continue;
		}
		const FVector At = PendingFireworks[i].Value;
		PendingFireworks.RemoveAt(i);
		const int32 Hue = BotRng.RandRange(0, NumFireworkHues - 1);
		if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
		{
			EmitFirework(*FxSys, At, Hue);
		}
		Cues.Play(this, Cue::Firework, 0.5f + BotRng.FRand() * 0.3f, 0.8f + BotRng.FRand() * 0.4f);
	}
}

void ADerbyGame::UpdateBoard()
{
	if (!Park)
	{
		return;
	}
	if (Phase == EDerbyPhase::Finished)
	{
		const TArray<int32> Rank = Ranking(Match);
		const FDerbySeat* Winner = Rank.Num() > 0 ? GetSeat(Rank[0]) : nullptr;
		const FString Line = Winner ? FString::Printf(TEXT("%d PTS  ·  %d HR"), Match.Batters[Rank[0]].Score, Match.Batters[Rank[0]].HomeRuns) : FString();
		Park->SetBoard(TEXT("DERBY CHAMPION"), Winner ? Winner->Name.ToUpper() : FString(), Line, Winner ? Winner->Color : FLinearColor::White);
		return;
	}
	const FDerbySeat* Up = GetBatter();
	const FBatter* Stats = GetBatterStats();
	if (!Up || !Stats)
	{
		return;
	}
	if (bHasOutcome && IsHomeRun(Outcome.Result) && Phase == EDerbyPhase::Result)
	{
		Park->SetBoard(Outcome.Result == EResult::Moonshot ? TEXT("MOONSHOT!") : TEXT("HOME RUN!"), FString::Printf(TEXT("%d FT"), Outcome.DistanceFt),
			FString::Printf(TEXT("%s  ·  %d HR  ·  %d PTS"), *Up->Name.ToUpper(), Stats->HomeRuns, Stats->Score), Up->Color);
		return;
	}
	Park->SetBoard(TEXT("NOW BATTING"), Up->Name.ToUpper(), FString::Printf(TEXT("HR %d  ·  LONG %d FT  ·  %d PTS"), Stats->HomeRuns, Stats->LongestFt, Stats->Score), Up->Color);
}

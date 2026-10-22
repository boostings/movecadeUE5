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

	/** An emit recipe: every field but where it goes (and what the moment varies), set once. */
	Fx::FKartFxEmit Recipe(Fx::EField Field, int32 Count, double Spread, const FVector2D& Speed, const FVector2D& Life, const FVector2D& Size, double SizeEnd,
		const FLinearColor& Color, const FLinearColor& ColorEnd, double Intensity, double Jitter = 0.0)
	{
		Fx::FKartFxEmit Emit;
		Emit.Field = Field;
		Emit.Count = Count;
		Emit.Spread = Spread;
		Emit.Speed = Speed;
		Emit.Life = Life;
		Emit.Size = Size;
		Emit.SizeEnd = SizeEnd;
		Emit.Color = Color;
		Emit.ColorEnd = ColorEnd;
		Emit.Intensity = Intensity;
		Emit.Jitter = Jitter;
		return Emit;
	}

	// The recipes (FKartFxEmit units: m/s, seconds, metres). Count, Size and colours a moment varies are noted.
	namespace Recipes
	{
		const FLinearColor& Gold() { static const FLinearColor C = MovecadeKart::Art::Hex(ContactGold); return C; }
		/** Contact sparks along the hit: Count grows with the contact quality. */
		const Fx::FKartFxEmit& ContactSparks()
		{
			static const Fx::FKartFxEmit E = Recipe(Fx::EField::Sparks, 1, 0.7, { 3.0, 8.0 }, { 0.2, 0.45 }, { 0.02, 0.04 }, 0.2, FLinearColor::White, Gold(), 3.0);
			return E;
		}
		/** The white flash at the plate: Size grows with the contact quality. */
		const Fx::FKartFxEmit& ContactFlash()
		{
			static const Fx::FKartFxEmit E = Recipe(Fx::EField::Flare, 1, 0.5, { 0.0, 0.0 }, { 0.14, 0.14 }, { 0.6, 0.6 }, 0.3, FLinearColor::White, Gold(), 3.0);
			return E;
		}
		/** A barrelled ball's shock ring, along the hit. */
		const Fx::FKartFxEmit& ContactRing()
		{
			static const Fx::FKartFxEmit E = Recipe(Fx::EField::Ring, 1, 0.5, { 0.0, 0.0 }, { 0.3, 0.3 }, { 0.5, 0.5 }, 3.0, FLinearColor::White, Gold(), 2.0);
			return E;
		}
		/** A ball in play kicks up the infield dirt where it lands. */
		const Fx::FKartFxEmit& LandingDust()
		{
			static const Fx::FKartFxEmit E = Recipe(Fx::EField::Smoke, 14, 1.2, { 0.6, 1.6 }, { 0.6, 1.0 }, { 0.4, 0.7 }, 2.0,
				MovecadeKart::Art::Hex(TEXT("#b08a63")), MovecadeKart::Art::Hex(TEXT("#6d5a44")), 0.6);
			return E;
		}
		/** A firework's coloured burst: Color (its hue whitened) and ColorEnd (the next hue) per burst. */
		const Fx::FKartFxEmit& FireworkBurst()
		{
			static const Fx::FKartFxEmit E = Recipe(Fx::EField::Sparks, 140, PI, { 12.0, 19.0 }, { 1.1, 1.9 }, { 1.0, 1.6 }, 0.25, FLinearColor::White, FLinearColor::White, 14.0, 0.6);
			return E;
		}
		/** Its white flash, fading to the hue. */
		const Fx::FKartFxEmit& FireworkFlash()
		{
			static const Fx::FKartFxEmit E = Recipe(Fx::EField::Flare, 1, 0.5, { 0.0, 0.0 }, { 0.3, 0.3 }, { 26.0, 26.0 }, 0.3, FLinearColor::White, FLinearColor::White, 5.0);
			return E;
		}
		/** Its slow glitter, fading two hues on. */
		const Fx::FKartFxEmit& FireworkGlitter()
		{
			static const Fx::FKartFxEmit E = Recipe(Fx::EField::Flare, 40, PI, { 4.0, 8.0 }, { 1.4, 2.4 }, { 1.6, 2.6 }, 0.1, FLinearColor::White, FLinearColor::White, 8.0, 2.0);
			return E;
		}
	}

	/** The crack: sparks along the hit, a white flash, and a ring for a barrelled ball. */
	void EmitContact(UKartFxSubsystem& FxSys, const FVector& At, const FVector& Dir, float Quality)
	{
		Fx::FKartFxEmit Sparks = Recipes::ContactSparks();
		Sparks.Position = At;
		Sparks.Direction = Dir;
		Sparks.Count = FMath::RoundToInt32(10.f + 26.f * Quality);
		FxSys.Emit(Sparks);
		Fx::FKartFxEmit Flash = Recipes::ContactFlash();
		Flash.Position = At;
		Flash.Size = FVector2D(0.6 + 0.9 * Quality, 0.6 + 0.9 * Quality);
		FxSys.Emit(Flash);
		if (Quality <= RingQuality)
		{
			return;
		}
		Fx::FKartFxEmit Ring = Recipes::ContactRing();
		Ring.Position = At;
		Ring.Direction = Dir;
		FxSys.Emit(Ring);
	}

	Fx::FKartFxEmit LandingDust(const FVector& At)
	{
		Fx::FKartFxEmit Dust = Recipes::LandingDust();
		Dust.Position = At;
		return Dust;
	}

	/** One firework: a coloured burst, a white flash, and slow glitter that fades into the next hues. */
	void EmitFirework(UKartFxSubsystem& FxSys, const FVector& At, int32 Hue)
	{
		Fx::FKartFxEmit Burst = Recipes::FireworkBurst();
		Burst.Position = At;
		Burst.Color = FMath::Lerp(FireworkColor(Hue), FLinearColor::White, 0.35f);
		Burst.ColorEnd = FireworkColor(Hue + 1);
		FxSys.Emit(Burst);
		Fx::FKartFxEmit Flash = Recipes::FireworkFlash();
		Flash.Position = At;
		Flash.ColorEnd = FireworkColor(Hue);
		FxSys.Emit(Flash);
		Fx::FKartFxEmit Glitter = Recipes::FireworkGlitter();
		Glitter.Position = At;
		Glitter.ColorEnd = FireworkColor(Hue + 2);
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

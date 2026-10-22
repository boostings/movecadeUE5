// ATrackLife's fireworks over the start gate: shells rise on a sparkling trail, burst into stars, coloured smoke
// (which reads against a bright sky) and glints, and crackle as the stars burn out.
#include "Track/TrackLife.h"

#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "NiagaraComponent.h"
#include "Track/TrackLifeShared.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;
using namespace MovecadeKart::LifeShared;

namespace
{
	const TCHAR* const FireworkColors[] = { TEXT("ff6bd6"), TEXT("55e0ff"), TEXT("ffd34d"), TEXT("8dff7a"), TEXT("fff3e0"), TEXT("a77bff"), TEXT("ff7b5c") };

	/** Shells launch from up to 22 m either side of the finish line (+-6 m scatter), 2 m up, and burst 32-44 m up
	 *  (+-5 m) after a 1-1.3 s climb. A show spreads its shells over its seconds, each up to 0.25 s late. */
	constexpr double ShowSpreadCm = 2200.0;
	constexpr double ShowScatterCm = 600.0;
	constexpr double ShowLateSeconds = 0.25;
	constexpr double LaunchHeightCm = 200.0;
	constexpr double BurstScatterCm = 500.0;
	constexpr double BurstMinHeightCm = 3200.0;
	constexpr double BurstMaxHeightCm = 4400.0;
	constexpr double MinRiseSeconds = 1.0;
	constexpr double MaxRiseSeconds = 1.3;
	/** The trail sheds this many sparks a second; the crackle comes this long after the burst, 6 m under it. */
	constexpr double TrailRate = 70.0;
	constexpr double CrackleDelay = 0.9;
	constexpr double CrackleDropCm = 600.0;

	/** The rising shell's spark trail (a stream that rides the shell; world-space particles stay where they fell). */
	const FKartFxEmit& TrailStream()
	{
		static const FKartFxEmit Trail = []
		{
			FKartFxEmit E;
			E.Field = EField::Sparks;
			E.Direction = -FVector::UpVector;
			E.Spread = 0.5;
			E.Speed = { 0.5, 2.5 };
			E.Life = { 0.35, 0.6 };
			E.Size = { 0.08, 0.14 };
			E.SizeEnd = 0.2;
			E.Color = Hex(TEXT("ffe2a8"));
			E.ColorEnd = Hex(TEXT("ff8a3d"));
			E.Intensity = 4.0;
			return E;
		}();
		return Trail;
	}

	void ShellBurst(const FVector& Cm, const FLinearColor& Color, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Stars;
		Stars.Field = EField::Sparks;
		Stars.Position = Cm;
		Stars.Count = 150;
		Stars.Spread = UE_DOUBLE_PI;
		Stars.Speed = { 12.0, 16.0 };
		Stars.Life = { 1.4, 2.1 };
		Stars.Size = { 0.55, 0.85 };
		Stars.SizeEnd = 0.35;
		Stars.Color = Color;
		Stars.ColorEnd = Color * 0.5f;
		Stars.Intensity = 14.0;
		Out.Add(Stars);
		// Daylight shows: a puff of coloured smoke where the shell broke.
		FKartFxEmit Smoke;
		Smoke.Field = EField::Smoke;
		Smoke.Position = Cm;
		Smoke.Count = 26;
		Smoke.Spread = UE_DOUBLE_PI;
		Smoke.Speed = { 3.0, 7.0 };
		Smoke.Life = { 2.2, 3.4 };
		Smoke.Size = { 2.4, 3.8 };
		Smoke.SizeEnd = 2.2;
		Smoke.Color = FLinearColor::LerpUsingHSV(Color, FLinearColor::White, 0.35f);
		Smoke.ColorEnd = FLinearColor::LerpUsingHSV(Color, FLinearColor::White, 0.7f);
		Smoke.Intensity = 1.0;
		Smoke.Jitter = 2.0;
		Out.Add(Smoke);
		FKartFxEmit Glints = Stars;
		Glints.Field = EField::Flare;
		Glints.Count = 22;
		Glints.Speed = { 6.0, 12.0 };
		Glints.Life = { 0.6, 1.1 };
		Glints.Size = { 0.6, 1.0 };
		Glints.SizeEnd = 0.2;
		Glints.Color = FLinearColor::White;
		Glints.ColorEnd = Color;
		Glints.Intensity = 4.0;
		Out.Add(Glints);
		FKartFxEmit Core;
		Core.Field = EField::Fire;
		Core.Position = Cm;
		Core.Count = 3;
		Core.Spread = UE_DOUBLE_PI;
		Core.Speed = { 0.0, 1.0 };
		Core.Life = { 0.3, 0.45 };
		Core.Size = { 3.0, 4.5 };
		Core.SizeEnd = 1.6;
		Core.Color = Color;
		Core.ColorEnd = Color;
		Core.Intensity = 3.0;
		Out.Add(Core);
	}

	void ShellCrackle(const FVector& Cm, const FLinearColor& Color, TArray<FKartFxEmit>& Out)
	{
		FKartFxEmit Crackle;
		Crackle.Field = EField::Flare;
		Crackle.Position = Cm;
		Crackle.Count = 40;
		Crackle.Spread = UE_DOUBLE_PI;
		Crackle.Speed = { 0.0, 1.5 };
		Crackle.Life = { 0.12, 0.35 };
		Crackle.Size = { 0.3, 0.55 };
		Crackle.SizeEnd = 0.1;
		Crackle.Color = FLinearColor::White;
		Crackle.ColorEnd = Color;
		Crackle.Intensity = 5.0;
		Crackle.Jitter = 10.0;
		Out.Add(Crackle);
	}
}

void ATrackLife::FireworkShow(int32 Count, double Over)
{
	if (!bFinishFirework)
	{
		return;
	}
	for (int32 i = 0; i < Count; ++i)
	{
		const double Side = Rng.FRandRange(-1.0, 1.0);
		const FVector Cm = FinishCm + FinishRight * (Side * ShowSpreadCm) + FVector(Rng.FRandRange(-ShowScatterCm, ShowScatterCm), Rng.FRandRange(-ShowScatterCm, ShowScatterCm), 0.0);
		LaunchFirework(Cm, Srgb(FireworkColors[Rng.RandHelper(UE_ARRAY_COUNT(FireworkColors))]), Over * i / FMath::Max(Count, 1) + Rng.FRandRange(0.0, ShowLateSeconds));
	}
}

void ATrackLife::LaunchFirework(const FVector& Cm, const FLinearColor& Color, double Delay)
{
	FShell& Shell = Shells.AddDefaulted_GetRef();
	Shell.Launch = Cm + FVector(0.0, 0.0, LaunchHeightCm);
	Shell.Burst = Cm + FVector(Rng.FRandRange(-BurstScatterCm, BurstScatterCm), Rng.FRandRange(-BurstScatterCm, BurstScatterCm), Rng.FRandRange(BurstMinHeightCm, BurstMaxHeightCm));
	Shell.Color = Color;
	Shell.Start = GetWorld()->GetTimeSeconds() + Delay;
	Shell.Rise = Rng.FRandRange(MinRiseSeconds, MaxRiseSeconds);
}

int32 ATrackLife::AcquireTrail()
{
	int32 Trail = TrailBusy.IndexOfByKey(false);
	if (Trail == INDEX_NONE)
	{
		UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
		if (UNiagaraComponent* Stream = Fx ? Fx->MakeStream(EField::Sparks, RootComponent, NAME_None) : nullptr)
		{
			Trail = TrailStreams.Add(Stream);
			TrailBusy.Add(false);
		}
	}
	if (Trail != INDEX_NONE)
	{
		TrailBusy[Trail] = true;
	}
	return Trail;
}

void ATrackLife::UpdateFireworks()
{
	if (Shells.Num() == 0)
	{
		return;
	}
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	const double Now = GetWorld()->GetTimeSeconds();
	TArray<FKartFxEmit> Out;
	for (int32 i = Shells.Num() - 1; i >= 0; --i)
	{
		FShell& S = Shells[i];
		const double Age = Now - S.Start;
		if (Age < 0.0)
		{
			continue;
		}
		if (!S.bBurst)
		{
			// The rising shell: a hot point shedding a sparkling trail, easing out toward the top.
			const double T = FMath::Clamp(Age / S.Rise, 0.0, 1.0);
			const FVector P = FMath::Lerp(S.Launch, S.Burst, 1.0 - FMath::Square(1.0 - T));
			if (S.Trail == INDEX_NONE && Fx)
			{
				S.Trail = AcquireTrail();
			}
			if (UNiagaraComponent* Stream = TrailStreams.IsValidIndex(S.Trail) ? TrailStreams[S.Trail].Get() : nullptr)
			{
				Stream->SetWorldLocation(P);
				UKartFxSubsystem::UpdateStream(Stream, TrailStream(), T < 1.0 ? TrailRate : 0.0);
			}
			if (T >= 1.0)
			{
				S.bBurst = true;
				if (TrailBusy.IsValidIndex(S.Trail))
				{
					TrailBusy[S.Trail] = false;
				}
				S.Trail = INDEX_NONE;
				ShellBurst(S.Burst, S.Color, Out);
			}
		}
		else if (!S.bCrackled && Age > S.Rise + CrackleDelay)
		{
			S.bCrackled = true;
			ShellCrackle(S.Burst - FVector(0.0, 0.0, CrackleDropCm), S.Color, Out);
			Shells.RemoveAtSwap(i);
		}
	}
	if (Fx && Out.Num() > 0)
	{
		Fx->Emit(Out);
	}
}

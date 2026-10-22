// ASamuraiGame: the blades. Local aim, phone frames and the house bots move each lane's katana; every fixed step
// samples them and resolves what each meant sweep passed through (bladeContact.ts on the sim clock, M2).

#include "Games/Samurai/SamuraiGame.h"

#include "GameFramework/PlayerController.h"
#include "Games/Samurai/SamuraiTuning.h"
#include "InputCoreTypes.h"
#include "KartCoords.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Samurai;
using namespace MovecadeKart::Samurai::Tuning;

namespace
{
	const FVector WebUp(0.0, 1.0, 0.0);

	/** The angle between two unit directions, radians. */
	double AngleBetween(const FVector& A, const FVector& B)
	{
		return FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0));
	}
}

FVector ASamuraiGame::KamaeDirection(double LaneX) const
{
	// The guard: blade raised before the face, tip forward and up.
	return BladeDirectionToward(LaneX, FVector(LaneX + KamaeOffsetX, KamaeY, KamaeZ));
}

void ASamuraiGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	FLane* Lane = Lanes.FindByPredicate([&PlayerId](const FLane& L) { return L.PlayerId == PlayerId; });
	if (!Lane)
	{
		return;
	}
	using namespace MovecadeKart::Net;
	// presence.ts / SamuraiSwordsman.tsx: the calibrated attitude is the katana; bladeIntent.ts gates the sweep.
	Lane->PhoneDirection = Frame.bHasQuaternion
		? BladeDirectionFromQuaternion(FQuat(Frame.Quaternion.X, Frame.Quaternion.Y, Frame.Quaternion.Z, Frame.Quaternion.W))
		: BladeDirectionFromEuler(Frame.Alpha, Frame.Beta, Frame.Gamma);
	const double Rate = (Frame.Flags & SensorFlagHasGyro) ? Frame.Gyro.Size() : 0.0;
	const double Accel = (Frame.Flags & SensorFlagHasAccel) ? Frame.Accel.Size() : 0.0;
	Lane->bPhoneIntent = Lane->Intent.Feed(Rate, Accel, Frame.DtMs > 0 ? Frame.DtMs : PhoneDefaultFrameMs);
	Lane->PhoneAt = Clock;
}

bool ASamuraiGame::ReadLocalInput(float RealDt)
{
	if (!Viewer)
	{
		return false;
	}
	// The local swordsman's tip: the mouse, the right (or left) stick, the keys. Real time, so slow motion
	// doesn't make the aim feel heavy.
	float MouseX = 0.f, MouseY = 0.f;
	Viewer->GetInputMouseDelta(MouseX, MouseY);
	FVector2D Stick(Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightX) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftX),
		Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightY) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftY));
	Stick.X += (Viewer->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (Viewer->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
	Stick.Y += (Viewer->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (Viewer->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
	for (FLane& Lane : Lanes)
	{
		if (Lane.Kind != ESwordKind::Local)
		{
			continue;
		}
		Lane.TipGoal.X = FMath::Clamp(Lane.TipGoal.X + MouseX * MouseMetresPerCount + Stick.X * StickMetresPerSecond * RealDt, Lane.LaneX - TipReachX, Lane.LaneX + TipReachX);
		Lane.TipGoal.Y = FMath::Clamp(Lane.TipGoal.Y + MouseY * MouseMetresPerCount + Stick.Y * StickMetresPerSecond * RealDt, TipMinY, TipMaxY);
	}
	return Viewer->WasInputKeyJustPressed(EKeys::SpaceBar) || Viewer->WasInputKeyJustPressed(EKeys::Enter)
		|| Viewer->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
}

void ASamuraiGame::SampleBlade(FLane& Lane, int32 Index, double DtSeconds)
{
	bool bIntent = false;
	FVector Direction = Lane.Blade.Direction;
	switch (Lane.Kind)
	{
	case ESwordKind::Local:
		Direction = AimLocal(Lane, DtSeconds, bIntent);
		break;
	case ESwordKind::Phone:
		Direction = AimPhone(Lane, bIntent);
		break;
	case ESwordKind::Bot:
		Direction = AimBot(Lane, Index, bIntent);
		break;
	}
	Lane.Previous = Lane.Blade;
	Lane.Blade = BladeFor(Lane.LaneX, Direction);
	const double Turn = AngleBetween(Lane.Previous.Direction, Lane.Blade.Direction) / FMath::Max(DtSeconds, MinStepSeconds);
	Lane.Energy = FMath::Lerp(Lane.Energy, FMath::Clamp(Turn / EnergyFullTurnRate, 0.0, 1.0), EnergyBlend);
	Lane.bIntent = bIntent;
}

FVector ASamuraiGame::AimLocal(FLane& Lane, double DtSeconds, bool& bOutIntent)
{
	const FVector Direction = BladeDirectionToward(Lane.LaneX, Lane.TipGoal);
	// The tip's angular speed is the swing: the same gate as a phone's gyro (M9).
	const double Degrees = FMath::RadiansToDegrees(AngleBetween(Lane.Blade.Direction, Direction));
	bOutIntent = Lane.Intent.Feed(Degrees / FMath::Max(DtSeconds, MinStepSeconds), 0.0, DtSeconds * 1000.0);
	return Direction;
}

FVector ASamuraiGame::AimPhone(const FLane& Lane, bool& bOutIntent) const
{
	if (Clock - Lane.PhoneAt < PhoneStaleSeconds)
	{
		bOutIntent = Lane.bPhoneIntent;
		return Lane.PhoneDirection;
	}
	// The phone went quiet: lower the blade back to the guard.
	return FMath::Lerp(Lane.Blade.Direction, KamaeDirection(Lane.LaneX), PhoneGuardBlend).GetSafeNormal();
}

FVector ASamuraiGame::AimBot(FLane& Lane, int32 Index, bool& bOutIntent)
{
	// Bots plan on the rules' clock, the countdown counting as negative time.
	const double SimSeconds = Match.ElapsedMs / 1000.0 + (Match.Phase == EMatchPhase::Countdown ? -Match.CountdownRemainingMs / 1000.0 : 0.0);
	const double T = (SimSeconds - Lane.SweepAt) / Lane.SweepSeconds;
	if (T >= 0.0 && T <= 1.0)
	{
		const double Along = Ease::Smooth(0.0, 1.0, T);
		bOutIntent = true;
		return FQuat::Slerp(FQuat::FindBetweenNormals(WebUp, Lane.SweepFrom), FQuat::FindBetweenNormals(WebUp, Lane.SweepTo), Along).RotateVector(WebUp);
	}
	if (T < 0.0 && T > -BotWindUpSweeps)
	{
		// The wind-up: draw the blade to where the cut starts.
		return FMath::Lerp(Lane.Blade.Direction, Lane.SweepFrom, BotWindUpBlend).GetSafeNormal();
	}
	const FVector Direction = FMath::Lerp(Lane.Blade.Direction, KamaeDirection(Lane.LaneX), BotGuardBlend).GetSafeNormal();
	if (SimSeconds >= Lane.NextThinkAt && Seats.IsValidIndex(Index))
	{
		PlanBot(Lane, Seats[Index]);
	}
	return Direction;
}

void ASamuraiGame::PlanBot(FLane& Lane, const FSamuraiSeat& Seat)
{
	const double SimSeconds = Match.ElapsedMs / 1000.0;
	const double Skill = Seat.Skill;
	const double Window = BotWindowSharp + BotWindowWild * (1.0 - Skill);
	// The fruit nearest its apex in this lane, if any is near enough to be worth a cut.
	const FTarget* Best = nullptr;
	for (const FTarget& Target : Match.Targets)
	{
		if (Target.PlayerId != Lane.PlayerId || Target.Kind == EKind::Bomb || FMath::Abs(Target.Progress - CutApex) > Window)
		{
			continue;
		}
		if (!Best || FMath::Abs(Target.Progress - CutApex) < FMath::Abs(Best->Progress - CutApex))
		{
			Best = &Target;
		}
	}
	if (!Best)
	{
		Lane.NextThinkAt = SimSeconds + BotIdleRethinkSeconds;
		return;
	}
	const FVector Goal = TargetCentre(*Best, LaneCount());
	// A sharp bot waits out a bomb beside the fruit; a wild one sometimes doesn't look.
	for (const FTarget& Target : Match.Targets)
	{
		if (Target.PlayerId == Lane.PlayerId && Target.Kind == EKind::Bomb && FVector::Dist(TargetCentre(Target, LaneCount()), Goal) < BotBombWaryMetres
			&& BotRng.FRand() < BotBombWaryChance + BotBombWaryChancePerSkill * Skill)
		{
			Lane.NextThinkAt = SimSeconds + BotBombWaitSeconds;
			return;
		}
	}
	const double Side = BotRng.FRand() < 0.5 ? -1.0 : 1.0;
	Lane.SweepFrom = BladeDirectionToward(Lane.LaneX, Goal + FVector(Side * BotSweepReachX, BotSweepReachY, 0.0));
	Lane.SweepTo = BladeDirectionToward(Lane.LaneX, Goal + FVector(-Side * BotSweepReachX, -BotSweepReachY, 0.0));
	Lane.SweepAt = SimSeconds + BotSweepLeadSeconds;
	Lane.SweepSeconds = BotSweepSeconds;
	Lane.NextThinkAt = SimSeconds + BotSweepLeadSeconds + BotSweepSeconds + BotRecoverSeconds + (1.0 - Skill) * BotWildPauseSeconds;
}

void ASamuraiGame::StepFixed(double DtMs, double DtSeconds)
{
	StepMatch(Match, DtMs);
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		SampleBlade(Lane, i, DtSeconds);
		if (Match.Phase != EMatchPhase::Playing)
		{
			continue;
		}
		if (Lane.bIntent)
		{
			ResolveContacts(Lane);
		}
		else
		{
			FeedbackForMisses(Lane, i);
		}
	}
	PublishNewEffects();
}

void ASamuraiGame::ResolveContacts(FLane& Lane)
{
	if (!Lane.bWasIntent)
	{
		Lane.bSweepHit = false;
	}
	Lane.bWasIntent = true;
	Lane.TrailAt = Clock;
	// bladeContact.ts on the sim clock: whatever the blade passed through since the last step.
	TSet<int32>& Hits = HitScratch;
	Hits.Reset();
	for (const FTarget& Target : Match.Targets)
	{
		if (Target.PlayerId == Lane.PlayerId && SweptBladeHits(Lane.Previous.Base, Lane.Previous.Tip, Lane.Blade.Base, Lane.Blade.Tip,
			TargetCentre(Target, LaneCount()), ContactRadius(Target)))
		{
			Hits.Add(Target.Id);
		}
	}
	if (Hits.Num() == 0)
	{
		return;
	}
	// The follow-through's side and strength, for the swordsman's cut pose (presentation only).
	const FVector Sweep = Lane.Blade.Tip - Lane.Previous.Tip;
	Lane.CutSide = Sweep.X >= 0.0 ? 1.0 : -1.0;
	Lane.CutPower = FMath::Clamp(Sweep.Size() / CutPowerFullSweep, CutPowerMin, 1.0);
	ResolveSwing(Match, Lane.PlayerId, ContactSwing(), &Hits);
	Lane.LastCutAt = Clock;
	Lane.bSweepHit = true;
}

void ASamuraiGame::PublishNewEffects()
{
	for (const FEffect& Effect : Match.Effects)
	{
		if (Effect.Id <= LastSeenEffectId)
		{
			continue;
		}
		LastSeenEffectId = Effect.Id;
		PresentEffect(Effect);
		if (OnEffect && Effect.Kind != EEffectKind::Slash)
		{
			OnEffect(Effect);
		}
	}
}

void ASamuraiGame::FeedbackForMisses(FLane& Lane, int32 Index)
{
	// What the rules stay silent about, for the people holding a blade: a sweep that ended without touching any
	// fruit near it, and a blade dragged through fruit too slowly to count as a cut (the intent gate is closed).
	const bool bSweepEnded = Lane.bWasIntent;
	Lane.bWasIntent = false;
	if (!IsHuman(Index) || Clock - Lane.HintAt < HintCooldownSeconds)
	{
		return;
	}
	const FTarget* Touched = nullptr;
	const FTarget* Nearest = nullptr;
	double NearestDistance = WhiffNearestMetres;
	for (const FTarget& Target : Match.Targets)
	{
		if (Target.LaneIndex != Index || Target.Kind == EKind::Bomb)
		{
			continue;
		}
		const FVector Centre = TargetCentre(Target, LaneCount());
		if (!Touched && SweptBladeHits(Lane.Previous.Base, Lane.Previous.Tip, Lane.Blade.Base, Lane.Blade.Tip, Centre, ContactRadius(Target)))
		{
			Touched = &Target;
		}
		const double Distance = FVector::Dist(FMath::ClosestPointOnSegment(Centre, Lane.Blade.Base, Lane.Blade.Tip), Centre);
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = &Target;
		}
	}
	const double TurnDegPerSecond = FMath::RadiansToDegrees(AngleBetween(Lane.Previous.Direction, Lane.Blade.Direction)) * StepsPerSecond;
	if (Touched && TurnDegPerSecond > TooSlowMinTurnDegPerSecond)
	{
		Lane.HintAt = Clock;
		Note(ToWorld(TargetCentre(*Touched, LaneCount()) + FVector(0.0, NoteLift, 0.0)), NSLOCTEXT("Samurai", "TooSlow", "Swing faster!"), Palette::Gold,
			TooSlowNoteLife, TooSlowNoteScale);
		Cues.Play(this, TEXT("samurai.miss"), TooSlowVolume, TooSlowPitch);
	}
	else if (bSweepEnded && !Lane.bSweepHit && Nearest && !LaneCalloutActive(Index))
	{
		Lane.HintAt = Clock;
		Note(ToWorld(TargetCentre(*Nearest, LaneCount()) + FVector(0.0, NoteLift, 0.0)), NSLOCTEXT("Samurai", "Whiff", "Missed"), Palette::Whiff,
			WhiffNoteLife, WhiffNoteScale);
	}
}

bool ASamuraiGame::LaneCalloutActive(int32 LaneIndex) const
{
	// The rules' own word on a miss or a bomb is on screen in this lane: don't talk over it.
	return Match.Effects.ContainsByPredicate([LaneIndex](const FEffect& Effect)
	{
		return Effect.LaneIndex == LaneIndex && (Effect.Kind == EEffectKind::Miss || Effect.Kind == EEffectKind::Bomb)
			&& Effect.DurationMs - Effect.TtlMs < CalloutQuietMs;
	});
}

FVector ASamuraiGame::CutNormal(int32 LaneIndex) const
{
	if (!Lanes.IsValidIndex(LaneIndex))
	{
		return FVector::ZeroVector;
	}
	const FLane& Lane = Lanes[LaneIndex];
	const FVector Sweep = Lane.Blade.Tip - Lane.Previous.Tip;
	const FVector Normal = FVector::CrossProduct(Lane.Blade.Direction, Sweep);
	return Normal.Size() < 1e-4 ? FVector::ZeroVector : MovecadeKart::Coords::DirToUE(Normal).GetSafeNormal();
}

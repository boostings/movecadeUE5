// ASamuraiGame: presentation. Everything here follows the rules and never feeds back into them: the targets on
// their arcs (with the bomb's telegraph and the precise moment's halo), the halves a cut leaves and its juice, the
// swordsmen and their katanas, the trails, the notes and banners, and the screen's mood.

#include "Games/Samurai/SamuraiGame.h"

#include "Avatar/ArcadeAvatar.h"
#include "Components/StaticMeshComponent.h"
#include "FX/KartFxSubsystem.h"
#include "Games/Samurai/SamuraiDojo.h"
#include "Games/Samurai/SamuraiFx.h"
#include "Games/Samurai/SamuraiLog.h"
#include "Games/Samurai/SamuraiTuning.h"
#include "KartCoords.h"
#include "ProceduralMeshComponent.h"

using namespace MovecadeKart::Samurai;
using namespace MovecadeKart::Samurai::Tuning;

namespace
{
	/** Prop meshes per variant: whole, plus half, minus half (Art::EPart). */
	constexpr int32 PartCount = 3;
	constexpr int32 PlusPart = 1;
	constexpr int32 MinusPart = 2;
	/** The halves split on the prop's local web x (UE +Y): the face a cut leaves. */
	const FVector SplitAxis(0, 1, 0);
	/** The katana's edge leads down and away from the swordsman (web). */
	const FVector KatanaEdgeHint(0.0, -0.4, -1.0);
}

void ASamuraiGame::UpdatePresentation(float Dt, float RealDt)
{
	Notes.RemoveAll([this](const FSamuraiNote& Note) { return Clock - Note.BornAt > Note.Life; });
	// Hit-stop holds the props and swordsmen; the trails, mood and camera keep moving through it.
	if (Clock >= FrozenUntil)
	{
		UpdateTargets(Dt);
		UpdatePieces();
		UpdateSwordsmen();
	}
	UpdateTrails();
	UpdateMood(RealDt);
	UpdateCamera(Dt);
}

// ---- Effects --------------------------------------------------------------------------------------------------

void ASamuraiGame::PresentEffect(const FEffect& Effect)
{
	const float Power = static_cast<float>(Effect.Intensity);
	switch (Effect.Kind)
	{
	case EEffectKind::Slash:
		Cues.Play(this, TEXT("samurai.slice"), SliceVolume + SliceVolumePerPower * Power, SlicePitch + SlicePitchPerPower * Power);
		break;
	case EEffectKind::Hit:
	case EEffectKind::Combo:
		PresentCut(Effect);
		break;
	case EEffectKind::Bomb:
		PresentBomb(Effect, IsFeatured(Effect.LaneIndex));
		break;
	case EEffectKind::Miss:
		Cues.Play(this, TEXT("samurai.miss"), MissVolume);
		break;
	}
}

void ASamuraiGame::PresentCut(const FEffect& Effect)
{
	const FPlayer* Player = Match.Players.IsValidIndex(Effect.LaneIndex) ? &Match.Players[Effect.LaneIndex] : Match.FindPlayer(Effect.PlayerId);
	const float Power = static_cast<float>(Effect.Intensity);
	const bool bCombo = Effect.Kind == EEffectKind::Combo;
	Cues.Play(this, bCombo ? TEXT("samurai.combo") : TEXT("samurai.hit"), HitVolume + HitVolumePerPower * Power);
	const int32 Combo = Player ? Player->Combo : 0;
	if (Combo >= 2)
	{
		// The bell climbs a semitone a cut (sound.ts), up an octave.
		const int32 Step = FMath::Min(BellMaxSemitones, Combo - 2);
		Cues.Play(this, TEXT("samurai.combo"), BellVolume + BellVolumePerStep * Step / SemitonesPerOctave, FMath::Pow(2.f, Step / SemitonesPerOctave));
	}
	FrozenUntil = Clock + HitStopSeconds;
	Punch = HitPunch * (HitPunchBase + HitPunchPerPower * Power);
	Shake = FMath::Max(Shake, bCombo ? ComboShake : HitShake);
	bool bAnyFlashworthy = false;
	for (const FTarget& Target : Effect.Struck)
	{
		const int32* Slot = TargetSlots.Find(Target.Id);
		const double Quality = CutQuality(Target.Progress);
		bAnyFlashworthy |= Quality >= FlashQuality;
		SplitTarget(Target, ToWorld(TargetCentre(Target, LaneCount())), Slot ? Slots[*Slot].Spin : FQuat::Identity, Effect.LaneIndex,
			Quality >= PreciseQuality || bCombo);
	}
	if (!IsFeatured(Effect.LaneIndex))
	{
		return;
	}
	const bool bMulti = Effect.Struck.Num() >= 2;
	if (bAnyFlashworthy || bMulti)
	{
		Flash(Palette::CutFlash, PerfectFlashPower, PerfectFlashFringe);
		FovKick = PerfectFovKick;
		if (bMulti)
		{
			// Slow motion is for the rare moments (a double cut), not every Perfect.
			SlowMo(DoubleCutSlowMoSeconds);
		}
	}
	if (Combo >= ComboShoutEvery && Combo % ComboShoutEvery == 0)
	{
		Shout(FText::Format(NSLOCTEXT("Samurai", "ComboShout", "{0}x COMBO!"), FText::AsNumber(Combo)), Combo >= GoldComboShout ? Palette::Gold : Palette::Jade);
		if (Dojo)
		{
			Dojo->PetalBurst(FVector(LaneX(Effect.LaneIndex, LaneCount()), ComboPetalY, ComboPetalZ), ComboPetals + Combo);
		}
	}
}

void ASamuraiGame::PresentBomb(const FEffect& Effect, bool bFeatured)
{
	Cues.Play(this, TEXT("samurai.bomb"));
	FrozenUntil = Clock + BombHitStopSeconds;
	Punch = BombPunch;
	Shake = FMath::Max(Shake, BombShake);
	const double FlashPowerNow = bFeatured ? BombFlashFeatured : BombFlashOther;
	Flash(Palette::BombFlash, FlashPowerNow, FlashPowerNow);
	FovKick = BombFovKick;
	UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this);
	if (!FxSystem)
	{
		return;
	}
	for (const FTarget& Target : Effect.Struck)
	{
		if (Target.Kind == EKind::Bomb)
		{
			FxScratch.Reset();
			AppendBombFx(ToWorld(TargetCentre(Target, LaneCount())), FxScratch);
			FxSystem->Emit(FxScratch);
		}
	}
}

void ASamuraiGame::SplitTarget(const FTarget& Target, const FVector& At, const FQuat& Spin, int32 LaneIndex, bool bPerfect)
{
	// The halves part along the plane the blade swept: turn the target so its split faces that way.
	FQuat Rotation = Spin;
	const FVector Cut = CutNormal(LaneIndex);
	if (!Cut.IsNearlyZero())
	{
		Rotation = FQuat::FindBetweenNormals(Rotation.RotateVector(SplitAxis), Cut) * Rotation;
	}
	SpawnPiece(Target, At, Rotation, true);
	SpawnPiece(Target, At, Rotation, false);
	if (UKartFxSubsystem* FxSystem = UKartFxSubsystem::Get(this))
	{
		FxScratch.Reset();
		AppendCutFx(Target.Variant, At, Rotation.RotateVector(SplitAxis), Target.Radius * TargetMeshScale, bPerfect, FxScratch);
		FxSystem->Emit(FxScratch);
	}
}

void ASamuraiGame::Note(const FVector& World, const FText& Label, const FLinearColor& Color, double Life, float Scale)
{
	if (Notes.Num() >= MaxNotes)
	{
		Notes.RemoveAt(0);
	}
	FSamuraiNote& Added = Notes.AddDefaulted_GetRef();
	Added.World = World;
	Added.Label = Label;
	Added.Color = Color;
	Added.BornAt = Clock;
	Added.Life = Life;
	Added.Scale = Scale;
}

void ASamuraiGame::Shout(const FText& Text, const FLinearColor& Color)
{
	Banner = Text;
	BannerColor = Color;
	BannerAt = Clock;
}

void ASamuraiGame::SlowMo(double Seconds)
{
	// A beat of slow motion (the whole round slows, rules included, so nothing desyncs); never back to back.
	if (RealClock - SlowAt > Seconds + SlowMoCooldownSeconds)
	{
		SlowAt = RealClock;
		SlowSeconds = Seconds;
	}
}

void ASamuraiGame::Flash(const FLinearColor& Color, double Power, double Fringe)
{
	FlashAt = RealClock;
	FlashColor = Color;
	FlashPower = Power;
	FringePower = Fringe;
}

void ASamuraiGame::UpdateMood(float RealDt)
{
	// The screen treatment: Focus warms and saturates while a featured swordsman's Focus burns; perfect cuts and
	// bombs flash and fringe for a moment.
	bool bFocus = false;
	for (int32 i = 0; i < Match.Players.Num(); ++i)
	{
		bFocus |= IsFeatured(i) && Match.Players[i].FocusRemainingMs > 0.0 && Stage == ESamuraiStage::Match;
	}
	FocusMood = FMath::FInterpTo(FocusMood, bFocus ? 1.f : 0.f, RealDt, FocusMoodRate);
	const double FlashAge = RealClock - FlashAt;
	const float FlashNow = static_cast<float>(FlashPower * FMath::Exp(-FlashDecay * FMath::Max(0.0, FlashAge)));
	const float FringeNow = static_cast<float>(FringePower * FMath::Exp(-FringeDecay * FMath::Max(0.0, FlashAge)));
	FovKick *= FMath::Exp(-FovKickDecay * RealDt);
	if (Dojo)
	{
		const float Pulse = FocusMood * (FocusPulseBase + FocusPulseDepth * FMath::Sin(static_cast<float>(RealClock) * FocusPulseRate));
		Dojo->SetMood(Pulse, FlashNow, FlashColor, FringeNow);
	}
}

// ---- Targets ----------------------------------------------------------------------------------------------------

void ASamuraiGame::Dress(UStaticMeshComponent* Component, EVariant Variant, int32 Part) const
{
	const int32 V = static_cast<int32>(Variant);
	check(TargetMeshes.IsValidIndex(V * PartCount + Part) && MaterialStart.IsValidIndex(V + 1));
	Component->SetStaticMesh(TargetMeshes[V * PartCount + Part]);
	for (int32 m = MaterialStart[V]; m < MaterialStart[V + 1]; ++m)
	{
		Component->SetMaterial(m - MaterialStart[V], TargetMaterials[m]);
	}
}

int32 ASamuraiGame::AcquireSlot(const FTarget& Target)
{
	// Stable slots: a target keeps its component from launch to removal, dressed once.
	if (const int32* Found = TargetSlots.Find(Target.Id))
	{
		return *Found;
	}
	const int32 SlotIndex = Slots.IndexOfByPredicate([](const FTargetSlot& Slot) { return Slot.TargetId == INDEX_NONE; });
	if (SlotIndex == INDEX_NONE)
	{
		UE_LOG(LogSamurai, Verbose, TEXT("Target pool full: target %d not shown"), Target.Id);
		return INDEX_NONE;
	}
	FTargetSlot& Fresh = Slots[SlotIndex];
	Fresh.TargetId = Target.Id;
	Fresh.Spin = FQuat(FVector::UpVector, Target.Id * SpawnTurnPerId);
	Fresh.Kind = Target.Kind;
	Fresh.Lane = Target.LaneIndex;
	TargetSlots.Add(Target.Id, SlotIndex);
	Dress(TargetPool[SlotIndex], Target.Variant, 0);
	TargetPool[SlotIndex]->SetVisibility(true);
	return SlotIndex;
}

void ASamuraiGame::UpdateTargets(float Dt)
{
	int32 Telegraphs = 0, Halos = 0;
	++SlotStamp;
	for (const FTarget& Target : Match.Targets)
	{
		const int32 SlotIndex = AcquireSlot(Target);
		if (SlotIndex == INDEX_NONE)
		{
			continue;
		}
		FTargetSlot& Slot = Slots[SlotIndex];
		Slot.Stamp = SlotStamp;
		Slot.LastProgress = Target.Progress;
		// Spin on two axes (SamuraiScene.tsx): about up and about across, at multiples of the target's spin.
		Slot.Spin = FQuat(FVector::UpVector, Target.Spin * SpinUpRate * Dt) * FQuat(FVector::RightVector, Target.Spin * SpinAcrossRate * Dt) * Slot.Spin;
		const FVector At = ToWorld(TargetCentre(Target, LaneCount()));
		Slot.LastAt = At;
		const double Scale = Target.Radius * TargetMeshScale;
		TargetPool[SlotIndex]->SetWorldTransform(FTransform(Slot.Spin, At, FVector(Scale)));
		PlaceMarker(Target, At, Scale, Telegraphs, Halos);
	}
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].TargetId != INDEX_NONE && Slots[i].Stamp != SlotStamp)
		{
			ReleaseSlot(i);
		}
	}
	for (int32 i = Telegraphs; i < TelegraphPool.Num(); ++i)
	{
		TelegraphPool[i]->SetVisibility(false);
	}
	for (int32 i = Halos; i < HaloPool.Num(); ++i)
	{
		HaloPool[i]->SetVisibility(false);
	}
}

void ASamuraiGame::PlaceMarker(const FTarget& Target, const FVector& At, double Scale, int32& Telegraphs, int32& Halos)
{
	if (Target.Kind == EKind::Bomb)
	{
		if (Telegraphs >= TelegraphPool.Num())
		{
			return;
		}
		// The bomb's barred octagon, pulsing, between it and the camera.
		UStaticMeshComponent* Ring = TelegraphPool[Telegraphs++];
		const double Pulse = TelegraphPulseBase + TelegraphPulseDepth * FMath::Sin(Clock * TelegraphPulseRate + Target.Id);
		Ring->SetWorldTransform(FTransform(FRotator::ZeroRotator, At - FVector(Scale * TelegraphForwardCm, 0.0, 0.0), FVector(Scale * Pulse / TelegraphPulseBase)));
		Ring->SetVisibility(true);
		return;
	}
	const FPlayer* Thrower = Match.Players.IsValidIndex(Target.LaneIndex) ? &Match.Players[Target.LaneIndex] : nullptr;
	const bool bFocus = Thrower && Thrower->FocusRemainingMs > 0.0;
	if (Halos >= HaloPool.Num() || !(Target.Kind == EKind::Bonus || bFocus || CutQuality(Target.Progress) >= PreciseQuality))
	{
		return;
	}
	// The halo: a scroll, a Focus lane, or the precise moment to cut.
	const int32 HaloIndex = Halos++;
	UStaticMeshComponent* Halo = HaloPool[HaloIndex];
	UMaterialInterface* Wanted = Target.Kind == EKind::Bonus || bFocus ? FocusHaloMaterial : HaloMaterial;
	if (HaloWorn[HaloIndex] != Wanted)
	{
		HaloWorn[HaloIndex] = Wanted;
		Halo->SetMaterial(0, Wanted);
	}
	Halo->SetWorldTransform(FTransform(FRotator::ZeroRotator, At + FVector(Scale * HaloForwardCm, 0.0, 0.0), FVector(Scale)));
	Halo->SetVisibility(true);
}

void ASamuraiGame::ReleaseSlot(int32 Index)
{
	FTargetSlot& Slot = Slots[Index];
	// A fruit that fell out of a human's lane uncut: the rules only speak up when it breaks a combo.
	if (Slot.LastProgress > DroppedProgress && Slot.Kind != EKind::Bomb && IsHuman(Slot.Lane) && Stage == ESamuraiStage::Match
		&& Match.Phase == EMatchPhase::Playing && !LaneCalloutActive(Slot.Lane))
	{
		Note(Slot.LastAt + FVector(0.0, 0.0, DroppedLiftCm), NSLOCTEXT("Samurai", "Dropped", "Dropped"), Palette::Dropped, DroppedNoteLife, DroppedNoteScale);
	}
	TargetSlots.Remove(Slot.TargetId);
	Slot.TargetId = INDEX_NONE;
	TargetPool[Index]->SetVisibility(false);
}

// ---- Cut halves -------------------------------------------------------------------------------------------------

void ASamuraiGame::SpawnPiece(const FTarget& Target, const FVector& At, const FQuat& Rotation, bool bPlus)
{
	// A ring over the pool: the oldest half gives way; each half is dressed once, at birth.
	const int32 Index = NextPiece;
	NextPiece = (NextPiece + 1) % Pieces.Num();
	FPiece& Piece = Pieces[Index];
	Piece.bLive = true;
	Piece.Origin = At;
	Piece.Rotation = Rotation;
	const FVector Normal = Rotation.RotateVector(SplitAxis);
	Piece.Normal = bPlus ? Normal : -Normal;
	Piece.Scale = Target.Radius * TargetMeshScale;
	Piece.BornAt = Clock;
	Piece.SpinSign = bPlus ? 1.0 : -1.0;
	UStaticMeshComponent* Component = PiecePool[Index];
	Dress(Component, Target.Variant, bPlus ? PlusPart : MinusPart);
	Component->SetWorldTransform(FTransform(Rotation, At, FVector(Piece.Scale)));
	Component->SetVisibility(true);
}

void ASamuraiGame::UpdatePieces()
{
	// cutEffects.ts: the halves part along the cut's normal, arc and spin, and shrink away at the end.
	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		FPiece& Piece = Pieces[i];
		if (!Piece.bLive)
		{
			continue;
		}
		UStaticMeshComponent* Component = PiecePool[i];
		const double Age = Clock - Piece.BornAt;
		if (Age > PieceSeconds)
		{
			Piece.bLive = false;
			Component->SetVisibility(false);
			continue;
		}
		const FVector Position = Piece.Origin + Piece.Normal * (PieceDriftSpeed * Age * CmPerMetre)
			+ FVector(0.0, 0.0, (PieceToss * Age - PieceFall * Age * Age) * CmPerMetre);
		const FQuat Tumble(Piece.Rotation.GetForwardVector(), Piece.SpinSign * PieceTumbleRate * Age);
		const double Fade = 1.0 - Ease::Smooth(PieceSeconds - PieceFadeSeconds, PieceSeconds, Age);
		Component->SetWorldTransform(FTransform(Tumble * Piece.Rotation, Position, FVector(Piece.Scale * FMath::Max(MinVisibleScale, Fade))));
	}
}

// ---- Swordsmen --------------------------------------------------------------------------------------------------

void ASamuraiGame::UpdateSwordsmen()
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		PoseSwordsman(Lanes[i], *Avatars[i]);
		PlaceKatana(Lanes[i], *Katanas[i]);
	}
}

void ASamuraiGame::PoseSwordsman(const FLane& Lane, UArcadeAvatarComponent& Avatar) const
{
	// Standing behind the lane's targets, facing them (web -z is UE +X).
	Avatar.SetWorldLocationAndRotation(ToWorld(FVector(Lane.LaneX, 0.0, FBladeRig::StandZ)), FRotator::ZeroRotator);
	FArcadePoseInput& Pose = Avatar.Pose();
	// The pose's hand targets are in the avatar's frame (UE axes), in arm lengths from the shoulder.
	auto ToArmFrame = [](const FVector& Web) { return FVector(-Web.Z, Web.X, Web.Y) / ArmLength; };
	const FBlade& Blade = Lane.Blade;
	Pose.Hand[1] = ToArmFrame(Blade.Hand - Blade.Shoulder);
	Pose.HandWeight[1] = 1.f;
	// The off hand low on the grip.
	const FVector LeftShoulder = Blade.Shoulder - FVector(2.0 * FBladeRig::ShoulderSide, 0.0, 0.0);
	Pose.Hand[0] = ToArmFrame(Blade.Hand - Blade.Direction * OffHandGrip - LeftShoulder);
	Pose.HandWeight[0] = 1.f;
	// The cut: a step into the stroke and a follow-through, snapping in and settling over a third of a second.
	const double CutAge = Clock - Lane.LastCutAt;
	const double Strike = CutAge < 0.0 || CutAge > StrikeSeconds ? 0.0 : Lane.CutPower * Ease::Smooth(0.0, StrikeSnapSeconds, CutAge) * FMath::Exp(-StrikeDecay * CutAge);
	Pose.Crouch = static_cast<float>(CrouchBase + CrouchPerEnergy * Lane.Energy + CrouchPerStrike * Strike);
	Pose.Lean = static_cast<float>(LeanBase + LeanPerEnergy * Lane.Energy + FMath::Max(0.0, -Blade.Direction.Y) * LeanPerDownBlade + LeanPerStrike * Strike);
	Pose.Twist = static_cast<float>(-Blade.Direction.X * TwistPerBladeX - Lane.CutSide * TwistPerStrike * Strike);
	Pose.Lunge = static_cast<float>(LungePerStrike * Strike);
	// Eyes on the target nearest its apex in this lane.
	FVector LookAt = ToWorld(FVector(Lane.LaneX, IdleLookY, IdleLookZ));
	double Nearest = TNumericLimits<double>::Max();
	for (const FTarget& Target : Match.Targets)
	{
		if (Target.PlayerId == Lane.PlayerId && FMath::Abs(Target.Progress - CutApex) < Nearest)
		{
			Nearest = FMath::Abs(Target.Progress - CutApex);
			LookAt = ToWorld(TargetCentre(Target, LaneCount()));
		}
	}
	Pose.Look = (LookAt - Avatar.GetComponentLocation() - FVector(0.0, 0.0, EyeHeightCm)).GetSafeNormal();
	Pose.LookWeight = LookWeight;
	Reactions(Lane, Pose.Cheer, Pose.Slump);
	Pose.Idle = 1.f;
}

void ASamuraiGame::Reactions(const FLane& Lane, float& OutCheer, float& OutSlump) const
{
	// The latest effect in this lane: a bomb slumps, a combo cheers.
	float Cheer = 0.f, Slump = 0.f;
	for (const FEffect& Effect : Match.Effects)
	{
		if (Effect.PlayerId != Lane.PlayerId)
		{
			continue;
		}
		const double Age = (Effect.DurationMs - Effect.TtlMs) / 1000.0;
		const double Life = Effect.DurationMs / 1000.0;
		const float In = static_cast<float>(Ease::Smooth(0.0, ReactionInSeconds, Age) * Ease::Smooth(Life, Life - ReactionOutSeconds, Age));
		if (Effect.Kind == EEffectKind::Bomb)
		{
			Slump = FMath::Max(Slump, In);
		}
		if (Effect.Kind == EEffectKind::Combo)
		{
			Cheer = FMath::Max(Cheer, ComboCheer * In);
		}
	}
	if (Stage == ESamuraiStage::Finished)
	{
		const bool bWinner = Match.WinnerIds.Contains(Lane.PlayerId);
		Cheer = FMath::Max(Cheer, bWinner ? WinnerCheer : 0.f);
		Slump = FMath::Max(Slump, bWinner ? 0.f : LoserSlump);
	}
	OutCheer = Cheer;
	OutSlump = Slump;
}

void ASamuraiGame::PlaceKatana(const FLane& Lane, UStaticMeshComponent& Katana) const
{
	// The katana in the hand, along the blade, its edge leading the cut.
	const FVector Along = MovecadeKart::Coords::DirToUE(Lane.Blade.Direction).GetSafeNormal();
	const FVector EdgeWorld = MovecadeKart::Coords::DirToUE(KatanaEdgeHint);
	const FVector Edge = (EdgeWorld - Along * FVector::DotProduct(EdgeWorld, Along)).GetSafeNormal();
	Katana.SetWorldLocationAndRotation(ToWorld(Lane.Blade.Hand), FRotationMatrix::MakeFromXZ(Along, Edge.IsNearlyZero() ? FVector::UpVector : Edge).Rotator());
}

// ---- Trails -----------------------------------------------------------------------------------------------------

void ASamuraiGame::UpdateTrails()
{
	// bladeTrail.tsx: a ribbon over the last dozen blade samples while the sweep is meant, fading 100 ms after.
	// The section's topology is fixed (TrailSamples pairs): only the vertices move, and a short trail repeats its
	// oldest sample (degenerate quads) rather than rebuilding the section.
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		const bool bLive = Clock - Lane.TrailAt < TrailLiveSeconds;
		if (bLive)
		{
			Lane.Trail.Add(TPair<FVector, FVector>(ToWorld(FMath::Lerp(Lane.Blade.Base, Lane.Blade.Tip, TrailInnerAlongBlade)), ToWorld(Lane.Blade.Tip)));
			if (Lane.Trail.Num() > TrailSamples)
			{
				Lane.Trail.RemoveAt(0, Lane.Trail.Num() - TrailSamples, EAllowShrinking::No);
			}
		}
		else if (Lane.Trail.Num() > 0 && Clock - Lane.TrailAt > TrailClearSeconds)
		{
			Lane.Trail.Reset();
		}
		UProceduralMeshComponent* Mesh = Trails[i];
		const int32 Count = Lane.Trail.Num();
		if (Count < 2)
		{
			if (Lane.bTrailShown)
			{
				Mesh->SetMeshSectionVisible(0, false);
				Lane.bTrailShown = false;
			}
			continue;
		}
		const int32 Pad = TrailSamples - Count;
		for (int32 k = 0; k < TrailSamples; ++k)
		{
			// Thinner toward the old end.
			const int32 j = FMath::Max(0, k - Pad);
			const double Along = static_cast<double>(j) / (Count - 1);
			TrailVerts[k * 2] = FMath::Lerp(Lane.Trail[j].Value, Lane.Trail[j].Key, Along);
			TrailVerts[k * 2 + 1] = Lane.Trail[j].Value;
		}
		Mesh->UpdateMeshSection_LinearColor(0, TrailVerts, TrailNormals, TrailUVs, TrailColors, TrailTangents, false);
		if (!Lane.bTrailShown)
		{
			Mesh->SetMeshSectionVisible(0, true);
			Lane.bTrailShown = true;
		}
	}
}

void ASamuraiGame::ResetPresentation()
{
	LastSeenEffectId = 0;
	Notes.Reset();
	BannerAt = -10.0;
	FocusMood = 0.f;
	for (FLane& Lane : Lanes)
	{
		Lane.bWasFocus = false;
		Lane.bWasIntent = false;
		Lane.LastCutAt = -10.0;
	}
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].TargetId != INDEX_NONE)
		{
			ReleaseSlot(i);
		}
	}
	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		Pieces[i].bLive = false;
		PiecePool[i]->SetVisibility(false);
	}
}

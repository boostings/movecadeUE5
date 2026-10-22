// AKartRaceManager's signals out of each step: HUD and room cues (names follow app/src/audio/kartCues.ts), phone
// haptics (spec 04 §6), the sounds the audio observer can't derive from state, and each kart's item status for audio.
#include "Race/KartRaceManager.h"

#include "Audio/KartAudioComponent.h"
#include "Audio/KartAudioSubsystem.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"

using namespace MovecadeKart;

namespace
{
	/** Off-road rumble: this strength every RumbleSeconds while moving faster than RumbleMinSpeed (SU)
	 *  (MultiplayerKart.tsx:1552-1568). */
	constexpr float RumbleStrength = 0.075f;
	constexpr double RumbleSeconds = 0.2;
	constexpr double RumbleMinSpeed = 5.0;
	/** Item haptics: a spin-out, an explosion in range, a kart bump. */
	constexpr float SpinOutHaptic = 0.8f;
	constexpr float ExplosionHaptic = 1.0f;
	constexpr float BumpHaptic = 0.6f;
	/** Sounds: the seeker warning's volume, a kart bump's impact strength. */
	constexpr float SeekerWarningVolume = 0.6f;
	constexpr float BumpImpact = 0.8f;

	const FName BarrierHaptic(TEXT("barrier"));
	const FName KartHaptic(TEXT("kart"));
	const FName TerrainHaptic(TEXT("terrain"));
	const FName ItemHaptic(TEXT("item"));

	/** The web's item ids, which the audio observer keys item-spend sounds on. */
	FName WebItemId(EKartItem Item)
	{
		// Read per kart per step, so the names are interned once rather than looked up on every call.
		static const FName Ids[] = { NAME_None, TEXT("GREEN_SHELL"), TEXT("RED_SHELL"), TEXT("BLUE_SHELL"), TEXT("BANANA"), TEXT("MUSHROOM"),
			TEXT("TRIPLE_MUSHROOM"), TEXT("STAR"), TEXT("LIGHTNING"), TEXT("BLOOPER"), TEXT("BOB_OMB"), TEXT("BULLET_BILL") };
		static_assert(UE_ARRAY_COUNT(Ids) == static_cast<SIZE_T>(EKartItem::Count), "one web id per item");
		const SIZE_T Index = static_cast<uint8>(Item);
		return Index < UE_ARRAY_COUNT(Ids) ? Ids[Index] : NAME_None;
	}

	EKartAudioPhase AudioPhase(EKartRacePhase Phase)
	{
		switch (Phase)
		{
		case EKartRacePhase::Countdown: return EKartAudioPhase::Countdown;
		case EKartRacePhase::Racing: return EKartAudioPhase::Racing;
		case EKartRacePhase::Finished: return EKartAudioPhase::Results;
		default: return EKartAudioPhase::Waiting;
		}
	}

	bool IsSeeker(EKartItem Item)
	{
		return Item == EKartItem::Seeker || Item == EKartItem::SkySeeker;
	}
}

void AKartRaceManager::EmitStepEvents(FKartRacer& Racer)
{
	const FKartState& S = Racer.Kart->GetKartMovement()->SimState();
	if (S.BarrierEvent > 0.0)
	{
		OnHaptic.Broadcast(Racer.Seat, static_cast<float>(S.BarrierEvent), BarrierHaptic, S.bBarrierEventSustained);
	}
	if (S.BumpEvent > 0.0)
	{
		OnHaptic.Broadcast(Racer.Seat, static_cast<float>(S.BumpEvent), KartHaptic, false);
		if (UKartAudioComponent* Audio = Racer.Kart->GetKartAudio())
		{
			Audio->PlayImpact(static_cast<float>(S.BumpEvent));
		}
	}
	if (S.bOnDirt && FMath::Abs(S.Speed) > RumbleMinSpeed && S.Time - Racer.LastTerrainRumble >= RumbleSeconds)
	{
		Racer.LastTerrainRumble = S.Time;
		OnHaptic.Broadcast(Racer.Seat, RumbleStrength, TerrainHaptic, true);
	}
}

void AKartRaceManager::DispatchItemEvents(const TArray<FKartItemEvent>& Events)
{
	for (const FKartItemEvent& E : Events)
	{
		switch (E.Type)
		{
		case EKartItemEventType::Pickup: OnCue.Broadcast(E.Seat, TEXT("kart.item-pickup")); break;
		case EKartItemEventType::Ready: OnCue.Broadcast(E.Seat, TEXT("kart.item-ready")); break;
		case EKartItemEventType::Used:
			if (Racers.IsValidIndex(E.Seat))
			{
				Racers[E.Seat].Kart->PlayItemThrow();
			}
			break;
		case EKartItemEventType::Launch:
			OnCue.Broadcast(E.Seat, TEXT("kart.projectile-launch"));
			if (IsSeeker(E.Item))
			{
				OnCue.Broadcast(-1, TEXT("kart.warning"));
			}
			break;
		case EKartItemEventType::Drop: OnCue.Broadcast(E.Seat, TEXT("kart.hazard-drop")); break;
		case EKartItemEventType::Bounce: OnCue.Broadcast(E.Other, TEXT("kart.projectile-bounce")); break;
		case EKartItemEventType::Hit: OnCue.Broadcast(E.Seat, TEXT("kart.projectile-hit")); break;
		case EKartItemEventType::SpinOut:
			OnCue.Broadcast(E.Seat, TEXT("kart.spinout"));
			OnHaptic.Broadcast(E.Seat, SpinOutHaptic, ItemHaptic, false);
			break;
		case EKartItemEventType::Explosion:
			OnCue.Broadcast(-1, TEXT("kart.explosion"));
			for (const FKartRacer& Racer : Racers)
			{
				if (FVector2D::Distance(Racer.Kart->GetKartMovement()->SimState().Pos, E.Position) < E.Radius)
				{
					OnHaptic.Broadcast(Racer.Seat, ExplosionHaptic, ItemHaptic, false);
				}
			}
			break;
		case EKartItemEventType::Lightning: OnCue.Broadcast(E.Seat, TEXT("kart.lightning")); break;
		case EKartItemEventType::Inked: OnCue.Broadcast(E.Seat, TEXT("kart.ink")); break;
		case EKartItemEventType::Invincible: OnCue.Broadcast(E.Seat, TEXT("kart.invincible-start")); break;
		case EKartItemEventType::RocketStart: OnCue.Broadcast(E.Seat, TEXT("kart.boost")); break;
		case EKartItemEventType::Bump: OnHaptic.Broadcast(E.Seat, BumpHaptic, KartHaptic, false); break;
		default: break;
		}
	}
	PlayItemSounds(Events);
	PendingItemEvents.Append(Events);
}

void AKartRaceManager::PlayItemSounds(const TArray<FKartItemEvent>& Events)
{
	// Projectiles, hits, explosions and the seeker warning: what the audio observer can't derive from state.
	UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this);
	auto KartAudio = [this](int32 Seat) -> UKartAudioComponent*
	{
		return Racers.IsValidIndex(Seat) ? Racers[Seat].Kart->GetKartAudio() : nullptr;
	};
	auto KartCue = [&KartAudio](int32 Seat, const TCHAR* Cue)
	{
		if (UKartAudioComponent* Audio = KartAudio(Seat))
		{
			Audio->PlayCue(Cue);
		}
	};
	for (const FKartItemEvent& E : Events)
	{
		switch (E.Type)
		{
		case EKartItemEventType::Launch:
			KartCue(E.Seat, TEXT("kart.projectile-launch"));
			if (Mixer && IsSeeker(E.Item))
			{
				Mixer->PlayCue(TEXT("kart.warning"), SeekerWarningVolume);
			}
			break;
		case EKartItemEventType::Drop: KartCue(E.Seat, TEXT("kart.hazard-drop")); break;
		case EKartItemEventType::Bounce: KartCue(E.Other, TEXT("kart.projectile-bounce")); break;
		case EKartItemEventType::Hit: KartCue(E.Seat, TEXT("kart.projectile-hit")); break;
		case EKartItemEventType::SpinOut: KartCue(E.Seat, TEXT("kart.spinout")); break;
		case EKartItemEventType::Explosion:
			if (Mixer)
			{
				Mixer->PlayCue(TEXT("kart.explosion"));
			}
			break;
		case EKartItemEventType::Bump:
			if (UKartAudioComponent* Audio = KartAudio(E.Seat))
			{
				Audio->PlayImpact(BumpImpact);
			}
			break;
		default:
			break;
		}
	}
}

void AKartRaceManager::UpdateAudioStatus(double ItemTime)
{
	for (FKartRacer& Racer : Racers)
	{
		UKartAudioComponent* Audio = Racer.Kart->GetKartAudio();
		if (!Audio)
		{
			continue;
		}
		const FKartItemSlot& Slot = Items.Slots()[Racer.Seat];
		MovecadeKart::Audio::FKartAudioStatus Status;
		Status.bRollingItem = Slot.bRolling;
		Status.bHasItem = Slot.Held != EKartItem::None;
		Status.HeldItem = WebItemId(Slot.Held);
		Status.bInvincible = Items.IsInvincible(Racer.Seat, ItemTime);
		Status.bBulletBill = Items.IsRocket(Racer.Seat, ItemTime);
		Status.bShrunk = ItemTime < Slot.ShrinkUntil;
		Status.bInk = ItemTime < Slot.InkUntil;
		Audio->SetStatus(Status);
	}
}

void AKartRaceManager::SetAudioPhase(EKartRacePhase NewPhase)
{
	for (FKartRacer& Racer : Racers)
	{
		if (UKartAudioComponent* Audio = Racer.Kart->GetKartAudio())
		{
			Audio->SetRacePhase(AudioPhase(NewPhase));
		}
	}
}

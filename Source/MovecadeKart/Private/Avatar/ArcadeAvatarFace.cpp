#include "Avatar/ArcadeAvatarFace.h"

#include "Components/SkeletalMeshComponent.h"

namespace MovecadeKart::Avatar
{
	void FaceFor(const FArcadePoseInput& Pose, float (&OutWeights)[DriverFace::MorphCount])
	{
		using namespace MovecadeKart::DriverFace;
		FMood Mood;
		const float Emote = Pose.Emote == EArcadeEmote::None ? 0.f : FMath::Clamp(Pose.EmoteWeight, 0.f, 1.f);
		switch (Pose.Emote)
		{
		case EArcadeEmote::Wave:
		case EArcadeEmote::Clap:
		case EArcadeEmote::FistPump:
			Mood.Boost = 0.8f * Emote;
			break;
		case EArcadeEmote::Point:
			Mood.Rev = 0.7f * Emote;
			break;
		case EArcadeEmote::Shrug:
			Mood.Meh = Emote;
			break;
		default:
			break;
		}
		Mood.Effort = FMath::Clamp(Pose.Effort, 0.f, 1.f);
		Mood.Cheer = FMath::Clamp(Pose.Cheer, 0.f, 1.f);
		Mood.Slump = FMath::Clamp(Pose.Slump, 0.f, 1.f);
		Mix(Mood, OutWeights);
		// A cheer yells: the jaw works open and shut ("woo!").
		const int32 Jaw = static_cast<int32>(EMorph::JawOpen);
		OutWeights[Jaw] = FMath::Clamp(OutWeights[Jaw] + 0.3f * Mood.Cheer * FMath::Abs(FMath::Sin(Pose.Time * 7.5f)), 0.f, 1.f);
	}

	bool FMorphWriter::Set(USkeletalMeshComponent* Mesh, int32 Slot, FName Name, float Weight, float Epsilon)
	{
		if (!Mesh || Slot < 0)
		{
			return false;
		}
		while (Written.Num() <= Slot)
		{
			Written.Add(-1.f); // never written
		}
		float& Last = Written[Slot];
		const bool bSettled = (Weight == 0.f || Weight == 1.f) && Last != Weight;
		if (Last >= 0.f && FMath::Abs(Weight - Last) <= Epsilon && !bSettled)
		{
			return false;
		}
		Mesh->SetMorphTarget(Name, Weight);
		Last = Weight;
		return true;
	}
}

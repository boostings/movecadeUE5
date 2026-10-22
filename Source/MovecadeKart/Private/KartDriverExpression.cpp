#include "KartDriverExpression.h"

namespace MovecadeKart::DriverFace
{
	namespace
	{
		using FFace = float[MorphCount];

		// Each expression as full morph weights, in EMorph order:
		//                              Smile Cheek BrowDn BrInUp BrOutUp Jaw  Press Frown Wide Squint
		constexpr FFace Rest      = { 0.22f, 0.12f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f };
		constexpr FFace Focused   = { 0.05f, 0.10f, 0.55f, 0.00f, 0.00f, 0.00f, 0.45f, 0.00f, 0.00f, 0.30f };
		constexpr FFace Grin      = { 0.85f, 0.55f, 0.10f, 0.00f, 0.20f, 0.15f, 0.00f, 0.00f, 0.00f, 0.35f };
		constexpr FFace Eager     = { 0.35f, 0.20f, 0.35f, 0.00f, 0.00f, 0.00f, 0.30f, 0.00f, 0.15f, 0.10f };
		constexpr FFace Shocked   = { 0.00f, 0.00f, 0.00f, 0.65f, 0.70f, 0.55f, 0.00f, 0.10f, 0.95f, 0.00f };
		constexpr FFace Beaming   = { 1.00f, 0.70f, 0.00f, 0.00f, 0.40f, 0.45f, 0.00f, 0.00f, 0.10f, 0.25f };
		constexpr FFace Crestfall = { 0.00f, 0.00f, 0.00f, 0.75f, 0.00f, 0.00f, 0.15f, 0.80f, 0.00f, 0.15f };
		constexpr FFace Strain    = { 0.00f, 0.45f, 0.80f, 0.00f, 0.00f, 0.06f, 0.85f, 0.15f, 0.00f, 0.60f };
		constexpr FFace Unbothered= { 0.06f, 0.00f, 0.00f, 0.60f, 0.45f, 0.00f, 0.55f, 0.35f, 0.10f, 0.00f };

		void Toward(float (&Out)[MorphCount], const FFace& Face, float Strength)
		{
			const float T = FMath::Clamp(Strength, 0.f, 1.f);
			for (int32 i = 0; i < MorphCount; ++i)
			{
				Out[i] = FMath::Lerp(Out[i], Face[i], T);
			}
		}
	}

	FName MorphName(EMorph Morph)
	{
		static const FName Names[MorphCount] = {
			TEXT("mouthSmile"), TEXT("cheekSquint"), TEXT("browDown"), TEXT("browInnerUp"), TEXT("browOuterUp"),
			TEXT("jawOpen"), TEXT("mouthPress"), TEXT("mouthFrown"), TEXT("eyeWide"), TEXT("eyeSquint"),
		};
		return Names[static_cast<int32>(Morph)];
	}

	void Mix(const FMood& Mood, float (&OutWeights)[MorphCount])
	{
		for (int32 i = 0; i < MorphCount; ++i)
		{
			OutWeights[i] = Rest[i];
		}
		Toward(OutWeights, Focused, Mood.Drift);
		Toward(OutWeights, Grin, Mood.Boost);
		Toward(OutWeights, Eager, Mood.Rev);
		Toward(OutWeights, Strain, Mood.Effort);
		Toward(OutWeights, Shocked, Mood.Shock);
		Toward(OutWeights, Beaming, Mood.Cheer);
		Toward(OutWeights, Crestfall, Mood.Slump);
		Toward(OutWeights, Unbothered, Mood.Meh);
	}
}

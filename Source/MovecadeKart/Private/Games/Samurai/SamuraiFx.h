#pragma once

#include "CoreMinimal.h"
#include "FX/KartFx.h"
#include "Games/Samurai/SamuraiRules.h"

/** Samurai Slice's particle recipes for UKartFxSubsystem: the juice of a cut and the blast of a bomb. */
namespace MovecadeKart::Samurai
{
	/**
	 * A cut through a target of Variant at At (UE cm) parting along Normal (UE), Scale its mesh scale: juice thrown
	 * off both faces, a spray, the pulp and a splash in the fruit's colour; a perfect cut adds a star and glints.
	 */
	void AppendCutFx(EVariant Variant, const FVector& At, const FVector& Normal, double Scale, bool bPerfect, TArray<MovecadeKart::Fx::FKartFxEmit>& Out);

	/** A bomb going off at At (UE cm): the shared explosion and the shell's shards flung at the camera. */
	void AppendBombFx(const FVector& At, TArray<MovecadeKart::Fx::FKartFxEmit>& Out);
}

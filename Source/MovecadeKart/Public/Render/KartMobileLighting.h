#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KartMobileLighting.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;

/**
 * Pure rules for the mobile lighting fallback (unit-tested in Tests/KartRenderTests.cpp).
 *
 * The PC look is Lumen GI and reflections over each venue's directional lights and sky light. The mobile renderer
 * has neither Lumen nor more than one directional light per lighting channel (the first one registered wins, which
 * in a venue with a rim or fill light can be the dim one), so on phones:
 *   - the key directional light (the brightest shadow-casting one: venues' rims are often brighter than their key
 *     but never cast shadows) keeps the slot and the cascaded shadows; the others are switched off and a share of
 *     their light is folded into the sky light, which is the only ambient term left;
 *   - sky lights are captured once instead of every frame (two venues capture in real time);
 *   - movable local lights don't cast shadows (point-light shadows are desktop-only; spot shadows cost a pass each);
 *   - every sky light's intensity is raised by the GI make-up factor, standing in for the bounce Lumen adds.
 */
namespace KartMobileLighting
{
	/** What the rules need to know about a directional light. */
	struct FDirectionalInfo
	{
		/** Illuminance (lux) times the light colour's luminance. */
		float Brightness = 0.f;
		bool bVisible = true;
		bool bCastsShadows = false;
	};

	/** The key light: the brightest visible shadow caster, else the brightest visible one; INDEX_NONE if none. */
	MOVECADEKART_API int32 PickKeyLight(TConstArrayView<FDirectionalInfo> Lights);

	/** Share of a dropped directional light's brightness (relative to the key) folded into the sky light. */
	constexpr float FoldShare = 0.5f;
	/** The fold adds at most half the sky light again (night venues have rims as bright as their key). */
	constexpr float MaxFoldScale = 1.5f;
	/** Stand-in for Lumen's bounce light on the sky light (measured against the PC look in the mobile audit). */
	constexpr float GiMakeUpScale = 1.35f;

	/** The sky light scale that compensates for dropping these lights (1 = none dropped). */
	MOVECADEKART_API float SkyScaleFor(TConstArrayView<FDirectionalInfo> Lights, int32 KeyIndex);

	/** A local light, for the indoor ambient's tint. */
	struct FLocalInfo
	{
		FLinearColor Color = FLinearColor::White;
		float Lumens = 0.f;
	};

	/**
	 * Indoor venues (no sky light: the bowling alley, the pub) get their ambient from Lumen on PC. On phones they get a
	 * uniform sky light tinted by their own lamps: the lumen-weighted mean of the local light colours, normalised to
	 * luminance 1 and pulled halfway to white so a single coloured neon can't tint the room.
	 */
	MOVECADEKART_API FLinearColor IndoorAmbientTint(TConstArrayView<FLocalInfo> Lights);
}

/**
 * Applies KartMobileLighting's rules to every played world that renders with the mobile renderer (phones, and
 * -featureleveles31 on PC). Does nothing on the desktop renderer, so the PC look is untouched. kart.MobileLighting=0
 * turns it off for comparisons.
 *
 * Venues and tracks build their lights in their constructors and at BeginPlay, so the pass runs at world BeginPlay
 * and once more shortly after (venues that rebuild for the player count); each light is handled once.
 */
UCLASS()
class MOVECADEKART_API UKartMobileLighting : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Runs the fallback over the world's lights now. Returns the number of lights changed. */
	int32 Apply();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Adds the indoor ambient sky light (see KartMobileLighting::IndoorAmbientTint). */
	void AddIndoorAmbient(UWorld& World, TConstArrayView<KartMobileLighting::FLocalInfo> Locals);

	/** Lights already handled, so a second pass never folds twice. */
	TSet<TObjectKey<UActorComponent>> Handled;
	FTimerHandle SecondPass;

	/** The indoor ambient sky light, when this pass added one. */
	UPROPERTY()
	TObjectPtr<USkyLightComponent> IndoorAmbient;
};

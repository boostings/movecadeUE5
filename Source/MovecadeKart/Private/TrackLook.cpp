// ATrackBuilder's look: sun, sky light, the painted sky dome, height fog and the per-track colour tint, from the
// track JSON's `look` (kartLook.ts's record, Docs/Port/03 §5.7) and its `look.ue` art direction.
#include "TrackBuilder.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture.h"
#include "KartDebugFlags.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Track/TrackBuildHelpers.h"

using namespace MovecadeKart;
using namespace MovecadeKart::TrackBuild;

namespace
{
	const TCHAR* const SkyMaterialPath = TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky");
	const TCHAR* const CloudTexturePath = TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds");

	/** Shadows from the sun reach 200 m; the fog stops short of the 10 km dome so the sky stays clean. */
	constexpr float SunShadowDistanceCm = 20000.f;
	constexpr float FogCutoffCm = 900000.f;
	/** The sky light leans this far from white toward the hemisphere's sky colour; the ground bounce is halved. */
	constexpr float SkyTintAmount = 0.35f;
	constexpr float GroundBounceScale = 0.5f;
	/** The sky light recaptures twice more once play starts (see ATrackBuilder::TickSkyCapture). */
	constexpr int32 SkyRecaptures = 2;
	/** Height fog reads as haze at the web's far distance: this much density spread over (far - near). */
	constexpr double FogDensityOverRange = 5.0;
	constexpr double FogMinRange = 100.0;
	/** The web lifts after tone mapping; UE's colour offset acts on scene colour, so a third of it gives the same tint. */
	constexpr double LiftToOffset = 3.0;

	/**
	 * look.ue's numbers and colours: the UE art direction per track (sun, sky, clouds, fog, exposure), which wins over
	 * the web record. -KartLookUE="key=value,..." overrides its numbers for quick look passes.
	 */
	struct FLookTuner
	{
		TSharedPtr<FJsonObject> UE;
		const TMap<FString, double>& Overrides;

		double Number(const TCHAR* Key, double Default) const
		{
			const double* Override = Overrides.Find(Key);
			return Override ? *Override : Num(UE, Key, Default);
		}

		FLinearColor Color(const TCHAR* Key, const FLinearColor& Default) const
		{
			FString Hex;
			return UE.IsValid() && UE->TryGetStringField(Key, Hex) ? ColorField(UE, Key, TEXT("ffffff")) : Default;
		}
	};

	/** The look's records, read once per ApplyLook. */
	struct FLookRecords
	{
		TSharedPtr<FJsonObject> Look;
		TSharedPtr<FJsonObject> Mood;
		TSharedPtr<FJsonObject> Grade;
		TSharedPtr<FJsonObject> Fog;
		TSharedPtr<FJsonObject> Hemisphere;
		TSharedPtr<FJsonObject> Fill;
		TSharedPtr<FJsonObject> Sky;
	};

	/** The sky dome (M_TrackSky: gradient, sun, horizon glow, clouds) in the look's colours. */
	void PaintSky(UMaterialInstanceDynamic* Sky, const FLookRecords& R, const FLookTuner& Tune, const FVector& SunDir,
		const FLinearColor& Horizon, const FLinearColor& Top)
	{
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
		Sky->SetVectorParameterValue(TEXT("SkyMid"), Tune.Color(TEXT("skyMid"), FMath::Lerp(Horizon, Top, 0.55f)));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Tune.Color(TEXT("skyBelow"), ColorField(R.Mood, TEXT("skyGround"), TEXT("a9bfd6"))));
		const FVector SkySun = Coords::DirToUE(Tune.UE.IsValid() && Tune.UE->HasField(TEXT("sunDirection")) ? SunDir : VecOr(R.Sky, TEXT("sunDirection"), SunDir)).GetSafeNormal();
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SkySun.X, SkySun.Y, SkySun.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Tune.Color(TEXT("sunDisc"), ColorField(R.Sky, TEXT("cloudColor"), TEXT("fff6ee"))));
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.6 * Tune.Number(TEXT("sunSize"), Num(R.Sky, TEXT("sunSize"), 0.03))));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), Tune.Number(TEXT("sunGlow"), Num(R.Sky, TEXT("sunGlow"), 0.8) * 0.6));
		Sky->SetScalarParameterValue(TEXT("GlowPower"), Tune.Number(TEXT("glowPower"), 24.0));
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), Tune.Number(TEXT("horizonGlow"), 0.4));
		Sky->SetScalarParameterValue(TEXT("Brightness"), Tune.Number(TEXT("skyBrightness"), 1.0));
		// Clouds: the generated density map, coverage from the web's mood (0 = clear).
		if (UTexture* Clouds = LoadObject<UTexture>(nullptr, CloudTexturePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
		}
		Sky->SetScalarParameterValue(TEXT("Clouds"), Tune.Number(TEXT("clouds"), Num(R.Sky, TEXT("clouds"), 0.4)));
		Sky->SetScalarParameterValue(TEXT("CloudScale"), Tune.Number(TEXT("cloudScale"), 0.35));
		Sky->SetVectorParameterValue(TEXT("CloudLit"), Tune.Color(TEXT("cloudLit"), ColorField(R.Sky, TEXT("cloudColor"), TEXT("fff6ee"))));
		Sky->SetVectorParameterValue(TEXT("CloudShade"), Tune.Color(TEXT("cloudShade"), FMath::Lerp(Horizon, Top, 0.4f) * 0.8f));
	}

	/** Height fog from `fogStart`, in the horizon colour unless look.ue picks one; the sun side scatters warmer. */
	void ApplyFog(UExponentialHeightFogComponent* Fog, const FLookRecords& R, const FLookTuner& Tune, const FLinearColor& Horizon, const FLinearColor& SunColor)
	{
		const double Near = Num(R.Fog, TEXT("near"), 260.0), Far = Num(R.Fog, TEXT("far"), 2000.0);
		Fog->SetStartDistance(static_cast<float>(Tune.Number(TEXT("fogStart"), Near) * 100.0));
		Fog->SetFogDensity(static_cast<float>(Tune.Number(TEXT("fogDensity"), FogDensityOverRange / FMath::Max(Far - Near, FogMinRange))));
		Fog->SetFogHeightFalloff(static_cast<float>(Tune.Number(TEXT("fogFalloff"), 0.02)));
		Fog->SetFogMaxOpacity(static_cast<float>(Tune.Number(TEXT("fogMaxOpacity"), 0.85)));
		Fog->SetFogInscatteringColor(Tune.Color(TEXT("fogColor"), Horizon) * static_cast<float>(Tune.Number(TEXT("fogBrightness"), 1.0)));
		Fog->SetDirectionalInscatteringColor(SunColor * static_cast<float>(Tune.Number(TEXT("fogSunScatter"), 0.25)));
		Fog->SetDirectionalInscatteringExponent(static_cast<float>(Tune.Number(TEXT("fogSunExponent"), 6.0)));
	}

	/**
	 * Only this track's colour tint (the web's per-track gain and lift). Exposure, tone curve, saturation, bloom,
	 * vignette and AO come from the global grade (AKartGradeVolume, priority -100), whose auto exposure centres on this
	 * level's sun; look.ue's exposureBias nudges it for a track that needs it.
	 */
	void ApplyGrade(FPostProcessSettings& S, const FLookRecords& R, const FLookTuner& Tune)
	{
		S = FPostProcessSettings();
		const FVector Gain = VecOr(R.Grade, TEXT("gain"), FVector::OneVector), Lift = VecOr(R.Grade, TEXT("lift"), FVector::ZeroVector);
		S.bOverride_ColorGain = true;
		S.ColorGain = FVector4(Gain.X, Gain.Y, Gain.Z, 1.0);
		S.bOverride_ColorOffset = true;
		S.ColorOffset = FVector4(Lift.X / LiftToOffset, Lift.Y / LiftToOffset, Lift.Z / LiftToOffset, 0.0);
		const double ExposureBias = Tune.Number(TEXT("exposureBias"), 0.0);
		if (ExposureBias != 0.0)
		{
			S.bOverride_AutoExposureBias = true;
			S.AutoExposureBias = static_cast<float>(1.0 + ExposureBias); // the global grade's +1 compensation, nudged
		}
	}
}

void ATrackBuilder::ApplyLook()
{
	Sun->SetVisibility(bAddLighting);
	SkyLight->SetVisibility(bAddLighting);
	Atmosphere->SetVisibility(bAddLighting);
	Fog->SetVisibility(bAddLighting);
	PostProcess->bEnabled = bAddLighting;
	if (!Json.IsValid())
	{
		return;
	}
	// The scene is lit in three.js units: with manual exposure at EV 0, a sun of `sunIntensity` lux on a white
	// Lambert surface gives the web's 1/pi-scaled radiance.
	FLookRecords R;
	R.Look = Obj(Json, TEXT("look"));
	R.Mood = Obj(R.Look, TEXT("mood"));
	R.Grade = Obj(R.Look, TEXT("grade"));
	R.Fog = Obj(R.Look, TEXT("fog"));
	R.Hemisphere = Obj(R.Look, TEXT("hemisphere"));
	R.Fill = Obj(R.Look, TEXT("fill"));
	R.Sky = Obj(R.Mood, TEXT("sky"));
	const FLookTuner Tune{ Obj(R.Look, TEXT("ue")), FKartDebugFlags::Get().LookOverrides };

	// Sun: direction toward the sun in three.js axes.
	const FVector WebSunDir = VecOr(R.Look, TEXT("sunDirection"), Track.IsValid() ? Track->SunDirection : FVector(0.5, 0.6, 0.6));
	const FVector SunDir = VecOr(Tune.UE, TEXT("sunDirection"), WebSunDir);
	const FLinearColor SunColor = Tune.Color(TEXT("sunColor"), ColorField(R.Look, TEXT("sunColor"), TEXT("ffffff")));
	Sun->SetWorldRotation((-Coords::DirToUE(SunDir).GetSafeNormal()).Rotation());
	Sun->SetLightColor(SunColor);
	Sun->SetIntensity(static_cast<float>(Num(R.Look, TEXT("sunIntensity"), 3.0) * Tune.Number(TEXT("sunScale"), 1.0)));
	Sun->SetCastShadows(true);
	Sun->SetDynamicShadowDistanceMovableLight(SunShadowDistanceCm);

	// Sky light: a capture of the dome below, tinted toward the hemisphere sky colour.
	const double Ambient = Num(R.Hemisphere, TEXT("intensity"), 0.6) + Num(R.Fill, TEXT("intensity"), 0.3);
	SkyLight->SetIntensity(static_cast<float>(Ambient * Tune.Number(TEXT("skyLightScale"), 1.5)));
	SkyLight->SetLightColor(FMath::Lerp(FLinearColor::White, Tune.Color(TEXT("skyLightTint"), ColorField(R.Hemisphere, TEXT("sky"), TEXT("ffffff"))), SkyTintAmount));
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Tune.Color(TEXT("groundBounce"), ColorField(R.Hemisphere, TEXT("ground"), TEXT("6e5a48"))) * GroundBounceScale);
	const bool bRealtimeSky = FKartDebugFlags::Get().bSkyRealtime;
	if (SkyLight->bRealTimeCapture != bRealtimeSky)
	{
		SkyLight->SetRealTimeCapture(bRealtimeSky);
	}
	SkyLight->RecaptureSky();
	SkyRecapturesLeft = bRealtimeSky ? 0 : SkyRecaptures;

	// Sky: a painted dome. The physical atmosphere stays off: at these light levels it renders a dim grey sky, and
	// it would tint the sun.
	Atmosphere->SetVisibility(false);
	Sun->SetAtmosphereSunLight(false);
	SkySphere->SetVisibility(bAddLighting);
	const FLinearColor Horizon = Tune.Color(TEXT("skyHorizon"), ColorField(R.Mood, TEXT("skyHorizon"), TEXT("d4e6f5")));
	const FLinearColor Top = Tune.Color(TEXT("skyTop"), ColorField(R.Mood, TEXT("skyTop"), TEXT("4f78b8")));
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, SkyMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this, TEXT("MID_Sky"));
		PaintSky(Sky, R, Tune, SunDir, Horizon, Top);
		SkySphere->SetMaterial(0, Sky);
	}
	Fog->SetFogCutoffDistance(FogCutoffCm);
	ApplyFog(Fog, R, Tune, Horizon, SunColor);
	ApplyGrade(PostProcess->Settings, R, Tune);
}

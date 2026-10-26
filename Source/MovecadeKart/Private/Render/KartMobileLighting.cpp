#include "Render/KartMobileLighting.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "RHIGlobals.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartMobileLighting, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarMobileLighting(
		TEXT("kart.MobileLighting"), 1,
		TEXT("Mobile renderer only: apply the lighting fallback (key directional light, folded fill, sky light captured once, ")
		TEXT("no local light shadows). 0 = leave the PC lighting as built, for comparisons."));

	TAutoConsoleVariable<float> CVarIndoorAmbient(
		TEXT("kart.MobileLighting.IndoorAmbient"), 1.f,
		TEXT("Mobile renderer only: intensity of the uniform sky light added to venues without one (indoor venues), standing in ")
		TEXT("for Lumen's bounce light. 0 = none."));

	/** Venues that rebuild for the player count do it within the first second of play. */
	constexpr float SecondPassSeconds = 1.5f;

	/** Always cooked (BaseEngine.ini loads it at startup): a uniform cube, so the indoor ambient is flat. */
	const TCHAR* const UniformCubePath = TEXT("/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube");
}

namespace KartMobileLighting
{
	int32 PickKeyLight(TConstArrayView<FDirectionalInfo> Lights)
	{
		int32 Key = INDEX_NONE;
		for (int32 i = 0; i < Lights.Num(); ++i)
		{
			if (Lights[i].bVisible && (Key == INDEX_NONE || Lights[i].Brightness > Lights[Key].Brightness))
			{
				Key = i;
			}
		}
		return Key;
	}

	float SkyScaleFor(TConstArrayView<FDirectionalInfo> Lights, int32 KeyIndex)
	{
		if (!Lights.IsValidIndex(KeyIndex) || Lights[KeyIndex].Brightness <= 0.f)
		{
			return 1.f;
		}
		float Dropped = 0.f;
		for (int32 i = 0; i < Lights.Num(); ++i)
		{
			if (i != KeyIndex && Lights[i].bVisible)
			{
				Dropped += Lights[i].Brightness;
			}
		}
		return FMath::Min(MaxFoldScale, 1.f + FoldShare * Dropped / Lights[KeyIndex].Brightness);
	}

	FLinearColor IndoorAmbientTint(TConstArrayView<FLocalInfo> Lights)
	{
		FLinearColor Sum = FLinearColor::Black;
		for (const FLocalInfo& Light : Lights)
		{
			Sum += Light.Color * FMath::Max(0.f, Light.Lumens);
		}
		const float Luminance = Sum.GetLuminance();
		if (Luminance <= UE_SMALL_NUMBER)
		{
			return FLinearColor::White;
		}
		FLinearColor Tint = FMath::Lerp(FLinearColor::White, Sum / Luminance, 0.5f);
		Tint.A = 1.f;
		return Tint;
	}
}

bool UKartMobileLighting::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UKartMobileLighting::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetFeatureLevel() > ERHIFeatureLevel::ES3_1 || CVarMobileLighting.GetValueOnGameThread() == 0)
	{
		return;
	}
	Apply();
	InWorld.GetTimerManager().SetTimer(SecondPass, FTimerDelegate::CreateWeakLambda(this, [this]() { Apply(); }), SecondPassSeconds, false);
}

void UKartMobileLighting::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SecondPass);
	}
	Super::Deinitialize();
}

int32 UKartMobileLighting::Apply()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	TArray<UDirectionalLightComponent*> Directionals;
	TArray<USkyLightComponent*> Skies;
	TArray<ULocalLightComponent*> Locals;
	for (TObjectIterator<ULightComponentBase> It(RF_ClassDefaultObject, /*bIncludeDerivedClasses*/ true, EInternalObjectFlags::Garbage); It; ++It)
	{
		ULightComponentBase* Light = *It;
		if (Light->GetWorld() != World || !Light->IsRegistered())
		{
			continue;
		}
		if (UDirectionalLightComponent* Directional = Cast<UDirectionalLightComponent>(Light))
		{
			Directionals.Add(Directional);
		}
		else if (USkyLightComponent* Sky = Cast<USkyLightComponent>(Light))
		{
			Skies.Add(Sky);
		}
		else if (ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light))
		{
			Locals.Add(Local);
		}
	}

	int32 Changed = 0;

	// One directional light: the brightest keeps the shadows, the rest fold into the sky light.
	float SkyScale = 1.f;
	bool bNewDirectionals = false;
	TArray<KartMobileLighting::FDirectionalInfo> Infos;
	for (const UDirectionalLightComponent* Light : Directionals)
	{
		bNewDirectionals |= !Handled.Contains(Light);
		Infos.Add({ Light->Intensity * Light->GetLightColor().GetLuminance(), Light->IsVisible() && Light->bAffectsWorld });
	}
	if (bNewDirectionals)
	{
		const int32 Key = KartMobileLighting::PickKeyLight(Infos);
		SkyScale = KartMobileLighting::SkyScaleFor(Infos, Key);
		for (int32 i = 0; i < Directionals.Num(); ++i)
		{
			UDirectionalLightComponent* Light = Directionals[i];
			Handled.Add(Light);
			if (i != Key && Infos[i].bVisible)
			{
				Light->SetVisibility(false);
				++Changed;
				UE_LOG(LogKartMobileLighting, Log, TEXT("Folded directional light %s into the sky light"), *Light->GetReadableName());
			}
		}
	}

	for (USkyLightComponent* Sky : Skies)
	{
		const bool bFirst = !Handled.Contains(Sky);
		if (!bFirst && SkyScale == 1.f)
		{
			continue;
		}
		Handled.Add(Sky);
		// The GI make-up applies once per sky light; the fold whenever new directional lights were dropped.
		const float Scale = SkyScale * (bFirst ? KartMobileLighting::GiMakeUpScale : 1.f);
		Sky->SetIntensity(Sky->Intensity * Scale);
		if (Sky->bRealTimeCapture)
		{
			Sky->SetRealTimeCapture(false);
		}
		Sky->RecaptureSky();
		++Changed;
		UE_LOG(LogKartMobileLighting, Log, TEXT("Sky light %s: intensity x%.2f, captured once"), *Sky->GetReadableName(), Scale);
	}

	if (Skies.IsEmpty() && !IndoorAmbient)
	{
		TArray<KartMobileLighting::FLocalInfo> LocalInfos;
		for (const ULocalLightComponent* Light : Locals)
		{
			// Lumens are what the venues author in; other units only tint by colour.
			LocalInfos.Add({ Light->GetLightColor(), Light->IntensityUnits == ELightUnits::Lumens ? Light->Intensity : 1.f });
		}
		AddIndoorAmbient(*World, LocalInfos);
		Changed += IndoorAmbient ? 1 : 0;
	}

	for (ULocalLightComponent* Light : Locals)
	{
		if (Handled.Contains(Light))
		{
			continue;
		}
		Handled.Add(Light);
		if (Light->CastShadows && Light->Mobility == EComponentMobility::Movable)
		{
			Light->SetCastShadows(false);
			++Changed;
		}
	}
	UE_LOG(LogKartMobileLighting, Log, TEXT("Mobile lighting pass: %d directional, %d sky, %d local lights; %d changed"),
		Directionals.Num(), Skies.Num(), Locals.Num(), Changed);
	return Changed;
}

void UKartMobileLighting::AddIndoorAmbient(UWorld& World, TConstArrayView<KartMobileLighting::FLocalInfo> Locals)
{
	const float Intensity = CVarIndoorAmbient.GetValueOnGameThread();
	UTextureCube* Cube = LoadObject<UTextureCube>(nullptr, UniformCubePath);
	if (Intensity <= 0.f || !Cube)
	{
		UE_CLOG(!Cube, LogKartMobileLighting, Warning, TEXT("No %s: indoor venue keeps no ambient"), UniformCubePath);
		return;
	}
	FActorSpawnParameters Params;
	Params.Name = MakeUniqueObjectName(&World, AActor::StaticClass(), TEXT("KartMobileAmbient"));
	Params.ObjectFlags |= RF_Transient;
	AActor* Holder = World.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Holder)
	{
		return;
	}
	IndoorAmbient = NewObject<USkyLightComponent>(Holder, TEXT("IndoorAmbient"));
	IndoorAmbient->SetMobility(EComponentMobility::Movable);
	IndoorAmbient->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
	IndoorAmbient->Cubemap = Cube;
	IndoorAmbient->bLowerHemisphereIsBlack = false;
	IndoorAmbient->SetLightColor(KartMobileLighting::IndoorAmbientTint(Locals));
	IndoorAmbient->SetIntensity(Intensity);
	Holder->SetRootComponent(IndoorAmbient);
	IndoorAmbient->RegisterComponent();
	IndoorAmbient->RecaptureSky();
	Handled.Add(IndoorAmbient);
	UE_LOG(LogKartMobileLighting, Log, TEXT("Indoor venue: added a uniform ambient sky light (intensity %.2f, tint %s)"),
		Intensity, *IndoorAmbient->GetLightColor().ToString());
}

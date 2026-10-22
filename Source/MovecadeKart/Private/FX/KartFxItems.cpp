// UKartFxComponent's item states: the prism's rainbow shell and twinkles, the rocket's afterburner, and the view's
// screen post-process (prism rainbow, hit flash, lightning, explosion flash and ink).
#include "FX/KartFxComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "FX/KartFxSubsystem.h"
#include "KartPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "Sim/KartItems.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;

namespace
{
	const TCHAR* const ScreenMaterialPath = TEXT("/Game/FX/Materials/M_PP_KartScreen.M_PP_KartScreen");

	constexpr double ItemInherit = 0.85;
	constexpr double PrismTwinkleRate = 40.0;
	constexpr double RocketFireRate = 110.0;
	constexpr double RocketCoreRate = 70.0;
	constexpr double RocketSmokeRate = 26.0;

	/** RaceUI.tsx: the explosion flash lasts 0.5 s and ink 4 s. */
	constexpr double ExplosionFlashSeconds = 0.5;
	constexpr double InkSeconds = 4.0;
	/** kartFxBus hit flash: peak * (1 - t)^2 over 0.28 s. */
	constexpr double HitFlashSeconds = 0.28;
	constexpr double ZapSeconds = 0.45;

	namespace Param
	{
		const FName Color(TEXT("Color"));
		const FName Prism(TEXT("Prism"));
		const FName FlashT(TEXT("FlashT"));
		const FName InkAge(TEXT("InkAge"));
		const FName InkLeft(TEXT("InkLeft"));
		const FName Hit(TEXT("Hit"));
		const FName HitColor(TEXT("HitColor"));
		const FName Zap(TEXT("Zap"));
	}

	FKartFxEmit PrismTwinkles(const FLinearColor& Rainbow, const FVector& InheritCm)
	{
		FKartFxEmit Twinkle;
		Twinkle.Field = EField::Flare;
		Twinkle.Direction = FVector::UpVector;
		Twinkle.Spread = 1.0;
		Twinkle.Speed = { 0.4, 1.8 };
		Twinkle.Life = { 0.35, 0.7 };
		Twinkle.Size = { 0.25, 0.5 };
		Twinkle.SizeEnd = 0.2;
		Twinkle.Color = FLinearColor::White;
		Twinkle.ColorEnd = Rainbow;
		Twinkle.Intensity = 3.2;
		Twinkle.Jitter = 1.1;
		Twinkle.InheritCm = InheritCm;
		return Twinkle;
	}

	FKartFxEmit RocketAfterburner(const FVector& Forward, const FVector& InheritCm)
	{
		static const FLinearColor Hot = Hex(TEXT("ffd68a"));
		static const FLinearColor Ember = Hex(TEXT("ff3d14"));
		FKartFxEmit Fire;
		Fire.Field = EField::Fire;
		Fire.Direction = -Forward;
		Fire.Spread = 0.16;
		Fire.Speed = { 5.0, 9.0 };
		Fire.Life = { 0.14, 0.3 };
		Fire.Size = { 0.7, 1.05 };
		Fire.SizeEnd = 0.25;
		Fire.Color = Hot;
		Fire.ColorEnd = Ember;
		Fire.Intensity = 3.0;
		Fire.Jitter = 0.12;
		Fire.InheritCm = InheritCm;
		return Fire;
	}

	FKartFxEmit RocketCoreFlame(const FKartFxEmit& Afterburner)
	{
		static const FLinearColor White = Hex(TEXT("f4f8ff"));
		static const FLinearColor Blue = Hex(TEXT("7ab8ff"));
		FKartFxEmit Core = Afterburner;
		Core.Speed = { 3.0, 5.0 };
		Core.Life = { 0.08, 0.14 };
		Core.Size = { 0.55, 0.7 };
		Core.SizeEnd = 0.5;
		Core.Color = White;
		Core.ColorEnd = Blue;
		Core.Intensity = 5.0;
		Core.Jitter = 0.03;
		return Core;
	}

	FKartFxEmit RocketSmokeTrail(const FVector& Forward, const FVector& InheritCm)
	{
		static const FLinearColor Light = Hex(TEXT("8e8882"));
		static const FLinearColor Dark = Hex(TEXT("5a5550"));
		FKartFxEmit Smoke;
		Smoke.Field = EField::Smoke;
		Smoke.Direction = FVector(-Forward.X, -Forward.Y, 0.3).GetSafeNormal();
		Smoke.Spread = 0.4;
		Smoke.Speed = { 1.0, 2.5 };
		Smoke.Life = { 0.6, 1.0 };
		Smoke.Size = { 0.45, 0.7 };
		Smoke.SizeEnd = 2.6;
		Smoke.Color = Light;
		Smoke.ColorEnd = Dark;
		Smoke.Jitter = 0.2;
		Smoke.InheritCm = InheritCm;
		return Smoke;
	}
}

void UKartFxComponent::UpdateItemFx(double Time, bool bPrism, bool bRocket)
{
	const FVector Forward = GetOwner()->GetActorForwardVector();
	const FVector Inherit = Velocity() * ItemInherit;

	// Prism: a rainbow-shimmer shell, rising rainbow twinkles and a sparkle trail.
	const FLinearColor Rainbow = FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(Time * 300.0, 256.0)), 190, 255);
	AuraShell->SetVisibility(bPrism && !bRocket);
	if (AuraMaterial && bPrism)
	{
		AuraMaterial->SetVectorParameterValue(Param::Color, Rainbow);
		const double Breathe = 1.0 + 0.04 * FMath::Sin(Time * 9.0);
		AuraShell->SetRelativeScale3D(AuraShellScale * Breathe);
	}
	UKartFxSubsystem::UpdateStream(AuraFlares, PrismTwinkles(Rainbow, Inherit * 0.7), bPrism ? PrismTwinkleRate : 0.0);

	// Rocket: the shell replaces the kart (the pawn hides the vehicle); afterburner, hot core and smoke from its tail.
	Rocket->SetVisibility(bRocket);
	const FKartFxEmit Fire = RocketAfterburner(Forward, Inherit);
	UKartFxSubsystem::UpdateStream(RocketFire, Fire, bRocket ? RocketFireRate : 0.0);
	UKartFxSubsystem::UpdateStream(RocketCore, RocketCoreFlame(Fire), bRocket ? RocketCoreRate : 0.0);
	UKartFxSubsystem::UpdateStream(RocketSmoke, RocketSmokeTrail(Forward, Inherit * 0.4), bRocket ? RocketSmokeRate : 0.0);
}

void UKartFxComponent::UpdateScreen(double Time, const FKartItemSlot* Slot, double ItemNow, bool bPrism)
{
	// The item screen effects as a post-process on this kart's own camera, so split-screen views each get theirs.
	if (!ScreenMaterial)
	{
		UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, ScreenMaterialPath);
		if (!Camera || !Parent)
		{
			return;
		}
		ScreenMaterial = UMaterialInstanceDynamic::Create(Parent, this);
		Camera->PostProcessSettings.AddBlendable(ScreenMaterial, 1.f);
	}
	// The explosion flash and ink run on the item clock, or the showcase's on the world clock.
	float FlashT = -1.f, InkAge = -1.f, InkLeft = 0.f;
	if (Slot && ItemNow < Slot->FlashUntil)
	{
		FlashT = static_cast<float>(1.0 - (Slot->FlashUntil - ItemNow) / ExplosionFlashSeconds);
	}
	else if (Time - Showcase.FlashStart < ExplosionFlashSeconds)
	{
		FlashT = static_cast<float>((Time - Showcase.FlashStart) / ExplosionFlashSeconds);
	}
	if (Slot && ItemNow < Slot->InkUntil)
	{
		InkLeft = static_cast<float>(Slot->InkUntil - ItemNow);
		InkAge = static_cast<float>(FMath::Max(0.0, InkSeconds - InkLeft));
	}
	else if (Time >= Showcase.InkStart && Time < Showcase.InkUntil)
	{
		InkLeft = static_cast<float>(Showcase.InkUntil - Time);
		InkAge = static_cast<float>(Time - Showcase.InkStart);
	}
	const double HitT = (Time - FlashStart) / HitFlashSeconds;
	const double Hit = HitT < 0.0 || HitT >= 1.0 ? 0.0 : FlashPeak * (1.0 - HitT) * (1.0 - HitT);
	// Lightning: two strobes, the second softer.
	const double ZapT = (Time - ZapStart) / ZapSeconds;
	double Zap = 0.0;
	if (ZapT >= 0.0 && ZapT < 1.0)
	{
		Zap = FMath::Max(FMath::Exp(-ZapT * 18.0), 0.6 * FMath::Exp(-FMath::Square((ZapT - 0.35) * 9.0)));
	}
	ScreenMaterial->SetScalarParameterValue(Param::Prism, bPrism ? 1.f : 0.f);
	ScreenMaterial->SetScalarParameterValue(Param::FlashT, FlashT);
	ScreenMaterial->SetScalarParameterValue(Param::InkAge, InkAge);
	ScreenMaterial->SetScalarParameterValue(Param::InkLeft, InkLeft);
	ScreenMaterial->SetScalarParameterValue(Param::Hit, static_cast<float>(Hit));
	ScreenMaterial->SetVectorParameterValue(Param::HitColor, FlashTint);
	ScreenMaterial->SetScalarParameterValue(Param::Zap, static_cast<float>(Zap));
}

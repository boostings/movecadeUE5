#include "Games/ArcadeFraming.h"

#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

namespace ArcadeFraming
{
	namespace
	{
		/** Never wider than this vertically, however narrow the screen (degrees). */
		constexpr double MaxVerticalFov = 120.0;
		constexpr double MinShare = 0.01;
	}

	double ViewportAspect(const UWorld* World)
	{
		FVector2D Size(DesignAspect, 1.0);
		if (const UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr)
		{
			Viewport->GetViewportSize(Size);
		}
		return Size.X > 0.0 && Size.Y > 0.0 ? Size.X / Size.Y : DesignAspect;
	}

	double HorizontalFov(double VerticalFovDeg, double Aspect)
	{
		return 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFovDeg) / 2.0) * Aspect));
	}

	void SetVerticalFov(UCameraComponent& Camera, double VerticalFovDeg)
	{
		const double Aspect = Camera.AspectRatio > 0.f ? Camera.AspectRatio : DesignAspect;
		Camera.SetFieldOfView(static_cast<float>(HorizontalFov(VerticalFovDeg, Aspect)));
	}

	double VerticalFovForWidth(double HalfWidth, double Distance, double Aspect, double Fill)
	{
		const double TanHalf = HalfWidth / (FMath::Max(Distance, UE_KINDA_SMALL_NUMBER) * FMath::Max(Aspect, MinShare) * FMath::Max(Fill, MinShare));
		return FMath::Min(MaxVerticalFov, 2.0 * FMath::RadiansToDegrees(FMath::Atan(TanHalf)));
	}

	double KeepWidth(double DesignFov, double Aspect, double KeepShare)
	{
		const double DesignTan = FMath::Tan(FMath::DegreesToRadians(DesignFov) / 2.0);
		const double NeededTan = DesignTan * DesignAspect * KeepShare / FMath::Max(Aspect, MinShare);
		if (NeededTan <= DesignTan)
		{
			return DesignFov;
		}
		return FMath::Min(MaxVerticalFov, 2.0 * FMath::RadiansToDegrees(FMath::Atan(NeededTan)));
	}
}

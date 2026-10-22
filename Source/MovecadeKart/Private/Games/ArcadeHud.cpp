#include "Games/ArcadeHud.h"

#include "Algo/StableSort.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "UnrealClient.h"

FArcadeHudView FArcadeHudView::Capture(const APlayerController* Viewer)
{
	const ULocalPlayer* Player = Viewer ? Viewer->GetLocalPlayer() : nullptr;
	if (!Player || !Player->ViewportClient || !Player->ViewportClient->Viewport)
	{
		return FArcadeHudView();
	}
	FSceneViewProjectionData Data;
	if (!Player->GetProjectionData(Player->ViewportClient->Viewport, Data))
	{
		return FArcadeHudView();
	}
	const FIntPoint Size = Player->ViewportClient->Viewport->GetSizeXY();
	return FromMatrix(Data.ComputeViewProjectionMatrix(), Data.GetConstrainedViewRect(), FVector2D(Size.X, Size.Y));
}

FArcadeHudView FArcadeHudView::FromMatrix(const FMatrix& InViewProjection, const FIntRect& InViewRect, const FVector2D& InViewportSize)
{
	FArcadeHudView View;
	View.ViewProjection = InViewProjection;
	View.ViewRect = InViewRect;
	View.ViewportSize = InViewportSize;
	View.bValid = InViewportSize.X > 0.0 && InViewportSize.Y > 0.0 && InViewRect.Width() > 0 && InViewRect.Height() > 0;
	return View;
}

bool FArcadeHudView::Project(const FVector& World, FVector2D& OutViewport01) const
{
	FVector2D Pixels;
	if (!bValid || !FSceneView::ProjectWorldToScreen(World, ViewRect, ViewProjection, Pixels))
	{
		return false;
	}
	OutViewport01 = Pixels / ViewportSize;
	return true;
}

bool FArcadeHudView::ProjectToViewRect(const FVector& World, FVector2D& OutRect01) const
{
	FVector2D Pixels;
	if (!bValid || !FSceneView::ProjectWorldToScreen(World, ViewRect, ViewProjection, Pixels))
	{
		return false;
	}
	OutRect01 = (Pixels - FVector2D(ViewRect.Min)) / FVector2D(ViewRect.Size());
	return true;
}

const TArray<int32>& FArcadeStandingsOrder::Update(int32 Count, TFunctionRef<int64(int32 Seat)> KeyOf)
{
	check(Count >= 0);
	bool bChanged = Keys.Num() != Count;
	Keys.SetNum(Count, EAllowShrinking::No);
	for (int32 Seat = 0; Seat < Count; ++Seat)
	{
		const int64 Key = KeyOf(Seat);
		bChanged |= Keys[Seat] != Key;
		Keys[Seat] = Key;
	}
	if (!bChanged && Order.Num() == Count)
	{
		return Order;
	}
	Order.SetNum(Count, EAllowShrinking::No);
	for (int32 Seat = 0; Seat < Count; ++Seat)
	{
		Order[Seat] = Seat;
	}
	Algo::StableSort(Order, [this](int32 A, int32 B) { return Keys[A] > Keys[B]; });
	return Order;
}

void FArcadeStandingsOrder::Reset()
{
	Keys.Reset();
	Order.Reset();
}

// AStackerGame: the towers on screen (block batches, trimmed shards), the sweeping block and the drop guide.

#include "Games/Stacker/StackerGame.h"

#include "Art/KartMaterialCache.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Stacker/StackerRooftop.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StackerScene.h"
#include "Store/KartCosmetics.h"

using namespace MovecadeKart::Stacker;
using namespace MovecadeKart::Stacker::Scene;

namespace
{
	/** The rows kept in the work area above the deck: the tower sinks once it is taller. */
	constexpr int32 WorkRows = 4;
	/** Only the top rows are drawn in play (the rest are below the deck); the showcase draws them all. */
	constexpr int32 VisibleRows = 16;
	/** Blocks scrolled this far below the deck (web units) are skipped. */
	constexpr double HiddenBelowY = -1.0;
	/** StackerScene.tsx: the scroll's exponential approach (per second); snapped when this close. */
	constexpr double ScrollRate = 8.0;
	constexpr double ScrollSnap = 1e-3;
	/** The shown lean follows the rules' lean at this rate (per second), and the topple's shudder fades at this one. */
	constexpr double ShownLeanRate = 16.0;
	constexpr double ToppleDecay = 1.6;

	/** presentation.ts: the landing squash over 280 ms: a damped bounce that widens and flattens the block. */
	constexpr double SquashMs = 280.0;
	constexpr double SquashGrow = 0.2;
	constexpr double SquashWiden = 0.32;
	constexpr double SquashFlatten = 0.5;
	constexpr double SquashSink = 0.08;
	/** The bend: tilt per lean ratio, and how far the danger's shiver and a topple's shudder move and tip the top. */
	constexpr double MaxShownLeanRatio = 1.6;
	constexpr double LeanTiltDeg = 4.5;
	constexpr double ShiverTiltDeg = 2.2;
	constexpr double ShudderTiltDeg = 9.0;
	constexpr double ShiverSway = 0.05;
	constexpr double ShudderSway = 0.4;
	constexpr double ShiverFrequency = 19.0;
	constexpr double ShudderFrequency = 8.5;
	/** A short stack bends as if it were this many rows tall, so it doesn't flop. */
	constexpr int32 BendMinRows = 4;
	/** How long the newest perfect block flashes. */
	constexpr double PerfectFlashSeconds = 0.16;

	/** The sweeping block bobs, and the less width is left the more it pulses and glows. */
	constexpr double CursorBob = 0.06;
	constexpr double UrgencyWidthRange = 1.6;
	constexpr double CursorPulse = 0.05;
	constexpr double CursorRollPerLean = 0.12;
	constexpr double CursorPitchPerLean = 0.1;
	constexpr double MaxCursorLeanRatio = 1.5;

	/** One block look (Scene::EBlockBatch order): the lane's colour shaded and mixed toward white, and its surface. */
	struct FBlockPaint
	{
		bool bLaneColor;
		float Shade;
		float WhiteMix;
		double Roughness;
		double Metallic;
		double Emissive;
	};

	/**
	 * Even and odd rows alternate shades; StackerScene.tsx lerps perfect blocks .45 toward white (less here, where the
	 * lit panel already reads bright); the showcase's losers go slate; the newest perfect flashes.
	 */
	constexpr FBlockPaint BlockPaints[MovecadeKart::Stacker::Scene::BlockLooks] = {
		{ true, 0.95f, 0.f, 0.42, 0.25, 0.0 },
		{ true, 0.74f, 0.f, 0.45, 0.25, 0.0 },
		{ true, 1.f, 0.24f, 0.32, 0.25, 0.14 },
		{ false, 1.f, 0.f, 0.6, 0.2, 0.0 },
		{ true, 1.f, 0.55f, 0.3, 0.1, 0.9 },
	};
	const TCHAR* const DimmedBlockColor = TEXT("#4f5566");

	/** The sweeping block's motion and glow: rates per second, glow 0..1 mixing the lane colour toward white. */
	struct FCursorLook
	{
		static constexpr double BobRate = 6.0;
		static constexpr double PulseRate = 12.0;
		static constexpr double GlowBase = 0.45;
		static constexpr double GlowSwing = 0.3;
		static constexpr double GlowRate = 7.0;
		static constexpr double GlowRatePerUrgency = 9.0;
		static constexpr double GlowPerUrgency = 0.2;
		static constexpr float GlowWhite = 0.3f;
		static constexpr double EmissiveBase = 0.12;
		static constexpr double EmissivePerGlow = 0.3;
		static constexpr double EmissiveUrgencyBase = 0.5;
	};

	/** The guide sits a hair above the top block; the cut ghost is a slab most of a block tall. */
	constexpr double GuideLift = 0.012;
	constexpr double GuideKeepThickness = 0.012;
	constexpr double GuideCutHeightShare = 0.9;
	constexpr double GuideDepthShare = 0.96;
	/** An overhang thinner than this isn't worth showing (the rules make no shard under .02 either). */
	constexpr double MinShownOverhang = 0.02;
	constexpr float GuideKeepOpacity = 0.38f;
	constexpr float GuideCutOpacity = 0.32f;
	const FLinearColor GuideShadowColor(0.01f, 0.01f, 0.02f);
	/** Over bright: the keep span blooms green when the drop would be perfect. */
	const FLinearColor GuidePerfectColor(0.55f, 1.6f, 1.1f);
	constexpr float GuidePerfectOpacity = 0.55f;
	constexpr float GuidePerfectFlicker = 0.2f;
	constexpr double GuidePerfectRate = 14.0;

	/** A shard as wide as this or wider is a real chunk: it gets the debris cue, louder and lower the wider it is. */
	constexpr double DebrisMinWidth = 0.3;
	struct FDebrisCue
	{
		static constexpr float VolumeBase = 0.5f;
		static constexpr float VolumePerWidth = 0.5f;
		static constexpr float PitchBase = 1.15f;
		static constexpr float PitchDrop = 0.35f;
		/** Width counts up to one unit for the volume and two for the pitch. */
		static constexpr float PitchWidthRange = 2.f;
	};
	/** A shard yaws at this share of its roll as it tumbles. */
	constexpr double ShardYawShare = 0.45;
	/** A shard never shrinks below this scale (a zero scale would park it). */
	constexpr double MinShardScale = 0.01;
	/** The lanes' shiver phases apart by this much (radians) so they don't move as one. */
	constexpr double LanePhase = 1.7;

	void SetMaterials(UMeshComponent* Mesh, std::initializer_list<UMaterialInterface*> Looks)
	{
		int32 Slot = 0;
		for (UMaterialInterface* Material : Looks)
		{
			Mesh->SetMaterial(Slot++, Material);
		}
	}

	/** A guide slab over [L, R] across the lane (web), from Y0 up Height; shown if it was hidden. */
	void PlaceSlab(UStaticMeshComponent* Slab, double LaneX, double L, double R, double Y0, double Height)
	{
		const double Width = FMath::Max(0.01, R - L);
		const FVector At = AStackerGame::W(FVector(LaneX + (L + R) / 2.0, Y0 + Height / 2.0, 0.0));
		Slab->SetWorldTransform(FTransform(FQuat::Identity, At, FVector(BlockSize.Z * GuideDepthShare, Width, Height)));
		if (!Slab->IsVisible())
		{
			Slab->SetVisibility(true);
		}
	}
}

UStaticMeshComponent* AStackerGame::MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name));
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->SetVisibility(false);
	C->RegisterComponent();
	return C;
}

UInstancedStaticMeshComponent* AStackerGame::MakeBatch(const TCHAR* Name, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Looks, bool bShadows)
{
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), Name));
	C->SetStaticMesh(Mesh);
	for (int32 i = 0; i < Looks.Num(); ++i)
	{
		C->SetMaterial(i, Looks[i]);
	}
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	// Keep each instance's previous transform, for motion vectors as the towers sway and compress.
	C->SetHasPerInstancePrevTransforms(true);
	C->RegisterComponent();
	return C;
}

void AStackerGame::BuildTowerPieces(int32 TowerIndex)
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Trim = Materials.Plain(this, Hex(TEXT("#59636e")), 0.45, 0.35);
	UMaterialInterface* Light = Materials.Plain(this, Hex(TEXT("#e6eaee")), 0.35, 0.1, 0.05);
	// Store cosmetics: a local builder's equipped block colour.
	const FLinearColor C = FKartCosmetics::StackerBlockColor(Lanes[TowerIndex].Kind == EBuilderKind::Local,
		AStackerRooftop::LaneColor(Match.Towers[TowerIndex].LaneIndex));
	UMaterialInterface* Paint[BlockLooks];
	for (int32 b = 0; b < BlockLooks; ++b)
	{
		const FBlockPaint& Look = BlockPaints[b];
		const FLinearColor Color = Look.bLaneColor ? FMath::Lerp(C * Look.Shade, FLinearColor::White, Look.WhiteMix) : Hex(DimmedBlockColor);
		Paint[b] = Materials.Plain(this, Color, Look.Roughness, Look.Metallic, Look.Emissive);
	}
	for (UMaterialInterface* Look : Paint)
	{
		Batches.Add(MakeBatch(TEXT("Blocks"), BlockMesh, { Look, Trim, Light }, true));
	}
	// Shards are short-lived and fast: no shadows.
	Batches.Add(MakeBatch(TEXT("Shards"), BlockMesh, { Paint[0], Trim, Light }, false));

	// The cursor's own instance (not the cache's): it glows with the lane's urgency every frame.
	UMaterialInstanceDynamic* CursorMat = Plain(this, C, 0.3, 0.25, 0.3);
	CursorMids.Add(CursorMat);
	UStaticMeshComponent* Cursor = MakeMesh(TEXT("Cursor"), BlockMesh);
	SetMaterials(Cursor, { CursorMat, Trim, Light });
	Cursors.Add(Cursor);
	UStaticMeshComponent* Held = MakeMesh(TEXT("Held"), BlockMesh);
	SetMaterials(Held, { CursorMat, Trim, Light });
	HeldBlocks.Add(Held);

	// The drop guide, translucent: a soft shadow on the tower's top where the block will sit, a red ghost of the
	// overhang. Each lane has its own instances: their colour and opacity change every frame.
	UMaterialInstanceDynamic* Keep = Glow(this, GuideShadowColor);
	UMaterialInstanceDynamic* Cut = Glow(this, Hex(TEXT("#ff4f4f")));
	if (Keep)
	{
		Keep->SetScalarParameterValue(TEXT("Opacity"), GuideKeepOpacity);
	}
	if (Cut)
	{
		Cut->SetScalarParameterValue(TEXT("Opacity"), GuideCutOpacity);
	}
	GuideKeepMids.Add(Keep);
	GuideCutMids.Add(Cut);
	UStaticMeshComponent* KeepMesh = MakeMesh(TEXT("GuideKeep"), CubeMesh, false);
	KeepMesh->SetMaterial(0, Keep);
	// The keep shadow sorts over the cut ghost where they meet.
	KeepMesh->SetTranslucentSortPriority(2);
	GuideKeep.Add(KeepMesh);
	UStaticMeshComponent* CutMesh = MakeMesh(TEXT("GuideCut"), CubeMesh, false);
	CutMesh->SetMaterial(0, Cut);
	CutMesh->SetTranslucentSortPriority(1);
	GuideCut.Add(CutMesh);
}

void AStackerGame::ResetBatches()
{
	for (int32 b = 0; b < BatchState.Num(); ++b)
	{
		const bool bShards = b % BatchesPerLane == static_cast<int32>(EBlockBatch::Shards);
		BatchState[b].Want.Init(Parked(), bShards ? MaxShards : MaxTowerHeight + 1);
	}
}

void AStackerGame::FlushBatches()
{
	for (int32 b = 0; b < Batches.Num(); ++b)
	{
		FlushBatch(b);
	}
}

void AStackerGame::FlushBatch(int32 Index)
{
	UInstancedStaticMeshComponent* Batch = Batches[Index];
	FBatch& State = BatchState[Index];
	if (Batch->GetInstanceCount() != State.Want.Num())
	{
		// The first flush lays out every slot; after that the count never changes.
		Batch->ClearInstances();
		Batch->AddInstances(State.Want, false, false, false);
		State.Have = State.Want;
		return;
	}
	bool bSame = true;
	for (int32 k = 0; bSame && k < State.Want.Num(); ++k)
	{
		bSame = State.Want[k].Equals(State.Have[k], 0.01);
	}
	if (bSame)
	{
		return;
	}
	// Last frame's transform of the same slot is the previous one, so the renderer has true motion vectors (no TSR
	// smear); a slot coming out of hiding starts still rather than streaking in from the parking spot.
	State.Prev.SetNum(State.Want.Num(), EAllowShrinking::No);
	for (int32 k = 0; k < State.Want.Num(); ++k)
	{
		State.Prev[k] = IsParked(State.Have[k]) ? State.Want[k] : State.Have[k];
	}
	Batch->BatchUpdateInstancesTransforms(0, State.Want, State.Prev, false, true, false);
	Swap(State.Have, State.Want);
}

void AStackerGame::UpdateTowers(float Dt)
{
	const bool bShowcase = Stage == EStackerStage::Finished;
	// The end showcase compresses each whole tower to .15 a row and lets the scroll fall back to the foundation.
	const double Compare = bShowcase ? Smooth(0.0, 1.0, GetStageAge() / CompressSeconds) : 0.0;
	const bool bDimLosers = bShowcase && GetStageAge() > CompressSeconds;
	double MaxScroll = 0.0;
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		const FTower& Tower = Match.Towers[i];
		const double Want = FMath::Max(0.0, Tower.Height() - WorkRows);
		// StackerScene.tsx: an exponential approach, never above the target; snapped once it is close, so the
		// tower and the city bands come to rest instead of creeping for ever.
		Lane.Scroll = FMath::Min(Want, Lane.Scroll + (Want - Lane.Scroll) * Approach(ScrollRate, Dt));
		if (FMath::Abs(Want - Lane.Scroll) < ScrollSnap)
		{
			Lane.Scroll = Want;
		}
		MaxScroll = FMath::Max(MaxScroll, Lane.Scroll);
		Lane.ShownLean += (Tower.Balance.LeanX - Lane.ShownLean) * Approach(ShownLeanRate, Dt);
		Lane.Topple *= FMath::Exp(-ToppleDecay * Dt);
		const double Scroll = FMath::Lerp(Lane.Scroll, 0.0, Compare);
		UpdateTowerBlocks(i, Compare, Scroll, bDimLosers);
		if (UpdateCursor(i, Scroll))
		{
			UpdateGuide(i, Scroll);
		}
		else
		{
			HideGuide(i);
		}
	}
	if (Rooftop)
	{
		Rooftop->SetScroll(MaxScroll);
	}
}

void AStackerGame::UpdateTowerBlocks(int32 TowerIndex, double Compare, double Scroll, bool bDimLosers)
{
	const FLane& Lane = Lanes[TowerIndex];
	const FTower& Tower = Match.Towers[TowerIndex];
	const double RowH = FMath::Lerp(RowHeight, CompareRowHeight, Compare);
	const int32 Len = Tower.Blocks.Num();
	const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
	const double Sway = BalanceDisplacement(Tower.Balance.LeanX);
	// The showcase holds the towers still.
	const double Live = 1.0 - Compare;
	// The tower bends with the lean (the top block sits exactly over the support the rules use), shivers as the
	// danger builds toward a topple, and shudders when it goes. Each lane's phase differs so they don't move as one.
	const double LeanRatio = FMath::Clamp(Lane.ShownLean / Limit, -MaxShownLeanRatio, MaxShownLeanRatio);
	const double Danger = FMath::Clamp(Tower.Balance.DangerMs / BalanceFailureMs, 0.0, 1.0);
	const double Shiver = Danger * Danger * FMath::Sin(Clock * ShiverFrequency + TowerIndex * LanePhase);
	const double Shudder = Lane.Topple * FMath::Sin(Clock * ShudderFrequency + TowerIndex);
	const bool bDim = bDimLosers && !Match.WinnerIds.Contains(Tower.PlayerId);
	const int32 First = Compare > 0.0 ? 0 : FMath::Max(0, Len - VisibleRows);
	const bool bFlash = Clock - Lane.PerfectAt < PerfectFlashSeconds;
	for (int32 k = First; k < Len; ++k)
	{
		const FBlock& B = Tower.Blocks[k];
		const double T = FMath::Min(1.0, B.AgeMs / SquashMs);
		const double Squash = T < 1.0 ? FMath::Pow(1.0 - T, 1.5) * FMath::Cos(3.0 * PI * T) : 0.0;
		// The sway is carried up the tower; the bend is anchored at the foundation (it never tips) and builds up it.
		const double Rise = static_cast<double>(k + 1) / Len;
		const double Bend = static_cast<double>(k) / FMath::Max(Len - 1, BendMinRows);
		const double X = Lane.LaneX + B.Center + Sway * Rise + (ShiverSway * Shiver + ShudderSway * Shudder) * Bend * Bend * Live;
		const double Y = (k - Scroll) * RowH + RowH / 2.0 - FMath::Max(0.0, Squash) * SquashSink;
		if (Y < HiddenBelowY)
		{
			continue;
		}
		const double TiltDeg = (LeanRatio * LeanTiltDeg + Shiver * ShiverTiltDeg + Shudder * ShudderTiltDeg) * Bend * Live;
		const FVector Scale(1.0 + SquashGrow * Squash, B.Width * (1.0 + SquashWiden * Squash), (1.0 - SquashFlatten * Squash) * RowH / RowHeight);
		const EBlockBatch Look = bDim ? EBlockBatch::Dimmed
			: B.bPerfect ? (bFlash && k == Len - 1 ? EBlockBatch::Flashing : EBlockBatch::Perfect)
			: (k % 2 ? EBlockBatch::Odd : EBlockBatch::Even);
		BatchState[BatchIndex(TowerIndex, Look)].Want[k] = FTransform(Tip(TiltDeg), W(FVector(X, Y, 0.0)), Scale);
	}
}

bool AStackerGame::UpdateCursor(int32 TowerIndex, double Scroll)
{
	const FLane& Lane = Lanes[TowerIndex];
	const FTower& Tower = Match.Towers[TowerIndex];
	UStaticMeshComponent* Cursor = Cursors[TowerIndex];
	// Hidden while it reloads (the cooldown) and while the tower recovers.
	const bool bCooling = Match.ElapsedMs - Tower.LastDropAtMs < DropCooldownMs;
	const bool bShow = Stage != EStackerStage::Finished && Match.Phase != EMatchPhase::Ended && !Tower.IsDone() && !bCooling
		&& Tower.Balance.RecoveryMs <= 0.0;
	if (Cursor->IsVisible() != bShow)
	{
		Cursor->SetVisibility(bShow);
	}
	if (!bShow)
	{
		return false;
	}
	const FBlock& Top = Tower.Top();
	const double Limit = BalanceLimit(Tower.Height(), Top.Width);
	using CL = FCursorLook;
	const double Y = (Top.Index + CursorRowsAbove - Scroll) * RowHeight + RowHeight / 2.0 + CursorBob * FMath::Sin(CL::BobRate * Clock + Tower.LaneIndex);
	// The narrower the tower, the more the block you can't afford to miss shouts: faster, brighter.
	const double Urgency = 1.0 - FMath::Min(1.0, (Top.Width - MinWidth) / UrgencyWidthRange);
	const double Pulse = 1.0 + CursorPulse * Urgency * FMath::Sin(Clock * CL::PulseRate);
	const double Glow = CL::GlowBase + CL::GlowSwing * FMath::Sin(Clock * (CL::GlowRate + Urgency * CL::GlowRatePerUrgency)) + Urgency * CL::GlowPerUrgency;
	const double Roll = FMath::Clamp(Tower.Balance.LeanX / Limit, -MaxCursorLeanRatio, MaxCursorLeanRatio) * CursorRollPerLean;
	const double Pitch = FMath::Clamp(Tower.Balance.LeanY / Limit, -MaxCursorLeanRatio, MaxCursorLeanRatio) * CursorPitchPerLean;
	Cursor->SetWorldTransform(FTransform(FRotator(FMath::RadiansToDegrees(Pitch), 0.0, FMath::RadiansToDegrees(Roll)),
		W(FVector(Lane.LaneX + Tower.CursorCenter, Y, 0.0)), FVector(Pulse, Top.Width * Pulse, Pulse)));
	if (UMaterialInstanceDynamic* M = CursorMids[TowerIndex])
	{
		const FLinearColor C = AStackerRooftop::LaneColor(Tower.LaneIndex);
		M->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(C, FMath::Lerp(C, FLinearColor::White, CL::GlowWhite), static_cast<float>(FMath::Clamp(Glow, 0.0, 1.0))));
		M->SetScalarParameterValue(TEXT("Emissive"), static_cast<float>(CL::EmissiveBase + CL::EmissivePerGlow * Glow * (CL::EmissiveUrgencyBase + Urgency)));
	}
	return true;
}

void AStackerGame::UpdateGuide(int32 TowerIndex, double Scroll)
{
	// The guide on the tower's top: the span this drop keeps (bright when it would be perfect) and a red ghost of
	// the overhang that will be cut.
	const FLane& Lane = Lanes[TowerIndex];
	const FTower& Tower = Match.Towers[TowerIndex];
	const FBlock& Top = Tower.Top();
	const FDropPreview Read = PreviewDrop(Tower, Tower.CursorCenter);
	const double TopSurface = (Top.Index - Scroll) * RowHeight + RowHeight / 2.0 + BlockSize.Y / 2.0 + GuideLift;
	const double CursorL = Tower.CursorCenter - Top.Width / 2.0, CursorR = Tower.CursorCenter + Top.Width / 2.0;
	const double CutY0 = TopSurface - GuideLift, CutHeight = BlockSize.Y * GuideCutHeightShare;
	UStaticMeshComponent* Keep = GuideKeep[TowerIndex];
	UStaticMeshComponent* Cut = GuideCut[TowerIndex];
	if (UMaterialInstanceDynamic* KeepMat = GuideKeepMids[TowerIndex])
	{
		KeepMat->SetVectorParameterValue(TEXT("Color"), Read.bPerfect ? GuidePerfectColor : GuideShadowColor);
		KeepMat->SetScalarParameterValue(TEXT("Opacity"), Read.bPerfect ? GuidePerfectOpacity + GuidePerfectFlicker * static_cast<float>(FMath::Sin(Clock * GuidePerfectRate)) : GuideKeepOpacity);
	}
	if (Read.bMiss)
	{
		// Nothing would stay: the whole block is the overhang.
		Keep->SetVisibility(false);
		PlaceSlab(Cut, Lane.LaneX, CursorL, CursorR, CutY0, CutHeight);
		return;
	}
	// A perfect regrows to the full width over the support, so the guide shows that rather than the overlap.
	const double L = Read.bPerfect ? Read.Support - Top.Width / 2.0 : Read.Left;
	const double R = Read.bPerfect ? Read.Support + Top.Width / 2.0 : Read.Right;
	PlaceSlab(Keep, Lane.LaneX, L, R, TopSurface - GuideKeepThickness / 2.0, GuideKeepThickness);
	if (Read.bPerfect || Top.Width - (Read.Right - Read.Left) <= MinShownOverhang)
	{
		Cut->SetVisibility(false);
		return;
	}
	// The overhang hangs on the side the block is off toward.
	const bool bRight = Tower.CursorCenter > Read.Support;
	PlaceSlab(Cut, Lane.LaneX, bRight ? Read.Right : CursorL, bRight ? CursorR : Read.Left, CutY0, CutHeight);
}

void AStackerGame::HideGuide(int32 TowerIndex)
{
	GuideKeep[TowerIndex]->SetVisibility(false);
	GuideCut[TowerIndex]->SetVisibility(false);
}

void AStackerGame::UpdateShards()
{
	// The rules stop stepping at the end: a shard still in the air would hang there through the showcase.
	if (Stage == EStackerStage::Finished)
	{
		return;
	}
	int32 Newest = LastSeenShardId;
	for (const FShard& Shard : Match.Shards)
	{
		const int32 i = TowerOfLane(Shard.LaneIndex);
		if (i == INDEX_NONE)
		{
			continue;
		}
		if (Shard.Id > LastSeenShardId)
		{
			Newest = FMath::Max(Newest, Shard.Id);
			// Debris for a real chunk: its pitch drops as it gets wider.
			if (Shard.Width >= DebrisMinWidth)
			{
				using D = FDebrisCue;
				const float Width = static_cast<float>(Shard.Width);
				Cues.Play(this, TEXT("stacker.debris"), D::VolumeBase + FMath::Min(1.f, Width) * D::VolumePerWidth,
					D::PitchBase - FMath::Min(1.f, Width / D::PitchWidthRange) * D::PitchDrop);
			}
		}
		// The shard shrinks and tumbles (rolling, and yawing at under half that) as its time runs out.
		const double Life = FMath::Max(0.0, Shard.TtlMs / Shard.DurationMs);
		const double Tumble = 1.0 - Life;
		const FVector Pos(Lanes[i].LaneX + Shard.X, (Shard.Row - Lanes[i].Scroll) * RowHeight + RowHeight / 2.0 + Shard.Y, 0.0);
		const FRotator Spin(0.0, -FMath::RadiansToDegrees(Tumble * Shard.Spin * ShardYawShare), -FMath::RadiansToDegrees(Tumble * Shard.Spin));
		const FVector Scale(FMath::Max(MinShardScale, Life), FMath::Max(MinShardScale, Shard.Width * Life), FMath::Max(MinShardScale, Life));
		BatchState[BatchIndex(i, EBlockBatch::Shards)].Want[Shard.Id % MaxShards] = FTransform(Spin, W(Pos), Scale);
	}
	LastSeenShardId = Newest;
}

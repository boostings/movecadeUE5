// AFishingGame's catch: the fish on the line (under the surface as it comes in, leaping on its runs), the reveal
// (lifted out, turned to the camera, held up, then into the bucket or back in the lake) and the weigh-in's trophies.
#include "Avatar/ArcadeAvatar.h"
#include "Components/StaticMeshComponent.h"
#include "Games/Fishing/FishingArt.h"
#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingGameInternal.h"
#include "Games/Fishing/FishingLake.h"

using namespace MovecadeKart::Fishing;
using namespace MovecadeKart::Fishing::GameMath;
namespace FA = MovecadeKart::Fishing::Art;

namespace
{
	/** A leap on a run: how long it lasts (s) and how high it goes (cm, plus more for a strong fish). */
	constexpr double JumpSeconds = 0.8;
	constexpr double JumpHeightCm = 50.0;
	constexpr double JumpStrengthCm = 60.0;
	/** The re-entry splash, this far through the leap. */
	constexpr double JumpReentry = 0.95;
	/** Under the surface inside this distance (m), rising from 14 cm deep to 2 cm by 1.5 m. */
	constexpr double ShowUnderM = 5.0;
	/** The reveal: the lift out of the water (s), its arc (cm), the toss away at the end (s) and its arc (cm). */
	constexpr double LiftSeconds = 0.5;
	constexpr double LiftArcCm = 60.0;
	constexpr double TossSeconds = 0.3;
	constexpr double TossArcCm = 70.0;
	/** Junk goes back in this far out, a little to the side (m, lateral). */
	constexpr double JunkTossM = 3.5;
	constexpr double JunkTossLateral = 0.6;
}

UStaticMesh* AFishingGame::FishMesh(int32 Species)
{
	if (!FishMeshes.IsValidIndex(Species))
	{
		return nullptr;
	}
	if (!FishMeshes[Species])
	{
		FishMeshes[Species] = FA::BuildFish(this, Species);
	}
	return FishMeshes[Species];
}

void AFishingGame::ShowFish(int32 i, int32 Species, int32 WeightG, const FTransform& At)
{
	UStaticMeshComponent* Shown = Fish[i];
	if (FishShown[i] != Species)
	{
		FishShown[i] = Species;
		Shown->SetStaticMesh(FishMesh(Species));
		FA::DressFish(Materials, this, Shown, Species);
	}
	// The meshes are 100 cm long: scale to the catch's own length.
	const double Scale = FA::LengthCm(Species, WeightG) / 100.0;
	Shown->SetWorldTransform(FTransform(At.GetRotation(), At.GetLocation(), FVector(Scale)));
	Shown->SetVisibility(true);
}

void AFishingGame::UpdateCatch(int32 i, const FLaneFrame& Frame, float Dt)
{
	const FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	bool bFish = false;
	if (Frame.bFinished && A.BiggestSpecies >= 0)
	{
		// The weigh-in: everyone holds up their biggest catch; the winner holds it high in one hand.
		const bool bWinner = Match.WinnerIds.Contains(A.PlayerId);
		const FTransform At = bWinner
			? FTransform(Facing(Lane.Show.TurnYaw + 90.0) + FRotator(70.0, 0.0, 0.0), Avatars[i]->BoneWorld(TEXT("leftHand")) + FVector(0.0, 0.0, 12.0))
			: FTransform(Facing(Lane.Show.TurnYaw + 90.0) + FRotator(0.0, 0.0, 6.0 * FMath::Sin(Clock * 5.0 + i)), HoldPoint(i));
		ShowFish(i, A.BiggestSpecies, A.BiggestG, At);
		bFish = true;
	}
	else if (A.State == EAnglerState::Fighting)
	{
		bFish = ShowFightingFish(i, Frame, Dt);
	}
	else if (A.State == EAnglerState::Landed)
	{
		bFish = ShowLandedCatch(i, Frame, Dt);
	}
	Fish[i]->SetVisibility(bFish);
}

bool AFishingGame::ShowFightingFish(int32 i, const FLaneFrame& Frame, float Dt)
{
	const FFight& F = Match.Anglers[i].Fight;
	const double JumpAge = Clock - Lanes[i].Show.JumpAt;
	if (JumpAge >= 0.0 && JumpAge < JumpSeconds)
	{
		// The leap: up out of the water nose first, over, and back in, with a splash each way.
		const double U = JumpAge / JumpSeconds;
		const double Height = JumpHeightCm + JumpStrengthCm * (Frame.Caught ? Frame.Caught->Strength : 0.5);
		const FVector P = Frame.Out + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * U) * Height - 10.0);
		ShowFish(i, F.Species, F.WeightG, FTransform(FRotator(FMath::Lerp(55.0, -55.0, U), 180.0 + F.Lateral * 40.0, 20.0 * FMath::Sin(Clock * 20.0)), P));
		if (JumpAge + Dt >= JumpSeconds * JumpReentry && JumpAge < JumpSeconds * JumpReentry)
		{
			Splash(Frame.Out, 0.8);
		}
		if (JumpAge < Dt)
		{
			Splash(Frame.Out, 0.7);
		}
		return true;
	}
	if (F.DistanceM < ShowUnderM)
	{
		// Just under the surface, wriggling harder the fresher it is.
		const double Depth = -14.0 + 12.0 * Smooth(ShowUnderM, 1.5, F.DistanceM);
		ShowFish(i, F.Species, F.WeightG, FTransform(FRotator(0.0, F.Lateral * 35.0 + 14.0 * FMath::Sin(Clock * (10.0 + 8.0 * F.Stamina)), 0.0), Frame.Out + FVector(25.0, 0.0, Depth)));
		return true;
	}
	return false;
}

bool AFishingGame::ShowLandedCatch(int32 i, const FLaneFrame& Frame, float Dt)
{
	const FLane& Lane = Lanes[i];
	const FCatch& C = Match.Anglers[i].LastCatch;
	const int32 N = LaneCount();
	const double T = Frame.InState;
	const double HoldSeconds = LandedHoldMs / 1000.0;
	const bool bJunk = Frame.bJunk;
	// Out of the water to the hands (junk dangles from the right hand), then into the bucket or back in the lake.
	const FVector From = AFishingLake::WaterPoint(i, N, LandM, 0.0) + FVector(0.0, 0.0, -10.0);
	const FVector HoldAt = bJunk ? Avatars[i]->BoneWorld(TEXT("rightHand")) + FVector(0.0, 0.0, -18.0) : HoldPoint(i);
	const double Lift = EaseOut(T / LiftSeconds);
	const double TossT = FMath::Clamp((T - (HoldSeconds - TossSeconds)) / TossSeconds, 0.0, 1.0);
	const FVector TossTo = bJunk ? AFishingLake::WaterPoint(i, N, JunkTossM, JunkTossLateral) : AFishingLake::BucketSpot(i, N) + FVector(0.0, 0.0, 28.0);
	FVector P = FMath::Lerp(From, HoldAt, Lift) + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * FMath::Min(1.0, T / LiftSeconds)) * LiftArcCm);
	if (TossT > 0.0)
	{
		P = FMath::Lerp(HoldAt, TossTo, TossT) + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * TossT) * TossArcCm);
	}
	// It wags on the way up and settles in the hands, side on to the camera.
	const double Wag = 14.0 * FMath::Sin(Clock * 16.0) * (1.0 - 0.6 * Lift);
	const FRotator Hang = bJunk ? FRotator(-60.0 + Wag * 0.3, Lane.Show.TurnYaw, 0.0) : Facing(Lane.Show.TurnYaw + 90.0) + FRotator(0.0, Wag, 0.0);
	ShowFish(i, C.Species, C.WeightG, FTransform(FMath::Lerp(FRotator(-70.0, Lane.Show.TurnYaw, 0.0), Hang, Lift), P));
	if (TossT > 0.0 && TossT < 1.0 && T + Dt >= HoldSeconds - Dt)
	{
		Splash(TossTo, bJunk ? 0.6 : 0.15, bJunk);
	}
	return TossT < 1.0;
}

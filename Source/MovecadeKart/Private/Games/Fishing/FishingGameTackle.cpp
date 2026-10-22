// AFishingGame's tackle: each lane's angler avatar, the rod (handle and a blank in bending segments), the line (a
// chain of thin pieces that sags) and the bobber on its spring.
#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "Components/StaticMeshComponent.h"
#include "Games/Fishing/FishingArt.h"
#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingGameInternal.h"
#include "Games/Fishing/FishingLake.h"

using namespace MovecadeKart::Fishing;
using namespace MovecadeKart::Fishing::GameTuning;
using namespace MovecadeKart::Fishing::GameMath;
namespace FA = MovecadeKart::Fishing::Art;

namespace
{
	/** The avatars are a little smaller than the karts' drivers, so four fit the dock with room for the rods. */
	constexpr float AnglerScale = 0.84f;
	/** Each blank segment's length (cm) and its radius taper from butt to tip. */
	constexpr double SegmentCm = 40.0;
	constexpr double ButtRadius = 1.05;
	constexpr double TipRadius = 0.38;
	/** The bend grows toward the tip: segment k carries Bend x ((k + 1) / RodSegments)^BendCurve. */
	constexpr double BendCurve = 1.7;
	/** The handle sits this far behind the hand, and the blank starts this far in front of it (cm). */
	constexpr double HandleBehindCm = 9.0;
	constexpr double BlankAheadCm = 31.0;
	/** The line's thickness (relative to the 1 cm line mesh) and the height it lies at on the water (cm). */
	constexpr double LineThickness = 0.6;
	constexpr double LineOnWaterZ = 0.6;
	/** The line goes under the surface only over its last stretch (the part near the lure). */
	constexpr double LineSinksFrom = 0.92;
	/** An idle line hangs this far under the tip, and the bobber this far under the line's end (cm). */
	constexpr double HangCm = 55.0;
	constexpr double BobberUnderEndCm = 12.0;
	/** The floating bobber's spring: stiffness and damping (per s^2, per s). */
	constexpr double BobberStiffness = 120.0;
	constexpr double BobberDamping = 9.0;
	/** A bite tips the bobber over (degrees). */
	constexpr double BiteTiltDeg = 18.0;
	/** The rod follows its target pitch (1/s): snappy through the cast, steadier otherwise; and its bend. */
	constexpr double RodPitchRateCasting = 30.0;
	constexpr double RodPitchRate = 12.0;
	constexpr double RodBendRate = 14.0;
	/** The rod's aim follows the line sideways at this fraction of its angle. */
	constexpr double AimFollow = 0.6;
	/** Holding a fish up in both hands, the rod leans on the bucket. */
	constexpr double LeanPitchDeg = 62.0;
	constexpr double LeanYawDeg = -20.0;
	constexpr double LeanBendDeg = 3.0;
}

void AFishingGame::BuildLanes()
{
	using namespace MovecadeKart::Art;
	const int32 N = Seats.Num();
	UStaticMesh* HandleMesh = FA::BuildRodHandle(this);
	UStaticMesh* SegmentMesh = FA::BuildRodSegment(this);
	UStaticMesh* LineMesh = FA::BuildRodLine(this);
	UStaticMesh* BobberMesh = FA::BuildBobber(this);
	UMaterialInterface* Cork = Materials.Surface(this, { Hex(TEXT("#c89a62")), TEXT("gravelly_sand"), 0.15, 0.5, 0.6, 0.4, 0.9 });
	UMaterialInterface* ReelBody = Materials.Plain(this, Hex(TEXT("#2a2c33")), 0.3, 0.6);
	UMaterialInterface* Metal = Materials.Plain(this, Hex(TEXT("#c9ccd2")), 0.25, 0.95);
	UMaterialInterface* Guide = Materials.Plain(this, Hex(TEXT("#d8d8dc")), 0.2, 1.0);
	UMaterialInterface* White = Materials.Plain(this, Hex(TEXT("#f4f1ea")), 0.35);
	UMaterialInterface* Stick = Materials.Plain(this, Hex(TEXT("#2a2c33")), 0.5);
	// A bright, solid line so it reads against the glittering water.
	UMaterialInterface* LineMaterial = Materials.GlowOpaque(this, Hex(TEXT("#fff4dc")) * 1.6f);
	FishMeshes.SetNum(Species().Num());
	for (int32 i = 0; i < N; ++i)
	{
		const FLinearColor Color = AFishingLake::LaneColor(i);
		UArcadeAvatarComponent* Avatar = NewObject<UArcadeAvatarComponent>(this, MakeUniqueObjectName(this, UArcadeAvatarComponent::StaticClass(), *FString::Printf(TEXT("Angler%d"), i)));
		Avatar->AvatarScale = AnglerScale;
		Avatar->SetupAttachment(RootComponent);
		Avatar->RegisterComponent();
		Avatar->SetLook(Seats[i].Look);
		Avatar->SetWorldLocation(AFishingLake::AnglerSpot(i, N));
		Avatars.Add(Avatar);

		UStaticMeshComponent* Handle = MakeMesh(TEXT("RodHandle"), HandleMesh);
		Handle->SetMaterialByName(TEXT("Cork"), Cork);
		Handle->SetMaterialByName(TEXT("Reel"), ReelBody);
		Handle->SetMaterialByName(TEXT("Metal"), Metal);
		Handles.Add(Handle);
		// The blank in the lane's colour, darkened toward graphite.
		UMaterialInterface* Blank = Materials.Plain(this, FMath::Lerp(Color, Hex(TEXT("#1c1c22")), 0.35f), 0.25, 0.3);
		for (int32 k = 0; k < RodSegments; ++k)
		{
			UStaticMeshComponent* Segment = MakeMesh(TEXT("RodBlank"), SegmentMesh);
			Segment->SetMaterialByName(TEXT("Blank"), Blank);
			Segment->SetMaterialByName(TEXT("Guide"), Guide);
			RodPieces.Add(Segment);
		}
		for (int32 k = 0; k < LineSegments; ++k)
		{
			UStaticMeshComponent* Piece = MakeMesh(TEXT("Line"), LineMesh, false);
			Piece->SetMaterial(0, LineMaterial);
			LinePieces.Add(Piece);
		}
		UStaticMeshComponent* Bobber = MakeMesh(TEXT("Bobber"), BobberMesh);
		Bobber->SetMaterialByName(TEXT("Top"), Materials.Plain(this, Color, 0.3, 0.0, 0.25));
		Bobber->SetMaterialByName(TEXT("Bottom"), White);
		Bobber->SetMaterialByName(TEXT("Stick"), Stick);
		Bobbers.Add(Bobber);
		Fish.Add(MakeMesh(TEXT("Catch"), nullptr));
		FishShown.Add(-1);
	}
}

UStaticMeshComponent* AFishingGame::MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows)
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

FVector AFishingGame::HeadOf(int32 Lane) const
{
	return AFishingLake::AnglerSpot(Lane, LaneCount()) + FVector(0.0, 0.0, HeadHeightCm);
}

FVector AFishingGame::BobberOf(int32 Lane) const
{
	return Bobbers.IsValidIndex(Lane) && Bobbers[Lane] ? Bobbers[Lane]->GetComponentLocation() : FVector::ZeroVector;
}

FVector AFishingGame::HoldPoint(int32 Lane) const
{
	const UArcadeAvatarComponent* Avatar = Avatars.IsValidIndex(Lane) ? Avatars[Lane].Get() : nullptr;
	if (!Avatar)
	{
		return FVector::ZeroVector;
	}
	return Avatar->GetComponentLocation() + Avatar->GetComponentRotation().RotateVector(FVector(HoldForwardCm, 0.0, HoldUpCm));
}

void AFishingGame::UpdateTackle(int32 i, FLaneFrame& Frame, float Dt)
{
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	// The rod: in the right hand, pitched in the facing frame toward the line, bending with the load.
	const double PitchRate = A.State == EAnglerState::Casting ? RodPitchRateCasting : RodPitchRate;
	Lane.Show.RodPitch += (Frame.Pitch - Lane.Show.RodPitch) * (1.0 - FMath::Exp(-PitchRate * Dt));
	Lane.Show.RodBend += (Frame.Bend - Lane.Show.RodBend) * (1.0 - FMath::Exp(-RodBendRate * Dt));
	const FVector Hand = Avatars[i]->BoneWorld(TEXT("rightHand"));
	const double AimYaw = A.IsLineOut() ? FMath::RadiansToDegrees(FMath::Atan2(Frame.Out.Y - Frame.Spot.Y, FMath::Max(50.0, Frame.Out.X - Frame.Spot.X))) * AimFollow : 0.0;
	Frame.bRodDown = Frame.bShowing && !Frame.bJunk;
	if (Frame.bRodDown)
	{
		const FVector Bucket = AFishingLake::BucketSpot(i, LaneCount());
		UpdateRod(i, Bucket + FVector(30.0, 10.0, 10.0), FRotator::ZeroRotator, LeanPitchDeg, LeanYawDeg, LeanBendDeg, Bucket + FVector(0.0, 0.0, 400.0), Frame.bRod);
	}
	else
	{
		// Before the avatar's first pose the hand bone is unplaced: hold the rod where the hand will be.
		const FVector Base = Hand.IsNearlyZero() ? Frame.Spot + FVector(40.0, 0.0, 110.0) : Hand;
		const FVector Toward = Frame.End.IsNearlyZero() ? Lane.Show.Tip + FVector(0.0, 0.0, -100.0) : Frame.End;
		UpdateRod(i, Base, Facing(Lane.Show.TurnYaw), Lane.Show.RodPitch, AimYaw, Lane.Show.RodBend, Toward, Frame.bRod);
	}
	if (A.State == EAnglerState::Idle || A.State == EAnglerState::Lost || Frame.bRodDown)
	{
		// Hanging from this frame's tip (the pose placed it under last frame's).
		Frame.End = Lane.Show.Tip - FVector(0.0, 0.0, HangCm);
		Frame.Bob = Frame.End - FVector(0.0, 0.0, BobberUnderEndCm);
	}
	UpdateLine(i, Lane.Show.Tip, Frame.End, Frame.Sag, Frame.bLine && !Frame.bRodDown && !Frame.End.IsNearlyZero());
	UpdateBobber(i, Frame, Dt);
}

void AFishingGame::UpdateRod(int32 i, const FVector& Base, const FRotator& Face, double Pitch, double Yaw, double Bend, const FVector& Toward, bool bShow)
{
	const FVector Butt = (Face + FRotator(Pitch, Yaw, 0.0)).Vector();
	UStaticMeshComponent* Handle = Handles[i];
	Handle->SetVisibility(bShow);
	Handle->SetWorldLocationAndRotation(Base - Butt * HandleBehindCm, Butt.Rotation());
	// The blank bends toward the line, more at the tip; Bend is the total curve in degrees.
	FVector Axis = FVector::CrossProduct(Butt, (Toward - Base).GetSafeNormal());
	if (Axis.SizeSquared() < 1e-6)
	{
		Axis = FVector::CrossProduct(Butt, -FVector::UpVector);
	}
	Axis.Normalize();
	FVector P = Base + Butt * BlankAheadCm;
	for (int32 k = 0; k < RodSegments; ++k)
	{
		const double Share = FMath::Pow(static_cast<double>(k + 1) / RodSegments, BendCurve);
		const FVector Along = FQuat(Axis, FMath::DegreesToRadians(Bend * Share)).RotateVector(Butt);
		const double Radius = FMath::Lerp(ButtRadius, TipRadius, static_cast<double>(k) / RodSegments);
		UStaticMeshComponent* Segment = RodPieces[i * RodSegments + k];
		Segment->SetVisibility(bShow);
		Segment->SetWorldTransform(FTransform(FRotationMatrix::MakeFromXZ(Along, FVector::UpVector).ToQuat(), P, FVector(SegmentCm / 100.0, Radius, Radius)));
		P += Along * SegmentCm;
	}
	Lanes[i].Show.Tip = P;
}

void AFishingGame::UpdateLine(int32 i, const FVector& From, const FVector& To, double Sag, bool bShow)
{
	FVector Prev = From;
	for (int32 k = 0; k < LineSegments; ++k)
	{
		UStaticMeshComponent* Piece = LinePieces[i * LineSegments + k];
		if (!bShow)
		{
			Piece->SetVisibility(false);
			continue;
		}
		// A parabola under the straight line, Sag cm deep at the middle.
		const double U = static_cast<double>(k + 1) / LineSegments;
		FVector P = FMath::Lerp(From, To, U) - FVector(0.0, 0.0, Sag * 4.0 * U * (1.0 - U));
		// The line lies on the water rather than through it, until it goes in near the end.
		if (U < LineSinksFrom)
		{
			P.Z = FMath::Max(P.Z, LineOnWaterZ);
		}
		const FVector D = P - Prev;
		const double Len = D.Size();
		if (Len < 0.01)
		{
			Piece->SetVisibility(false);
			continue;
		}
		Piece->SetWorldTransform(FTransform(D.Rotation(), Prev, FVector(Len / 100.0, LineThickness, LineThickness)));
		Piece->SetVisibility(true);
		Prev = P;
	}
}

void AFishingGame::UpdateBobber(int32 i, const FLaneFrame& Frame, float Dt)
{
	FLane& Lane = Lanes[i];
	const EAnglerState State = Match.Anglers[i].State;
	// In the air, hanging, or floating on its spring.
	const bool bFloating = State == EAnglerState::Waiting || State == EAnglerState::Bite || State == EAnglerState::Fighting;
	FVector Bob = Frame.Bob;
	if (bFloating)
	{
		const double Acc = -BobberStiffness * (Lane.Show.BobZ - Frame.Rest) - BobberDamping * Lane.Show.BobV;
		Lane.Show.BobV += Acc * Dt;
		Lane.Show.BobZ += Lane.Show.BobV * Dt;
		Bob.Z = Lane.Show.BobZ;
	}
	UStaticMeshComponent* Bobber = Bobbers[i];
	Bobber->SetVisibility(Frame.bBobber && !Frame.bRodDown && !Bob.IsNearlyZero());
	const FRotator Tilt = bFloating ? FRotator(4.0 * FMath::Sin(Clock * 1.7 + i) + (State == EAnglerState::Bite ? BiteTiltDeg : 0.0), 0.0, 5.0 * FMath::Sin(Clock * 1.3 + i)) : FRotator::ZeroRotator;
	Bobber->SetWorldLocationAndRotation(Bob, Tilt);
}

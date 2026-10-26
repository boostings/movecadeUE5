// ABowlingGame: the lane camera (presentation.ts cameraPlan). The address tripod behind the bowler, the impact and
// result shots over the deck; the port adds a drift after the ball, the slow-motion strike cam and an orbit over a
// celebrating deck. Each phase picks a shot; the camera eases toward it and the impact jolt rides on top.
#include "Games/Bowling/BowlingGame.h"

#include "BowlingGameTuning.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Games/ArcadeFraming.h"
#include "Games/Bowling/BowlingAlley.h"

using namespace MovecadeKart::Bowling;
using namespace MovecadeKart::Bowling::Tuning;

/** Where the camera wants to be (web units), its vertical field of view (degrees) and how fast it gets there (1/s). */
struct FBowlingCameraShot
{
	FVector Pos;
	FVector Look;
	double Fov = 39.0;
	double Response = 3.05;
};

namespace
{
	constexpr double DeckZ = ABowlingAlley::DeckZ;
	/** The address tripod, behind and above the bowler. */
	const FVector AddressPos(0.0, 3.65, 6.55);
	const FVector AddressLook(0.0, 0.86, -9.55);
	/** The intro's push starts low over the lane, looking at the rack. */
	const FVector IntroPos(2.4, 2.6, -2.0);
	const FVector IntroLook(0.0, 0.6, -13.6);
	constexpr double IntroResponse = 8.0;
	/** The strike cam takes over this far down a strike-bound roll. */
	constexpr double StrikeCamFrom = 0.62;
	/** A plain roll cuts from the impact shot to the result shot this far into the settle, and holds it into the reset. */
	constexpr double ImpactShare = 0.55;
	constexpr double ResultHoldShare = 0.58;
	/** The lens eases toward a new field of view at this rate, whatever the shot's response. */
	constexpr double FovResponse = 4.0;
	/** The position ease never steps more than this at once (a hitch doesn't snap the camera). */
	constexpr double MaxEaseStep = 1.0 / 20.0;
	/** The jolt's wobble frequencies (rad/s) per axis; the depth axis moves half as much. */
	constexpr double JoltRateX = 71.0;
	constexpr double JoltRateY = 53.0;
	constexpr double JoltRateZ = 61.0;

	/** A fixed framing (web units): the camera and its look-at, each sliding across with where the ball meets the rack. */
	struct FShotKey
	{
		FVector Pos;
		double PosPerEndX = 0.0;
		FVector Look;
		double LookPerEndX = 0.0;
		double Fov = 39.0;
		double Response = 3.05;
	};

	// presentation.ts cameraPlan's fixed shots, and the port's strike cam.
	const FShotKey AddressKey{ AddressPos, 0.0, AddressLook, 0.0, 39.0, 3.05 };
	const FShotKey ImpactKey{ FVector(0.0, 2.55, -6.85), 0.0, FVector(0.0, 0.58, -13.6), 0.14, 39.0, 3.05 };
	const FShotKey ResultKey{ FVector(0.0, 3.3, -6.3), 0.0, FVector(0.0, 0.48, -13.6), 0.16, 40.0, 5.2 };
	/** A strike-bound ball: the low cam at the deck as time slows (the roll's end, then the pins flying). */
	const FShotKey StrikeRollingKey{ FVector(-1.25, 0.75, DeckZ + 3.4), 0.3, FVector(0.0, 0.45, DeckZ - 0.4), 0.5, 40.0, 4.2 };
	const FShotKey StrikeSettlingKey{ FVector(-1.25, 0.8, DeckZ + 3.2), 0.3, FVector(0.0, 0.55, DeckZ - 0.6), 0.4, 42.0, 2.0 };

	/** The roll: the tripod leans after the ball, travelling down the lane as it goes. */
	const FShotKey RollingKey{ FVector(0.0, 3.4, 6.2), 0.0, FVector(0.0, 0.7, -10.5), 0.1, 37.0, 3.05 };
	constexpr double RollingTravelZ = 3.6;

	/** A celebration: a low swing across the deck (an arc of Sweep radians from Start), rising as it goes. */
	struct FOrbit
	{
		double Start, Sweep, ExtraSeconds, RadiusX, RadiusZ, Height, Rise, RiseSeconds;
	};
	constexpr FOrbit CelebrationOrbit{ -0.55, 1.1, 0.4, 3.4, 4.6, 1.45, 0.4, 2.0 };
	const FVector CelebrationLook(0.0, 0.9, DeckZ - 0.6);
	constexpr double CelebrationFov = 44.0;
	constexpr double CelebrationResponse = 3.4;

	/** The results: a slow sway over the house. */
	constexpr double FinishedSway = 1.6;
	constexpr double FinishedSwayRate = 0.15;
	const FShotKey FinishedKey{ FVector(0.0, 3.1, 4.6), 0.0, FVector(0.0, 1.0, -6.0), 0.0, 46.0, 1.2 };

	/** A phone held upright (ForAspect): the lane's half width the lens fits at the look point (web units), the
	 *  narrowest lens, how far (share of the frame's half height) the look point drops, and the wide shots' share. */
	constexpr double PortraitLaneHalfWidth = 1.5;
	constexpr double PortraitMinFov = 20.0;
	constexpr double PortraitLookDropShare = 0.3;
	constexpr double PortraitWideKeepShare = 0.55;

	FBowlingCameraShot Frame(const FShotKey& Key, double EndX)
	{
		return { Key.Pos + FVector(EndX * Key.PosPerEndX, 0.0, 0.0), Key.Look + FVector(EndX * Key.LookPerEndX, 0.0, 0.0), Key.Fov, Key.Response };
	}

	FBowlingCameraShot IntroShot(double Age)
	{
		// A slow push down the lane toward the rack before the first ball.
		const double T = Smooth(0.0, 1.0, Age / IntroSeconds);
		return { FMath::Lerp(IntroPos, AddressPos, T), FMath::Lerp(IntroLook, AddressLook, T), AddressKey.Fov, IntroResponse };
	}

	FBowlingCameraShot RollingShot(double T, double EndX)
	{
		FBowlingCameraShot Shot = Frame(RollingKey, EndX);
		Shot.Pos.Z = RollingKey.Pos.Z - RollingTravelZ * Smooth(0.0, 1.0, T);
		return Shot;
	}

	FBowlingCameraShot CelebrationShot(double Since)
	{
		const FOrbit& O = CelebrationOrbit;
		const double A = O.Start + O.Sweep * Smooth(0.0, CelebrationSeconds + O.ExtraSeconds, Since);
		return { FVector(FMath::Sin(A) * O.RadiusX, O.Height + O.Rise * Smooth(0.0, O.RiseSeconds, Since), DeckZ + FMath::Cos(A) * O.RadiusZ),
			CelebrationLook, CelebrationFov, CelebrationResponse };
	}

	FBowlingCameraShot FinishedShot(double Clock)
	{
		FBowlingCameraShot Shot = Frame(FinishedKey, 0.0);
		Shot.Pos.X = FinishedSway * FMath::Sin(Clock * FinishedSwayRate);
		return Shot;
	}
}

FBowlingCameraShot ABowlingGame::ForAspect(FBowlingCameraShot Want, double Aspect) const
{
	// Held upright, the lane runs up the screen: the lane shots narrow the lens until the lane (and a little of the
	// next one) spans the width at the shot's look point, and drop the look point so the rack rides high with the
	// bowler and the near lane below; the celebration and the results keep the middle of their 16:9 frame.
	if (!ArcadeFraming::IsPortrait(Aspect))
	{
		return Want;
	}
	const bool bWide = Phase == EBowlingPhase::Finished || (Phase == EBowlingPhase::Reset && IsCelebration(LastBeat) && Clock - LastBeatAt < CelebrationSeconds);
	if (bWide)
	{
		Want.Fov = ArcadeFraming::KeepWidth(Want.Fov, Aspect, PortraitWideKeepShare);
		return Want;
	}
	const double Distance = FVector::Dist(Want.Pos, Want.Look);
	Want.Fov = FMath::Max(PortraitMinFov, ArcadeFraming::VerticalFovForWidth(PortraitLaneHalfWidth, Distance, Aspect));
	Want.Look.Y -= Distance * FMath::Tan(FMath::DegreesToRadians(Want.Fov / 2.0)) * PortraitLookDropShare;
	return Want;
}

FBowlingCameraShot ABowlingGame::PickCameraShot() const
{
	const double EndX = bHasShot ? Shot.EndX * ABowlingAlley::VisualPinScale : 0.0;
	const double Age = GetPhaseAge();
	switch (Phase)
	{
	case EBowlingPhase::Intro:
		return IntroShot(Age);
	case EBowlingPhase::Rolling:
	{
		const double T = FMath::Clamp(Age / RollSeconds, 0.0, 1.0);
		return bStrikeBound && T > StrikeCamFrom ? Frame(StrikeRollingKey, EndX) : RollingShot(T, EndX);
	}
	case EBowlingPhase::Settling:
		if (bStrikeBound)
		{
			return Frame(StrikeSettlingKey, EndX);
		}
		return Age / SettleSeconds <= ImpactShare ? Frame(ImpactKey, EndX) : Frame(ResultKey, EndX);
	case EBowlingPhase::Reset:
	{
		const double Since = Clock - LastBeatAt;
		if (IsCelebration(LastBeat) && Since < CelebrationSeconds)
		{
			return CelebrationShot(Since);
		}
		return Age / ResetSeconds < ResultHoldShare ? Frame(ResultKey, EndX) : Frame(AddressKey, 0.0);
	}
	case EBowlingPhase::Finished:
		return FinishedShot(Clock);
	default:
		return Frame(AddressKey, 0.0);
	}
}

FVector ABowlingGame::CameraJolt() const
{
	// The impact's jolt: a quick decaying wobble on top of the framing (held still while paused).
	const double ShakeAge = Clock - ShakeAt;
	if (ShakeAge < 0.0 || ShakeAge >= ShakeSeconds || bPaused)
	{
		return FVector::ZeroVector;
	}
	const double Amp = ShakeMetres * FMath::Square(1.0 - ShakeAge / ShakeSeconds);
	return FVector(FMath::Sin(ShakeAge * JoltRateX), FMath::Sin(ShakeAge * JoltRateY + 1.3), FMath::Sin(ShakeAge * JoltRateZ + 2.1) * 0.5) * Amp;
}

void ABowlingGame::UpdateCamera(float Dt)
{
	if (!Camera)
	{
		return;
	}
	const FBowlingCameraShot Want = ForAspect(PickCameraShot(), ArcadeFraming::ViewportAspect(GetWorld()));
	if (!bCamPlaced)
	{
		CamPos = Want.Pos;
		CamLook = Want.Look;
		CamFov = Want.Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-Want.Response * FMath::Min<double>(Dt, MaxEaseStep));
	CamPos += (Want.Pos - CamPos) * K;
	CamLook += (Want.Look - CamLook) * K;
	CamFov += (Want.Fov - CamFov) * (1.0 - FMath::Exp(-FovResponse * Dt));
	const FVector Jolt = CameraJolt();
	const FVector From = LaneToWorld(CamPos.X + Jolt.X, CamPos.Y + Jolt.Y, CamPos.Z + Jolt.Z);
	const FVector To = LaneToWorld(CamLook.X, CamLook.Y, CamLook.Z);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	ArcadeFraming::SetVerticalFov(*Camera->GetCameraComponent(), CamFov);
}

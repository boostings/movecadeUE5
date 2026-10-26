// ADartsGame: the camera director (cameraDirector.ts plus DartsScene's overrides): the intro's push in, the home
// shot, the aim over the thrower's shoulder, the reaction at the board after a landing, the results, and the big
// hits' punch and shake.

#include "Games/Darts/DartsGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Games/ArcadeFraming.h"
#include "Games/Darts/DartsGameTuning.h"
#include "Games/Darts/DartsPub.h"

using namespace MovecadeKart::Darts;
using MovecadeKart::Darts::Tuning::Smooth;
using MovecadeKart::Darts::Tuning::ToWorld;

namespace
{
	/** A shot: where the camera is, what it looks at (web metres) and its vertical field of view (degrees). */
	struct FDartsShot
	{
		FVector Pos;
		FVector Look;
		double Fov = 44.0;
	};

	/** Home: square on to the board from behind the oche, drifting side to side. */
	constexpr double HomeY = 1.1;
	constexpr double HomeZ = 6.7;
	constexpr double HomeLookY = 0.55;
	constexpr double HomeFov = 44.0;
	constexpr double HomeDriftM = 0.16;
	constexpr double HomeDriftRate = 0.24;

	/** The intro starts wide and high off to the right and pushes most of the way to home. */
	const FVector IntroPos(1.7, 1.7, 8.2);
	constexpr double IntroLookZ = -0.75;
	constexpr double IntroFov = 46.0;
	constexpr double IntroPush = 0.6;

	/** Over the throwing shoulder, high enough that the board sits clear of the head (the web's aim shot, lifted and
	 *  opened: at TV distance the board has to read whole while the thrower lines up). Offsets from the oche. */
	const FVector AimOffset(1.02, 2.1, 2.3);
	const FVector AimLookOffset(-0.12, -0.12, 0.0);
	constexpr double AimFov = 40.0;

	/** The reaction after a landing: held this long (longer on the visit's last dart), easing in over ReactSettle. */
	constexpr double ReactSeconds = 0.95;
	constexpr double ReactLastSeconds = 1.3;
	constexpr double ReactSettleSeconds = 0.9;
	/** A hard cut into the reaction for its first frames. */
	constexpr double ReactCutSeconds = 0.05;
	constexpr double ReactFov = 32.0;
	constexpr double ReactZoomFov = 2.0;
	/** The reaction camera follows the landing at this share (the rest is the board's centre), off to the right and
	 *  up, this far from the board, easing in by ReactPushIn; it looks at this share of the way to the landing. */
	constexpr double ReactFollow = 0.4;
	constexpr double ReactRightM = 0.55;
	constexpr double ReactUpM = 0.3;
	constexpr double ReactDistanceM = 3.5;
	constexpr double ReactPushInM = 0.4;
	constexpr double ReactLookShare = 0.45;
	/** The shake's second axis runs a little out of phase with the first. */
	constexpr double ShakePhaseY = 1.3;

	/** The results: a wider shot off to the right, the board and the thrower in frame. */
	const FVector ResultsPos(2.15, 1.5, 6.4);
	const FVector ResultsLook(-0.3, 0.55, 0.3);
	constexpr double ResultsFov = 40.0;

	/** The camera chases its shot at this rate (1/s), stepped at no less than 20 Hz. */
	constexpr double FollowRate = 8.0;
	constexpr double MaxFollowStep = 1.0 / 20.0;
	/** The big hit's punch decays at this rate (1/s), kicks the field of view in by up to PunchFov degrees. */
	constexpr double PunchDecay = 5.5;
	constexpr double PunchFov = 6.0;
	constexpr double MinFov = 10.0;
	/** Shake (web metres) per unit of board wobble and of punch, and its rates (rad/s). */
	constexpr double WobbleShakeM = 0.006;
	constexpr double PunchShakeM = 0.012;
	constexpr double ShakeRateX = 53.0;
	constexpr double ShakeRateY = 47.0;

	FDartsShot HomeShot(double Clock)
	{
		return { FVector(HomeDriftM * FMath::Sin(Clock * HomeDriftRate), HomeY, HomeZ), FVector(0.0, HomeLookY, 0.0), HomeFov };
	}

	/** The reaction: a close look at the dart in the board from off its right shoulder (in front of the thrower, so
	 *  nobody's head fills the frame), easing in. */
	FDartsShot ReactionShot(const FDartLanding& Landing, double Since)
	{
		const double Settle = Smooth(0.0, 1.0, Since / ReactSettleSeconds);
		const FVector At = ADartsPub::BoardPoint(Landing.X, Landing.Y);
		return { FVector(At.X * ReactFollow + ReactRightM, At.Y * ReactFollow + ADartsPub::BoardY * (1.0 - ReactFollow) + ReactUpM, ReactDistanceM - ReactPushInM * Settle),
			FMath::Lerp(FVector(0.0, ADartsPub::BoardY, 0.0), At, ReactLookShare), ReactFov - ReactZoomFov * Settle };
	}

	/**
	 * A phone held upright (9:19.5): the board fills the width, sitting just below the middle so the scoreboard has the top and the prompts the bottom. The home and aim shots set the lens so
	 * BoardHalfWidth (the board and its number ring, with a sliver of wall) spans the screen; the reaction keeps the whole board in; the intro and results keep the middle of their frame.
	 */
	namespace Portrait
	{
		constexpr double BoardHalfWidth = 0.72;
		/** The look point rises this share of the board's half width above its centre, so the board sits just
		 *  below the middle, clear of the scoreboard (the visit card and the controls fit under it). */
		constexpr double LookRaiseShare = 0.18;
		constexpr double ReactHalfWidth = 0.66;
		constexpr double IntroKeepShare = 0.45;
		constexpr double ResultsKeepShare = 0.6;
	}

	enum class EShotKind : uint8 { Home, Aim, Reaction, Intro, Results };

	/** The shot re-framed for the screen's shape (unchanged in landscape). */
	FDartsShot ForAspect(FDartsShot Shot, EShotKind Kind, double Aspect)
	{
		if (!ArcadeFraming::IsPortrait(Aspect))
		{
			return Shot;
		}
		using namespace Portrait;
		const double Distance = FVector::Dist(Shot.Pos, Shot.Look);
		switch (Kind)
		{
		case EShotKind::Home:
		case EShotKind::Aim:
			Shot.Fov = ArcadeFraming::VerticalFovForWidth(BoardHalfWidth, Distance, Aspect);
			Shot.Look.Y += BoardHalfWidth * LookRaiseShare;
			break;
		case EShotKind::Reaction:
			Shot.Fov = FMath::Max(Shot.Fov, ArcadeFraming::VerticalFovForWidth(ReactHalfWidth, Distance, Aspect));
			break;
		case EShotKind::Intro:
			Shot.Fov = ArcadeFraming::KeepWidth(Shot.Fov, Aspect, IntroKeepShare);
			break;
		case EShotKind::Results:
			Shot.Fov = ArcadeFraming::KeepWidth(Shot.Fov, Aspect, ResultsKeepShare);
			break;
		}
		return Shot;
	}
}

void ADartsGame::UpdateCamera(float Dt)
{
	const double Aspect = ArcadeFraming::ViewportAspect(GetWorld());
	FDartsShot Shot = HomeShot(Clock);
	EShotKind Kind = EShotKind::Home;
	bool bCut = false;
	bReacting = false;
	switch (Stage)
	{
	case EDartsStage::Intro:
	{
		const double T = Smooth(0.0, 1.0, GetStageAge() / IntroSeconds);
		Shot.Pos = FMath::Lerp(IntroPos, Shot.Pos, T * IntroPush);
		Shot.Look = FMath::Lerp(FVector(0.0, ADartsPub::BoardY, IntroLookZ), Shot.Look, T);
		Shot.Fov = FMath::Lerp(IntroFov, HomeFov, T);
		Kind = EShotKind::Intro;
		break;
	}
	case EDartsStage::Match:
	{
		const FDartsSeat* Thrower = GetThrower();
		const bool bBotUp = Thrower && Thrower->Kind == EThrowerKind::Bot;
		const double Since = Clock - LastLandingAt;
		const double Hold = LastLanding.DartIndex == DartsPerRound - 1 ? ReactLastSeconds : ReactSeconds;
		// A thrower winding up for the next dart (a bot drawing back, a local hand on the grip) ends the reaction
		// early, so the camera is back over the shoulder by the time the dart leaves.
		const bool bWindingUp = Local.bGripped || (bBotUp && Clock >= BotReadyAt - Tuning::BotWindupSeconds);
		FVector2D Aim;
		if (Since < Hold && Match.Flights.Num() == 0 && !bWindingUp)
		{
			Shot = ReactionShot(LastLanding, Since);
			bCut = Since < ReactCutSeconds;
			bReacting = true;
			Kind = EShotKind::Reaction;
		}
		else if ((GetReticle(Aim) || bBotUp) && Match.Phase == EMatchPhase::Playing)
		{
			const FVector Oche(ADartsPub::ThrowerX, ADartsPub::FloorY, ADartsPub::OcheZ);
			Shot = { Oche + AimOffset, FVector(0.0, ADartsPub::BoardY, 0.0) + AimLookOffset, AimFov };
			Kind = EShotKind::Aim;
		}
		break;
	}
	case EDartsStage::Finished:
		Shot = { ResultsPos, ResultsLook, ResultsFov };
		Kind = EShotKind::Results;
		break;
	}
	Shot = ForAspect(Shot, Kind, Aspect);
	if (!bCamPlaced || bCut)
	{
		CamPos = Shot.Pos;
		CamLook = Shot.Look;
		CamFov = Shot.Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-FollowRate * FMath::Min<double>(Dt, MaxFollowStep));
	CamPos += (Shot.Pos - CamPos) * K;
	CamLook += (Shot.Look - CamLook) * K;
	CamFov += (Shot.Fov - CamFov) * K;
	// The impact's shake (impactFx.ts), decaying with the board's wobble, plus the big hit's punch: a quick push in
	// and a harder shake that settle in a quarter second.
	Punch *= FMath::Exp(-PunchDecay * Dt);
	const FVector Shake = FVector(FMath::Sin(Clock * ShakeRateX), FMath::Sin(Clock * ShakeRateY + ShakePhaseY), 0.0) * (Wobble * WobbleShakeM + Punch * PunchShakeM);
	const FVector From = ToWorld(CamPos + Shake);
	const FVector To = ToWorld(CamLook);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	ArcadeFraming::SetVerticalFov(*Camera->GetCameraComponent(), FMath::Max(MinFov, CamFov - PunchFov * Punch));
}

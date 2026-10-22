// ASumoGame's pilots: a phone's tilt, the local stick and keys, the house bots, and the fixed rules step they feed.

#include "Games/Sumo/SumoGame.h"

#include "GameFramework/PlayerController.h"
#include "Games/SArcadeOverlay.h"
#include "Games/Sumo/SumoInput.h"
#include "Games/Sumo/SumoPresentation.h"
#include "InputCoreTypes.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Sumo;
using namespace MovecadeKart::Sumo::Presentation;

namespace
{
	/** The left stick's dead zone (of its full throw). */
	constexpr double StickDeadZone = 0.15;
	/** How fast the keys' tilt ramps toward full (1/s): like a tilting palm rather than a switch. */
	constexpr double KeyTiltRate = 14.0;

	double KeyAxis(const APlayerController& Viewer, const FKey& Plus, const FKey& PlusAlt, const FKey& Minus, const FKey& MinusAlt)
	{
		const double Up = Viewer.IsInputKeyDown(Plus) || Viewer.IsInputKeyDown(PlusAlt) ? 1.0 : 0.0;
		const double Down = Viewer.IsInputKeyDown(Minus) || Viewer.IsInputKeyDown(MinusAlt) ? 1.0 : 0.0;
		return Up - Down;
	}

	bool WasDashPressed(const APlayerController& Viewer)
	{
		return Viewer.WasInputKeyJustPressed(EKeys::SpaceBar) || Viewer.WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)
			|| Viewer.WasInputKeyJustPressed(EKeys::LeftMouseButton) || Viewer.WasInputKeyJustPressed(EKeys::Gamepad_RightTrigger)
			|| Viewer.WasInputKeyJustPressed(EKeys::Enter);
	}
}

namespace MovecadeKart::Sumo
{
	FVector2D PhoneTilt(const Net::FSensorFrame& Frame)
	{
		double RollDeg = 0.0;
		double PitchDeg = 0.0;
		if (Frame.bHasQuaternion)
		{
			// The attitude's up axis (the screen normal): its lean across and away from the player.
			const double X = Frame.Quaternion.X, Y = Frame.Quaternion.Y, Z = Frame.Quaternion.Z, W = Frame.Quaternion.W;
			const double Nx = 2.0 * (X * Z + W * Y);
			const double Ny = 2.0 * (Y * Z - W * X);
			const double Nz = 1.0 - 2.0 * (X * X + Y * Y);
			RollDeg = FMath::RadiansToDegrees(FMath::Atan2(Nx, Nz));
			PitchDeg = FMath::RadiansToDegrees(FMath::Atan2(Ny, Nz));
		}
		else
		{
			RollDeg = Frame.Gamma;
			PitchDeg = -Frame.Beta;
		}
		const FVector2D Deg(PitchDeg, RollDeg);
		const double Magnitude = Deg.Size();
		const double Amount = FMath::Clamp((Magnitude - PhoneTiltDeadDeg) / (PhoneTiltFullDeg - PhoneTiltDeadDeg), 0.0, 1.0);
		return Magnitude > 1e-6 ? Deg / Magnitude * Amount : FVector2D::ZeroVector;
	}
}

void ASumoGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	const int32 Slot = Match.IndexOf(PlayerId);
	if (!Pilots.IsValidIndex(Slot))
	{
		return;
	}
	FSumoPilot& Pilot = Pilots[Slot];
	Pilot.PhoneTilt = PhoneTilt(Frame);
	Pilot.PhoneAt = Clock;
}

void ASumoGame::PressDash(const FString& PlayerId)
{
	const int32 Slot = Match.IndexOf(PlayerId);
	if (Pilots.IsValidIndex(Slot) && Stage == ESumoStage::Match && !bPaused)
	{
		Pilots[Slot].bDashQueued = true;
	}
}

void ASumoGame::ReadLocal(FSumoPilot& Pilot, float Dt) const
{
	if (!Viewer || SArcadeOverlay::IsMenuOpen(GetWorld()))
	{
		Pilot.LocalTilt = FVector2D::ZeroVector;
		return;
	}
	// The rules' plane: X away from the camera (stick up, W), Y to its right (stick right, D).
	FVector2D Stick(Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftY), Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftX));
	const FVector2D Keys(KeyAxis(*Viewer, EKeys::W, EKeys::Up, EKeys::S, EKeys::Down), KeyAxis(*Viewer, EKeys::D, EKeys::Right, EKeys::A, EKeys::Left));
	if (Stick.Size() < StickDeadZone)
	{
		Stick = FVector2D::ZeroVector;
	}
	const FVector2D KeyTarget = Keys.IsNearlyZero() ? FVector2D::ZeroVector : Keys.GetSafeNormal();
	const FVector2D Target = Stick.IsNearlyZero() ? KeyTarget : Stick;
	Pilot.LocalTilt += (Target - Pilot.LocalTilt) * (1.0 - FMath::Exp(-KeyTiltRate * Dt));
	if (WasDashPressed(*Viewer))
	{
		Pilot.bDashQueued = true;
	}
}

FInput ASumoGame::InputFor(int32 Slot)
{
	FSumoPilot& Pilot = Pilots[Slot];
	FInput In;
	switch (Pilot.Kind)
	{
	case ESumoPilot::Local:
		In.Tilt = Pilot.LocalTilt;
		break;
	case ESumoPilot::Phone:
		// A quiet phone coasts rather than holding its last lean.
		In.Tilt = Clock - Pilot.PhoneAt <= PhoneQuietSeconds ? Pilot.PhoneTilt : FVector2D::ZeroVector;
		break;
	case ESumoPilot::Bot:
		In = ThinkBot(Match, Slot, Pilot.Brain);
		break;
	}
	In.bDash |= Pilot.bDashQueued;
	Pilot.bDashQueued = false;
	return In;
}

void ASumoGame::StepFixed()
{
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		Pilots[i].PrevPos = Match.Pucks[i].Pos;
		Pilots[i].PrevVel = Match.Pucks[i].Vel;
		SetInput(Match, i, InputFor(i));
	}
	StepMatch(Match);
	for (const FRingEvent& E : Match.Events)
	{
		if (E.Id <= LastEventId)
		{
			continue;
		}
		LastEventId = E.Id;
		Present(E);
		if (OnEvent)
		{
			OnEvent(E);
		}
	}
}

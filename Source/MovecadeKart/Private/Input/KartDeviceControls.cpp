#include "Input/KartDeviceControls.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "Input/DeviceMotionSim.h"
#include "Net/RoomProtocol.h"

#define LOCTEXT_NAMESPACE "KartDeviceControls"

namespace MovecadeKart::Input
{
	namespace
	{
		/** The web kart controller's rate (index.ts sendHz). */
		constexpr double KartSendHz = 30.0;

		// The autoplay: phone-sim.mjs --game kart (a 60 degree sweep every 4 s, GAS held, one ITEM at 3 s).
		constexpr double SimSweepDeg = 60.0;
		constexpr double SimSweepPeriod = 4.0;
		constexpr double SimGasAt = 0.5;
		constexpr double SimItemAt = 3.0;
		constexpr double SimItemHold = 0.15;
		/** On-screen steering goes out at the frame rate at most, and only when it moved this much. */
		constexpr double SteerSendStep = 0.01;
		/** A frame hitch doesn't jump the wheel: the ramp advances at most this many seconds per tick. */
		constexpr double MaxSteerStep = 0.1;

		TSharedPtr<FJsonObject> KartInput(const TCHAR* Field, bool bValue)
		{
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(Field, bValue);
			return Data;
		}
	}

	void FKartPhoneSeatInput::ApplyFrame(const Net::FSensorFrame& Frame, double Sensitivity, double Now)
	{
		Steering = Net::SteeringFromGamma(Frame.Gamma, Sensitivity);
		LastSensorTime = Now;
		++SensorFrames;
	}

	void FKartPhoneSeatInput::ApplyEvent(const Net::FKartInputEvent& Event, double Now)
	{
		if (Event.Accelerating) bAccelerating = *Event.Accelerating;
		if (Event.Reversing) bReversing = *Event.Reversing;
		if (Event.Drifting) bDrifting = *Event.Drifting;
		if (Event.Steering)
		{
			// Button steering took over: frames that stopped no longer mean "centre the wheel".
			Steering = *Event.Steering;
			SensorFrames = 0;
		}
		if (Event.bUseItem && Now - LastItemTime >= Net::ItemDebounceSeconds)
		{
			LastItemTime = Now;
			bItemPending = true;
		}
	}

	FKartInput FKartPhoneSeatInput::Read(double Now) const
	{
		FKartInput Out;
		const bool bTiltStale = SensorFrames > 0 && Now - LastSensorTime > Net::SensorStaleSeconds;
		Out.SteeringX = bTiltStale ? 0.0 : Steering;
		Out.bForward = bAccelerating;
		Out.bBackward = bReversing;
		Out.bJump = bDrifting;
		return Out;
	}

	bool FKartPhoneSeatInput::ConsumeUseItem()
	{
		const bool bPending = bItemPending;
		bItemPending = false;
		return bPending;
	}

	FKartDeviceSeat::FKartDeviceSeat(TFunction<double()> InClock)
		: Clock(InClock ? MoveTemp(InClock) : TFunction<double()>([]() { return FPlatformTime::Seconds(); }))
	{
	}

	void FKartDeviceSeat::SendFrame(const Net::FSensorFrame& Frame)
	{
		State.ApplyFrame(Frame, Sensitivity, Clock());
	}

	void FKartDeviceSeat::SendEvent(const FString& Event, const TSharedPtr<FJsonObject>& Data)
	{
		if (Data.IsValid() && Event.Equals(Net::KartInputEvent, ESearchCase::CaseSensitive))
		{
			State.ApplyEvent(Net::ParseKartInput(*Data), Clock());
		}
	}

	FKartInput FKartDeviceSeat::Read()
	{
		FKartInput Out = State.Read(Clock());
		Out.bUseItem = State.ConsumeUseItem();
		return Out;
	}

	FKartDeviceControls::FKartDeviceControls()
	{
		Layout.FrameKind = EDeviceFrameKind::Wheel;
		Layout.SendHz = KartSendHz;
		Layout.Buttons = {
			{ EDeviceButton::Gas, LOCTEXT("Gas", "GAS"), EButtonAnchor::BottomRight, true },
			{ EDeviceButton::Drift, LOCTEXT("Drift", "DRIFT"), EButtonAnchor::RightRaised, false },
			{ EDeviceButton::Brake, LOCTEXT("Brake", "BRAKE"), EButtonAnchor::BottomLeft, true },
			{ EDeviceButton::Item, LOCTEXT("Item", "ITEM"), EButtonAnchor::LeftRaised, false },
		};
		Layout.Hint = LOCTEXT("Hint", "Turn the phone like a wheel to steer");

		TouchLayout = Layout;
		TouchLayout.Buttons = {
			{ EDeviceButton::SteerLeft, FText::GetEmpty(), EButtonAnchor::BottomLeft, true },
			{ EDeviceButton::SteerRight, FText::GetEmpty(), EButtonAnchor::BottomLeftInner, true },
			{ EDeviceButton::Item, LOCTEXT("Item", "ITEM"), EButtonAnchor::LeftRaised, false },
			{ EDeviceButton::Gas, LOCTEXT("Gas", "GAS"), EButtonAnchor::BottomRight, true },
			{ EDeviceButton::Brake, LOCTEXT("Brake", "BRAKE"), EButtonAnchor::BottomRightInner, false },
			{ EDeviceButton::Drift, LOCTEXT("Drift", "DRIFT"), EButtonAnchor::RightRaised, false },
		};
		TouchLayout.Hint = LOCTEXT("TouchHint", "Hold the arrows to steer");
	}

	void FKartDeviceControls::SetTouchSteering(bool bOn, double Now, IDeviceInputSink& Sink)
	{
		if (bOn == bTouchSteering)
		{
			return;
		}
		bTouchSteering = bOn;
		++LayoutRevision;
		bLeftHeld = bRightHeld = false;
		Steering = 0.0;
		// Coming in, button steering takes over the seat (the last tilt is dropped); going out, the wheel the buttons
		// last turned is centred before the frames resume.
		if (bOn || SentSteering != 0.0)
		{
			SendSteering(Sink);
		}
	}

	void FKartDeviceControls::SendSteering(IDeviceInputSink& Sink)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("steeringInput"), Steering);
		Sink.SendEvent(Net::KartInputEvent, Data);
		SentSteering = Steering;
	}

	void FKartDeviceControls::Tick(double Now, IDeviceInputSink& Sink)
	{
		const double Dt = LastTickAt >= 0.0 ? FMath::Clamp(Now - LastTickAt, 0.0, MaxSteerStep) : 0.0;
		LastTickAt = Now;
		if (!bTouchSteering)
		{
			return;
		}
		// Right is negative in the kart convention (SteeringFromGamma's INVERT_STEERING).
		const double Target = (bLeftHeld ? 1.0 : 0.0) - (bRightHeld ? 1.0 : 0.0);
		Steering = FMath::FInterpConstantTo(Steering, Target, Dt, 1.0 / SteerRampSeconds);
		// At the phone's send rate while it moves; the resting value always goes out at once.
		const bool bDue = LastSentAt < 0.0 || Now - LastSentAt >= 1.0 / KartSendHz;
		const bool bArrived = Steering == Target && Steering != SentSteering;
		if ((bDue && FMath::Abs(Steering - SentSteering) >= SteerSendStep) || bArrived)
		{
			LastSentAt = Now;
			SendSteering(Sink);
		}
	}

	void FKartDeviceControls::OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink)
	{
		// KartController.tsx: each control sends only the field that changed; ITEM sends a press, never a release.
		switch (Button)
		{
		case EDeviceButton::Gas: Sink.SendEvent(Net::KartInputEvent, KartInput(TEXT("isAccelerating"), bDown)); break;
		case EDeviceButton::Brake: Sink.SendEvent(Net::KartInputEvent, KartInput(TEXT("isReversing"), bDown)); break;
		case EDeviceButton::Drift: Sink.SendEvent(Net::KartInputEvent, KartInput(TEXT("isDrifting"), bDown)); break;
		case EDeviceButton::Item:
			if (bDown)
			{
				Sink.SendEvent(Net::KartInputEvent, KartInput(TEXT("useItem"), true));
			}
			break;
		case EDeviceButton::SteerLeft: bLeftHeld = bDown && bTouchSteering; break;
		case EDeviceButton::SteerRight: bRightHeld = bDown && bTouchSteering; break;
		default:
			break;
		}
	}

	TSharedPtr<IDeviceMotionScript> FKartDeviceControls::MakeAutoplayScript() const
	{
		return MakeShared<FTimedDeviceScript>([](double T)
		{
			return FDeviceGestures::Wheel(SimSweepDeg * FMath::Sin(2.0 * UE_DOUBLE_PI * T / SimSweepPeriod));
		}, TArray<FTimedDeviceScript::FPress>{
			{ SimGasAt, EDeviceButton::Gas, true },
			{ SimItemAt, EDeviceButton::Item, true },
			{ SimItemAt + SimItemHold, EDeviceButton::Item, false },
		}, /*Period*/ 0.0);
	}
}

#undef LOCTEXT_NAMESPACE

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
		if (Event.Steering) Steering = *Event.Steering;
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
		Layout.Orientation = EDeviceOrientationHint::Landscape;
		Layout.Buttons = {
			{ EDeviceButton::Gas, LOCTEXT("Gas", "GAS"), EButtonAnchor::BottomRight, true },
			{ EDeviceButton::Drift, LOCTEXT("Drift", "DRIFT"), EButtonAnchor::RightRaised, false },
			{ EDeviceButton::Brake, LOCTEXT("Brake", "BRAKE"), EButtonAnchor::BottomLeft, true },
			{ EDeviceButton::Item, LOCTEXT("Item", "ITEM"), EButtonAnchor::LeftRaised, false },
		};
		Layout.Hint = LOCTEXT("Hint", "Turn the phone like a wheel to steer");
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

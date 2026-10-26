#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Sim/KartSimTypes.h"

namespace MovecadeKart::Net { struct FKartInputEvent; }

/**
 * The kart on the device: tilt-to-steer in landscape (the phone's wheel mode) and on-screen GAS, BRAKE, DRIFT and
 * ITEM, the buttons of the web kart controller.
 */
namespace MovecadeKart::Input
{
	/**
	 * One seat's phone controls as the room host keeps them (URoomHostSubsystem FSeatInput, RoomHostInbound.cpp):
	 * steering from each frame's wheel angle (Net::SteeringFromGamma, sample-and-hold, neutral once frames stop for
	 * Net::SensorStaleSeconds), the pedals and drift from kart-input events, ITEM debounced to one press per
	 * Net::ItemDebounceSeconds. The device seat keeps its own copy, so a local race reads it the way the room's race
	 * reads GetSeatInput / ConsumeUseItem.
	 */
	struct MOVECADEKART_API FKartPhoneSeatInput
	{
		double Steering = 0.0;
		bool bAccelerating = false;
		bool bReversing = false;
		bool bDrifting = false;
		bool bItemPending = false;
		double LastItemTime = -1000.0;
		double LastSensorTime = -1000.0;
		int32 SensorFrames = 0;

		void ApplyFrame(const Net::FSensorFrame& Frame, double Sensitivity, double Now);
		void ApplyEvent(const Net::FKartInputEvent& Event, double Now);
		/** GetSeatInput's FKartInput (bUseItem is left to ConsumeUseItem). */
		FKartInput Read(double Now) const;
		/** True once per accepted ITEM press. */
		bool ConsumeUseItem();
	};

	/** The device seat of a local race: a sink with the room's semantics (frames and kart-input into FKartPhoneSeatInput). */
	class MOVECADEKART_API FKartDeviceSeat : public IDeviceInputSink
	{
	public:
		/** Clock: seconds (FPlatformTime by default; the tests drive their own). */
		explicit FKartDeviceSeat(TFunction<double()> InClock = nullptr);

		virtual void SendFrame(const Net::FSensorFrame& Frame) override;
		virtual void SendEvent(const FString& Event, const TSharedPtr<FJsonObject>& Data) override;

		void SetSensitivity(double InSensitivity) { Sensitivity = InSensitivity; }
		/** The seat's controls now, with bUseItem set once per ITEM press (the race turns it into an edge). */
		FKartInput Read();
		const FKartPhoneSeatInput& GetState() const { return State; }

	private:
		TFunction<double()> Clock;
		FKartPhoneSeatInput State;
		double Sensitivity = 1.0;
	};

	/** The kart controller: frames in wheel mode at 30 Hz, the four buttons as kart-input events. */
	class MOVECADEKART_API FKartDeviceControls : public IDeviceControlScheme
	{
	public:
		FKartDeviceControls();

		virtual const FDeviceLayout& GetLayout() const override { return Layout; }
		virtual void OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink) override;
		virtual TSharedPtr<IDeviceMotionScript> MakeAutoplayScript() const override;

	private:
		FDeviceLayout Layout;
	};
}

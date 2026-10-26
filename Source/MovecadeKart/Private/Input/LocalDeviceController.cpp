#include "Dom/JsonObject.h"
#include "Input/DeviceControls.h"

namespace MovecadeKart::Input
{
	namespace
	{
		/** The phone's throttle (lib/motion.ts): a frame goes when this much of its interval has passed. */
		constexpr double SendIntervalSlack = 0.999;
		/** Frame dt (ms) of the very first sample, when there is no previous one. */
		constexpr double FirstSampleMs = 16.0;
	}

	TSharedPtr<FJsonObject> MakePadButtonData(TCHAR Button)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("button"), FString::Chr(Button));
		return Data;
	}

	FLocalDeviceController::FLocalDeviceController(TSharedRef<IDeviceControlScheme> InScheme, TSharedRef<IDeviceInputSink> InSink)
		: Scheme(MoveTemp(InScheme))
		, Sink(MoveTemp(InSink))
		, Builder(Scheme->GetLayout().FrameKind)
	{
	}

	void FLocalDeviceController::PushMotion(const FDeviceMotion& Motion)
	{
		LastDeviceMotionAt = Motion.TimeSeconds;
		// On-screen steering replaces the device's frames entirely: a tilt left over would fight the buttons.
		if (!bSynthesizing && !bTouchSteering)
		{
			Sample(Motion);
		}
	}

	void FLocalDeviceController::PushButton(EDeviceButton Button, bool bDown, double Now)
	{
		Scheme->OnButton(Button, bDown, Now, *Sink);
	}

	void FLocalDeviceController::PushTouch(const FDeviceTouch& Touch)
	{
		Scheme->OnSurface(Touch, *Sink);
	}

	bool FLocalDeviceController::IsMotionAvailable(double Now) const
	{
		if (StartedAt < 0.0 || LastDeviceMotionAt < StartedAt - MotionLostSeconds)
		{
			// Nothing from the sensors yet: they get a moment to start before the buttons come up.
			return StartedAt < 0.0 || Now - StartedAt < FirstMotionGraceSeconds;
		}
		return Now - LastDeviceMotionAt <= MotionLostSeconds;
	}

	void FLocalDeviceController::Tick(double Now)
	{
		if (StartedAt < 0.0)
		{
			StartedAt = Now;
		}
		if (Scheme->HasTouchSteering())
		{
			const bool bTouch = !bMotionSteeringWanted || !IsMotionAvailable(Now);
			if (bTouch != bTouchSteering)
			{
				bTouchSteering = bTouch;
				Scheme->SetTouchSteering(bTouch, Now, *Sink);
			}
		}
		FDeviceMotion Synth;
		bSynthesizing = Scheme->SynthesizeMotion(Now, Synth);
		if (bSynthesizing)
		{
			// The scheme speaks in the calibrated pose: put the neutral back so the builder's calibration cancels it.
			Synth.Attitude = Builder.GetReference() * Synth.Attitude;
			Synth.TimeSeconds = Now;
			if (bCalibrateNext && Builder.IsCalibrated())
			{
				bCalibrateNext = false;
			}
			Sample(Synth);
		}
		Scheme->Tick(Now, *Sink);
	}

	void FLocalDeviceController::Sample(const FDeviceMotion& Motion)
	{
		if (bCalibrateNext)
		{
			bCalibrateNext = false;
			Builder.Calibrate(Motion);
			Scheme->OnCalibrated();
		}
		const double T = Motion.TimeSeconds;
		const double DtMs = bHaveSample ? (T - LastSampleAt) * 1000.0 : FirstSampleMs;
		if (bHaveSample && DtMs <= 0.0)
		{
			return; // a repeated sample (two readings in one engine frame) is not new motion
		}
		bHaveSample = true;
		LastSampleAt = T;
		const Net::FSensorFrame Frame = Builder.Build(Motion, DtMs);
		Scheme->OnSample(Frame, *Sink);

		const double Interval = 1.0 / FMath::Max(1.0, Scheme->GetLayout().SendHz);
		if (bHaveSent && T - LastSentAt < Interval * SendIntervalSlack)
		{
			return;
		}
		Net::FSensorFrame Sent = Frame;
		Sent.Sequence = SentSequence++;
		Sent.DtMs = bHaveSent ? static_cast<uint16>(FMath::Clamp(FMath::RoundToDouble((T - LastSentAt) * 1000.0), 0.0, 65535.0)) : Frame.DtMs;
		bHaveSent = true;
		LastSentAt = T;
		++FramesSent;
		Sink->SendFrame(Sent);
	}
}

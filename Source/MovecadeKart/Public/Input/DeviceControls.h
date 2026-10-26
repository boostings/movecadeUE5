#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceFrameBuilder.h"

class FJsonObject;

/**
 * The on-device controls' contract: the phone build plays with the device it runs on, through the same path a phone
 * controller takes. A game's IDeviceControlScheme turns the device's frames and touches into exactly the traffic a
 * web controller would send (sensor frames, and game events such as darts:release or pad:press with the same JSON),
 * and an IDeviceInputSink delivers that traffic where the room host would: AArcadeGameModeBase's HandleSensorFrame /
 * HandleGameEvent for the party games, the kart's seat input for the race. FLocalDeviceController runs a scheme.
 */
namespace MovecadeKart::Input
{
	class IDeviceMotionScript;

	/** Where a scheme's phone traffic goes (what the room host does with a phone's frames and events). */
	class IDeviceInputSink
	{
	public:
		virtual ~IDeviceInputSink() = default;
		virtual void SendFrame(const Net::FSensorFrame& Frame) = 0;
		/** Event as a phone names it; Data as the phone's payload (null for none, like stacker:drop). */
		virtual void SendEvent(const FString& Event, const TSharedPtr<FJsonObject>& Data) = 0;
	};

	/** The on-screen buttons a scheme can ask for. */
	enum class EDeviceButton : uint8
	{
		/** The motion pad's big button (and the grip / drop / dash of the arcade games). */
		A,
		B,
		Gas,
		Brake,
		Drift,
		Item,
	};

	/** Where a button sits on screen. */
	enum class EButtonAnchor : uint8
	{
		BottomRight,
		BottomLeft,
		/** Above the bottom-right one. */
		RightRaised,
		/** Above the bottom-left one. */
		LeftRaised,
		/** The middle of the bottom edge (a portrait game's one big button). */
		BottomCentre,
	};

	struct FDeviceButtonSpec
	{
		EDeviceButton Button = EDeviceButton::A;
		FText Label;
		EButtonAnchor Anchor = EButtonAnchor::BottomRight;
		bool bLarge = false;
	};

	/** Where the pause and settings chips go: clear of the game's HUD. */
	enum class EChipPlacement : uint8
	{
		/** A column at the right edge in portrait, a pair at the top centre in landscape. */
		Auto,
		/** A column at the left edge (a landscape HUD with its title at the top centre). */
		LeftEdge,
	};

	/** What the touch overlay shows for a scheme, and how often frames go out. */
	struct FDeviceLayout
	{
		EDeviceFrameKind FrameKind = EDeviceFrameKind::Attitude;
		/** The phone controller's sendHz for this game (kart 30, samurai 100, the rest 60). */
		double SendHz = 60.0;
		TArray<FDeviceButtonSpec> Buttons;
		EChipPlacement Chips = EChipPlacement::Auto;
		/** A touch surface over the rest of the screen: the scheme's swipe fallback reads it. */
		bool bSurface = false;
		/** One line under the calibrate chip ("Tilt to steer", "Hold, swing, let go"). */
		FText Hint;
	};

	enum class ETouchPhase : uint8 { Down, Move, Up };

	/** One finger on the touch surface. Position is 0..1 of the surface (0,0 top-left), Time in seconds. */
	struct FDeviceTouch
	{
		ETouchPhase Phase = ETouchPhase::Down;
		int32 Pointer = 0;
		FVector2D Position = FVector2D::ZeroVector;
		double Time = 0.0;
	};

	/**
	 * One game's on-device controls. Every call carries the sink to emit into; nothing here may keep state outside
	 * the scheme. Lives in the game's own folder (Games/<Game>/<Game>DeviceControls.*), the kart's in Input/.
	 */
	class IDeviceControlScheme
	{
	public:
		virtual ~IDeviceControlScheme() = default;

		virtual const FDeviceLayout& GetLayout() const = 0;
		/**
		 * Every motion sample, already a wire frame (calibrated and rounded), before the send-rate throttle: where a
		 * phone runs its own detectors. The controller forwards frames to the sink at SendHz itself.
		 */
		virtual void OnSample(const Net::FSensorFrame& Frame, IDeviceInputSink& Sink) {}
		virtual void OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink) {}
		virtual void OnSurface(const FDeviceTouch& Touch, IDeviceInputSink& Sink) {}
		/**
		 * The touch fallback: a motion the scheme makes from the fingers (the swipe as a cut, the stick as a tilt).
		 * While it returns true its motion replaces the device's.
		 */
		virtual bool SynthesizeMotion(double Now, FDeviceMotion& OutMotion) { return false; }
		/** The neutral pose changed: re-centre any detector that keeps its own. */
		virtual void OnCalibrated() {}
		virtual void Tick(double Now, IDeviceInputSink& Sink) {}
		/** -DeviceMotion=sim: the gestures this game's device plays by itself (the phone simulator's moves). */
		virtual TSharedPtr<IDeviceMotionScript> MakeAutoplayScript() const { return nullptr; }
	};

	/**
	 * Runs one scheme: calibrates, builds every sample's frame, lets the scheme see it, and forwards frames to the
	 * sink at the scheme's send rate with the phone's own sequence and dt. Pure (no UObjects, no Slate): the world
	 * subsystem feeds it the live motion and touches, the tests feed it simulated ones.
	 */
	class MOVECADEKART_API FLocalDeviceController
	{
	public:
		FLocalDeviceController(TSharedRef<IDeviceControlScheme> InScheme, TSharedRef<IDeviceInputSink> InSink);

		/** The next sample becomes the neutral pose (the calibrate button; also the very first sample). */
		void RequestCalibration() { bCalibrateNext = true; }
		void SetSensitivity(double InSensitivity) { Builder.SetSensitivity(InSensitivity); }
		double GetSensitivity() const { return Builder.GetSensitivity(); }

		/** One motion sample from the device (or the simulator). Ignored while the touch fallback supplies motion. */
		void PushMotion(const FDeviceMotion& Motion);
		void PushButton(EDeviceButton Button, bool bDown, double Now);
		void PushTouch(const FDeviceTouch& Touch);
		/** Once a frame: the fallback's motion, then the scheme's own clock. */
		void Tick(double Now);

		const FDeviceLayout& GetLayout() const { return Scheme->GetLayout(); }
		IDeviceControlScheme& GetScheme() const { return *Scheme; }
		/** Seconds of the last device sample, or a large negative number before any. */
		double GetLastMotionTime() const { return LastDeviceMotionAt; }
		int32 GetFramesSent() const { return FramesSent; }

	private:
		void Sample(const FDeviceMotion& Motion);

		TSharedRef<IDeviceControlScheme> Scheme;
		TSharedRef<IDeviceInputSink> Sink;
		FDeviceFrameBuilder Builder;
		bool bCalibrateNext = true;
		bool bHaveSample = false;
		double LastSampleAt = 0.0;
		double LastSentAt = 0.0;
		bool bHaveSent = false;
		uint8 SentSequence = 0;
		double LastDeviceMotionAt = -1000.0;
		bool bSynthesizing = false;
		int32 FramesSent = 0;
	};

	/** Small helpers the schemes share for building a phone's JSON payloads. */
	MOVECADEKART_API TSharedPtr<FJsonObject> MakePadButtonData(TCHAR Button);
}

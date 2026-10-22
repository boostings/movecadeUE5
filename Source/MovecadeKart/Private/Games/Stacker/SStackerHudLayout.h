#pragma once

#include "CoreMinimal.h"

namespace MovecadeKart::Stacker::Hud
{
	/**
	 * Where everything on the stacker HUD sits, in layout units (1920 x 1080 reference; the HUD multiplies by S),
	 * measured from the anchor each group names. Font sizes are in the same units.
	 */
	struct FHudLayout
	{
		/** The round clock, top centre under the safe area. */
		struct FClock
		{
			static constexpr float Width = 220.f;
			static constexpr float Height = 112.f;
			static constexpr float Radius = 26.f;
			static constexpr float Shadow = 6.f;
			static constexpr float Outline = 3.f;
			static constexpr float TextY = 46.f;
			static constexpr float FontSize = 56.f;
			static constexpr float FontOutline = 3.f;
			static constexpr float TextShadow = 4.f;
			/** The last seconds turn the clock hot and pulse it. */
			static constexpr int32 LateSeconds = 10;
			static constexpr float Pulse = 0.06f;
			static constexpr float PulseRate = 6.f;
		};

		/** The wind pill under the clock: a label, three chevrons streaming the way it blows, five strength bars. */
		struct FWind
		{
			static constexpr float CentreY = 140.f;
			static constexpr float Width = 260.f;
			static constexpr float Height = 44.f;
			static constexpr float Radius = 22.f;
			static constexpr float Shadow = 4.f;
			static constexpr float Outline = 2.f;
			static constexpr float LabelX = -112.f;
			static constexpr float LabelSize = 15.f;
			/** A wind this strong (degrees of push) fills the gauge. */
			static constexpr double FullWind = 7.0;
			static constexpr int32 Chevrons = 3;
			static constexpr float ChevronX = -18.f;
			static constexpr float ChevronStep = 22.f;
			static constexpr float ChevronOffset = 10.f;
			static constexpr float ChevronDrift = 8.f;
			static constexpr float DriftRate = 0.6f;
			static constexpr float DriftRatePerStrength = 1.6f;
			static constexpr float ChevronBack = 7.f;
			static constexpr float ChevronTip = 5.f;
			static constexpr float ChevronHalfHeight = 10.f;
			static constexpr float ChevronThickness = 4.f;
			static constexpr int32 Bars = 5;
			static constexpr float BarX = 62.f;
			static constexpr float BarStep = 11.f;
			static constexpr float BarWidth = 7.f;
			static constexpr float BarBaseHeight = 8.f;
			static constexpr float BarHeightStep = 4.f;
			static constexpr float BarBottom = 12.f;
			static constexpr float BarRadius = 2.f;
			/** A bar lights once the strength passes this far into it. */
			static constexpr float BarLitAt = 0.25f;
		};

		/** A lane's panel, along the bottom of the safe area under its lane. */
		struct FPanel
		{
			static constexpr float Height = 168.f;
			static constexpr float MaxWidth = 460.f;
			static constexpr float MinWidth = 300.f;
			/** All the panels share this much of the screen, with a gap each; never closer than NeighbourGap. */
			static constexpr float ScreenShare = 0.96f;
			static constexpr float Gap = 16.f;
			static constexpr float NeighbourGap = 10.f;
			static constexpr float ScreenMargin = 24.f;
			/** The panel is centred under this point of its lane (web), projected. */
			static constexpr double AnchorY = -0.7;
			static constexpr double AnchorZ = 2.5;
			static constexpr float Radius = 22.f;
			static constexpr float Shadow = 6.f;
			static constexpr float Outline = 2.f;
			static constexpr float WarnOutline = 4.f;
			static constexpr float EliminatedAlpha = 0.5f;
			static constexpr float Alpha = 0.86f;
			static constexpr float WarnFlashRate = 10.f;
			static constexpr float StripInset = 18.f;
			static constexpr float StripHeight = 5.f;
			static constexpr float StripRadius = 2.5f;
			static constexpr float DotX = 30.f;
			static constexpr float DotY = 34.f;
			static constexpr float DotRadius = 11.f;
			static constexpr float DotOutline = 2.f;
			static constexpr float NameX = 52.f;
			static constexpr float NameY = 34.f;
			static constexpr float NameSize = 26.f;
			static constexpr float ScoreRight = 22.f;
			static constexpr float ScoreY = 40.f;
			static constexpr float ScoreSize = 48.f;
			static constexpr float ScoreShadow = 3.f;
			static constexpr float LifeX = 36.f;
			static constexpr float LifeStep = 30.f;
			static constexpr float StatY = 78.f;
			static constexpr float LifeWidth = 24.f;
			static constexpr float LifeHeight = 14.f;
			static constexpr float LifeRadius = 4.f;
			static constexpr float RowX = 122.f;
			static constexpr float RowSize = 16.f;
			static constexpr float ComboSize = 14.f;
			static constexpr float ComboPadding = 20.f;
			static constexpr float ComboHeight = 26.f;
			static constexpr float ComboOutline = 1.5f;
			/** A long streak turns the pill gold. */
			static constexpr int32 ComboGold = 5;
			static constexpr float BannerY = -26.f;
			static constexpr float BannerSize = 40.f;
			static constexpr float QuietBannerSize = 26.f;
			static constexpr float BannerOutline = 3.f;
			static constexpr float BannerShadow = 4.f;
			static constexpr float BannerWobbleDeg = 4.f;
			static constexpr float BannerWobbleRate = 22.f;
			/** The "it's tipping" banner shows once the danger is this far toward a topple. */
			static constexpr float BannerDanger = 0.35f;
		};

		/** The balance gauge inside a panel: the lean across, the steady band, the limits and the danger bar. */
		struct FGauge
		{
			static constexpr float Top = 106.f;
			static constexpr float Inset = 30.f;
			static constexpr float Height = 22.f;
			/** The gauge spans 1.5 times the limit either side. */
			static constexpr float Range = 1.5f;
			static constexpr float MidMarkWidth = 2.f;
			static constexpr float MidMarkInset = 4.f;
			static constexpr float LimitMarkWidth = 3.f;
			static constexpr float LimitMarkOverhang = 4.f;
			static constexpr float SideLabelGap = 14.f;
			static constexpr float SideLabelSize = 14.f;
			static constexpr float DotRadius = 12.f;
			static constexpr float DotHalo = 17.f;
			static constexpr float DotOutline = 2.5f;
			static constexpr int32 Chevrons = 2;
			static constexpr float ChevronX = 26.f;
			static constexpr float ChevronStep = 12.f;
			static constexpr float ChevronBack = 5.f;
			static constexpr float ChevronTip = 4.f;
			static constexpr float ChevronHalfHeight = 7.f;
			static constexpr float ChevronThickness = 3.f;
			static constexpr float DangerGap = 10.f;
			static constexpr float DangerHeight = 8.f;
			static constexpr float DangerRadius = 4.f;
		};

		/** The callouts over each tower. */
		struct FCallout
		{
			/** Above the top block (web units), rising this fast (layout units per second). */
			static constexpr double AboveTop = 0.9;
			static constexpr double RiseRate = 80.0;
			static constexpr float InSeconds = 0.2f;
			static constexpr float InOvershoot = 2.4f;
			static constexpr double OutSeconds = 0.25;
			static constexpr float BigScale = 1.2f;
			static constexpr float SmallScale = 0.9f;
			static constexpr float BigTiltDeg = -4.f;
			static constexpr float FontSize = 38.f;
			static constexpr float FontOutline = 4.f;
			static constexpr float Shadow = 6.f;
		};

		/** The coach's speech bubbles. */
		struct FCoach
		{
			static constexpr float PointerGap = 16.f;
			static constexpr float PointerHalfWidth = 14.f;
			static constexpr float PointerHeight = 18.f;
			static constexpr float BubbleRadius = 18.f;
			static constexpr float BubbleShadow = 6.f;
			static constexpr float BubbleOutline = 3.f;
			/** "Drop when aligned", over the sweeping block (web units above the top). */
			static constexpr double DropAboveTop = 1.0;
			static constexpr float DropLift = 12.f;
			static constexpr float DropWidth = 340.f;
			static constexpr float AlignedWidth = 300.f;
			static constexpr float DropHeight = 78.f;
			static constexpr float AlignedPop = 1.06f;
			static constexpr float AlignedPulse = 0.04f;
			static constexpr float AlignedPulseRate = 16.f;
			static constexpr float DropTitleY = -11.f;
			static constexpr float DropTitleSize = 28.f;
			static constexpr float DropTitleOutline = 2.f;
			static constexpr float DropKeysY = 20.f;
			static constexpr float KeysSize = 16.f;
			/** "Lean against the wind", over the builder's panel, with an arrow the way to tip. */
			static constexpr float LeanAbovePanel = 64.f;
			static constexpr float LeanWidth = 420.f;
			static constexpr float LeanHeight = 92.f;
			static constexpr float LeanTitleY = -16.f;
			static constexpr float LeanTitleSize = 26.f;
			static constexpr float LeanTitleOutline = 2.f;
			static constexpr float ArrowX = 190.f;
			static constexpr float ArrowY = 18.f;
			static constexpr float ArrowNudge = 6.f;
			static constexpr float ArrowNudgeRate = 9.f;
			static constexpr float ArrowLength = 70.f;
			static constexpr float ArrowHead = 16.f;
			static constexpr float ArrowThickness = 8.f;
			/** The keys sit off centre, away from the arrow. */
			static constexpr float LeanKeysAway = 45.f;
			static constexpr float LeanKeysY = 18.f;
			/** A mark this faint isn't drawn. */
			static constexpr float MinAlpha = 0.02f;
		};

		/** The engine's 3-2-1 and the "stack!" that follows. */
		struct FCountdown
		{
			static constexpr float CentreY = 0.36f;
			static constexpr float PopSeconds = 0.35f;
			static constexpr float DigitSize = 200.f;
			static constexpr float DigitOutline = 9.f;
			static constexpr float DigitShadow = 10.f;
			/** runtime.ts: "go" while the first 700 ms play, fading over the last 300. */
			static constexpr double GoMs = 700.0;
			static constexpr double GoFadeMs = 300.0;
			static constexpr float GoSize = 150.f;
			static constexpr float GoOutline = 8.f;
			static constexpr float GoShadow = 9.f;
		};

		/** The controls card during the countdown (local builders). */
		struct FControls
		{
			static constexpr float CentreY = 0.62f;
			static constexpr float Width = 940.f;
			static constexpr float Height = 80.f;
			static constexpr float Radius = 22.f;
			static constexpr float Shadow = 6.f;
			static constexpr float Outline = 2.f;
			static constexpr float FontSize = 22.f;
			/** It fades over the countdown's last 400 ms. */
			static constexpr double FadeMs = 400.0;
		};

		/** The intro card. */
		struct FIntro
		{
			static constexpr float CentreY = 0.44f;
			static constexpr double FadeInSeconds = 0.5;
			static constexpr double FadeOutSeconds = 0.4;
			static constexpr double RiseSeconds = 0.6;
			static constexpr float TitleY = -60.f;
			static constexpr float TitleSize = 140.f;
			static constexpr float TitleOutline = 7.f;
			static constexpr float TitleShadow = 9.f;
			static constexpr float NamesY = 50.f;
			static constexpr float NamesSize = 34.f;
			static constexpr float NamesOutline = 2.f;
			static constexpr float NamesShadow = 4.f;
			static constexpr float RulesY = 100.f;
			static constexpr float RulesSize = 24.f;
			static constexpr float RulesShadow = 3.f;
		};

		/** The final standings: low and narrow on the right, so the showcase of the towers is the picture. */
		struct FStandings
		{
			static constexpr float TitleY = 0.12f;
			static constexpr float TitleSize = 84.f;
			static constexpr float TitleOutline = 5.f;
			static constexpr float TitleShadow = 7.f;
			static constexpr double FadeSeconds = 0.5;
			static constexpr double RowDelay = 0.6;
			static constexpr double RowStagger = 0.18;
			static constexpr double RowPopSeconds = 0.4;
			static constexpr float FromRight = 760.f;
			static constexpr float Top = 0.22f;
			static constexpr float RowStep = 96.f;
			static constexpr float Width = 680.f;
			static constexpr float Height = 80.f;
			static constexpr float Radius = 22.f;
			static constexpr float Shadow = 6.f;
			static constexpr float Outline = 3.f;
			static constexpr float PlaceX = 44.f;
			static constexpr float PlaceSize = 48.f;
			static constexpr float PlaceShadow = 3.f;
			static constexpr float NameX = 92.f;
			static constexpr float NameY = 27.f;
			static constexpr float NameSize = 32.f;
			static constexpr float DetailY = 58.f;
			static constexpr float DetailSize = 17.f;
			static constexpr float ScoreX = 655.f;
			static constexpr float ScoreSize = 50.f;
		};
	};
}

#pragma once

#include "CoreMinimal.h"

/**
 * SDartsHud's layout, one namespace per panel (the scoreboard, who's up, the countdown, the callouts, the visit card,
 * the gauge, the controls, the coach marks, the intro and the standings). Sizes are canvas units
 * (KartUI::Metric::CanvasH tall), scaled at paint time; fonts are (size, outline) and cards (corner radius, shadow
 * depth, outline width) in the same units; Y fractions are of the screen's height.
 */
namespace MovecadeKart::Darts::HudLayout
{
	/** The bull's flash is skipped below this (it would only cost a full-screen box). */
	constexpr float MinFlash = 0.002f;
	/** At the results the last callouts fade out over this while the standings come in. */
	constexpr double CalloutsOutSeconds = 0.4;
	/** Painter scales never reach 0 (a pop starts from nearly nothing). */
	constexpr float MinScale = 0.01f;
	/** KartUI::Ease::OutBack's own overshoot. */
	constexpr float DefaultOvershoot = 1.70158f;
	/** A visit of a ton or more, or a bounty with every dart, is a big one (gold card, bigger call). */
	constexpr int32 BigVisitTotal = 100;

	/** A card's corners, drop shadow and outline. */
	struct FCardLook
	{
		float Radius = 0.f;
		float Shadow = 0.f;
		float Outline = 0.f;
	};

	namespace Scoreboard
	{
		constexpr float RowHeight = 62.f;
		constexpr float RowGap = 8.f;
		constexpr float NameWidth = 238.f;
		constexpr float SlotWidth = 36.f;
		constexpr float VisitGap = 16.f;
		/** Between the last dart and the total, and the total's own column. */
		constexpr float TotalGap = 20.f;
		constexpr float TotalWidth = 132.f;
		constexpr FCardLook Row = { 16.f, 5.f, 3.f };
		constexpr float IdleAlpha = 0.84f;
		/** The thrower's colour tab down the row's left edge. */
		constexpr float TabInset = 10.f;
		constexpr float TabWidth = 6.f;
		constexpr float TabRadius = 3.f;
		constexpr float NameX = 26.f;
		/** A name is shrunk (down to MinNameSize) and then shortened to fit this. */
		constexpr float NameRoom = 186.f;
		constexpr float NameSize = 26.f;
		constexpr float MinNameSize = 16.f;
		/** The soft band behind the visit in play. */
		constexpr float BandPadX = 4.f;
		constexpr float BandInsetY = 7.f;
		constexpr float BandRadius = 12.f;
		constexpr float BandAlpha = 0.22f;
		constexpr float DartRadius = 15.f;
		constexpr float DartOutline = 1.5f;
		constexpr float BountyOutline = 3.f;
		constexpr float DartOutlineAlpha = 0.6f;
		constexpr float MissAlpha = 0.75f;
		constexpr float EmptyDartRadius = 11.f;
		constexpr float EmptyDartOutline = 2.f;
		constexpr float EmptyDartAlpha = 0.28f;
		/** Three-digit points (a bounty treble) drop a size to fit the dot. */
		constexpr float PointsSize = 14.f;
		constexpr float BigPointsSize = 11.f;
		constexpr int32 BigPoints = 100;
		constexpr float ScoreSize = 38.f;
	}

	namespace UpPanel
	{
		constexpr float RoundSize = 60.f;
		constexpr float RoundOutline = 4.f;
		constexpr float RoundShadow = 5.f;
		/** The bounty chip under the round: "BOUNTY", the called number, "x2". */
		inline const FVector2D ChipSize(300.0, 62.0);
		constexpr float ChipBelow = 84.f;
		constexpr FCardLook Chip = { 31.f, 4.f, 3.f };
		constexpr float ChipAlpha = 0.88f;
		constexpr float ChipLabelX = 26.f;
		constexpr float ChipLabelSize = 18.f;
		constexpr float ChipNumberFromRight = 82.f;
		constexpr float ChipNumberSize = 40.f;
		constexpr float ChipTimesFromRight = 26.f;
		constexpr float ChipTimesSize = 28.f;
		/** The darts in hand, right to left under the chip: a diamond tip over a little flight. */
		constexpr float DartsBelow = 186.f;
		constexpr float DartsFromRight = 20.f;
		constexpr float DartSpacing = 36.f;
		constexpr float DartTip = 24.f;
		constexpr float DartHalfWidth = 5.5f;
		constexpr float DartShoulder = 6.f;
		inline const FVector2D FlightOffset(-10.0, 10.0);
		inline const FVector2D FlightSize(20.0, 13.0);
		constexpr float FlightRadius = 3.f;
		constexpr float SpentAlpha = 0.2f;
	}

	namespace Countdown
	{
		/** High in the frame, clear of the board. */
		constexpr float CentreY = 0.2f;
		/** The 3-2-1 pops in over the first BeatPop of each second. */
		constexpr float BeatPop = 0.35f;
		constexpr float DigitSize = 170.f;
		constexpr float DigitOutline = 8.f;
		constexpr float DigitShadow = 10.f;
		/** "THROW!" holds for ThrowSeconds after the countdown, fading over its last ThrowFade; wide, so it sits lower,
		 *  over the board, clear of the scoreboard and the round header. */
		constexpr double ThrowSeconds = 0.8;
		constexpr double ThrowFade = 0.3;
		constexpr float ThrowY = 0.42f;
		constexpr float ThrowSize = 120.f;
		constexpr float ThrowOutline = 7.f;
		constexpr float ThrowShadow = 9.f;
	}

	namespace Callout
	{
		/** A callout rises from RiseFrom to RiseTo (x RisePixels) over its life, popping in over PopSeconds. */
		constexpr double RiseFrom = 0.26;
		constexpr double RiseTo = 0.68;
		constexpr double RisePixels = 110.0;
		constexpr double PopSeconds = 0.22;
		constexpr float PopOvershoot = 2.4f;
		constexpr double FadeSeconds = 0.25;
		constexpr float LabelSize = 56.f;
		constexpr float LabelOutline = 5.f;
		constexpr float LabelShadow = 6.f;
		constexpr float MissAlpha = 0.8f;
		/** The bull and the bounty land bigger and tilted. */
		constexpr float BigScale = 1.3f;
		constexpr float BigTiltDegrees = -4.f;
		/** The finish banner, over the board's top: the winner's name and the call on their colour. */
		constexpr double BannerBoardY = 1.62;
		constexpr double BannerPopSeconds = 0.3;
		constexpr float BannerSize = 46.f;
		constexpr float BannerOutline = 3.f;
		constexpr float BannerShadow = 4.f;
		constexpr float BannerHeight = 68.f;
		constexpr float BannerPadX = 60.f;
		constexpr FCardLook Banner = { 34.f, 6.f, 3.f };
	}

	namespace VisitCard
	{
		constexpr double LifeSeconds = 2.4;
		/** It lingers this long past the next dart's landing, fading over FadeSeconds. */
		constexpr double LingerSeconds = 0.3;
		constexpr double FadeSeconds = 0.3;
		constexpr double PopSeconds = 0.3;
		inline const FVector2D Size(640.0, 176.0);
		constexpr float CentreY = 0.7f;
		constexpr FCardLook Card = { 28.f, 8.f, 4.f };
		constexpr float CardAlpha = 0.9f;
		/** The call, on a ribbon that overhangs the card's top. */
		constexpr float CallSize = 44.f;
		constexpr float BigCallSize = 54.f;
		constexpr float CallOutline = 4.f;
		constexpr float CallShadow = 4.f;
		constexpr float RibbonHeight = 68.f;
		constexpr float RibbonPadX = 64.f;
		constexpr FCardLook Ribbon = { 34.f, 6.f, 0.f };
		/** The three darts' chips pop in one after another. */
		inline const FVector2D ChipsAt(40.0, 58.0);
		inline const FVector2D ChipSize(112.0, 84.0);
		inline const FVector2D ChipPivot(56.0, 42.0);
		constexpr float ChipPitch = 128.f;
		constexpr double ChipDelay = 0.12;
		constexpr double ChipStagger = 0.1;
		constexpr double ChipPopSeconds = 0.25;
		constexpr FCardLook Chip = { 16.f, 4.f, 0.f };
		constexpr float MissChipAlpha = 0.9f;
		/** A chip's label shrinks to fit LabelRoom at LabelSize; a plain single reads bigger, centred. */
		constexpr float LabelSize = 26.f;
		constexpr float LabelRoom = 96.f;
		constexpr float PlainLabelScale = 1.35f;
		constexpr float LabelY = 28.f;
		constexpr float PlainLabelY = 42.f;
		constexpr float PointsY = 60.f;
		constexpr float PointsSize = 20.f;
		constexpr float PointsAlpha = 0.85f;
		/** The total, right-aligned, and the bonus line under it. */
		constexpr float TotalFromRight = 34.f;
		constexpr float TotalY = 92.f;
		constexpr float TotalSize = 64.f;
		constexpr float TotalOutline = 3.f;
		constexpr float TotalShadow = 4.f;
		constexpr float FooterBelow = 46.f;
		constexpr float FooterSize = 17.f;
		constexpr float FooterAlpha = 0.75f;
	}

	namespace Gauge
	{
		inline const FVector2D Size(20.0, 170.0);
		/** Right of the reticle. */
		constexpr float OffsetX = 70.f;
		constexpr float FramePad = 4.f;
		constexpr float FrameRadius = 12.f;
		constexpr float FrameAlpha = 0.8f;
		constexpr float SweetRadius = 4.f;
		constexpr float SweetAlpha = 0.45f;
		constexpr float FillRadius = 8.f;
		/** The marker across the fill's top, and the read beside it. */
		constexpr float MarkerOverhang = 8.f;
		constexpr float MarkerHeight = 4.f;
		constexpr float MarkerRadius = 2.f;
		constexpr float ReadGap = 14.f;
		constexpr float ReadSize = 20.f;
		constexpr float SweetReadSize = 30.f;
		constexpr float ReadOutline = 3.f;
		constexpr float ReadShadow = 3.f;
		/** The "HOLD TO THROW" pill under the reticle, pulsing between HintMin and 1. */
		inline const FVector2D HintPill(176.0, 34.0);
		constexpr float HintBelow = 70.f;
		constexpr float HintPulseRate = 4.f;
		constexpr float HintMin = 0.7f;
		constexpr FCardLook Hint = { 17.f, 3.f, 2.f };
		constexpr float HintAlpha = 0.82f;
		constexpr float HintSize = 15.f;
	}

	namespace Controls
	{
		/** Lower right, clear of the thrower and the board. */
		constexpr float LeftOfSafeRight = 400.f;
		constexpr float CentreY = 0.87f;
		constexpr float CardAlpha = 0.86f;
		/** A phone's or a bot's turn: who's up and what they're doing. */
		inline const FVector2D RemoteCard(700.0, 88.0);
		constexpr FCardLook Remote = { 22.f, 6.f, 3.f };
		constexpr float RemoteUpY = -13.f;
		constexpr float RemoteUpSize = 32.f;
		constexpr float RemoteHintY = 22.f;
		constexpr float RemoteHintSize = 21.f;
		constexpr float RemoteHintAlpha = 0.82f;
		/** A local turn: who's up, the controls, and the meter. */
		inline const FVector2D LocalCard(800.0, 140.0);
		constexpr FCardLook Local = { 24.f, 6.f, 3.f };
		constexpr float LocalUpY = -40.f;
		constexpr float LocalUpSize = 30.f;
		constexpr float LocalHintY = -6.f;
		constexpr float LocalHintSize = 18.f;
		constexpr float LocalHintAlpha = 0.88f;
		inline const FVector2D MeterSize(520.0, 22.0);
		constexpr float MeterY = 26.f;
		constexpr float MeterRadius = 11.f;
		constexpr float MeterTrackAlpha = 0.12f;
		constexpr float SweetAlpha = 0.45f;
		constexpr float EndLabelGap = 14.f;
		constexpr float EndLabelSize = 16.f;
	}

	namespace Coach
	{
		constexpr float TopY = 0.36f;
		constexpr float StepPitch = 100.f;
		inline const FVector2D CardSize(470.0, 88.0);
		constexpr FCardLook Card = { 22.f, 6.f, 3.f };
		constexpr float IdleAlpha = 0.82f;
		/** The step in play breathes. */
		constexpr float PulseRate = 6.f;
		constexpr float Pulse = 0.04f;
		/** The step's number in a disc, its title and its keys. */
		inline const FVector2D NumberAt(46.0, 44.0);
		constexpr float DiscRadius = 26.f;
		constexpr float IdleDiscAlpha = 0.15f;
		constexpr float NumberSize = 32.f;
		constexpr float TextX = 90.f;
		constexpr float TitleY = 30.f;
		constexpr float TitleSize = 30.f;
		constexpr float KeysY = 62.f;
		constexpr float KeysSize = 19.f;
		constexpr float KeysAlpha = 0.8f;
		/** The aim step is done once the reticle has moved this far off centre. */
		constexpr double AimedAway = 0.02;
	}

	namespace Intro
	{
		constexpr double FadeInSeconds = 0.4;
		constexpr double FadeOutSeconds = 0.35;
		constexpr double PopSeconds = 0.45;
		constexpr float CentreY = 0.6f;
		constexpr float TitleAbove = 60.f;
		constexpr float TitleSize = 132.f;
		constexpr float TitleOutline = 7.f;
		constexpr float TitleShadow = 9.f;
		constexpr float NamesBelow = 50.f;
		constexpr float NamesSize = 34.f;
		constexpr float NamesOutline = 2.f;
		constexpr float NamesShadow = 4.f;
		constexpr float RulesBelow = 102.f;
		constexpr float RulesSize = 26.f;
		constexpr float RulesOutline = 2.f;
		constexpr float RulesShadow = 3.f;
		constexpr float RulesAlpha = 0.92f;
	}

	/**
	 * A phone held upright: the scoreboard across the top, the round and the bounty in a row under it, the board in
	 * the middle (the camera frames it there), the visit card, the coach marks and the controls at the bottom. Y
	 * fractions are of the screen's height.
	 */
	namespace Portrait
	{
		/** Between the scoreboard and the round row. */
		constexpr float RoundRowGap = 14.f;
		/** The round's line is smaller than the landscape header (it shares its row with the chip). */
		constexpr float RoundSize = 46.f;
		/** The 3-2-1 and "THROW!" sit over the board's middle. */
		constexpr float CountdownY = 0.46f;
		constexpr float VisitCentreY = 0.73f;
		/** The coach marks stack, centred, below the board. */
		constexpr float CoachTopY = 0.64f;
		/** The intro's title is shrunk to fit the width. */
		constexpr float IntroTitleScale = 0.62f;
		constexpr float IntroCentreY = 0.5f;
		constexpr float StandingsTitleY = 0.24f;
		constexpr float StandingsTopY = 0.31f;
	}

	namespace Standings
	{
		constexpr double FadeInSeconds = 0.5;
		constexpr float TitleY = 0.2f;
		constexpr float TitleSize = 96.f;
		constexpr float TitleOutline = 5.f;
		constexpr float TitleShadow = 7.f;
		constexpr float TopY = 0.3f;
		constexpr float RowPitch = 104.f;
		inline const FVector2D RowSize(800.0, 88.0);
		constexpr FCardLook Row = { 22.f, 6.f, 3.f };
		constexpr float IdleAlpha = 0.88f;
		/** Rows pop in one after another. */
		constexpr float RowDelay = 0.3f;
		constexpr float RowStagger = 0.18f;
		constexpr double RowPopSeconds = 0.4;
		/** Place, name over the stats line, score. */
		inline const FVector2D PlaceAt(48.0, 44.0);
		constexpr float PlaceSize = 52.f;
		constexpr float PlaceShadow = 3.f;
		inline const FVector2D NameAt(100.0, 30.0);
		constexpr float NameSize = 36.f;
		inline const FVector2D StatsAt(100.0, 64.0);
		constexpr float StatsSize = 19.f;
		constexpr float StatsAlpha = 0.78f;
		inline const FVector2D TotalAt(770.0, 44.0);
		constexpr float ScoreSize = 56.f;
	}
}

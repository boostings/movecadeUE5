#pragma once

#include "CoreMinimal.h"
#include "UI/KartScreen.h"

/**
 * The shared controls, drawn immediate-mode: buttons, option rows, sliders, the screen header, button
 * glyphs and the hint bar. They take a focus age (seconds since the item gained focus) to animate the pop.
 */
namespace KartWidgets
{
	/** Hit ids for an option row's arrows: the row's id with a flag. */
	constexpr int32 LeftArrowFlag = 1 << 16;
	constexpr int32 RightArrowFlag = 1 << 17;
	inline int32 HitBase(int32 HitId) { return HitId & 0xffff; }
	inline int32 HitArrow(int32 HitId) { return (HitId & LeftArrowFlag) ? -1 : (HitId & RightArrowFlag) ? 1 : 0; }

	/** Eased 0..1 entrance for an element that starts Delay seconds after its screen appears. */
	MOVECADEKART_API float Entrance(const FKartScreenFrame& Frame, float Delay, float Duration = 0.38f);
	/** Horizontal slide for the whole screen during its transition (Slate units). */
	MOVECADEKART_API float SlideX(const FKartScreenFrame& Frame, float Distance = 140.f);

	/** A button glyph for Action on Device, its left edge centred on LeftCentre. Returns its width. */
	MOVECADEKART_API float Glyph(const FKartPainter& P, EKartMenuAction Action, EKartInputDevice Device, const FVector2D& LeftCentre, float Height);
	MOVECADEKART_API float GlyphWidth(EKartMenuAction Action, EKartInputDevice Device, float Height);
	/** A keyboard key cap with Label (for the controls page). Returns its width. */
	MOVECADEKART_API float KeyCap(const FKartPainter& P, const FString& Label, const FVector2D& LeftCentre, float Height);
	/** A gamepad control drawn generically (face buttons by position, shoulders, triggers, sticks, d-pad). */
	MOVECADEKART_API float PadGlyph(const FKartPainter& P, const FKey& Key, const FVector2D& LeftCentre, float Height);

	/** The hint bar along the bottom-right of the safe area. */
	MOVECADEKART_API void Hints(const FKartPainter& P, const FKartScreenFrame& Frame, const TArray<FKartHint>& Hints);

	/** Title (and optional subtitle) at the top-left of the safe area, with an accent tag. */
	MOVECADEKART_API void Header(const FKartPainter& P, const FKartScreenFrame& Frame, const FText& Title, const FText& Subtitle, const FLinearColor& Accent);
	/** "1 Racers  2 Rules  3 Track" at the top-right. */
	MOVECADEKART_API void Steps(const FKartPainter& P, const FKartScreenFrame& Frame, const TArray<FText>& Names, int32 Current);

	struct FItemState
	{
		bool bFocused = false;
		bool bEnabled = true;
		/** Seconds since focus arrived (animates the pop). */
		double FocusAge = 10.0;
		/** Seconds since the value changed, and which way (-1 / +1). */
		double ChangeAge = 10.0;
		int32 ChangeDir = 0;
	};

	MOVECADEKART_API void Button(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label,
		const FItemState& State, const FLinearColor& Accent, int32 HitId, const FText& Detail = FText::GetEmpty());
	MOVECADEKART_API void OptionRow(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label,
		const FText& Value, const FItemState& State, int32 HitId, const FLinearColor& Swatch = FLinearColor::Transparent);
	/** Value in [0, 1], drawn as Segments chunky bars. */
	MOVECADEKART_API void SliderRow(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, const FText& Label,
		float Value, const FItemState& State, int32 HitId, int32 Segments = 10);
	/** A big pill for primary actions ("Start race"). */
	MOVECADEKART_API void PrimaryButton(const FKartPainter& P, const FVector2D& Centre, const FText& Label, EKartMenuAction Action,
		EKartInputDevice Device, const FItemState& State, int32 HitId, float Height = 76.f);

	/** "P1".."P8" (clamped), for seat badges. */
	MOVECADEKART_API const FString& SeatTag(int32 Seat);
	/** The badge on a house player's seat. */
	MOVECADEKART_API const FString& CpuTag();

	/** A right-pointing (or left) chunky triangle. */
	MOVECADEKART_API void Arrow(const FKartPainter& P, const FVector2D& Centre, float Size, bool bRight, const FLinearColor& Color);
	/** A five-point star. */
	MOVECADEKART_API void Star(const FKartPainter& P, const FVector2D& Centre, float Outer, float Inner, const FLinearColor& Color, float Degrees = 0.f);
	/** A track outline (normalised points) fitted into a box. */
	MOVECADEKART_API void TrackOutline(const FKartPainter& P, const TArray<FVector2D>& Outline, const FVector2D& Pos, const FVector2D& Size,
		const FLinearColor& Road, float Thickness, double MarkerPhase = -1.0, const FLinearColor& Marker = FLinearColor::White);
}

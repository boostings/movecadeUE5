#include "UI/Screens/KartScreens.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Input/DeviceControlsSettings.h"
#include "Input/DeviceControlsSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Platform/Mobile/MobileHaptics.h"
#include "Render/KartGameUserSettings.h"
#include "UI/KartAccessibility.h"
#include "UI/KartPlayerPrefs.h"
#include "UI/KartSafeArea.h"
#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartSettings"

using namespace KartUI;
using namespace KartScreens::Shared;
using KartWidgets::FItemState;

namespace
{
	/** Graphics changes are staged on the settings object and applied together (ApplySettings), or reset. */
	UKartGameUserSettings* GameSettings()
	{
		return UKartGameUserSettings::Get();
	}

	/** The frame rate caps the row cycles through (0: unlimited). */
	const float FrameCaps[] = { 30.f, 60.f, 120.f, 144.f, 0.f };
	constexpr int32 NumPresets = 4;

	enum ESettingsTab : int32 { GraphicsTab, AudioTab, ControlsTab, AccessibilityTab, NumTabs };
	// Row indices per tab (plain ints: they index rows and multiply row pitches).
	enum EGraphicsRow : int32 { Preset, Resolution, DisplayMode, VSync, FrameCap, RayTracing, MotionBlur, ApplyDisplay, GraphicsRows };
	enum EAudioRow : int32 { Master, Music, Effects, AudioRows };
	enum EAccessRow : int32 { UIScaleRow, ReduceMotionRow, ColorVisionRow, ReplayWelcomeRow, AccessRows };
	/** The controls tab on a phone (or with -DeviceControls): the device's own settings instead of the key chart. */
	enum EDeviceRow : int32 { HapticsRow, SensitivityRow, SteeringRow, CalibrateRow, DeviceRows };

	/** The rows' column in the middle of the screen: 1040 wide, narrower where the safe area is (a portrait phone). */
	constexpr double ColumnHalfWidth = 520.0;
	/** The least room kept between the column (and the tab strip) and the safe area's sides. */
	constexpr double ColumnSideMargin = 36.0;
	/** Upright, the column is the screen's width: a wider margin, and the tabs and rows sit a little lower under the header. */
	constexpr double PortraitSideMargin = 72.0;
	constexpr double PortraitDrop = 60.0;
	constexpr double RowsTop = 230.0;
	const FVector2D DesignRowSize(1040.0, 72.0);
	constexpr double RowGap = 12.0;
	const FVector2D CalibrateButtonSize(620.0, 80.0);
	constexpr double TabSlide = 60.0;
	constexpr double TabSwitchSeconds = 0.25;
	constexpr float DimSeconds = 0.3f;
	constexpr float DimAlpha = 0.66f;
	/** Sliders are taller than option rows, so they step further apart. */
	constexpr double SliderHeight = 84.0;
	constexpr double SliderExtraGap = 12.0;
	/** The buttons under the rows (Apply, Replay the guide) and the note under the accessibility rows. */
	constexpr double ButtonGap = 10.0;
	const FVector2D ApplyButtonSize(520.0, 80.0);
	const FVector2D ReplayButtonSize(620.0, 80.0);
	constexpr float NoteSize = 24.f;
	constexpr float NoteOutline = 2.f;
	constexpr double NoteInset = 8.0;
	constexpr double NoteGap = 40.0;

	/** The tab strip over the rows, with the shoulder glyphs either side. */
	namespace TabArt
	{
		constexpr double Width = 250.0;
		constexpr double Height = 66.0;
		constexpr double Gap = 14.0;
		constexpr double Top = 120.0;
		constexpr float InAt = 0.05f;
		constexpr double DropIn = 40.0;
		constexpr float Pop = 0.06f;
		constexpr float PopOvershoot = 3.f;
		constexpr float FontSize = 28.f;
		constexpr float IdleAlpha = 0.9f;
		constexpr float Shadow = 6.f;
		constexpr float Rim = 4.f;
		constexpr float GlyphHeight = 40.f;
		constexpr double GlyphGap = 24.0;
		/** Room kept for the shoulder glyph either side of the strip, and the label's inset inside its tab. */
		constexpr double GlyphRoom = 90.0;
		constexpr double LabelInset = 14.0;
	}
	/** Hit ids from here up are the tabs. */
	constexpr int32 TabHit = 900;

	/** The controls reference card: three columns, striped rows and a note. */
	namespace ControlsArt
	{
		/** The card sits a little higher than the other tabs' rows. */
		constexpr double Lift = 10.0;
		const FVector2D Size(1040.0, 640.0);
		constexpr float FillAlpha = 0.96f;
		constexpr float Shadow = 10.f;
		constexpr double ActionX = 40.0;
		constexpr double KeysX = 420.0;
		constexpr double PadX = 760.0;
		constexpr float HeadSize = 20.f;
		constexpr double HeadY = 28.0;
		constexpr double FirstRowY = 88.0;
		constexpr double RowHeight = 60.0;
		constexpr double StripeInset = 16.0;
		constexpr float StripeAlpha = 0.7f;
		constexpr float StripeRound = 14.f;
		constexpr float ActionSize = 28.f;
		constexpr float SlashSize = 24.f;
		constexpr double SlashGap = 6.0;
		constexpr double SlashAfter = 14.0;
		constexpr float KeyCapHeight = 42.f;
		constexpr float PadGlyphHeight = 44.f;
		constexpr double PadGap = 12.0;
		constexpr float NoteFontSize = 20.f;
		constexpr double NoteDrop = 18.0;
		constexpr double NoteSideInset = 40.0;
	}

	struct FBinding
	{
		FText Action;
		TArray<FString> Keys;
		TArray<FKey> Pad;
	};

	const TArray<FBinding>& Bindings()
	{
		// KartPlayerController's mapping context, plus the race and menu keys (README "Play").
		static const TArray<FBinding> List = {
			{ LOCTEXT("Gas", "Accelerate"), { TEXT("W"), TEXT("Up") }, { EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_RightTriggerAxis } },
			{ LOCTEXT("Brake", "Brake / reverse"), { TEXT("S"), TEXT("Down") }, { EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_LeftTriggerAxis } },
			{ LOCTEXT("Steer", "Steer"), { TEXT("A  D"), TEXT("Left  Right") }, { EKeys::Gamepad_LeftX, EKeys::Gamepad_DPad_Left } },
			{ LOCTEXT("Drift", "Hop / drift (hold)"), { TEXT("Space") }, { EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftShoulder } },
			{ LOCTEXT("Item", "Use item"), { TEXT("E"), TEXT("Left Shift") }, { EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top } },
			{ LOCTEXT("LookBehind", "Look behind"), { TEXT("C") }, { EKeys::Gamepad_RightThumbstick } },
			{ LOCTEXT("Pause", "Pause"), { TEXT("Esc"), TEXT("P") }, { EKeys::Gamepad_Special_Right, EKeys::Gamepad_Special_Left } },
			{ LOCTEXT("Skip", "Skip intro"), { TEXT("Enter") }, { EKeys::Gamepad_Special_Right } },
		};
		return List;
	}

	/**
	 * Settings, in four tabs: graphics (staged, applied together), audio, the controls reference and
	 * accessibility. Opened from the main menu and from the race's pause menu.
	 */
	class FSettingsScreen final : public FKartScreen
	{
	public:
		explicit FSettingsScreen(int32 InTab)
			: Tab(FMath::Clamp(InTab, 0, NumTabs - 1))
			// A phone (or the desktop's -DeviceControls / -MobilePreview) plays with its own device: its controls tab
			// sets haptics, tilt and steering. A PC keeps the key and pad chart.
			, bDeviceControls(UDeviceControlsSubsystem::IsEnabled() || KartSafeArea::FKartDisplay::Get(FVector2D(Metric::CanvasW, Metric::CanvasH)).bPhone)
		{
			FIntPoint Current(1920, 1080);
			if (const UGameUserSettings* S = GameSettings())
			{
				Current = S->GetScreenResolution();
			}
			UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
			if (Resolutions.Num() == 0)
			{
				Resolutions = { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160) };
			}
			Resolutions.AddUnique(Current);
			Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y < B.X * B.Y; });
			ResetCursor();
		}

		virtual FName GetId() const override
		{
			static const FName Ids[] = { TEXT("settings"), TEXT("audio"), TEXT("controls"), TEXT("accessibility") };
			return Ids[FMath::Clamp(Tab, 0, NumTabs - 1)];
		}

		virtual void OnShown() override { Focus.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			TArray<FKartHint> Hints = { { EKartMenuAction::PrevTab, FText::GetEmpty() }, { EKartMenuAction::NextTab, LOCTEXT("Tabs", "Tabs") } };
			if ((Tab == AccessibilityTab && Focus.Cursor.Index == ReplayWelcomeRow) || (IsDeviceTab() && Focus.Cursor.Index == CalibrateRow))
			{
				Hints.Add({ EKartMenuAction::Accept, LOCTEXT("Select", "Select") });
			}
			else if (!IsReadOnlyTab())
			{
				Hints.Add({ EKartMenuAction::Left, LOCTEXT("Change", "Change") });
			}
			Hints.Add({ EKartMenuAction::Back, LOCTEXT("Done", "Done") });
			return Hints;
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::PrevTab:
			case EKartMenuAction::NextTab:
				SetTab(FKartMenuCursor::Cycle(Tab, Event.Action == EKartMenuAction::PrevTab ? -1 : 1, NumTabs));
				return true;
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				Focus.Step(Event.Action == EKartMenuAction::Up ? -1 : 1, *Host);
				return true;
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Change(Focus.Cursor.Index, Event.Action == EKartMenuAction::Left ? -1 : 1);
				return true;
			case EKartMenuAction::Accept:
				if (Tab == GraphicsTab && Focus.Cursor.Index == ApplyDisplay)
				{
					Apply();
				}
				else if (!IsReadOnlyTab())
				{
					Change(Focus.Cursor.Index, 1);
				}
				else
				{
					Host->PlaySound(EKartUISound::Error); // the controls page is read-only
				}
				return true;
			case EKartMenuAction::Back:
				return Leave();
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			const int32 Base = KartWidgets::HitBase(HitId);
			if (Base < TabHit)
			{
				Focus.HoverTo(Base, *Host);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			const int32 Base = KartWidgets::HitBase(HitId);
			if (Base >= TabHit)
			{
				SetTab(Base - TabHit);
				return;
			}
			Focus.Cursor.Set(Base);
			if (Tab == GraphicsTab && Base == ApplyDisplay)
			{
				Apply();
			}
			else if (!IsReadOnlyTab())
			{
				Change(Base, KartWidgets::HitArrow(HitId) < 0 ? -1 : 1);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const float Dim = KartWidgets::Entrance(Frame, 0.f, DimSeconds);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, DimAlpha * Dim));
			KartWidgets::Header(P, Frame, LOCTEXT("SettingsTitle", "Settings"), FText::GetEmpty(), Color::Grape);
			PaintTabs(P, Frame);
			// The column keeps a margin inside the safe area: on a 16:9 PC that is the full 1040, upright on a phone less.
			const double HalfWidth = FMath::Min(ColumnHalfWidth, Frame.SafeSize().X * 0.5 - (Frame.bPortrait ? PortraitSideMargin : ColumnSideMargin));
			RowBox = FVector2D(2.0 * HalfWidth, DesignRowSize.Y);
			const double Left = Frame.Size.X * 0.5 - HalfWidth + KartWidgets::SlideX(Frame);
			const FKartPaintGroup Group(P);
			const double Top = Frame.SafeMin.Y + RowsTop + (Frame.bPortrait ? PortraitDrop : 0.0);
			const float TabIn = Ease::OutCubic(static_cast<float>((Frame.Now - TabAt) / TabSwitchSeconds));
			const FKartPainter Body = P.Faded(TabIn);
			const FVector2D Pos(Left + (1.0 - TabIn) * TabSlide * TabDir, Top);
			switch (Tab)
			{
			case GraphicsTab: PaintGraphics(Body, Frame, Group, Pos); break;
			case AudioTab: PaintAudio(Body, Frame, Group, Pos); break;
			case ControlsTab:
				if (bDeviceControls)
				{
					PaintDevice(Body, Frame, Group, Pos);
				}
				else
				{
					PaintControls(Body, Frame, FVector2D(Pos.X, Pos.Y - ControlsArt::Lift));
				}
				break;
			default: PaintAccessibility(Body, Frame, Group, Pos); break;
			}
		}

	private:
		/** The PC's key and pad chart: nothing to change. */
		bool IsReadOnlyTab() const { return Tab == ControlsTab && !bDeviceControls; }
		bool IsDeviceTab() const { return Tab == ControlsTab && bDeviceControls; }

		static const FText& OnOff(bool bOn)
		{
			static const FText On = LOCTEXT("On", "On");
			static const FText Off = LOCTEXT("Off", "Off");
			return bOn ? On : Off;
		}

		static const FText& ModeName(EWindowMode::Type Mode)
		{
			static const FText Fullscreen = LOCTEXT("Fullscreen", "Fullscreen");
			static const FText Borderless = LOCTEXT("Borderless", "Borderless");
			static const FText Windowed = LOCTEXT("Windowed", "Windowed");
			switch (Mode)
			{
			case EWindowMode::Fullscreen: return Fullscreen;
			case EWindowMode::WindowedFullscreen: return Borderless;
			default: return Windowed;
			}
		}

		static double RowY(double Top, int32 Row, double Step) { return Top + Row * Step; }

		/** The graphics rows' values, rebuilt only when a setting they show changes. */
		void RefreshGraphicsText(const UKartGameUserSettings* S)
		{
			static const FText Presets[NumPresets] = { LOCTEXT("Performance", "Performance"), LOCTEXT("Balanced", "Balanced"), LOCTEXT("QualityPreset", "Quality"), LOCTEXT("Ultra", "Ultra") };
			const int32 PresetIndex = S ? static_cast<int32>(S->GetGraphicsPreset()) : 3;
			const float Cap = S ? S->GetFrameRateLimit() : 0.f;
			const bool bRayTracing = UKartGameUserSettings::IsRayTracingSupported();
			const FIntPoint Res = S ? S->GetScreenResolution() : FIntPoint(1920, 1080);
			const EWindowMode::Type Mode = S ? S->GetFullscreenMode() : EWindowMode::Windowed;
			const bool bVSync = S && S->IsVSyncEnabled();
			const bool bRayTracingOn = S && S->IsRayTracingEnabled();
			const bool bMotionBlur = S && S->IsMotionBlurEnabled();
			const FGraphicsKey Key{ PresetIndex, Res, static_cast<int32>(Mode), bVSync, Cap, bRayTracing, bRayTracingOn, bMotionBlur };
			if (bGraphicsTextValid && Key == GraphicsKey)
			{
				return;
			}
			bGraphicsTextValid = true;
			GraphicsKey = Key;
			GraphicsValues[Preset] = Presets[FMath::Clamp(PresetIndex, 0, NumPresets - 1)];
			GraphicsValues[Resolution] = FText::FromString(FString::Printf(TEXT("%d x %d"), Res.X, Res.Y));
			GraphicsValues[DisplayMode] = ModeName(Mode);
			GraphicsValues[VSync] = OnOff(bVSync);
			GraphicsValues[FrameCap] = Cap <= 0.f ? LOCTEXT("Unlimited", "Unlimited") : FText::Format(LOCTEXT("Fps", "{0} fps"), FMath::RoundToInt32(Cap));
			GraphicsValues[RayTracing] = bRayTracing ? OnOff(bRayTracingOn) : LOCTEXT("Unsupported", "Not supported");
			GraphicsValues[MotionBlur] = OnOff(bMotionBlur);
		}

		void PaintGraphics(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartPaintGroup& Group, const FVector2D& Pos)
		{
			static const FText Labels[ApplyDisplay] = { LOCTEXT("Quality", "Graphics preset"), LOCTEXT("Resolution", "Resolution"), LOCTEXT("DisplayMode", "Display mode"),
				LOCTEXT("VSync", "V-sync"), LOCTEXT("FrameCap", "Frame rate cap"), LOCTEXT("RayTracing", "Ray tracing"), LOCTEXT("MotionBlur", "Motion blur") };
			const UKartGameUserSettings* S = GameSettings();
			RefreshGraphicsText(S);
			const bool bPending = S && S->IsDirty();
			const double Step = RowBox.Y + RowGap;
			for (int32 Row = 0; Row < ApplyDisplay; ++Row)
			{
				Group.Next();
				// Borderless always uses the desktop resolution; ray tracing needs hardware support.
				const bool bEnabled = Row == Resolution ? GraphicsKey.Mode != static_cast<int32>(EWindowMode::WindowedFullscreen)
					: Row == RayTracing ? GraphicsKey.bRayTracingSupported : true;
				KartWidgets::OptionRow(P, FVector2D(Pos.X, RowY(Pos.Y, Row, Step)), RowBox, Labels[Row], GraphicsValues[Row], Focus.State(Row, Frame.Now, bEnabled), Row);
			}
			Group.Next();
			KartWidgets::Button(P, FVector2D(Pos.X, RowY(Pos.Y, ApplyDisplay, Step) + ButtonGap), ApplyButtonSize,
				bPending ? LOCTEXT("ApplyChanges", "Apply changes") : LOCTEXT("Applied", "All changes applied"), Focus.State(ApplyDisplay, Frame.Now, bPending), Color::Mint, ApplyDisplay);
		}

		void PaintAudio(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartPaintGroup& Group, const FVector2D& Pos) const
		{
			static const FText Labels[AudioRows] = { LOCTEXT("Master", "Master volume"), LOCTEXT("MusicVol", "Music"), LOCTEXT("Sfx", "Sound effects") };
			const UKartPlayerPrefs* Prefs = GetDefault<UKartPlayerPrefs>();
			const float Values[AudioRows] = { Prefs->MasterVolume, Prefs->MusicVolume, Prefs->SfxVolume };
			const double Step = RowBox.Y + RowGap + SliderExtraGap;
			for (int32 Row = 0; Row < AudioRows; ++Row)
			{
				Group.Next();
				KartWidgets::SliderRow(P, FVector2D(Pos.X, RowY(Pos.Y, Row, Step)), FVector2D(RowBox.X, SliderHeight), Labels[Row], Values[Row], Focus.State(Row, Frame.Now), Row);
			}
		}

		void PaintAccessibility(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartPaintGroup& Group, const FVector2D& Pos)
		{
			static const FText Labels[ReplayWelcomeRow] = { LOCTEXT("UIScale", "UI scale"), LOCTEXT("ReduceMotion", "Reduce motion"), LOCTEXT("ColorVision", "Colour vision") };
			static const FText Visions[] = { LOCTEXT("VisionStandard", "Standard"), LOCTEXT("VisionRedGreen", "Red-green friendly"), LOCTEXT("VisionBlueYellow", "Blue-yellow friendly") };
			// What the focused option does, under the rows.
			static const FText Notes[AccessRows] = {
				LOCTEXT("UIScaleNote", "Makes the race and party-game HUDs bigger or smaller; below 100% the menus shrink too. Bigger reads better from the couch."),
				LOCTEXT("ReduceMotionNote", "Tones down screen shake, flashes, bounces and spinning effects in menus, races and party games."),
				LOCTEXT("ColorVisionNote", "Swaps the colours that carry meaning (ready, gained or lost a place, warnings) for a pair you can tell apart, and labels players on the minimap."),
				LOCTEXT("ReplayNote", "Shows the one-page guide to phones, pads and house players after the title screen."),
			};
			const UKartPlayerPrefs* Prefs = GetDefault<UKartPlayerPrefs>();
			const int32 ScalePercent = FMath::RoundToInt32(FKartAccessibility::UIScale() * 100.f);
			if (ScalePercent != ScaleShown)
			{
				ScaleShown = ScalePercent;
				ScaleText = FText::Format(LOCTEXT("ScalePercent", "{0}%"), ScalePercent);
			}
			const FText* Values[ReplayWelcomeRow] = { &ScaleText, &OnOff(Prefs->bReduceMotion), &Visions[static_cast<int32>(FKartAccessibility::ColorVision())] };
			const double Step = RowBox.Y + RowGap;
			for (int32 Row = 0; Row < ReplayWelcomeRow; ++Row)
			{
				Group.Next();
				// The colour-vision row shows the good / bad pair it uses as a swatch.
				KartWidgets::OptionRow(P, FVector2D(Pos.X, RowY(Pos.Y, Row, Step)), RowBox, Labels[Row], *Values[Row], Focus.State(Row, Frame.Now), Row,
					Row == ColorVisionRow ? FKartAccessibility::Good() : FLinearColor::Transparent);
			}
			Group.Next();
			KartWidgets::Button(P, FVector2D(Pos.X, RowY(Pos.Y, ReplayWelcomeRow, Step) + ButtonGap), FVector2D(FMath::Min(ReplayButtonSize.X, RowBox.X), ReplayButtonSize.Y),
				Prefs->bSeenHowItWorks ? LOCTEXT("ReplayWelcome", "Replay the welcome guide") : LOCTEXT("WelcomeQueued", "Guide shows at next start"),
				Focus.State(ReplayWelcomeRow, Frame.Now, Prefs->bSeenHowItWorks), Color::Sky, ReplayWelcomeRow);
			Group.Next();
			const FText& Note = Notes[FMath::Clamp(Focus.Cursor.Index, 0, AccessRows - 1)];
			P.TextWrapped(Note, Font(EFont::Body, NoteSize, NoteOutline), FVector2D(Pos.X + NoteInset, RowY(Pos.Y, AccessRows, Step) + NoteGap), RowBox.X - 2.0 * NoteInset,
				Color::White);
		}

		/** The device's running controls (in a game), or null in the menus. */
		UDeviceControlsSubsystem* ActiveDeviceControls() const
		{
			UDeviceControlsSubsystem* Controls = UDeviceControlsSubsystem::Get(Host ? Host->GetWorld() : nullptr);
			return Controls && Controls->IsActive() ? Controls : nullptr;
		}

		/** The device's own controls: haptics, tilt sensitivity, the kart's steering, and calibrate. */
		void PaintDevice(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartPaintGroup& Group, const FVector2D& Pos)
		{
			static const FText Labels[CalibrateRow] = { LOCTEXT("Haptics", "Vibration"), LOCTEXT("Sensitivity", "Tilt sensitivity"), LOCTEXT("KartSteering", "Kart steering") };
			static const FText Tilt = LOCTEXT("SteerTilt", "Tilt the phone");
			static const FText Arrows = LOCTEXT("SteerArrows", "On-screen arrows");
			static const FText Notes[DeviceRows] = {
				LOCTEXT("HapticsNote", "Buzzes on throws, hits, landings and big moments."),
				LOCTEXT("SensitivityNote", "How far you tilt for a full turn or a full lean. Higher reacts to smaller moves."),
				LOCTEXT("SteeringNote", "Steer the kart by tilting the phone like a wheel, or with left and right arrows on screen. Without motion sensors the arrows always show."),
				LOCTEXT("CalibrateNote", "Hold the phone the way you play and press: that pose becomes straight ahead. Each game also calibrates as it starts."),
			};
			const UDeviceControlsSettings* Settings = UDeviceControlsSettings::Get();
			const int32 Tenths = FMath::RoundToInt32(Settings->GetSensitivity() * 10.f);
			if (Tenths != SensitivityShown)
			{
				SensitivityShown = Tenths;
				SensitivityText = FText::FromString(FString::Printf(TEXT("%.1fx"), Tenths / 10.f));
			}
			const FText* Values[CalibrateRow] = { &OnOff(MovecadeKart::Mobile::FHaptics::IsEnabled()), &SensitivityText, Settings->bTiltSteering ? &Tilt : &Arrows };
			const double Step = RowBox.Y + RowGap;
			for (int32 Row = 0; Row < CalibrateRow; ++Row)
			{
				Group.Next();
				KartWidgets::OptionRow(P, FVector2D(Pos.X, RowY(Pos.Y, Row, Step)), RowBox, Labels[Row], *Values[Row], Focus.State(Row, Frame.Now), Row);
			}
			Group.Next();
			// Calibrating needs a game running on the device's motion; in the menus each game does it as it starts.
			const bool bCanCalibrate = ActiveDeviceControls() != nullptr;
			KartWidgets::Button(P, FVector2D(Pos.X, RowY(Pos.Y, CalibrateRow, Step) + ButtonGap), FVector2D(FMath::Min(CalibrateButtonSize.X, RowBox.X), CalibrateButtonSize.Y),
				bCanCalibrate ? LOCTEXT("CalibrateNow", "Calibrate now") : LOCTEXT("CalibrateInGame", "Calibrates in each game"),
				Focus.State(CalibrateRow, Frame.Now, bCanCalibrate), Color::Mint, CalibrateRow);
			Group.Next();
			const FText& Note = Notes[FMath::Clamp(Focus.Cursor.Index, 0, DeviceRows - 1)];
			P.TextWrapped(Note, Font(EFont::Body, NoteSize, NoteOutline), FVector2D(Pos.X + NoteInset, RowY(Pos.Y, DeviceRows, Step) + NoteGap), RowBox.X - 2.0 * NoteInset,
				Color::White);
		}

		void ChangeDevice(int32 Row, int32 Dir)
		{
			UDeviceControlsSubsystem* Running = ActiveDeviceControls();
			UDeviceControlsSettings* Settings = UDeviceControlsSettings::Get();
			switch (Row)
			{
			case HapticsRow:
				MovecadeKart::Mobile::FHaptics::SetEnabled(!MovecadeKart::Mobile::FHaptics::IsEnabled());
				break;
			case SensitivityRow:
			{
				const float Before = Settings->GetSensitivity();
				if (Running)
				{
					Running->NudgeSensitivity(Dir); // saves, and applies to the game being played
				}
				else
				{
					Settings->NudgeSensitivity(Dir);
				}
				if (Settings->GetSensitivity() == Before)
				{
					Host->PlaySound(EKartUISound::Error);
					return;
				}
				break;
			}
			case SteeringRow:
				if (Running)
				{
					Running->SetTiltSteering(!Settings->bTiltSteering);
				}
				else
				{
					Settings->SetTiltSteering(!Settings->bTiltSteering);
				}
				break;
			default:
				if (!Running)
				{
					Host->PlaySound(EKartUISound::Error);
					return;
				}
				Running->Calibrate();
				Host->PlaySound(EKartUISound::Accept);
				return;
			}
			Changed(Row, Dir);
		}

		void ResetCursor()
		{
			Focus.Cursor.Count = Tab == GraphicsTab ? GraphicsRows : Tab == AudioTab ? AudioRows : Tab == AccessibilityTab ? AccessRows : bDeviceControls ? DeviceRows : 0;
			Focus.Cursor.Index = 0;
			Focus.FocusAt = NowSeconds();
		}

		void SetTab(int32 NewTab)
		{
			if (NewTab == Tab)
			{
				return;
			}
			TabDir = NewTab > Tab ? 1 : -1;
			Tab = NewTab;
			TabAt = NowSeconds();
			ResetCursor();
			Host->PlaySound(EKartUISound::Whoosh);
		}

		void Changed(int32 Row, int32 Dir)
		{
			Focus.Changed(Row, Dir, NowSeconds());
			Host->PlaySound(EKartUISound::Tick);
		}

		void Change(int32 Row, int32 Dir)
		{
			switch (Tab)
			{
			case GraphicsTab: ChangeGraphics(Row, Dir); break;
			case AudioTab: ChangeVolume(Row, Dir); break;
			case AccessibilityTab: ChangeAccessibility(Row, Dir); break;
			case ControlsTab:
				if (bDeviceControls)
				{
					ChangeDevice(Row, Dir);
				}
				break;
			default: break;
			}
		}

		/** Volumes step by a tenth and apply live. */
		void ChangeVolume(int32 Row, int32 Dir)
		{
			UKartPlayerPrefs* Prefs = GetMutableDefault<UKartPlayerPrefs>();
			float* Value = Row == Master ? &Prefs->MasterVolume : Row == Music ? &Prefs->MusicVolume : &Prefs->SfxVolume;
			const float Old = *Value;
			*Value = FMath::Clamp(FMath::RoundToFloat(*Value * 10.f + Dir) / 10.f, 0.f, 1.f);
			if (*Value == Old)
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			if (UKartUISubsystem* UI = UKartUISubsystem::Get(Host->GetWorld()))
			{
				UI->ApplyVolumes();
			}
			Changed(Row, Dir);
		}

		/** Every graphics change is staged; "Apply changes" (or leaving and confirming) applies them together. */
		void ChangeGraphics(int32 Row, int32 Dir)
		{
			UKartGameUserSettings* S = GameSettings();
			if (!S)
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			switch (Row)
			{
			case Preset:
				S->SetGraphicsPreset(static_cast<EKartGraphicsPreset>(FKartMenuCursor::Cycle(static_cast<int32>(S->GetGraphicsPreset()), Dir, NumPresets)));
				break;
			case Resolution:
			{
				if (S->GetFullscreenMode() == EWindowMode::WindowedFullscreen)
				{
					Host->PlaySound(EKartUISound::Error); // borderless always uses the desktop resolution
					return;
				}
				const int32 Index = FMath::Max(0, Resolutions.IndexOfByKey(S->GetScreenResolution()));
				S->SetScreenResolution(Resolutions[FKartMenuCursor::Cycle(Index, Dir, Resolutions.Num())]);
				break;
			}
			case DisplayMode:
			{
				static const EWindowMode::Type Modes[] = { EWindowMode::Fullscreen, EWindowMode::WindowedFullscreen, EWindowMode::Windowed };
				constexpr int32 NumModes = UE_ARRAY_COUNT(Modes);
				int32 Index = 0;
				for (int32 i = 0; i < NumModes; ++i)
				{
					Index = Modes[i] == S->GetFullscreenMode() ? i : Index;
				}
				const EWindowMode::Type Mode = Modes[FKartMenuCursor::Cycle(Index, Dir, NumModes)];
				S->SetFullscreenMode(Mode);
				if (Mode == EWindowMode::WindowedFullscreen)
				{
					S->SetScreenResolution(S->GetDesktopResolution());
				}
				break;
			}
			case VSync:
				S->SetVSyncEnabled(!S->IsVSyncEnabled());
				break;
			case FrameCap:
			{
				constexpr int32 NumCaps = UE_ARRAY_COUNT(FrameCaps);
				int32 Index = 1;
				for (int32 i = 0; i < NumCaps; ++i)
				{
					Index = FMath::IsNearlyEqual(FrameCaps[i], S->GetFrameRateLimit()) ? i : Index;
				}
				S->SetFrameRateLimit(FrameCaps[FKartMenuCursor::Cycle(Index, Dir, NumCaps)]);
				break;
			}
			case RayTracing:
				if (!UKartGameUserSettings::IsRayTracingSupported())
				{
					Host->PlaySound(EKartUISound::Error);
					return;
				}
				S->SetRayTracingEnabled(!S->IsRayTracingEnabled());
				break;
			case MotionBlur:
				S->SetMotionBlurEnabled(!S->IsMotionBlurEnabled());
				break;
			default:
				return;
			}
			Changed(Row, Dir);
		}

		void ChangeAccessibility(int32 Row, int32 Dir)
		{
			UKartPlayerPrefs* Prefs = GetMutableDefault<UKartPlayerPrefs>();
			switch (Row)
			{
			case UIScaleRow:
			{
				const TConstArrayView<float> Steps = FKartAccessibility::UIScaleSteps();
				int32 Index = 0;
				for (int32 i = 0; i < Steps.Num(); ++i)
				{
					Index = FMath::IsNearlyEqual(Steps[i], FKartAccessibility::UIScale(), 0.01f) ? i : Index;
				}
				const int32 Next = FMath::Clamp(Index + Dir, 0, Steps.Num() - 1);
				if (Next == Index)
				{
					Host->PlaySound(EKartUISound::Error);
					return;
				}
				Prefs->UIScale = Steps[Next];
				FKartAccessibility::ApplyUIScale(); // live: the menu rescales as you change it
				break;
			}
			case ReduceMotionRow:
				Prefs->bReduceMotion = !Prefs->bReduceMotion;
				break;
			case ColorVisionRow:
				Prefs->ColorVision = static_cast<uint8>(FKartMenuCursor::Cycle(Prefs->ColorVision, Dir, static_cast<int32>(EKartColorVision::Count)));
				break;
			default:
				if (!Prefs->bSeenHowItWorks)
				{
					Host->PlaySound(EKartUISound::Error);
					return;
				}
				Prefs->bSeenHowItWorks = false;
				Prefs->Save();
				Host->PlaySound(EKartUISound::Accept);
				return;
			}
			Prefs->Save();
			Changed(Row, Dir);
		}

		void Apply()
		{
			UKartGameUserSettings* S = GameSettings();
			if (!S || !S->IsDirty())
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			S->ApplySettings(false);
			Host->PlaySound(EKartUISound::Accept);
		}

		/** Back: with graphics changes still staged, ask whether to apply or discard them first. */
		bool Leave()
		{
			GetMutableDefault<UKartPlayerPrefs>()->Save();
			UKartGameUserSettings* S = GameSettings();
			if (!S || !S->IsDirty())
			{
				return false; // the stack pops
			}
			// The dialog closes before its callbacks run, so they pop this screen through the stack itself.
			IKartScreenHost* StackHost = Host;
			Host->PlaySound(EKartUISound::Accept);
			Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("ApplyTitle", "Apply your changes?"),
				LOCTEXT("ApplyBody", "Some graphics settings haven't been applied yet."), LOCTEXT("ApplyYes", "Apply"), LOCTEXT("ApplyNo", "Discard"),
				[StackHost]()
				{
					if (UKartGameUserSettings* Settings = UKartGameUserSettings::Get())
					{
						Settings->ApplySettings(false);
					}
					StackHost->PopScreen();
				},
				[StackHost]()
				{
					if (UKartGameUserSettings* Settings = UKartGameUserSettings::Get())
					{
						Settings->ResetToCurrentSettings();
					}
					StackHost->PopScreen();
				}));
			return true;
		}

		void PaintTabs(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			using namespace TabArt;
			static const FText Names[NumTabs] = { LOCTEXT("Graphics", "Graphics"), LOCTEXT("Audio", "Audio"), LOCTEXT("Controls", "Controls"), LOCTEXT("Accessibility", "Accessibility") };
			// Four 250-unit tabs on a PC; narrower where the safe area is (a portrait phone), keeping the glyphs' room.
			const double TabW = FMath::Min(Width, (Frame.SafeSize().X - 2.0 * GlyphRoom - (NumTabs - 1) * Gap) / NumTabs);
			const FVector2D TabSize(TabW, Height);
			const double Total = static_cast<int32>(NumTabs) * TabW + (NumTabs - 1) * Gap;
			const double X0 = Frame.Size.X * 0.5 - Total * 0.5;
			const double Y = Frame.SafeMin.Y + Top + (Frame.bPortrait ? PortraitDrop : 0.0);
			const float In = KartWidgets::Entrance(Frame, InAt);
			// A label wider than its tab (a phone's bigger text, a narrow tab) shrinks to fit; on a PC every one fits.
			FSlateFontInfo TabFonts[NumTabs];
			for (int32 i = 0; i < NumTabs; ++i)
			{
				TabFonts[i] = Font(EFont::Button, FontSize);
				const double Room = TabW - 2.0 * LabelInset;
				const double Wide = FKartPainter::Measure(Names[i].ToString(), TabFonts[i]).X;
				if (Wide > Room)
				{
					TabFonts[i] = Font(EFont::Button, FMath::FloorToFloat(FontSize * static_cast<float>(Room / Wide)));
				}
			}
			{
				const FKartPaintGroup Group(P);
				for (int32 i = 0; i < NumTabs; ++i)
				{
					Group.Next();
					const FVector2D Pos(X0 + i * (TabW + Gap), Y + (1.0 - In) * -DropIn);
					const bool bOn = i == Tab;
					P.Hit(TabHit + i, Pos, TabSize);
					const float TabPop = bOn ? 1.f + Pop * Ease::OutBack(static_cast<float>((Frame.Now - TabAt) / TabSwitchSeconds), PopOvershoot) : 1.f;
					const FKartPainter T = P.Transformed(Pos + TabSize * 0.5, TabPop).Faded(In);
					T.Card(Pos, TabSize, bOn ? Color::Sun : Color::WithAlpha(Color::Paper, IdleAlpha), Height * 0.5f, Shadow, bOn ? Color::White : FLinearColor::Transparent, Rim);
					T.Text(Names[i], TabFonts[i], Pos + TabSize * 0.5, Color::Navy, FVector2D(0.5, 0.5));
				}
			}
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::PrevTab, Frame.Device,
				FVector2D(X0 - GlyphGap - KartWidgets::GlyphWidth(EKartMenuAction::PrevTab, Frame.Device, GlyphHeight), Y + Height * 0.5), GlyphHeight);
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::NextTab, Frame.Device, FVector2D(X0 + Total + GlyphGap, Y + Height * 0.5), GlyphHeight);
		}

		/** The read-only controls reference: every action's keys and pad buttons. */
		void PaintControls(const FKartPainter& P, const FKartScreenFrame& Frame, const FVector2D& Pos) const
		{
			using namespace ControlsArt;
			static const FString Slash(TEXT("/"));
			P.Card(Pos, Size, Color::WithAlpha(Color::Paper, FillAlpha), Metric::RadiusLarge, Shadow);
			const double ColAction = Pos.X + ActionX, ColKeys = Pos.X + KeysX, ColPad = Pos.X + PadX;
			const FSlateFontInfo HeadFont = Font(EFont::Label, HeadSize);
			P.Text(LOCTEXT("ColAction", "ACTION"), HeadFont, FVector2D(ColAction, Pos.Y + HeadY), Color::NavyDim);
			P.Text(LOCTEXT("ColKeyboard", "KEYBOARD"), HeadFont, FVector2D(ColKeys, Pos.Y + HeadY), Color::NavyDim);
			P.Text(LOCTEXT("ColGamepad", "GAMEPAD"), HeadFont, FVector2D(ColPad, Pos.Y + HeadY), Color::NavyDim);
			const double RowH = RowHeight;
			double Y = Pos.Y + FirstRowY;
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Bindings().Num(); ++i)
			{
				Group.Next();
				const FBinding& B = Bindings()[i];
				if (i % 2 == 0)
				{
					P.Box(FVector2D(Pos.X + StripeInset, Y - RowH * 0.5), FVector2D(Size.X - 2.0 * StripeInset, RowH), Color::WithAlpha(Color::PaperShade, StripeAlpha), StripeRound);
				}
				P.Text(B.Action, Font(EFont::Button, ActionSize), FVector2D(ColAction, Y), Color::Navy, FVector2D(0.0, 0.5));
				double X = ColKeys;
				for (int32 k = 0; k < B.Keys.Num(); ++k)
				{
					if (k > 0)
					{
						X += P.Text(Slash, Font(EFont::Body, SlashSize), FVector2D(X + SlashGap, Y), Color::NavyDim, FVector2D(0.0, 0.5)).X + SlashAfter;
					}
					X += KartWidgets::KeyCap(P, B.Keys[k], FVector2D(X, Y), KeyCapHeight);
				}
				X = ColPad;
				for (const FKey& Key : B.Pad)
				{
					X += KartWidgets::PadGlyph(P, Key, FVector2D(X, Y), PadGlyphHeight) + PadGap;
				}
				Y += RowH;
			}
			Group.Next();
			P.TextWrapped(LOCTEXT("ControlsNote", "Gamepads 2 to 4 drive players 2 to 4. Hold drift through a corner and let go for a boost."),
				Font(EFont::Body, NoteFontSize), FVector2D(Pos.X + Size.X * 0.5, Y - RowH * 0.5 + NoteDrop), Size.X - 2.0 * NoteSideInset, Color::NavyDim, 0.5);
		}

		/** Everything the graphics rows' text depends on. */
		struct FGraphicsKey
		{
			int32 Preset = -1;
			FIntPoint Resolution = FIntPoint::ZeroValue;
			int32 Mode = -1;
			bool bVSync = false;
			float Cap = -1.f;
			bool bRayTracingSupported = false;
			bool bRayTracing = false;
			bool bMotionBlur = false;

			bool operator==(const FGraphicsKey& O) const
			{
				return Preset == O.Preset && Resolution == O.Resolution && Mode == O.Mode && bVSync == O.bVSync && Cap == O.Cap
					&& bRayTracingSupported == O.bRayTracingSupported && bRayTracing == O.bRayTracing && bMotionBlur == O.bMotionBlur;
			}
		};

		int32 Tab = GraphicsTab;
		/** The controls tab sets the device's own controls (see the constructor). */
		bool bDeviceControls = false;
		/** This frame's row size: the design row, narrowed to the safe area's width. */
		FVector2D RowBox = DesignRowSize;
		int32 SensitivityShown = -1;
		FText SensitivityText;
		int32 TabDir = 1;
		double TabAt = -10.0;
		FListFocus Focus;
		TArray<FIntPoint> Resolutions;
		bool bGraphicsTextValid = false;
		FGraphicsKey GraphicsKey;
		FText GraphicsValues[ApplyDisplay];
		int32 ScaleShown = -1;
		FText ScaleText;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeSettings(int32 Tab) { return MakeShared<FSettingsScreen>(Tab); }
}

#undef LOCTEXT_NAMESPACE

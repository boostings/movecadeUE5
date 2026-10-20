#include "UI/Screens/KartScreens.h"

#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/KartPlayerPrefs.h"
#include "Render/KartGameUserSettings.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "KartSettings"

using namespace KartUI;
using KartWidgets::FItemState;

namespace
{
	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	/** Graphics changes are staged on the settings object and applied together (ApplySettings), or reset. */
	UKartGameUserSettings* GameSettings()
	{
		return UKartGameUserSettings::Get();
	}

	const float FrameCaps[] = { 30.f, 60.f, 120.f, 144.f, 0.f };

	enum EGraphicsRow : int32 { Preset, Resolution, DisplayMode, VSync, FrameCap, RayTracing, MotionBlur, ApplyDisplay, GraphicsRows };
	enum EAudioRow : int32 { Master, Music, Effects, AudioRows };

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

	class FSettingsScreen final : public FKartScreen
	{
	public:
		explicit FSettingsScreen(int32 InTab) : Tab(FMath::Clamp(InTab, 0, 2))
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

		virtual FName GetId() const override { return Tab == 0 ? FName(TEXT("settings")) : Tab == 1 ? FName(TEXT("audio")) : FName(TEXT("controls")); }

		virtual void OnShown() override { FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			TArray<FKartHint> Hints = { { EKartMenuAction::PrevTab, FText::GetEmpty() }, { EKartMenuAction::NextTab, LOCTEXT("Tabs", "Tabs") } };
			if (Tab != 2)
			{
				Hints.Add({ EKartMenuAction::Left, LOCTEXT("Change", "Change") });
			}
			Hints.Add({ EKartMenuAction::Back, LOCTEXT("Done", "Done") });
			return Hints;
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			const double Now = NowSeconds();
			switch (Event.Action)
			{
			case EKartMenuAction::PrevTab:
			case EKartMenuAction::NextTab:
				SetTab(FKartMenuCursor::Cycle(Tab, Event.Action == EKartMenuAction::PrevTab ? -1 : 1, 3));
				return true;
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				if (Cursor.Move(Event.Action == EKartMenuAction::Up ? -1 : 1))
				{
					FocusAt = Now;
					Host->PlaySound(EKartUISound::Move);
				}
				return true;
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Change(Cursor.Index, Event.Action == EKartMenuAction::Left ? -1 : 1);
				return true;
			case EKartMenuAction::Accept:
				if (Tab == 0 && Cursor.Index == ApplyDisplay)
				{
					Apply();
				}
				else if (Tab != 2)
				{
					Change(Cursor.Index, 1);
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
			if (Base >= TabHit)
			{
				return;
			}
			if (Cursor.Set(Base))
			{
				FocusAt = NowSeconds();
				Host->PlaySound(EKartUISound::Move);
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
			Cursor.Set(Base);
			if (Tab == 0 && Base == ApplyDisplay)
			{
				Apply();
			}
			else if (Tab != 2)
			{
				Change(Base, KartWidgets::HitArrow(HitId) < 0 ? -1 : 1);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const float Dim = KartWidgets::Entrance(Frame, 0.f, 0.3f);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.66f * Dim));
			KartWidgets::Header(P, Frame, LOCTEXT("SettingsTitle", "Settings"), FText::GetEmpty(), Color::Grape);
			PaintTabs(P, Frame);
			const double Left = Frame.Size.X * 0.5 - 520.0 + KartWidgets::SlideX(Frame);
			const double Top = Frame.SafeMin.Y + 230.0;
			const FVector2D RowSize(1040.0, 72.0);
			const double Step = RowSize.Y + 12.0;
			const float TabIn = Ease::OutCubic(static_cast<float>((Frame.Now - TabAt) / 0.25));
			const FKartPainter Body = P.Faded(TabIn);
			const double Shift = (1.0 - TabIn) * 60.0 * TabDir;

			if (Tab == 0)
			{
				const UKartGameUserSettings* S = GameSettings();
				const FText Presets[] = { LOCTEXT("Performance", "Performance"), LOCTEXT("Balanced", "Balanced"), LOCTEXT("QualityPreset", "Quality"), LOCTEXT("Ultra", "Ultra") };
				const int32 PresetIndex = S ? static_cast<int32>(S->GetGraphicsPreset()) : 3;
				const float Cap = S ? S->GetFrameRateLimit() : 0.f;
				const bool bPending = S && S->IsDirty();
				const bool bRayTracing = UKartGameUserSettings::IsRayTracingSupported();
				const FIntPoint Res = S ? S->GetScreenResolution() : FIntPoint(1920, 1080);
				const EWindowMode::Type Mode = S ? S->GetFullscreenMode() : EWindowMode::Windowed;
				struct FRow { FText Label; FText Value; bool bEnabled; };
				const FRow Rows[] = {
					{ LOCTEXT("Quality", "Graphics preset"), Presets[FMath::Clamp(PresetIndex, 0, 3)], true },
					{ LOCTEXT("Resolution", "Resolution"), FText::FromString(FString::Printf(TEXT("%d x %d"), Res.X, Res.Y)), Mode != EWindowMode::WindowedFullscreen },
					{ LOCTEXT("DisplayMode", "Display mode"), ModeName(Mode), true },
					{ LOCTEXT("VSync", "V-sync"), OnOff(S && S->IsVSyncEnabled()), true },
					{ LOCTEXT("FrameCap", "Frame rate cap"), Cap <= 0.f ? LOCTEXT("Unlimited", "Unlimited") : FText::Format(LOCTEXT("Fps", "{0} fps"), FMath::RoundToInt32(Cap)), true },
					{ LOCTEXT("RayTracing", "Ray tracing"), bRayTracing ? OnOff(S && S->IsRayTracingEnabled()) : LOCTEXT("Unsupported", "Not supported"), bRayTracing },
					{ LOCTEXT("MotionBlur", "Motion blur"), OnOff(S && S->IsMotionBlurEnabled()), true },
				};
				for (int32 Row = 0; Row < UE_ARRAY_COUNT(Rows); ++Row)
				{
					KartWidgets::OptionRow(Body, FVector2D(Left + Shift, Top + Row * Step), RowSize, Rows[Row].Label, Rows[Row].Value, State(Row, Frame, Rows[Row].bEnabled), Row);
				}
				FItemState ApplyState = State(ApplyDisplay, Frame, bPending);
				KartWidgets::Button(Body, FVector2D(Left + Shift, Top + ApplyDisplay * Step + 10.0), FVector2D(520.0, 80.0),
					bPending ? LOCTEXT("ApplyChanges", "Apply changes") : LOCTEXT("Applied", "All changes applied"), ApplyState, Color::Mint, ApplyDisplay);
			}
			else if (Tab == 1)
			{
				const UKartPlayerPrefs* Prefs = GetDefault<UKartPlayerPrefs>();
				const FText Labels[] = { LOCTEXT("Master", "Master volume"), LOCTEXT("MusicVol", "Music"), LOCTEXT("Sfx", "Sound effects") };
				const float Values[] = { Prefs->MasterVolume, Prefs->MusicVolume, Prefs->SfxVolume };
				for (int32 Row = 0; Row < AudioRows; ++Row)
				{
					KartWidgets::SliderRow(Body, FVector2D(Left + Shift, Top + Row * (Step + 12.0)), FVector2D(RowSize.X, 84.0), Labels[Row], Values[Row], State(Row, Frame), Row);
				}
			}
			else
			{
				PaintControls(Body, Frame, FVector2D(Left + Shift, Top - 10.0));
			}
		}

	private:
		static constexpr int32 TabHit = 900;

		static FText OnOff(bool bOn) { return bOn ? LOCTEXT("On", "On") : LOCTEXT("Off", "Off"); }

		static FText ModeName(EWindowMode::Type Mode)
		{
			switch (Mode)
			{
			case EWindowMode::Fullscreen: return LOCTEXT("Fullscreen", "Fullscreen");
			case EWindowMode::WindowedFullscreen: return LOCTEXT("Borderless", "Borderless");
			default: return LOCTEXT("Windowed", "Windowed");
			}
		}

		FItemState State(int32 Row, const FKartScreenFrame& Frame, bool bEnabled = true) const
		{
			FItemState S;
			S.bFocused = Row == Cursor.Index;
			S.bEnabled = bEnabled;
			S.FocusAge = Frame.Now - FocusAt;
			if (Row == ChangedRow)
			{
				S.ChangeAge = Frame.Now - ChangeAt;
				S.ChangeDir = ChangeDir;
			}
			return S;
		}

		void ResetCursor()
		{
			Cursor.Count = Tab == 0 ? GraphicsRows : Tab == 1 ? AudioRows : 0;
			Cursor.Index = 0;
			FocusAt = NowSeconds();
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
			ChangedRow = Row;
			ChangeDir = Dir;
			ChangeAt = NowSeconds();
			Host->PlaySound(EKartUISound::Tick);
		}

		void Change(int32 Row, int32 Dir)
		{
			if (Tab == 1)
			{
				UKartPlayerPrefs* Prefs = GetMutableDefault<UKartPlayerPrefs>();
				float* Value = Row == Master ? &Prefs->MasterVolume : Row == Music ? &Prefs->MusicVolume : &Prefs->SfxVolume;
				const float Old = *Value;
				*Value = FMath::Clamp(FMath::RoundToFloat(*Value * 10.f + Dir) / 10.f, 0.f, 1.f);
				if (*Value != Old)
				{
					if (UKartUISubsystem* UI = UKartUISubsystem::Get(Host->GetWorld()))
					{
						UI->ApplyVolumes();
					}
					Changed(Row, Dir);
				}
				else
				{
					Host->PlaySound(EKartUISound::Error);
				}
				return;
			}
			if (Tab != 0)
			{
				return;
			}
			UKartGameUserSettings* S = GameSettings();
			if (!S)
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			// Every graphics change is staged; "Apply changes" (or leaving and confirming) applies them together.
			switch (Row)
			{
			case Preset:
				S->SetGraphicsPreset(static_cast<EKartGraphicsPreset>(FKartMenuCursor::Cycle(static_cast<int32>(S->GetGraphicsPreset()), Dir, 4)));
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
				const EWindowMode::Type Modes[] = { EWindowMode::Fullscreen, EWindowMode::WindowedFullscreen, EWindowMode::Windowed };
				int32 Index = 0;
				for (int32 i = 0; i < 3; ++i)
				{
					Index = Modes[i] == S->GetFullscreenMode() ? i : Index;
				}
				const EWindowMode::Type Mode = Modes[FKartMenuCursor::Cycle(Index, Dir, 3)];
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
				int32 Index = 1;
				for (int32 i = 0; i < UE_ARRAY_COUNT(FrameCaps); ++i)
				{
					Index = FMath::IsNearlyEqual(FrameCaps[i], S->GetFrameRateLimit()) ? i : Index;
				}
				S->SetFrameRateLimit(FrameCaps[FKartMenuCursor::Cycle(Index, Dir, UE_ARRAY_COUNT(FrameCaps))]);
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

		void PaintTabs(const FKartPainter& P, const FKartScreenFrame& Frame)
		{
			const FText Names[] = { LOCTEXT("Graphics", "Graphics"), LOCTEXT("Audio", "Audio"), LOCTEXT("Controls", "Controls") };
			const FSlateFontInfo TabFont = Font(EFont::Button, 30.f);
			const double W = 260.0, H = 66.0, Gap = 16.0;
			const double Total = 3 * W + 2 * Gap;
			const double X0 = Frame.Size.X * 0.5 - Total * 0.5;
			const double Y = Frame.SafeMin.Y + 120.0;
			const float In = KartWidgets::Entrance(Frame, 0.05f);
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2D Pos(X0 + i * (W + Gap), Y + (1.0 - In) * -40.0);
				const bool bOn = i == Tab;
				P.Hit(TabHit + i, Pos, FVector2D(W, H));
				const float Pop = bOn ? 1.f + 0.06f * Ease::OutBack(static_cast<float>((Frame.Now - TabAt) / 0.25), 3.f) : 1.f;
				const FKartPainter T = P.Transformed(Pos + FVector2D(W, H) * 0.5, Pop).Faded(In);
				T.Card(Pos, FVector2D(W, H), bOn ? Color::Sun : Color::WithAlpha(Color::Paper, 0.9f), H * 0.5f, 6.f, bOn ? Color::White : FLinearColor::Transparent, 4.f);
				T.Text(Names[i], TabFont, Pos + FVector2D(W, H) * 0.5, Color::Navy, FVector2D(0.5, 0.5));
			}
			const float GH = 40.f;
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::PrevTab, Frame.Device, FVector2D(X0 - 24.0 - KartWidgets::GlyphWidth(EKartMenuAction::PrevTab, Frame.Device, GH), Y + H * 0.5), GH);
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::NextTab, Frame.Device, FVector2D(X0 + Total + 24.0, Y + H * 0.5), GH);
		}

		void PaintControls(const FKartPainter& P, const FKartScreenFrame& Frame, const FVector2D& Pos)
		{
			const FVector2D Size(1040.0, 640.0);
			P.Card(Pos, Size, Color::WithAlpha(Color::Paper, 0.96f), Metric::RadiusLarge, 10.f);
			const double ColAction = Pos.X + 40.0, ColKeys = Pos.X + 420.0, ColPad = Pos.X + 760.0;
			const FSlateFontInfo HeadFont = Font(EFont::Label, 20.f);
			P.Text(LOCTEXT("ColAction", "ACTION"), HeadFont, FVector2D(ColAction, Pos.Y + 28.0), Color::NavyDim);
			P.Text(LOCTEXT("ColKeyboard", "KEYBOARD"), HeadFont, FVector2D(ColKeys, Pos.Y + 28.0), Color::NavyDim);
			P.Text(LOCTEXT("ColGamepad", "GAMEPAD"), HeadFont, FVector2D(ColPad, Pos.Y + 28.0), Color::NavyDim);
			const double RowH = 64.0;
			double Y = Pos.Y + 92.0;
			for (int32 i = 0; i < Bindings().Num(); ++i)
			{
				const FBinding& B = Bindings()[i];
				if (i % 2 == 0)
				{
					P.Box(FVector2D(Pos.X + 16.0, Y - RowH * 0.5), FVector2D(Size.X - 32.0, RowH), Color::WithAlpha(Color::PaperShade, 0.7f), 14.f);
				}
				P.Text(B.Action, Font(EFont::Button, 28.f), FVector2D(ColAction, Y), Color::Navy, FVector2D(0.0, 0.5));
				double X = ColKeys;
				for (int32 k = 0; k < B.Keys.Num(); ++k)
				{
					if (k > 0)
					{
						X += P.Text(TEXT("/"), Font(EFont::Body, 24.f), FVector2D(X + 6.0, Y), Color::NavyDim, FVector2D(0.0, 0.5)).X + 14.0;
					}
					X += KartWidgets::KeyCap(P, B.Keys[k], FVector2D(X, Y), 42.f);
				}
				X = ColPad;
				for (const FKey& Key : B.Pad)
				{
					X += KartWidgets::PadGlyph(P, Key, FVector2D(X, Y), 44.f) + 12.0;
				}
				Y += RowH;
			}
			P.TextWrapped(LOCTEXT("ControlsNote", "Gamepads 2 to 4 drive players 2 to 4. Hold drift through a corner and let go for a boost."),
				Font(EFont::Body, 20.f), FVector2D(Pos.X + Size.X * 0.5, Pos.Y + Size.Y - 60.0), Size.X - 80.0, Color::NavyDim, 0.5);
		}

		int32 Tab = 0;
		int32 TabDir = 1;
		double TabAt = -10.0;
		FKartMenuCursor Cursor;
		double FocusAt = 0.0;
		int32 ChangedRow = INDEX_NONE;
		int32 ChangeDir = 0;
		double ChangeAt = -10.0;
		TArray<FIntPoint> Resolutions;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeSettings(int32 Tab) { return MakeShared<FSettingsScreen>(Tab); }
}

#undef LOCTEXT_NAMESPACE

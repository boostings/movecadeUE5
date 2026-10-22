#include "UI/KartScreen.h"

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/KartWidgets.h"

#define LOCTEXT_NAMESPACE "KartScreen"

bool FKartMenuCursor::Move(int32 Delta)
{
	if (Count <= 0)
	{
		return false;
	}
	const int32 Old = Index;
	Index = bWrap ? Cycle(Index, Delta, Count) : FMath::Clamp(Index + Delta, 0, Count - 1);
	return Index != Old;
}

bool FKartMenuCursor::Set(int32 NewIndex)
{
	const int32 Old = Index;
	Index = Count > 0 ? FMath::Clamp(NewIndex, 0, Count - 1) : 0;
	return Index != Old;
}

int32 FKartMenuCursor::Cycle(int32 Value, int32 Delta, int32 Num)
{
	return Num <= 0 ? 0 : (((Value + Delta) % Num) + Num) % Num;
}

TArray<FKartHint> FKartScreen::GetHints() const
{
	TArray<FKartHint> Hints;
	Hints.Add({ EKartMenuAction::Accept, LOCTEXT("Select", "Select") });
	if (CanGoBack())
	{
		Hints.Add({ EKartMenuAction::Back, LOCTEXT("Back", "Back") });
	}
	return Hints;
}

SKartScreenStack::SKartScreenStack()
{
	SetCanTick(false);
}

void SKartScreenStack::Construct(const FArguments& InArgs)
{
	World = InArgs._World;
	Background = InArgs._Background;
	bShowHints = InArgs._ShowHints;
	// Every screen animates from the clock: repaint each frame even under global invalidation.
	ForceVolatile(true);
}

const TSharedPtr<FKartScreen>& SKartScreenStack::Top() const
{
	static const TSharedPtr<FKartScreen> None;
	return Stack.Num() > 0 ? Stack.Last() : None;
}

void SKartScreenStack::Clear()
{
	Stack.Reset();
	Leaving.Reset();
}

void SKartScreenStack::Show(const TSharedRef<FKartScreen>& Screen, float InDirection)
{
	Screen->Host = this;
	Direction = InDirection;
	ShownAt = FSlateApplication::Get().GetCurrentTime();
	HoverId = INDEX_NONE;
	Hits.Reset();
	bHintsDirty = true;
	Screen->OnShown();
}

void SKartScreenStack::PushScreen(const TSharedRef<FKartScreen>& Screen)
{
	Leaving = Screen->IsModal() ? nullptr : Top();
	Stack.Add(Screen);
	Show(Screen, 1.f);
}

void SKartScreenStack::PopScreen()
{
	if (Stack.Num() == 0)
	{
		return;
	}
	const TSharedPtr<FKartScreen> Popped = Stack.Pop();
	Leaving = Popped->IsModal() ? nullptr : Popped;
	if (Stack.Num() > 0)
	{
		Show(Stack.Last().ToSharedRef(), -1.f);
		if (Popped->IsModal())
		{
			ShownAt -= TransitionSeconds; // the screen under a dialog never left
		}
	}
}

void SKartScreenStack::PopTo(const FKartScreen* Screen)
{
	if (!Stack.ContainsByPredicate([Screen](const TSharedPtr<FKartScreen>& S) { return S.Get() == Screen; }))
	{
		return;
	}
	const TSharedPtr<FKartScreen> Old = Top();
	while (Stack.Num() > 0 && Stack.Last().Get() != Screen)
	{
		Stack.Pop();
	}
	Leaving = Old;
	Show(Stack.Last().ToSharedRef(), -1.f);
}

void SKartScreenStack::ReplaceScreen(const TSharedRef<FKartScreen>& Screen)
{
	Leaving = Top();
	if (Stack.Num() > 0)
	{
		Stack.Pop();
	}
	Stack.Add(Screen);
	Show(Screen, 1.f);
}

void SKartScreenStack::PlaySound(EKartUISound Sound) const
{
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(World.Get()))
	{
		UI->PlaySound(Sound);
	}
}

EKartInputDevice SKartScreenStack::GetDevice() const
{
	const UKartUISubsystem* UI = UKartUISubsystem::Get(World.Get());
	return UI ? UI->GetInputDevice() : EKartInputDevice::Keyboard;
}

bool SKartScreenStack::HandleMenuEvent(const FKartMenuEvent& Event)
{
	const TSharedPtr<FKartScreen> Screen = Top();
	if (!Screen.IsValid() || GetVisibility() == EVisibility::Collapsed)
	{
		return false;
	}
	// Input during the first part of a transition is dropped, so a double press can't skip a screen.
	if (FSlateApplication::Get().GetCurrentTime() - ShownAt < TransitionSeconds * 0.5)
	{
		return Event.Action != EKartMenuAction::None;
	}
	bHintsDirty = true; // a handled key may change what the screen offers (a tab, a ready state)
	if (Screen->HandleEvent(Event))
	{
		return true;
	}
	if (Event.Action == EKartMenuAction::Back && Screen->CanGoBack())
	{
		if (Stack.Num() > 1)
		{
			PlaySound(EKartUISound::Back);
			PopScreen();
		}
		else if (OnBackAtRoot)
		{
			OnBackAtRoot();
		}
		return true;
	}
	// The stack is modal: every mapped key stays in the menus.
	return Event.Action != EKartMenuAction::None;
}

FVector2D SKartScreenStack::ToCanvas(const FGeometry& Geometry, const FVector2D& ScreenPos) const
{
	return FVector2D(Geometry.AbsoluteToLocal(ScreenPos)) / FMath::Max(Fit, 0.01f);
}

int32 SKartScreenStack::HitAt(const FVector2D& Local) const
{
	// Arrows are registered after their row, so search from the top.
	for (int32 i = Hits.Num() - 1; i >= 0; --i)
	{
		if (Hits[i].Contains(Local))
		{
			return Hits[i].Id;
		}
	}
	return INDEX_NONE;
}

FReply SKartScreenStack::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const TSharedPtr<FKartScreen> Screen = Top();
	if (!Screen.IsValid())
	{
		return FReply::Unhandled();
	}
	const int32 Id = HitAt(ToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	if (Id != HoverId)
	{
		HoverId = Id;
		if (Id != INDEX_NONE)
		{
			Screen->OnHover(Id);
		}
	}
	return FReply::Handled();
}

FReply SKartScreenStack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Top().IsValid())
	{
		return FReply::Unhandled();
	}
	PressedId = HitAt(ToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	return FReply::Handled();
}

FReply SKartScreenStack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const TSharedPtr<FKartScreen> Screen = Top();
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && Screen.IsValid())
	{
		// Right click is Back, like a pad's B.
		FKartMenuEvent Back;
		Back.Action = EKartMenuAction::Back;
		Back.Device = EKartInputDevice::Mouse;
		HandleMenuEvent(Back);
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Screen.IsValid())
	{
		return FReply::Unhandled();
	}
	const int32 Id = HitAt(ToCanvas(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	if (Id != INDEX_NONE && Id == PressedId && FSlateApplication::Get().GetCurrentTime() - ShownAt > TransitionSeconds * 0.5)
	{
		Screen->OnClick(Id);
		bHintsDirty = true;
	}
	PressedId = INDEX_NONE;
	return FReply::Handled();
}

int32 SKartScreenStack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	Hits.Reset();
	// Screens are laid out on a canvas at least 1080 units tall. A UI scale above 100 % makes the widget shorter
	// than that; the menus then scale back down to fit (the HUDs still grow), so no layout ever runs off screen.
	const FVector2D Allotted(AllottedGeometry.GetLocalSize());
	Fit = Allotted.Y > 1.0 ? FMath::Min(1.f, static_cast<float>(Allotted.Y / KartUI::Metric::CanvasH)) : 1.f;
	const FGeometry Canvas = Fit < 1.f ? AllottedGeometry.MakeChild(FVector2f(Allotted / Fit), FSlateLayoutTransform(Fit)) : AllottedGeometry;
	FKartPainter P(Canvas, OutDrawElements, Layer, &Hits);

	FKartScreenFrame Frame;
	Frame.Size = P.Size();
	LastSize = Frame.Size;
	const FVector2D Margin = Frame.Size * KartUI::Metric::SafeFraction;
	Frame.SafeMin = Margin;
	Frame.SafeMax = Frame.Size - Margin;
	Frame.Now = FSlateApplication::Get().GetCurrentTime();
	Frame.Device = GetDevice();
	Frame.Direction = Direction;
	const double Since = Frame.Now - ShownAt;

	if (Background)
	{
		FKartScreenFrame Back = Frame;
		Back.Age = Since;
		Background(P, Back);
	}

	// The leaving screen slides out the other way and fades.
	const float T = FMath::Clamp(static_cast<float>(Since / TransitionSeconds), 0.f, 1.f);
	if (Leaving.IsValid() && T < 1.f)
	{
		FKartScreenFrame Out = Frame;
		Out.Presence = 1.f - T;
		Out.Direction = -Direction;
		Out.Age = 10.0;
		const FVector2D Shift(-Direction * KartUI::Ease::InCubic(T) * 160.0, 0.0);
		Leaving->Paint(P.Transformed(Frame.Centre(), 1.f, 0.f, Shift).Faded(1.f - T), Out);
	}
	const TSharedPtr<FKartScreen> Screen = Top();
	if (Screen.IsValid())
	{
		// A dialog paints over the screen beneath it.
		if (Screen->IsModal() && Stack.Num() > 1)
		{
			FKartScreenFrame Under = Frame;
			Under.Age = 10.0;
			Stack[Stack.Num() - 2]->Paint(P.Faded(0.999f), Under);
			Hits.Reset();
		}
		Frame.Age = Since;
		Frame.Presence = KartUI::Ease::OutCubic(T);
		if (T < 1.f && !Screen->IsModal())
		{
			const FVector2D Shift(Direction * (1.f - KartUI::Ease::OutCubic(T)) * 160.0, 0.0);
			Screen->Paint(P.Transformed(Frame.Centre(), 1.f, 0.f, Shift).Faded(Frame.Presence), Frame);
		}
		else
		{
			Screen->Paint(P, Frame);
		}
		if (bShowHints)
		{
			// Hints are rebuilt only when something could change them (plus a slow safety refresh), not every paint.
			if (bHintsDirty || HintsScreen != Screen.Get() || HintsDevice != Frame.Device || Frame.Now - HintsAt > 0.5)
			{
				CachedHints = Screen->GetHints();
				bHintsDirty = false;
				HintsScreen = Screen.Get();
				HintsDevice = Frame.Device;
				HintsAt = Frame.Now;
			}
			KartWidgets::Hints(P.Faded(Frame.Presence), Frame, CachedHints);
		}
	}
	return Layer;
}

#undef LOCTEXT_NAMESPACE

// UKartLobbyWidget's text and colours, from the room host. Refreshed when the room's lobby revision moves (a state
// frame, the connection, the selected game) or the lobby countdown ticks a second, never on a timer.

#include "Net/KartLobbyLayout.h"
#include "Net/KartLobbyWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Texture2D.h"
#include "Games/ArcadeCatalog.h"
#include "Net/RoomHostSubsystem.h"
#include "Net/RoomProtocol.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartUIStyle.h"

using namespace KartUI;

namespace
{
	const TCHAR* const NoCode = TEXT("------");
	const FLinearColor ReadyGreen = FLinearColor::FromSRGBColor(FColor(0x1f, 0xa8, 0x72));
	/** An empty seat's card fades back a little. */
	constexpr float EmptySeatAlpha = 0.82f;
	constexpr float UnselectedTrackAlpha = 0.9f;

	/** The lobby's game list (kart first, then the arcade catalog's order), shared with the carousel's keys. */
	int32 LobbyGameIndex(const FString& GameId, int32& OutCount)
	{
		const TArray<MovecadeKart::Arcade::FGameInfo>& Games = MovecadeKart::Arcade::Games();
		OutCount = Games.Num() + 1;
		const int32 Found = Games.IndexOfByPredicate([&GameId](const MovecadeKart::Arcade::FGameInfo& G) { return G.Id == GameId; });
		return Found == INDEX_NONE ? 0 : Found + 1;
	}

	/** The join URL as shown under the code: no scheme, and never the token (that stays in the QR). */
	FString DisplayUrl(FString Url)
	{
		Url.Split(TEXT("#"), &Url, nullptr);
		Url.RemoveFromStart(TEXT("https://"));
		Url.RemoveFromStart(TEXT("http://"));
		return Url;
	}

	/** The countdown's whole second (5..1), 0 at "go", INDEX_NONE when none runs. */
	int32 CountdownSecond(int32 RemainingMs)
	{
		return RemainingMs < 0 ? INDEX_NONE : FMath::CeilToInt32(RemainingMs / 1000.f);
	}
}

void UKartLobbyWidget::RefreshIfChanged()
{
	const URoomHostSubsystem* Subsystem = Room();
	if (!Subsystem)
	{
		return;
	}
	const uint32 Revision = Subsystem->GetLobbyRevision();
	const int32 Second = CountdownSecond(Subsystem->GetLobbyCountdownRemainingMs());
	if (bShownOnce && Revision == ShownRevision && Second == ShownCountdownSecond)
	{
		return;
	}
	Refresh();
}

void UKartLobbyWidget::Refresh()
{
	const URoomHostSubsystem* Subsystem = Room();
	if (!Subsystem || !CodeText)
	{
		return;
	}
	ShownRevision = Subsystem->GetLobbyRevision();
	ShownCountdownSecond = CountdownSecond(Subsystem->GetLobbyCountdownRemainingMs());
	bShownOnce = true;

	const FMcRoomState& State = Subsystem->GetRoom();
	RefreshJoinCard(*Subsystem);
	RefreshGameCard(*Subsystem);
	RefreshTracks(State);
	RefreshSeats(State);
	RefreshFooter(*Subsystem, State);
}

void UKartLobbyWidget::RefreshJoinCard(const URoomHostSubsystem& Subsystem)
{
	const FString Code = Subsystem.GetJoinCode();
	CodeText->SetText(FText::FromString(Code.IsEmpty() ? NoCode : Code));
	UrlText->SetText(FText::FromString(DisplayUrl(Subsystem.GetJoinUrl())));

	UTexture2D* Qr = Subsystem.GetJoinQrTexture();
	if (Qr != ShownQr)
	{
		ShownQr = Qr;
		QrImage->SetBrushFromTexture(Qr, false);
		QrImage->SetOpacity(Qr ? 1.f : 0.f);
	}

	FString Line;
	FLinearColor Ink = Color::Navy;
	switch (Subsystem.GetConnectionState())
	{
	case EMcConnection::Idle:
		Line = TEXT("Room closed.");
		break;
	case EMcConnection::Connecting:
		Line = TEXT("Connecting to the room server...");
		break;
	case EMcConnection::Reconnecting:
		Line = TEXT("Reconnecting... everyone keeps their spot.");
		Ink = Color::SunDeep;
		break;
	case EMcConnection::Open:
		Line = Subsystem.IsHost() ? TEXT("Room open. Phones join from the QR code.") : TEXT("Another screen is hosting this room.");
		break;
	case EMcConnection::Failed:
		// The one refusal a player can't fix from here: the server hasn't admitted this host yet.
		Line = Subsystem.GetLastError().StartsWith(MovecadeKart::Net::ErrorUnauthorizedOrigin, ESearchCase::CaseSensitive)
			? FString::Printf(TEXT("%s doesn't accept this game as a host yet (it needs the server update in Docs/Port/server)."), *Subsystem.PartyHost)
			: TEXT("Room server error: ") + Subsystem.GetLastError();
		Ink = Color::Hot;
		break;
	}
	ConnectionText->SetText(FText::FromString(Line));
	ConnectionText->SetColorAndOpacity(FSlateColor(Ink));
}

void UKartLobbyWidget::RefreshGameCard(const URoomHostSubsystem& Subsystem)
{
	const FString GameId = Subsystem.GetGameId();
	const bool bKart = GameId == TEXT("kart");
	const MovecadeKart::Arcade::FGameInfo* Info = MovecadeKart::Arcade::Find(GameId);
	int32 GameCountTotal = 0;
	const int32 GameIndex = LobbyGameIndex(GameId, GameCountTotal);
	GameName->SetText(Info ? Info->Name : FText::FromString(TEXT("Kart Racer")));
	GameTagline->SetText(Info ? Info->Tagline : FText::FromString(TEXT("Race the harbour and the violet loop. Drift, boost and fire items.")));
	GameAccent->SetBrushColor(Info ? Info->Accent : Color::Hot);
	GameCount->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), GameIndex + 1, GameCountTotal)));
	TrackSection->SetVisibility(bKart ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (bKart)
	{
		SubtitleText->SetText(FText::FromString(TEXT("Phones are the controllers: scan the code, tilt to steer.")));
	}
	else
	{
		SubtitleText->SetText(Info && !Info->PhoneHint.IsEmpty() ? Info->PhoneHint : FText::FromString(TEXT("Phones are the controllers: scan the code to join.")));
	}
}

void UKartLobbyWidget::RefreshTracks(const FMcRoomState& State)
{
	const TArray<FKartTrackInfo>& Catalog = FKartTrackCatalog::All();
	for (int32 i = 0; i < TrackButtons.Num() && i < Catalog.Num(); ++i)
	{
		const bool bSelected = State.SelectedMapId == Catalog[i].Id;
		const double Scale = bSelected ? MovecadeKart::Lobby::LobbyMetric::SelectedScale : 1.0;
		TrackButtons[i]->SetBackgroundColor(bSelected ? Color::Sun : Color::WithAlpha(Color::Paper, UnselectedTrackAlpha));
		TrackButtons[i]->SetRenderScale(FVector2D(Scale, Scale));
	}
}

void UKartLobbyWidget::RefreshSeats(const FMcRoomState& State)
{
	for (int32 i = 0; i < SeatRows.Num(); ++i)
	{
		const FMcSeat* Seat = State.FindBySeat(i + 1);
		if (!Seat)
		{
			SeatRows[i]->SetBrushColor(Color::WithAlpha(Color::Paper, EmptySeatAlpha));
			SeatSwatches[i]->SetBrushColor(Color::PaperShade);
			SeatNames[i]->SetText(FText::FromString(TEXT("Waiting for a phone")));
			SeatNames[i]->SetColorAndOpacity(FSlateColor(Color::NavyDim));
			SeatStates[i]->SetText(FText::GetEmpty());
			continue;
		}
		SeatRows[i]->SetBrushColor(Color::Paper);
		SeatSwatches[i]->SetBrushColor(Seat->PaintColor);
		SeatNames[i]->SetText(FText::FromString(Seat->bBot ? Seat->Name + TEXT("  (bot)") : Seat->Name));
		SeatNames[i]->SetColorAndOpacity(FSlateColor(Color::Navy));
		FString Status;
		FLinearColor StatusColor = Color::NavyDim;
		if (Seat->bEliminated)
		{
			Status = TEXT("Out");
		}
		else if (!Seat->bConnected)
		{
			Status = FString::Printf(TEXT("Reconnecting · %ds"), FMath::CeilToInt32(Seat->GraceRemainingMs / 1000.f));
			StatusColor = Color::SunDeep;
		}
		else if (Seat->bReady)
		{
			Status = TEXT("Ready!");
			StatusColor = ReadyGreen;
		}
		else
		{
			Status = Seat->bCalibrated ? TEXT("Not ready") : TEXT("Calibrating");
		}
		SeatStates[i]->SetText(FText::FromString(Status));
		SeatStates[i]->SetColorAndOpacity(FSlateColor(StatusColor));
	}
}

void UKartLobbyWidget::RefreshFooter(const URoomHostSubsystem& Subsystem, const FMcRoomState& State)
{
	const bool bSolo = State.Players.Num() == 1 && !State.Players[0].bBot;
	const int32 CountdownMs = Subsystem.GetLobbyCountdownRemainingMs();
	FString Line;
	if (CountdownMs > 0)
	{
		Line = FString::Printf(TEXT("Starting in %d..."), FMath::CeilToInt32(CountdownMs / 1000.f));
	}
	else if (CountdownMs == 0 || State.Status == EMcRoomStatus::InProgress)
	{
		Line = TEXT("Here we go!");
	}
	else
	{
		Line = Subsystem.GetStartBlockedReason();
		if (Line.IsEmpty())
		{
			Line = bSolo ? TEXT("Everyone ready. Start solo when you are.") : TEXT("Everyone ready!");
		}
	}
	StatusText->SetText(FText::FromString(Line));
	StartLabel->SetText(FText::FromString(bSolo ? TEXT("Start solo") : TEXT("Start")));
	const bool bCanStart = Subsystem.CanStart();
	StartButton->SetIsEnabled(bCanStart);
	StartButton->SetBackgroundColor(bCanStart ? Color::Hot : Color::NavyDim);
}

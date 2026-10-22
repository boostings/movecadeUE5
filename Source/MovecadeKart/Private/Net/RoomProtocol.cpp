#include "Net/RoomProtocol.h"

#include "Dom/JsonObject.h"
#include "Misc/Guid.h"

namespace MovecadeKart::Net
{
	const TCHAR* const RoomCodeAlphabet = TEXT("23456789ABCDEFGHJKMNPQRSTUVWXYZ");

	namespace
	{
		int16 ReadI16(const uint8* Data, int32 Offset)
		{
			return static_cast<int16>(static_cast<uint16>(Data[Offset]) | (static_cast<uint16>(Data[Offset + 1]) << 8));
		}

		uint16 ReadU16(const uint8* Data, int32 Offset)
		{
			return static_cast<uint16>(Data[Offset]) | (static_cast<uint16>(Data[Offset + 1]) << 8);
		}

		FString GetString(const FJsonObject& Object, const TCHAR* Key)
		{
			FString Value;
			Object.TryGetStringField(Key, Value);
			return Value;
		}

		bool GetBool(const FJsonObject& Object, const TCHAR* Key)
		{
			bool bValue = false;
			Object.TryGetBoolField(Key, bValue);
			return bValue;
		}

		/** A JSON number as an integer, truncated like a C cast; Default if it is missing, NaN or beyond int64 (where
		 *  the cast is undefined). */
		int64 GetInt64(const FJsonObject& Object, const TCHAR* Key, int64 Default = 0)
		{
			double Value = 0.0;
			return Object.TryGetNumberField(Key, Value) && FMath::Abs(Value) < 9.2e18 ? static_cast<int64>(Value) : Default;
		}

		/** As GetInt64, saturated to int32: a seat of 2^32 + 1 must fail the seat bounds check, not wrap to seat 1. */
		int32 GetInt32(const FJsonObject& Object, const TCHAR* Key, int32 Default = 0)
		{
			return static_cast<int32>(FMath::Clamp<int64>(GetInt64(Object, Key, Default), MIN_int32, MAX_int32));
		}

		FLinearColor Hex(const TCHAR* Value)
		{
			return FLinearColor(FColor::FromHex(Value));
		}

		bool Is(const FString& Value, const TCHAR* Literal)
		{
			return Value.Equals(Literal, ESearchCase::CaseSensitive);
		}

		bool IsPlayerId(const FString& Id)
		{
			// messages.ts PLAYER_ID_RE /^p[1-9][0-9]{0,2}$/
			if (Id.Len() < 2 || Id.Len() > 4 || Id[0] != TEXT('p') || Id[1] < TEXT('1') || Id[1] > TEXT('9'))
			{
				return false;
			}
			for (int32 i = 2; i < Id.Len(); ++i)
			{
				if (!FChar::IsDigit(Id[i]))
				{
					return false;
				}
			}
			return true;
		}
	}

	const TCHAR* LexToString(ESensorError Error)
	{
		switch (Error)
		{
		case ESensorError::None: return TEXT("OK");
		case ESensorError::BadSize: return TEXT("BAD_SIZE");
		case ESensorError::BadFrameType: return TEXT("BAD_FRAME_TYPE");
		case ESensorError::BadFlags: return TEXT("BAD_FLAGS");
		case ESensorError::BadOrientation: return TEXT("BAD_ORIENTATION");
		}
		return TEXT("?");
	}

	ESensorError ValidateSensorFrame(const uint8* Data, int32 Size)
	{
		const bool bQuaternionSize = Size == SensorFrameV2Bytes || Size == SensorFrameV2WithPlayerBytes;
		if (!Data || (Size != SensorFrameBytes && Size != SensorFrameWithPlayerBytes && !bQuaternionSize))
		{
			return ESensorError::BadSize;
		}
		if (Data[0] != SensorFrameType)
		{
			return ESensorError::BadFrameType;
		}
		const uint8 Flags = Data[1];
		if ((Flags & ~SensorFlagMask) != 0)
		{
			return ESensorError::BadFlags;
		}
		if ((Flags & SensorFlagHasQuaternion) != 0 && !bQuaternionSize)
		{
			return ESensorError::BadSize;
		}
		if (Data[2] > 3)
		{
			return ESensorError::BadOrientation;
		}
		return ESensorError::None;
	}

	bool DecodeSensorFrame(const uint8* Data, int32 Size, FSensorFrame& Out, ESensorError* OutError)
	{
		const ESensorError Error = ValidateSensorFrame(Data, Size);
		if (OutError)
		{
			*OutError = Error;
		}
		if (Error != ESensorError::None)
		{
			return false;
		}
		FSensorFrame F;
		F.Flags = Data[1];
		F.ScreenOrientation = Data[2];
		F.Sequence = Data[3];
		F.Alpha = ReadI16(Data, 4) / 100.0;
		F.Beta = ReadI16(Data, 6) / 100.0;
		F.Gamma = ReadI16(Data, 8) / 100.0;
		F.Gyro = FVector3d(ReadI16(Data, 10), ReadI16(Data, 12), ReadI16(Data, 14)) / 20.0;
		F.Accel = FVector3d(ReadI16(Data, 16), ReadI16(Data, 18), ReadI16(Data, 20)) / 1000.0;
		F.Gravity = FVector3d(ReadI16(Data, 22), ReadI16(Data, 24), ReadI16(Data, 26)) / 1000.0;
		F.DtMs = ReadU16(Data, 28);
		// Only the flag decides: a 38-byte frame with the flag clear carries padding, not a rotation.
		if ((F.Flags & SensorFlagHasQuaternion) != 0)
		{
			F.bHasQuaternion = true;
			F.Quaternion = FVector4d(ReadI16(Data, 30), ReadI16(Data, 32), ReadI16(Data, 34), ReadI16(Data, 36)) / 16384.0;
		}
		if (Size == SensorFrameWithPlayerBytes)
		{
			F.PlayerIndex = Data[SensorFrameBytes];
		}
		else if (Size == SensorFrameV2WithPlayerBytes)
		{
			F.PlayerIndex = Data[SensorFrameV2Bytes];
		}
		Out = F;
		return true;
	}

	double SteeringFromGamma(double GammaDeg, double Sensitivity)
	{
		if (!FMath::IsFinite(GammaDeg))
		{
			return 0.0;
		}
		double Delta = GammaDeg;
		if (Delta > 180.0) Delta -= 360.0;
		if (Delta < -180.0) Delta += 360.0;
		double S = FMath::Clamp(Delta / MaxSteerAngleDeg, -1.0, 1.0);
		const double DeadZone = SteerDeadzoneDeg / MaxSteerAngleDeg;
		S = FMath::Abs(S) < DeadZone ? 0.0 : FMath::Sign(S) * (FMath::Abs(S) - DeadZone) / (1.0 - DeadZone);
		S = FMath::Clamp(S * Sensitivity, -1.0, 1.0);
		// INVERT_STEERING: clockwise phone = steer right = negative in the kart convention.
		return S == 0.0 ? 0.0 : -S;
	}

	bool ReadPracticeAction(const FJsonObject& Data, const FString& GameId, FString& OutActionId)
	{
		// PRACTICE_KINDS in useLobbyPractice.ts.
		static const TMap<FString, FString> Kinds = {
			{ TEXT("bowling"), TEXT("roll") }, { TEXT("samurai"), TEXT("cut") }, { TEXT("stacker"), TEXT("drop") },
			{ TEXT("darts"), TEXT("throw") }, { TEXT("kart"), TEXT("steer") } };
		const FString* Kind = Kinds.Find(GameId);
		FString DataGame, DataKind, ActionId;
		if (!Kind || !Data.TryGetStringField(TEXT("gameId"), DataGame) || !Data.TryGetStringField(TEXT("kind"), DataKind)
			|| !Data.TryGetStringField(TEXT("actionId"), ActionId) || DataGame != GameId || DataKind != *Kind
			|| ActionId.Len() < 1 || ActionId.Len() > 80)
		{
			return false;
		}
		OutActionId = ActionId;
		return true;
	}

	bool ReadPadButton(const FJsonObject& Data, TCHAR& OutButton)
	{
		FString Button;
		// FString == ignores case; the phone (and the wire contract) is exact.
		if (!Data.TryGetStringField(TEXT("button"), Button)
			|| (!Button.Equals(TEXT("a"), ESearchCase::CaseSensitive) && !Button.Equals(TEXT("b"), ESearchCase::CaseSensitive)))
		{
			return false;
		}
		OutButton = Button[0];
		return true;
	}

	bool IsPadMode(const FString& Mode)
	{
		for (const TCHAR* Known : { TEXT("tilt"), TEXT("swing"), TEXT("aim"), TEXT("flick") })
		{
			if (Mode.Equals(Known, ESearchCase::CaseSensitive)) return true;
		}
		return false;
	}

	FKartInputEvent ParseKartInput(const FJsonObject& Data)
	{
		FKartInputEvent Event;
		bool bValue = false;
		if (Data.HasTypedField<EJson::Boolean>(TEXT("isAccelerating")) && Data.TryGetBoolField(TEXT("isAccelerating"), bValue)) Event.Accelerating = bValue;
		if (Data.HasTypedField<EJson::Boolean>(TEXT("isReversing")) && Data.TryGetBoolField(TEXT("isReversing"), bValue)) Event.Reversing = bValue;
		if (Data.HasTypedField<EJson::Boolean>(TEXT("isDrifting")) && Data.TryGetBoolField(TEXT("isDrifting"), bValue)) Event.Drifting = bValue;
		double Steering = 0.0;
		if (Data.HasTypedField<EJson::Number>(TEXT("steeringInput")) && Data.TryGetNumberField(TEXT("steeringInput"), Steering) && FMath::IsFinite(Steering))
		{
			Event.Steering = FMath::Clamp(Steering, -1.0, 1.0);
		}
		// `useItem: true` only; a false or non-boolean is not a press.
		Event.bUseItem = Data.HasTypedField<EJson::Boolean>(TEXT("useItem")) && Data.GetBoolField(TEXT("useItem"));
		return Event;
	}

	FString GenerateRoomCode(TFunctionRef<void(uint8*, int32)> RandomBytes)
	{
		const int32 AlphabetLength = FCString::Strlen(RoomCodeAlphabet);
		const int32 Limit = 256 - (256 % AlphabetLength);
		FString Out;
		uint8 Chunk[RoomCodeLength * 2];
		while (Out.Len() < RoomCodeLength)
		{
			RandomBytes(Chunk, UE_ARRAY_COUNT(Chunk));
			for (const uint8 Byte : Chunk)
			{
				if (Byte >= Limit)
				{
					continue;
				}
				Out.AppendChar(RoomCodeAlphabet[Byte % AlphabetLength]);
				if (Out.Len() == RoomCodeLength)
				{
					break;
				}
			}
		}
		return Out;
	}

	FString GenerateRoomCode()
	{
		return GenerateRoomCode([](uint8* Bytes, int32 Count)
		{
			// FGuid::NewGuid is the OS's random GUID source (CoCreateGuid on Windows).
			for (int32 i = 0; i < Count; i += 16)
			{
				const FGuid Guid = FGuid::NewGuid();
				const uint32 Words[4] = { Guid.A, Guid.B, Guid.C, Guid.D };
				FMemory::Memcpy(Bytes + i, Words, FMath::Min(16, Count - i));
			}
		});
	}

	bool IsValidRoomCode(const FString& Code)
	{
		if (Code.Len() != RoomCodeLength)
		{
			return false;
		}
		for (const TCHAR C : Code)
		{
			if (!FCString::Strchr(RoomCodeAlphabet, C))
			{
				return false;
			}
		}
		return true;
	}

	FString RoomIdForCode(const FString& Code)
	{
		return TEXT("r-") + Code.ToLower();
	}

	FString FormEncode(const FString& Value)
	{
		FString Out;
		const FTCHARToUTF8 Utf8(*Value);
		for (int32 i = 0; i < Utf8.Length(); ++i)
		{
			const uint8 C = static_cast<uint8>(Utf8.Get()[i]);
			if ((C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '*' || C == '-' || C == '.' || C == '_')
			{
				Out.AppendChar(static_cast<TCHAR>(C));
			}
			else if (C == ' ')
			{
				Out.AppendChar(TEXT('+'));
			}
			else
			{
				Out += FString::Printf(TEXT("%%%02X"), C);
			}
		}
		return Out;
	}

	FString BuildJoinUrl(const FString& BaseUrl, const FString& Code, const FString& JoinToken)
	{
		// new URL(`/join/${code}`, base): the path replaces whatever path the base had.
		FString Origin = BaseUrl;
		const int32 SchemeEnd = Origin.Find(TEXT("://"));
		if (SchemeEnd != INDEX_NONE)
		{
			const int32 PathStart = Origin.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SchemeEnd + 3);
			if (PathStart != INDEX_NONE)
			{
				Origin.LeftInline(PathStart);
			}
		}
		FString Url = FString::Printf(TEXT("%s/join/%s#v=%d"), *Origin, *Code, ProtocolVersion);
		if (!JoinToken.IsEmpty())
		{
			Url += TEXT("&t=") + FormEncode(JoinToken);
		}
		return Url;
	}

	FString MakeIdentity()
	{
		FGuid Guid = FGuid::NewGuid();
		// Force RFC 4122 version 4 and variant 10xx, whatever the platform GUID is.
		Guid.B = (Guid.B & 0xFFFF0FFF) | 0x00004000;
		Guid.C = (Guid.C & 0x3FFFFFFF) | 0x80000000;
		return Guid.ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	bool IsV4Uuid(const FString& Value)
	{
		if (Value.Len() != 36)
		{
			return false;
		}
		for (int32 i = 0; i < 36; ++i)
		{
			const TCHAR C = FChar::ToLower(Value[i]);
			const bool bDash = i == 8 || i == 13 || i == 18 || i == 23;
			if (bDash ? C != TEXT('-') : !FChar::IsHexDigit(C))
			{
				return false;
			}
		}
		const TCHAR Variant = FChar::ToLower(Value[19]);
		return Value[14] == TEXT('4') && (Variant == TEXT('8') || Variant == TEXT('9') || Variant == TEXT('a') || Variant == TEXT('b'));
	}

	EMcRoomStatus ParseStatus(const FString& Status)
	{
		if (Is(Status, TEXT("lobby"))) return EMcRoomStatus::Lobby;
		if (Is(Status, TEXT("countdown"))) return EMcRoomStatus::Countdown;
		if (Is(Status, TEXT("in-progress"))) return EMcRoomStatus::InProgress;
		if (Is(Status, TEXT("paused"))) return EMcRoomStatus::Paused;
		if (Is(Status, TEXT("complete"))) return EMcRoomStatus::Complete;
		return EMcRoomStatus::Unknown;
	}

	EServerMessage ParseServerMessageType(const FString& Type)
	{
		struct FEntry
		{
			const TCHAR* Type;
			EServerMessage Message;
		};
		static constexpr FEntry Table[] = {
			{ TEXT("ping"), EServerMessage::Ping },
			{ TEXT("welcome"), EServerMessage::Welcome },
			{ TEXT("state"), EServerMessage::State },
			{ TEXT("game-event"), EServerMessage::GameEvent },
			{ TEXT("error"), EServerMessage::Error },
			{ TEXT("host-changed"), EServerMessage::HostChanged },
			{ TEXT("signed-scores"), EServerMessage::SignedScores },
			{ TEXT("room-closed"), EServerMessage::RoomClosed },
			{ TEXT("action-ack"), EServerMessage::ActionAck },
			{ TEXT("resume-countdown"), EServerMessage::ResumeCountdown },
		};
		for (const FEntry& Entry : Table)
		{
			if (Is(Type, Entry.Type))
			{
				return Entry.Message;
			}
		}
		return EServerMessage::Unknown;
	}

	bool IsStartRejectedError(const FString& Code)
	{
		return Is(Code, TEXT("PLAYERS_NOT_READY")) || Is(Code, TEXT("NO_ACTIVE_PLAYERS")) || Is(Code, TEXT("INVALID_STATE_TRANSITION"))
			|| Is(Code, TEXT("ENTITLEMENT_REQUIRED"));
	}

	FStartCheck EvaluateStart(const FMcRoomState& Room, bool bConnectionOpen, bool bIsHost, bool bPreparationReady)
	{
		FStartCheck Check;
		if (!bConnectionOpen)
		{
			Check.Block = EStartBlock::NotConnected;
			return Check;
		}
		if (!bIsHost)
		{
			Check.Block = EStartBlock::NotHost;
			return Check;
		}
		if (Room.Status != EMcRoomStatus::Lobby)
		{
			Check.Block = EStartBlock::MatchRunning;
			return Check;
		}
		int32 People = 0;
		for (const FMcSeat& Seat : Room.Players)
		{
			People += Seat.bBot ? 0 : 1;
			Check.NotReady += (Seat.bReady && Seat.bConnected && !Seat.bEliminated) ? 0 : 1;
		}
		if (Room.Players.Num() == 0)
		{
			Check.Block = EStartBlock::NobodySeated;
		}
		else if (People == 0)
		{
			Check.Block = EStartBlock::OnlyBots;
		}
		else if (!bPreparationReady)
		{
			Check.Block = EStartBlock::Preparing;
		}
		else if (Check.NotReady > 0)
		{
			Check.Block = EStartBlock::NotReady;
		}
		return Check;
	}

	FString DescribeStartBlock(const FStartCheck& Check, const FString& LastError)
	{
		switch (Check.Block)
		{
		case EStartBlock::None: return FString();
		case EStartBlock::NotConnected: return LastError.IsEmpty() ? TEXT("Connecting to the room server...") : LastError;
		case EStartBlock::NotHost: return TEXT("Another screen is hosting this room.");
		case EStartBlock::MatchRunning: return TEXT("A match is in progress.");
		case EStartBlock::NobodySeated: return TEXT("Scan the code with a phone to join.");
		case EStartBlock::OnlyBots: return TEXT("Bots are ready. Scan the code to play with them.");
		case EStartBlock::Preparing: return TEXT("Preparing Kart Racer...");
		case EStartBlock::NotReady:
			return FString::Printf(TEXT("Waiting for %d %s to get ready."), Check.NotReady, Check.NotReady == 1 ? TEXT("player") : TEXT("players"));
		}
		return FString();
	}

	FLinearColor PaintColor(const FString& KartPaint, int32 Seat)
	{
		if (KartPaint == TEXT("pink")) return Hex(TEXT("f0508e"));
		if (KartPaint == TEXT("cyan")) return Hex(TEXT("5ec8e5"));
		if (KartPaint == TEXT("gold")) return Hex(TEXT("f7c948"));
		if (KartPaint == TEXT("violet")) return Hex(TEXT("a67cf0"));
		if (KartPaint == TEXT("white")) return Hex(TEXT("f4f0ea"));
		if (KartPaint == TEXT("navy")) return Hex(TEXT("2a3558"));
		static const TCHAR* const SeatColors[] = { TEXT("6d8bff"), TEXT("3fc9c2"), TEXT("f2b23f"), TEXT("a083ff") };
		return Hex(SeatColors[FMath::Clamp(Seat - 1, 0, 3)]);
	}

	bool ParseRoomState(const FJsonObject& State, FMcRoomState& Out)
	{
		FMcRoomState R;
		R.RoomId = GetString(State, TEXT("roomId"));
		R.JoinCode = GetString(State, TEXT("joinCode"));
		R.GameId = GetString(State, TEXT("gameId"));
		R.SelectedMapId = GetString(State, TEXT("selectedMapId"));
		if (R.SelectedMapId.IsEmpty())
		{
			// Older servers omit it; the protocol default (maps.ts) applies.
			R.SelectedMapId = TEXT("violet-loop");
		}
		R.StateVersion = GetInt64(State, TEXT("stateVersion"), -1);
		R.HostSessionId = GetString(State, TEXT("hostSessionId"));
		R.bHostMissing = GetBool(State, TEXT("hostMissing"));
		R.ServerTime = GetInt64(State, TEXT("serverTime"));

		const TArray<TSharedPtr<FJsonValue>>* Players = nullptr;
		if (State.TryGetArrayField(TEXT("players"), Players))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Players)
			{
				const TSharedPtr<FJsonObject>* P = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(P))
				{
					continue;
				}
				const FJsonObject& Player = **P;
				FMcSeat Seat;
				Seat.PlayerId = GetString(Player, TEXT("id"));
				Seat.Seat = GetInt32(Player, TEXT("seat"));
				Seat.Name = GetString(Player, TEXT("name"));
				Seat.Emoji = GetString(Player, TEXT("emoji"));
				Seat.bReady = GetBool(Player, TEXT("ready"));
				Seat.bCalibrated = GetBool(Player, TEXT("calibrated"));
				Seat.bConnected = GetBool(Player, TEXT("connected"));
				Seat.bEliminated = GetBool(Player, TEXT("eliminated"));
				Seat.bBot = GetBool(Player, TEXT("bot"));
				Seat.GraceRemainingMs = GetInt32(Player, TEXT("graceRemainingMs"));
				Seat.Score = GetInt32(Player, TEXT("score"));
				Seat.Screen = GetString(Player, TEXT("screen"));
				const TSharedPtr<FJsonObject>* Avatar = nullptr;
				if (Player.TryGetObjectField(TEXT("avatar"), Avatar))
				{
					Seat.KartPaint = GetString(**Avatar, TEXT("kartPaint"));
					Seat.Vehicle = GetString(**Avatar, TEXT("vehicle"));
					Seat.Face = GetString(**Avatar, TEXT("face"));
					Seat.Body = GetString(**Avatar, TEXT("body"));
				}
				Seat.PaintColor = PaintColor(Seat.KartPaint, Seat.Seat);
				if (Seat.PlayerId.IsEmpty() || Seat.Seat < 1 || Seat.Seat > MaxPlayers)
				{
					continue;
				}
				R.Players.Add(Seat);
			}
			R.Players.Sort([](const FMcSeat& A, const FMcSeat& B) { return A.Seat < B.Seat; });
		}

		const TSharedPtr<FJsonObject>* Game = nullptr;
		if (State.TryGetObjectField(TEXT("game"), Game))
		{
			R.Status = ParseStatus(GetString(**Game, TEXT("status")));
			R.Phase = GetString(**Game, TEXT("phase"));
			R.MatchNumber = GetInt32(**Game, TEXT("matchNumber"));
			R.StartedAt = GetInt64(**Game, TEXT("startedAt"));
			R.PauseReason = GetString(**Game, TEXT("pauseReason"));
			R.PausedForPlayerId = GetString(**Game, TEXT("pausedForPlayerId"));
		}
		const TSharedPtr<FJsonObject>* Countdown = nullptr;
		if (State.TryGetObjectField(TEXT("countdown"), Countdown))
		{
			R.CountdownKind = GetString(**Countdown, TEXT("kind"));
			R.CountdownEndsAt = GetInt64(**Countdown, TEXT("endsAt"));
		}
		if (R.RoomId.IsEmpty() || R.StateVersion < 0)
		{
			return false;
		}
		Out = MoveTemp(R);
		return true;
	}

	TArray<FMcScoreEntry> BuildFinalScores(const TArray<FMcRaceResult>& Results, const FMcRoomState& Room)
	{
		TArray<FMcScoreEntry> Scores;
		for (const FMcRaceResult& Result : Results)
		{
			if (!Result.bFinished || !FMath::IsFinite(Result.FinishTimeMs))
			{
				continue;
			}
			const int64 Ms = FMath::RoundToInt64(Result.FinishTimeMs);
			if (Ms < MinFinishTimeMs || Ms > MaxScore)
			{
				continue;
			}
			FString PlayerId = Result.PlayerId;
			if (PlayerId.IsEmpty())
			{
				const FMcSeat* Seat = Room.FindBySeat(Result.SeatIndex + 1);
				PlayerId = Seat ? Seat->PlayerId : FString();
			}
			if (!IsPlayerId(PlayerId) || Scores.ContainsByPredicate([&](const FMcScoreEntry& E) { return E.PlayerId == PlayerId; }))
			{
				continue;
			}
			Scores.Add({ PlayerId, static_cast<int32>(Ms) });
		}
		Scores.StableSort([](const FMcScoreEntry& A, const FMcScoreEntry& B) { return A.Score < B.Score; });
		if (Scores.Num() > MaxPlayers)
		{
			Scores.SetNum(MaxPlayers);
		}
		return Scores;
	}

	bool AreScoresValid(const TArray<FMcScoreEntry>& Scores)
	{
		if (Scores.Num() > MaxPlayers)
		{
			return false;
		}
		TSet<FString> Seen;
		for (const FMcScoreEntry& Entry : Scores)
		{
			if (!IsPlayerId(Entry.PlayerId) || Seen.Contains(Entry.PlayerId) || Entry.Score < 0 || Entry.Score > MaxScore)
			{
				return false;
			}
			Seen.Add(Entry.PlayerId);
		}
		return true;
	}

}

const FMcSeat* FMcRoomState::FindBySeat(int32 Seat) const
{
	return Players.FindByPredicate([Seat](const FMcSeat& P) { return P.Seat == Seat; });
}

const FMcSeat* FMcRoomState::FindByPlayerId(const FString& Id) const
{
	return Players.FindByPredicate([&Id](const FMcSeat& P) { return P.PlayerId == Id; });
}

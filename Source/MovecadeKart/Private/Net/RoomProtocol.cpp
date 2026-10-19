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

		/** Compact JSON object writer with stable key order. */
		class FJsonOut
		{
		public:
			explicit FJsonOut(const TCHAR* Type)
			{
				Out = FString::Printf(TEXT("{\"v\":%d,\"t\":"), ProtocolVersion);
				AppendString(Type);
			}

			FJsonOut& Str(const TCHAR* Key, const FString& Value) { KeyPrefix(Key); AppendString(Value); return *this; }
			FJsonOut& Int(const TCHAR* Key, int64 Value) { KeyPrefix(Key); Out += FString::Printf(TEXT("%lld"), Value); return *this; }
			FJsonOut& Num(const TCHAR* Key, double Value) { KeyPrefix(Key); Out += FString::SanitizeFloat(Value, 0); return *this; }
			FJsonOut& Bool(const TCHAR* Key, bool bValue) { KeyPrefix(Key); Out += bValue ? TEXT("true") : TEXT("false"); return *this; }
			FJsonOut& Null(const TCHAR* Key) { KeyPrefix(Key); Out += TEXT("null"); return *this; }
			FJsonOut& Raw(const TCHAR* Key, const FString& Json) { KeyPrefix(Key); Out += Json; return *this; }
			FString Done() { return Out + TEXT("}"); }

			static FString Quote(const FString& Value)
			{
				FString Result = TEXT("\"");
				for (const TCHAR C : Value)
				{
					switch (C)
					{
					case TEXT('"'): Result += TEXT("\\\""); break;
					case TEXT('\\'): Result += TEXT("\\\\"); break;
					case TEXT('\n'): Result += TEXT("\\n"); break;
					case TEXT('\r'): Result += TEXT("\\r"); break;
					case TEXT('\t'): Result += TEXT("\\t"); break;
					default:
						if (C < 0x20)
						{
							Result += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C));
						}
						else
						{
							Result.AppendChar(C);
						}
					}
				}
				return Result + TEXT("\"");
			}

		private:
			void KeyPrefix(const TCHAR* Key) { Out += TEXT(","); AppendString(Key); Out += TEXT(":"); }
			void AppendString(const FString& Value) { Out += Quote(Value); }

			FString Out;
		};

		FString ScoresJson(const TArray<FMcScoreEntry>& Scores)
		{
			FString Out = TEXT("[");
			for (int32 i = 0; i < Scores.Num(); ++i)
			{
				Out += FString::Printf(TEXT("%s{\"playerId\":%s,\"score\":%d}"), i ? TEXT(",") : TEXT(""), *FJsonOut::Quote(Scores[i].PlayerId), Scores[i].Score);
			}
			return Out + TEXT("]");
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

		int64 GetInt64(const FJsonObject& Object, const TCHAR* Key, int64 Default = 0)
		{
			double Value = 0.0;
			return Object.TryGetNumberField(Key, Value) ? static_cast<int64>(Value) : Default;
		}

		FLinearColor Hex(const TCHAR* Value)
		{
			return FLinearColor(FColor::FromHex(Value));
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
		if (Status == TEXT("lobby")) return EMcRoomStatus::Lobby;
		if (Status == TEXT("countdown")) return EMcRoomStatus::Countdown;
		if (Status == TEXT("in-progress")) return EMcRoomStatus::InProgress;
		if (Status == TEXT("paused")) return EMcRoomStatus::Paused;
		if (Status == TEXT("complete")) return EMcRoomStatus::Complete;
		return EMcRoomStatus::Unknown;
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
				Seat.Seat = static_cast<int32>(GetInt64(Player, TEXT("seat")));
				Seat.Name = GetString(Player, TEXT("name"));
				Seat.Emoji = GetString(Player, TEXT("emoji"));
				Seat.bReady = GetBool(Player, TEXT("ready"));
				Seat.bCalibrated = GetBool(Player, TEXT("calibrated"));
				Seat.bConnected = GetBool(Player, TEXT("connected"));
				Seat.bEliminated = GetBool(Player, TEXT("eliminated"));
				Seat.bBot = GetBool(Player, TEXT("bot"));
				Seat.GraceRemainingMs = static_cast<int32>(GetInt64(Player, TEXT("graceRemainingMs")));
				Seat.Score = static_cast<int32>(GetInt64(Player, TEXT("score")));
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
			R.MatchNumber = static_cast<int32>(GetInt64(**Game, TEXT("matchNumber")));
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

	namespace Msg
	{
		FString Hello(const FString& Client, const FString& GameId, const FString& Identity, const FString& HostToken)
		{
			FJsonOut J(TEXT("hello"));
			J.Str(TEXT("role"), TEXT("host")).Str(TEXT("client"), Client.Left(64));
			if (!HostToken.IsEmpty())
			{
				J.Str(TEXT("hostToken"), HostToken);
			}
			J.Str(TEXT("gameId"), GameId);
			if (!Identity.IsEmpty())
			{
				J.Str(TEXT("identity"), Identity.ToLower());
			}
			return J.Done();
		}

		FString Pong() { return FJsonOut(TEXT("pong")).Done(); }
		FString StateAck(int64 StateVersion) { return FJsonOut(TEXT("state-ack")).Int(TEXT("stateVersion"), StateVersion).Done(); }
		FString ConfigureRoomMap(const FString& MapId) { return FJsonOut(TEXT("configure-room")).Str(TEXT("mapId"), MapId).Done(); }

		FString SetCountdown(const FString& Kind, int32 DurationMs)
		{
			return FJsonOut(TEXT("set-countdown")).Str(TEXT("kind"), Kind).Int(TEXT("durationMs"), FMath::Clamp(DurationMs, 0, CountdownMaxMs)).Done();
		}

		FString StartGame() { return FJsonOut(TEXT("start-game")).Done(); }
		FString AdvanceTurnSimultaneous() { return FJsonOut(TEXT("advance-turn")).Null(TEXT("activePlayerId")).Str(TEXT("phase"), TEXT("active")).Done(); }
		FString ScoreUpdate(const TArray<FMcScoreEntry>& Scores) { return FJsonOut(TEXT("score-update")).Raw(TEXT("scores"), ScoresJson(Scores)).Done(); }
		FString CompleteGame(const TArray<FMcScoreEntry>& Scores) { return FJsonOut(TEXT("complete-game")).Raw(TEXT("scores"), ScoresJson(Scores)).Done(); }

		FString RestartGame(const FString& ActionId)
		{
			FJsonOut J(TEXT("restart-game"));
			if (!ActionId.IsEmpty()) J.Str(TEXT("actionId"), ActionId.Left(64));
			return J.Done();
		}

		FString ReturnToLobby(const FString& ActionId)
		{
			FJsonOut J(TEXT("return-to-lobby"));
			if (!ActionId.IsEmpty()) J.Str(TEXT("actionId"), ActionId.Left(64));
			return J.Done();
		}

		FString KickPlayer(const FString& PlayerId) { return FJsonOut(TEXT("kick-player")).Str(TEXT("playerId"), PlayerId).Done(); }
		FString DropPlayer(const FString& PlayerId) { return FJsonOut(TEXT("drop-player")).Str(TEXT("playerId"), PlayerId).Done(); }
		FString CloseRoom() { return FJsonOut(TEXT("close-room")).Done(); }

		FString Haptic(const FString& TargetPlayerId, double Intensity, const FString& CollisionType, bool bSustained)
		{
			const double Clamped = FMath::RoundToDouble(FMath::Clamp(FMath::IsFinite(Intensity) ? Intensity : 0.0, 0.0, 1.0) * 1000.0) / 1000.0;
			const FString Data = FString::Printf(TEXT("{\"type\":\"collision\",\"targetPlayerId\":%s,\"intensity\":%s,\"collisionType\":%s,\"sustained\":%s}"),
				*FJsonOut::Quote(TargetPlayerId), *FString::SanitizeFloat(Clamped, 0), *FJsonOut::Quote(CollisionType), bSustained ? TEXT("true") : TEXT("false"));
			return FJsonOut(TEXT("game-event")).Str(TEXT("event"), TEXT("collision")).Raw(TEXT("data"), Data).Done();
		}

		FString ScoresAck(int32 MatchNumber) { return FJsonOut(TEXT("scores-ack")).Int(TEXT("matchNumber"), MatchNumber).Done(); }
	}

	bool IsMomentaryType(const FString& Type)
	{
		static const TCHAR* const Types[] = { TEXT("game-event"), TEXT("advance-turn"), TEXT("score-update"), TEXT("state-ack"), TEXT("host-lobby-visible"), TEXT("ping"), TEXT("pong") };
		for (const TCHAR* T : Types)
		{
			if (Type == T) return true;
		}
		return false;
	}

	bool IsTerminalCloseCode(int32 Code)
	{
		return Code == 1008 || Code == 4000 || Code == 4003 || Code == 4008 || Code == 4100 || Code == 4101;
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

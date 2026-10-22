#include "Net/RoomProtocol.h"

#include "Algo/NoneOf.h"
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

		/** Unicode Cc: what sanitizePlayerName turns into spaces (Go's unicode.IsControl). */
		bool IsControlCharacter(TCHAR C)
		{
			return C <= 0x1F || (C >= 0x7F && C <= 0x9F);
		}

		/** JavaScript's \s: ECMA-262 WhiteSpace and LineTerminator. */
		bool IsJsWhitespace(TCHAR C)
		{
			switch (C)
			{
			case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0: case 0x1680:
			case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
				return true;
			default:
				return C >= 0x2000 && C <= 0x200A;
			}
		}

		/** The bidi marks, embeddings, overrides and isolates: they reorder the text drawn after them. */
		bool IsBidiControl(TCHAR C)
		{
			return C == 0x200E || C == 0x200F || (C >= 0x202A && C <= 0x202E) || (C >= 0x2066 && C <= 0x2069);
		}

		/** sanitizePlayerName's whitelist, less \s: [a-zA-Z0-9_-]. */
		bool IsNameCharacter(TCHAR C)
		{
			return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('0') && C <= TEXT('9'))
				|| C == TEXT('_') || C == TEXT('-');
		}

		/** Value when it is at most MaxLength UTF-16 units with no control character, else empty (an id is never cut). */
		FString BoundedField(const FString& Value, int32 MaxLength)
		{
			const bool bClean = Value.Len() <= MaxLength && Algo::NoneOf(Value, [](TCHAR C) { return IsControlCharacter(C) || IsBidiControl(C); });
			return bClean ? Value : FString();
		}

		/** A string from the server, bounded (BoundedField); counts it in CleanedStrings when that changed it. */
		FString ReadBoundedString(const FJsonObject& Object, const TCHAR* Key, int32 MaxLength, int32& CleanedStrings)
		{
			const FString Raw = GetString(Object, Key);
			FString Bounded = BoundedField(Raw, MaxLength);
			CleanedStrings += Bounded.Len() == Raw.Len() ? 0 : 1;
			return Bounded;
		}

		/** Everything about a seat but its id and number, from one roster entry. */
		void ReadSeatDetails(const FJsonObject& Player, FMcSeat& Seat, int32& CleanedStrings)
		{
			const FString RawName = GetString(Player, TEXT("name"));
			Seat.Name = SanitizePlayerName(RawName);
			CleanedStrings += Seat.Name.Equals(RawName, ESearchCase::CaseSensitive) ? 0 : 1;
			Seat.Emoji = ReadBoundedString(Player, TEXT("emoji"), MaxEmojiLength, CleanedStrings);
			Seat.bReady = GetBool(Player, TEXT("ready"));
			Seat.bCalibrated = GetBool(Player, TEXT("calibrated"));
			Seat.bConnected = GetBool(Player, TEXT("connected"));
			Seat.bEliminated = GetBool(Player, TEXT("eliminated"));
			Seat.bBot = GetBool(Player, TEXT("bot"));
			Seat.GraceRemainingMs = GetInt32(Player, TEXT("graceRemainingMs"));
			Seat.Score = GetInt32(Player, TEXT("score"));
			Seat.Screen = ReadBoundedString(Player, TEXT("screen"), MaxSeatIdLength, CleanedStrings);
			const TSharedPtr<FJsonObject>* Avatar = nullptr;
			if (Player.TryGetObjectField(TEXT("avatar"), Avatar))
			{
				Seat.KartPaint = ReadBoundedString(**Avatar, TEXT("kartPaint"), MaxSeatIdLength, CleanedStrings);
				Seat.Vehicle = ReadBoundedString(**Avatar, TEXT("vehicle"), MaxSeatIdLength, CleanedStrings);
				Seat.Face = ReadBoundedString(**Avatar, TEXT("face"), MaxSeatIdLength, CleanedStrings);
				Seat.Body = ReadBoundedString(**Avatar, TEXT("body"), MaxSeatIdLength, CleanedStrings);
			}
			Seat.PaintColor = PaintColor(Seat.KartPaint, Seat.Seat);
		}
	}

	FString SanitizePlayerName(const FString& Name)
	{
		// validate.ts in one pass: (1) control characters become spaces; (2) only [a-zA-Z0-9\s\-_] survives;
		// (3) whitespace runs collapse to one space, none at either end (which also covers its leading trim);
		// (4) the cut to 20; (5) the final trim. Past 20 kept characters the rest can't change the result.
		FString Out;
		bool bPendingSpace = false;
		for (const TCHAR C : Name)
		{
			if (IsControlCharacter(C) || IsJsWhitespace(C))
			{
				bPendingSpace = !Out.IsEmpty();
				continue;
			}
			if (!IsNameCharacter(C))
			{
				continue;
			}
			if (bPendingSpace)
			{
				Out.AppendChar(TEXT(' '));
				bPendingSpace = false;
			}
			Out.AppendChar(C);
			if (Out.Len() > MaxPlayerNameLength)
			{
				break;
			}
		}
		Out.LeftInline(MaxPlayerNameLength);
		Out.TrimEndInline();
		return Out;
	}

	FString SanitizeServerText(const FString& Text)
	{
		// Control characters become spaces so "a\nb" stays two words; bidi controls go, so nothing reorders the line.
		FString Out;
		Out.Reserve(FMath::Min(Text.Len(), MaxServerTextLength));
		for (const TCHAR C : Text)
		{
			if (Out.Len() == MaxServerTextLength)
			{
				break;
			}
			if (!IsBidiControl(C))
			{
				Out.AppendChar(IsControlCharacter(C) ? TEXT(' ') : C);
			}
		}
		// Never end on half of a surrogate pair.
		if (Out.Len() > 0 && StringConv::IsHighSurrogate(Out[Out.Len() - 1]))
		{
			Out.LeftChopInline(1);
		}
		Out.TrimStartAndEndInline();
		return Out;
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

	bool IsLocalHost(const FString& HostOrUrl)
	{
		FString Host = HostOrUrl.TrimStartAndEnd();
		if (const int32 SchemeEnd = Host.Find(TEXT("://")); SchemeEnd != INDEX_NONE)
		{
			Host.RightChopInline(SchemeEnd + 3);
		}
		// The authority ends at the path, query or fragment; user info before an @ is not where the socket goes.
		if (const int32 AuthorityEnd = Host.GetCharArray().IndexOfByPredicate([](TCHAR C) { return C == TEXT('/') || C == TEXT('?') || C == TEXT('#'); });
			AuthorityEnd != INDEX_NONE)
		{
			Host.LeftInline(AuthorityEnd);
		}
		if (int32 At = INDEX_NONE; Host.FindLastChar(TEXT('@'), At))
		{
			Host.RightChopInline(At + 1);
		}
		if (Host.StartsWith(TEXT("[")))
		{
			// [v6]:port. Anything after the bracket other than a port is not an address.
			const int32 Close = Host.Find(TEXT("]"));
			if (Close == INDEX_NONE || (Close + 1 < Host.Len() && Host[Close + 1] != TEXT(':')))
			{
				return false;
			}
			Host = Host.Mid(1, Close - 1);
		}
		else
		{
			// host:port. A bare v6 address (::1) has more than one colon and keeps them.
			int32 FirstColon = INDEX_NONE;
			int32 LastColon = INDEX_NONE;
			if (Host.FindChar(TEXT(':'), FirstColon) && Host.FindLastChar(TEXT(':'), LastColon) && FirstColon == LastColon)
			{
				Host.LeftInline(FirstColon);
			}
		}
		Host.ToLowerInline();
		static constexpr int32 LocalhostSuffixLength = 10; // ".localhost"
		return Host == TEXT("localhost") || Host == TEXT("127.0.0.1") || Host == TEXT("::1")
			|| (Host.Len() > LocalhostSuffixLength && Host.EndsWith(TEXT(".localhost")));
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

	bool ParseRoomState(const FJsonObject& State, FMcRoomState& Out, FRoomStateIssues* OutIssues)
	{
		FRoomStateIssues Issues;
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
			// NET-01: a bounded look at the roster, first come first seated. Truncated rather than refused: the room
			// must stay playable, and an honest roster (eliminated seats included) never reaches either limit.
			const int32 Readable = FMath::Min(Players->Num(), MaxRosterEntries);
			int32 Index = 0;
			for (; Index < Readable && R.Players.Num() < MaxPlayers; ++Index)
			{
				const TSharedPtr<FJsonValue>& Value = (*Players)[Index];
				const TSharedPtr<FJsonObject>* Player = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(Player))
				{
					++Issues.SkippedEntries;
					continue;
				}
				FMcSeat Seat;
				Seat.PlayerId = GetString(**Player, TEXT("id"));
				Seat.Seat = GetInt32(**Player, TEXT("seat"));
				if (Seat.PlayerId.IsEmpty() || Seat.PlayerId.Len() > MaxPlayerIdLength || Seat.Seat < 1 || Seat.Seat > MaxPlayers
					|| R.FindBySeat(Seat.Seat) || R.FindByPlayerId(Seat.PlayerId))
				{
					++Issues.SkippedEntries;
					continue;
				}
				ReadSeatDetails(**Player, Seat, Issues.CleanedStrings);
				R.Players.Add(MoveTemp(Seat));
			}
			Issues.UnreadEntries = Players->Num() - Index;
			R.Players.Sort([](const FMcSeat& A, const FMcSeat& B) { return A.Seat < B.Seat; });
		}

		const TSharedPtr<FJsonObject>* Game = nullptr;
		if (State.TryGetObjectField(TEXT("game"), Game))
		{
			R.Status = ParseStatus(GetString(**Game, TEXT("status")));
			R.Phase = GetString(**Game, TEXT("phase"));
			R.MatchNumber = GetInt32(**Game, TEXT("matchNumber"));
			R.StartedAt = GetInt64(**Game, TEXT("startedAt"));
			// Both go out through OnPauseChanged to the games' pause overlays.
			R.PauseReason = ReadBoundedString(**Game, TEXT("pauseReason"), MaxSeatIdLength, Issues.CleanedStrings);
			R.PausedForPlayerId = ReadBoundedString(**Game, TEXT("pausedForPlayerId"), MaxPlayerIdLength, Issues.CleanedStrings);
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
		if (OutIssues)
		{
			*OutIssues = Issues;
		}
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

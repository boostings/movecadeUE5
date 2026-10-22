// The host's JSON frames (protocol messages.ts): every client message the host sends, written compactly with a
// stable key order so they match the movecade fixtures byte for byte (MovecadeKart.Net.ClientMessages).

#include "Net/RoomProtocol.h"

namespace MovecadeKart::Net
{
	namespace
	{
		bool Is(const FString& Value, const TCHAR* Literal)
		{
			return Value.Equals(Literal, ESearchCase::CaseSensitive);
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
	}

	FString QuoteJson(const FString& Value)
	{
		return FJsonOut::Quote(Value);
	}

	FString HexColor(const FLinearColor& Color)
	{
		const FColor Srgb = Color.ToFColor(/*bSRGB*/ true);
		return FString::Printf(TEXT("#%02x%02x%02x"), Srgb.R, Srgb.G, Srgb.B);
	}

	FString PadPromptData(const FString& PlayerId, const FString& Title, const FString& Subtitle,
		const FString& A, const FString& B, const FString& AccentHex, const FString& Mode)
	{
		bool bAccent = AccentHex.Len() == 7 && AccentHex[0] == TEXT('#');
		for (int32 i = 1; bAccent && i < 7; ++i)
		{
			bAccent = FChar::IsHexDigit(AccentHex[i]);
		}
		FString Data = FString::Printf(TEXT("{\"playerId\":%s,\"title\":%s,\"subtitle\":%s,\"a\":%s,\"b\":%s"),
			*QuoteJson(PlayerId), *QuoteJson(Title.Left(80)), *QuoteJson(Subtitle.Left(160)), *QuoteJson(A.Left(16)), *QuoteJson(B.Left(16)));
		if (bAccent)
		{
			Data += FString::Printf(TEXT(",\"accent\":%s"), *QuoteJson(AccentHex));
		}
		if (IsPadMode(Mode))
		{
			Data += FString::Printf(TEXT(",\"mode\":%s"), *QuoteJson(Mode));
		}
		return Data + TEXT("}");
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
		FString AdvanceTurn(const FString& ActivePlayerId)
		{
			return FJsonOut(TEXT("advance-turn")).Str(TEXT("activePlayerId"), ActivePlayerId).Str(TEXT("phase"), TEXT("active")).Done();
		}
		FString GameEvent(const FString& Event, const FString& DataJson) { return FJsonOut(TEXT("game-event")).Str(TEXT("event"), Event).Raw(TEXT("data"), DataJson).Done(); }
		FString ConfigureRoomGame(const FString& GameId) { return FJsonOut(TEXT("configure-room")).Str(TEXT("gameId"), GameId).Done(); }
		FString PlayFeedback(const FString& GameId, int32 MatchNumber, const FString& PlayerId, const FString& Title,
			const FString& Detail, const FString& Tone, const FString& Haptic, bool bMajor)
		{
			FString Data = FString::Printf(TEXT("{\"gameId\":%s,\"matchNumber\":%d,\"playerId\":%s,\"title\":%s,\"detail\":%s,\"tone\":%s"),
				*FJsonOut::Quote(GameId), MatchNumber, *FJsonOut::Quote(PlayerId), *FJsonOut::Quote(Title.Left(80)), *FJsonOut::Quote(Detail.Left(160)), *FJsonOut::Quote(Tone));
			if (!Haptic.IsEmpty())
			{
				Data += FString::Printf(TEXT(",\"haptic\":%s"), *FJsonOut::Quote(Haptic));
			}
			if (bMajor)
			{
				Data += TEXT(",\"prominence\":\"major\"");
			}
			return GameEvent(TEXT("play:feedback"), Data + TEXT("}"));
		}
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

		FString PracticeAck(const FString& GameId, const FString& PlayerId, const FString& ActionId)
		{
			const FString Data = FString::Printf(TEXT("{\"gameId\":%s,\"playerId\":%s,\"actionId\":%s}"),
				*FJsonOut::Quote(GameId), *FJsonOut::Quote(PlayerId), *FJsonOut::Quote(ActionId));
			return FJsonOut(TEXT("game-event")).Str(TEXT("event"), TEXT("practice:ack")).Raw(TEXT("data"), Data).Done();
		}
	}

	bool IsMomentaryType(const FString& Type)
	{
		static const TCHAR* const Types[] = { TEXT("game-event"), TEXT("advance-turn"), TEXT("score-update"), TEXT("state-ack"), TEXT("host-lobby-visible"), TEXT("ping"), TEXT("pong") };
		for (const TCHAR* T : Types)
		{
			if (Is(Type, T)) return true;
		}
		return false;
	}

	bool IsTerminalCloseCode(int32 Code)
	{
		return Code == 1008 || Code == 4000 || Code == 4003 || Code == 4008 || Code == 4100 || Code == 4101;
	}
}

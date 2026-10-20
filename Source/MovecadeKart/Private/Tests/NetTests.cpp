#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/QrCode.h"
#include "Net/RoomProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Net;

// Fixtures: Data/Net/protocol-fixtures.json, written by Tools/net-sim/make-fixtures.mjs from movecade-v2's
// own protocol package and the qrcode-generator library behind the web lobby's QR.
namespace
{
	constexpr EAutomationTestFlags NetTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Object);
		return Object;
	}

	TSharedPtr<FJsonObject> LoadFixtures(FAutomationTestBase& Test)
	{
		FString Text;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Net/protocol-fixtures.json"));
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			Test.AddError(FString::Printf(TEXT("Missing %s (run node Tools/net-sim/make-fixtures.mjs)"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> Object = ParseJson(Text);
		if (!Object.IsValid())
		{
			Test.AddError(TEXT("protocol-fixtures.json does not parse"));
		}
		return Object;
	}

	TArray<uint8> FromHex(const FString& Hex)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Hex.Len() / 2);
		HexToBytes(Hex, Bytes.GetData());
		return Bytes;
	}

	bool JsonEqual(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B, FString& Where)
	{
		if (!A.IsValid() || !B.IsValid() || A->Type != B->Type)
		{
			return false;
		}
		switch (A->Type)
		{
		case EJson::Null: return true;
		case EJson::Boolean: return A->AsBool() == B->AsBool();
		case EJson::Number: return FMath::IsNearlyEqual(A->AsNumber(), B->AsNumber(), 1e-9);
		case EJson::String: return A->AsString() == B->AsString();
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& X = A->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& Y = B->AsArray();
			if (X.Num() != Y.Num()) return false;
			for (int32 i = 0; i < X.Num(); ++i)
			{
				if (!JsonEqual(X[i], Y[i], Where)) { Where = FString::Printf(TEXT("[%d]%s"), i, *Where); return false; }
			}
			return true;
		}
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> X = A->AsObject();
			const TSharedPtr<FJsonObject> Y = B->AsObject();
			if (X->Values.Num() != Y->Values.Num()) { Where = TEXT(" (key count)"); return false; }
			for (const auto& Pair : X->Values)
			{
				const FString Key(Pair.Key);
				const TSharedPtr<FJsonValue> Other = Y->TryGetField(Key);
				if (!Other.IsValid() || !JsonEqual(Pair.Value, Other, Where)) { Where = TEXT(".") + Key + Where; return false; }
			}
			return true;
		}
		default: return false;
		}
	}

	void TestSameJson(FAutomationTestBase& Test, const FString& What, const FString& Actual, const FString& Expected)
	{
		const TSharedPtr<FJsonObject> A = ParseJson(Actual);
		const TSharedPtr<FJsonObject> E = ParseJson(Expected);
		FString Where;
		const bool bSame = A.IsValid() && E.IsValid() && JsonEqual(MakeShared<FJsonValueObject>(A), MakeShared<FJsonValueObject>(E), Where);
		Test.TestTrue(FString::Printf(TEXT("%s matches the movecade fixture%s\n  actual   %s\n  expected %s"), *What, *Where, *Actual, *Expected), bSame);
	}

	double Num(const FJsonObject& O, const TCHAR* Key) { return O.GetNumberField(Key); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetSensorFrameTest, "MovecadeKart.Net.SensorFrameDecode", NetTestFlags)
bool FNetSensorFrameTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> F = LoadFixtures(*this);
	if (!F) return false;
	const TSharedPtr<FJsonObject> Sensor = F->GetObjectField(TEXT("sensor"));
	for (const TSharedPtr<FJsonValue>& Value : Sensor->GetArrayField(TEXT("valid")))
	{
		const FJsonObject& Case = *Value->AsObject();
		const FString Name = Case.GetStringField(TEXT("name"));
		const TArray<uint8> Bytes = FromHex(Case.GetStringField(TEXT("hex")));
		FSensorFrame Frame;
		if (!TestTrue(Name + TEXT(": decodes"), DecodeSensorFrame(Bytes.GetData(), Bytes.Num(), Frame)))
		{
			continue;
		}
		const FJsonObject& D = *Case.GetObjectField(TEXT("decoded"));
		TestEqual(Name + TEXT(": flags"), static_cast<double>(Frame.Flags), Num(D, TEXT("flags")));
		TestEqual(Name + TEXT(": orientation"), static_cast<double>(Frame.ScreenOrientation), Num(D, TEXT("screenOrientation")));
		TestEqual(Name + TEXT(": sequence"), static_cast<double>(Frame.Sequence), Num(D, TEXT("sequence")));
		TestNearlyEqual(Name + TEXT(": alpha"), Frame.Alpha, Num(D, TEXT("alpha")), 1e-9);
		TestNearlyEqual(Name + TEXT(": beta"), Frame.Beta, Num(D, TEXT("beta")), 1e-9);
		TestNearlyEqual(Name + TEXT(": gamma"), Frame.Gamma, Num(D, TEXT("gamma")), 1e-9);
		const FJsonObject& Gyro = *D.GetObjectField(TEXT("gyro"));
		const FJsonObject& Accel = *D.GetObjectField(TEXT("accel"));
		const FJsonObject& Gravity = *D.GetObjectField(TEXT("gravity"));
		TestNearlyEqual(Name + TEXT(": gyro"), (Frame.Gyro - FVector3d(Num(Gyro, TEXT("x")), Num(Gyro, TEXT("y")), Num(Gyro, TEXT("z")))).Length(), 0.0, 1e-9);
		TestNearlyEqual(Name + TEXT(": accel"), (Frame.Accel - FVector3d(Num(Accel, TEXT("x")), Num(Accel, TEXT("y")), Num(Accel, TEXT("z")))).Length(), 0.0, 1e-9);
		TestNearlyEqual(Name + TEXT(": gravity"), (Frame.Gravity - FVector3d(Num(Gravity, TEXT("x")), Num(Gravity, TEXT("y")), Num(Gravity, TEXT("z")))).Length(), 0.0, 1e-9);
		TestEqual(Name + TEXT(": dtMs"), static_cast<double>(Frame.DtMs), Num(D, TEXT("dtMs")));
		const TSharedPtr<FJsonObject>* Quat = nullptr;
		const bool bQuat = D.TryGetObjectField(TEXT("quaternion"), Quat);
		TestTrue(Name + TEXT(": has quaternion"), Frame.bHasQuaternion == bQuat);
		if (bQuat && Frame.bHasQuaternion)
		{
			const FVector4d Q(Num(**Quat, TEXT("x")), Num(**Quat, TEXT("y")), Num(**Quat, TEXT("z")), Num(**Quat, TEXT("w")));
			TestNearlyEqual(Name + TEXT(": quaternion"), (Frame.Quaternion - Q).Size(), 0.0, 1e-9);
		}
		double PlayerIndex = -1.0;
		const bool bRelayed = D.TryGetNumberField(TEXT("playerIndex"), PlayerIndex);
		TestEqual(Name + TEXT(": seat suffix"), Frame.PlayerIndex, bRelayed ? static_cast<int32>(PlayerIndex) : INDEX_NONE);
	}
	for (const TSharedPtr<FJsonValue>& Value : Sensor->GetArrayField(TEXT("invalid")))
	{
		const FJsonObject& Case = *Value->AsObject();
		const TArray<uint8> Bytes = FromHex(Case.GetStringField(TEXT("hex")));
		FSensorFrame Frame;
		ESensorError Error = ESensorError::None;
		TestFalse(Case.GetStringField(TEXT("name")) + TEXT(": rejected"), DecodeSensorFrame(Bytes.GetData(), Bytes.Num(), Frame, &Error));
		TestEqual(Case.GetStringField(TEXT("name")) + TEXT(": error code"), FString(LexToString(Error)), Case.GetStringField(TEXT("error")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetSteeringTest, "MovecadeKart.Net.SteeringMapping", NetTestFlags)
bool FNetSteeringTest::RunTest(const FString&)
{
	// MultiplayerMobileControlBridge.tsx: clamp(g/90), 2 deg deadzone rescaled, sensitivity, invert.
	TestEqual(TEXT("level wheel"), SteeringFromGamma(0.0), 0.0);
	TestEqual(TEXT("inside the 2 deg deadzone"), SteeringFromGamma(1.9), 0.0);
	TestEqual(TEXT("inside the deadzone, left"), SteeringFromGamma(-1.99), 0.0);
	TestNearlyEqual(TEXT("45 deg clockwise steers right (negative)"), SteeringFromGamma(45.0), -43.0 / 88.0, 1e-12);
	TestNearlyEqual(TEXT("45 deg counter-clockwise steers left (positive)"), SteeringFromGamma(-45.0), 43.0 / 88.0, 1e-12);
	TestNearlyEqual(TEXT("-37.25 deg (fixture frame)"), SteeringFromGamma(-37.25), 35.25 / 88.0, 1e-12);
	TestNearlyEqual(TEXT("just past the deadzone is near zero"), SteeringFromGamma(2.5), -0.5 / 88.0, 1e-12);
	TestEqual(TEXT("90 deg is full right"), SteeringFromGamma(90.0), -1.0);
	TestEqual(TEXT("past 90 deg clamps"), SteeringFromGamma(135.0), -1.0);
	TestEqual(TEXT("200 deg wraps to -160 = full left"), SteeringFromGamma(200.0), 1.0);
	TestEqual(TEXT("-270 deg wraps to +90 = full right"), SteeringFromGamma(-270.0), -1.0);
	TestNearlyEqual(TEXT("sensitivity 2"), SteeringFromGamma(45.0, 2.0), -86.0 / 88.0, 1e-12);
	TestEqual(TEXT("sensitivity clamps"), SteeringFromGamma(45.0, 3.0), -1.0);
	TestEqual(TEXT("NaN is neutral"), SteeringFromGamma(std::numeric_limits<double>::quiet_NaN()), 0.0);

	// The fixture frame relayed for seat index 0 carries gamma -37.25.
	const TSharedPtr<FJsonObject> F = LoadFixtures(*this);
	if (!F) return false;
	const TArray<TSharedPtr<FJsonValue>>& Valid = F->GetObjectField(TEXT("sensor"))->GetArrayField(TEXT("valid"));
	const TArray<uint8> Bytes = FromHex(Valid[2]->AsObject()->GetStringField(TEXT("hex")));
	FSensorFrame Frame;
	TestTrue(TEXT("fixture frame decodes"), DecodeSensorFrame(Bytes.GetData(), Bytes.Num(), Frame));
	TestEqual(TEXT("fixture frame seat"), Frame.PlayerIndex, 0);
	TestNearlyEqual(TEXT("fixture frame steering"), SteeringFromGamma(Frame.Gamma), 35.25 / 88.0, 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetClientMessagesTest, "MovecadeKart.Net.ClientMessages", NetTestFlags)
bool FNetClientMessagesTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> F = LoadFixtures(*this);
	if (!F) return false;
	const TSharedPtr<FJsonObject> M = F->GetObjectField(TEXT("clientMessages"));
	auto Expect = [&](const TCHAR* Name, const FString& Actual) { TestSameJson(*this, Name, Actual, M->GetStringField(Name)); };
	const TArray<FMcScoreEntry> Scores = { { TEXT("p2"), 61234 }, { TEXT("p1"), 65000 } };
	Expect(TEXT("hello"), Msg::Hello(TEXT("ue5-host/0.1.0"), TEXT("kart"), TEXT("0F8FAD5B-D9CB-469F-A165-70867728950E"), FString()));
	Expect(TEXT("helloReclaim"), Msg::Hello(TEXT("ue5-host/0.1.0"), TEXT("kart"), TEXT("0f8fad5b-d9cb-469f-a165-70867728950e"), TEXT("host-token_1")));
	Expect(TEXT("pong"), Msg::Pong());
	Expect(TEXT("stateAck"), Msg::StateAck(42));
	Expect(TEXT("configureMap"), Msg::ConfigureRoomMap(TEXT("sunset-harbor")));
	Expect(TEXT("lobbyCountdown"), Msg::SetCountdown(TEXT("lobby"), 5000));
	Expect(TEXT("lobbyCountdownCancel"), Msg::SetCountdown(TEXT("lobby"), 0));
	Expect(TEXT("matchCountdown"), Msg::SetCountdown(TEXT("match"), 5000));
	Expect(TEXT("matchGo"), Msg::SetCountdown(TEXT("match"), 0));
	Expect(TEXT("startGame"), Msg::StartGame());
	Expect(TEXT("advanceTurn"), Msg::AdvanceTurnSimultaneous());
	Expect(TEXT("scoreUpdate"), Msg::ScoreUpdate(Scores));
	Expect(TEXT("completeGame"), Msg::CompleteGame(Scores));
	Expect(TEXT("restartGame"), Msg::RestartGame(TEXT("ue-restart-1")));
	Expect(TEXT("returnToLobby"), Msg::ReturnToLobby(TEXT("ue-lobby-2")));
	Expect(TEXT("kickPlayer"), Msg::KickPlayer(TEXT("p3")));
	Expect(TEXT("dropPlayer"), Msg::DropPlayer(TEXT("p3")));
	Expect(TEXT("closeRoom"), Msg::CloseRoom());
	Expect(TEXT("haptic"), Msg::Haptic(TEXT("p2"), 0.45, TEXT("barrier"), false));
	Expect(TEXT("scoresAck"), Msg::ScoresAck(3));
	TestTrue(TEXT("haptic intensity is clamped to 1"), Msg::Haptic(TEXT("p1"), 7.0, TEXT("barrier"), true).Contains(TEXT("\"intensity\":1,")));
	TestTrue(TEXT("countdown is clamped to 60 s"), Msg::SetCountdown(TEXT("lobby"), 99999).Contains(TEXT("\"durationMs\":60000")));
	TestTrue(TEXT("strings are escaped"), ParseJson(Msg::KickPlayer(TEXT("a\"b\\c\n"))).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetPracticeTest, "MovecadeKart.Net.PracticeAck", NetTestFlags)
bool FNetPracticeTest::RunTest(const FString&)
{
	auto Read = [](const FString& Json, FString& Id) { return ReadPracticeAction(*ParseJson(Json), TEXT("kart"), Id); };
	auto Action = [](const TCHAR* Game, const TCHAR* Kind, const FString& ActionId)
	{
		return FString::Printf(TEXT("{\"gameId\":\"%s\",\"kind\":\"%s\",\"actionId\":\"%s\"}"), Game, Kind, *ActionId);
	};
	FString Id;
	TestTrue(TEXT("a kart steer practice is read"), Read(Action(TEXT("kart"), TEXT("steer"), TEXT("A-1")), Id));
	TestEqual(TEXT("its action id"), Id, FString(TEXT("A-1")));
	TestFalse(TEXT("another game's practice is refused"), Read(Action(TEXT("darts"), TEXT("throw"), TEXT("A")), Id));
	TestFalse(TEXT("the wrong kind is refused"), Read(Action(TEXT("kart"), TEXT("roll"), TEXT("A")), Id));
	TestFalse(TEXT("an empty action id is refused"), Read(Action(TEXT("kart"), TEXT("steer"), FString()), Id));
	TestFalse(TEXT("an 81-char action id is refused"), Read(Action(TEXT("kart"), TEXT("steer"), FString::ChrN(81, 'a')), Id));

	const TSharedPtr<FJsonObject> Ack = ParseJson(Msg::PracticeAck(TEXT("kart"), TEXT("p1"), TEXT("A-1")));
	if (!TestTrue(TEXT("ack parses"), Ack.IsValid())) return false;
	TestEqual(TEXT("ack type"), Ack->GetStringField(TEXT("t")), FString(TEXT("game-event")));
	TestEqual(TEXT("ack event"), Ack->GetStringField(TEXT("event")), FString(TEXT("practice:ack")));
	const TSharedPtr<FJsonObject> Data = Ack->GetObjectField(TEXT("data"));
	TestEqual(TEXT("ack game"), Data->GetStringField(TEXT("gameId")), FString(TEXT("kart")));
	TestEqual(TEXT("ack player"), Data->GetStringField(TEXT("playerId")), FString(TEXT("p1")));
	TestEqual(TEXT("ack action"), Data->GetStringField(TEXT("actionId")), FString(TEXT("A-1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetJoinUrlTest, "MovecadeKart.Net.JoinUrlAndRoomCode", NetTestFlags)
bool FNetJoinUrlTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> F = LoadFixtures(*this);
	if (!F) return false;
	for (const TSharedPtr<FJsonValue>& Value : F->GetArrayField(TEXT("joinUrls")))
	{
		const FJsonObject& Case = *Value->AsObject();
		TestEqual(TEXT("buildJoinUrl ") + Case.GetStringField(TEXT("url")),
			BuildJoinUrl(Case.GetStringField(TEXT("base")), Case.GetStringField(TEXT("code")), Case.GetStringField(TEXT("token"))), Case.GetStringField(TEXT("url")));
	}
	for (const TSharedPtr<FJsonValue>& Value : F->GetArrayField(TEXT("roomCodes")))
	{
		const FJsonObject& Case = *Value->AsObject();
		TArray<uint8> Source;
		for (const TSharedPtr<FJsonValue>& B : Case.GetArrayField(TEXT("bytes")))
		{
			Source.Add(static_cast<uint8>(B->AsNumber()));
		}
		int32 Offset = 0;
		const FString Code = GenerateRoomCode([&](uint8* Out, int32 Count)
		{
			for (int32 i = 0; i < Count; ++i) Out[i] = Source[(Offset + i) % Source.Num()];
			Offset += Count;
		});
		TestEqual(TEXT("generateRoomCode with fixed bytes"), Code, Case.GetStringField(TEXT("code")));
		TestEqual(TEXT("roomIdForCode"), RoomIdForCode(Code), Case.GetStringField(TEXT("roomId")));
	}
	for (int32 i = 0; i < 50; ++i)
	{
		const FString Code = GenerateRoomCode();
		TestTrue(FString::Printf(TEXT("random code %s is valid"), *Code), IsValidRoomCode(Code));
	}
	TestFalse(TEXT("O is not in the alphabet"), IsValidRoomCode(TEXT("ABCDEO")));
	TestFalse(TEXT("5 chars"), IsValidRoomCode(TEXT("ABCDE")));
	for (int32 i = 0; i < 20; ++i)
	{
		const FString Id = MakeIdentity();
		TestTrue(FString::Printf(TEXT("identity %s is a lower-case v4 UUID"), *Id), IsV4Uuid(Id) && Id == Id.ToLower());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetServerMessagesTest, "MovecadeKart.Net.ServerMessages", NetTestFlags)
bool FNetServerMessagesTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> F = LoadFixtures(*this);
	if (!F) return false;
	const TSharedPtr<FJsonObject> S = F->GetObjectField(TEXT("serverMessages"));

	FMcRoomState Welcome;
	TestTrue(TEXT("welcome state parses"), ParseRoomState(*S->GetObjectField(TEXT("welcome"))->GetObjectField(TEXT("state")), Welcome));
	TestEqual(TEXT("join code"), Welcome.JoinCode, FString(TEXT("ABCDEF")));
	TestEqual(TEXT("lobby"), static_cast<int32>(Welcome.Status), static_cast<int32>(EMcRoomStatus::Lobby));
	TestEqual(TEXT("default map"), Welcome.SelectedMapId, FString(TEXT("violet-loop")));
	TestEqual(TEXT("host session"), Welcome.HostSessionId, FString(TEXT("s1")));

	FMcRoomState State;
	TestTrue(TEXT("state parses"), ParseRoomState(*S->GetObjectField(TEXT("state"))->GetObjectField(TEXT("state")), State));
	TestEqual(TEXT("two seats"), State.Players.Num(), 2);
	TestEqual(TEXT("paused"), static_cast<int32>(State.Status), static_cast<int32>(EMcRoomStatus::Paused));
	TestEqual(TEXT("pause reason"), State.PauseReason, FString(TEXT("player-disconnected")));
	TestEqual(TEXT("paused for"), State.PausedForPlayerId, FString(TEXT("p3")));
	TestEqual(TEXT("match number"), State.MatchNumber, 2);
	TestEqual(TEXT("countdown kind"), State.CountdownKind, FString(TEXT("lobby")));
	TestEqual(TEXT("countdown remaining"), State.CountdownEndsAt - State.ServerTime, int64(4000));
	TestEqual(TEXT("sunset harbor"), State.SelectedMapId, FString(TEXT("sunset-harbor")));
	const FMcSeat* P1 = State.FindBySeat(1);
	const FMcSeat* P3 = State.FindBySeat(3);
	TestTrue(TEXT("seat 1 and seat 3 found by seat"), P1 && P3 && !State.FindBySeat(2));
	if (P1 && P3)
	{
		TestEqual(TEXT("p1 name"), P1->Name, FString(TEXT("Jimmy")));
		TestEqual(TEXT("p1 paint"), P1->KartPaint, FString(TEXT("cyan")));
		TestEqual(TEXT("p1 vehicle"), P1->Vehicle, FString(TEXT("kart")));
		TestTrue(TEXT("p1 paint colour is #5ec8e5"), P1->PaintColor.Equals(FLinearColor(FColor::FromHex(TEXT("5ec8e5")))));
		TestTrue(TEXT("p1 ready and connected"), P1->bReady && P1->bConnected && P1->bCalibrated);
		TestEqual(TEXT("p3 seat index"), P3->SeatIndex(), 2);
		TestFalse(TEXT("p3 disconnected"), P3->bConnected);
		TestEqual(TEXT("p3 grace"), P3->GraceRemainingMs, 125000);
		TestTrue(TEXT("p3 uses the seat-3 colour #f2b23f"), P3->PaintColor.Equals(FLinearColor(FColor::FromHex(TEXT("f2b23f")))));
	}

	auto Input = [&](const TCHAR* Name) { return ParseKartInput(*S->GetObjectField(Name)->GetObjectField(TEXT("data"))); };
	const FKartInputEvent Gas = Input(TEXT("kartInputGas"));
	TestTrue(TEXT("gas only"), Gas.Accelerating.IsSet() && *Gas.Accelerating && !Gas.Reversing && !Gas.Drifting && !Gas.Steering && !Gas.bUseItem);
	const FKartInputEvent Off = Input(TEXT("kartInputPedalsOff"));
	TestTrue(TEXT("all pedals off"), Off.Accelerating && !*Off.Accelerating && Off.Reversing && !*Off.Reversing && Off.Drifting && !*Off.Drifting);
	const FKartInputEvent Bot = Input(TEXT("kartInputBot"));
	TestTrue(TEXT("bot steering clamped to -1"), Bot.Steering.IsSet() && *Bot.Steering == -1.0 && Bot.Drifting && *Bot.Drifting);
	const FKartInputEvent Item = Input(TEXT("kartInputItem"));
	TestTrue(TEXT("item press"), Item.bUseItem && !Item.Accelerating);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetFinalScoresTest, "MovecadeKart.Net.FinalScores", NetTestFlags)
bool FNetFinalScoresTest::RunTest(const FString&)
{
	FMcRoomState Room;
	for (int32 Seat = 1; Seat <= 4; ++Seat)
	{
		FMcSeat P;
		P.Seat = Seat;
		P.PlayerId = FString::Printf(TEXT("p%d"), Seat + 4);
		Room.Players.Add(P);
	}
	auto Result = [](int32 SeatIndex, bool bFinished, double Ms) { FMcRaceResult R; R.SeatIndex = SeatIndex; R.bFinished = bFinished; R.FinishTimeMs = Ms; return R; };
	const TArray<FMcScoreEntry> Scores = BuildFinalScores({
		Result(0, true, 75000.4),  // slowest finisher
		Result(1, false, 0.0),     // DNF: never reported (GAMES-02)
		Result(2, true, 61234.6),  // winner, rounded up
		Result(3, true, 0.0),      // a 0 ms "finish" is never reported
	}, Room);
	TestEqual(TEXT("two finishers"), Scores.Num(), 2);
	if (Scores.Num() == 2)
	{
		TestEqual(TEXT("fastest first (GAMES-01)"), Scores[0].PlayerId, FString(TEXT("p7")));
		TestEqual(TEXT("rounded ms"), Scores[0].Score, 61235);
		TestEqual(TEXT("then the slower"), Scores[1].PlayerId, FString(TEXT("p5")));
		TestEqual(TEXT("rounded down"), Scores[1].Score, 75000);
	}
	const TArray<FMcScoreEntry> Edge = BuildFinalScores({ Result(0, true, 9999.0), Result(1, true, 600001.0), Result(2, true, 10000.0) }, Room);
	TestEqual(TEXT("only [10000, 600000] counts as a finish"), Edge.Num(), 1);
	FMcRaceResult ById = Result(INDEX_NONE, true, 30000.0);
	ById.PlayerId = TEXT("p9");
	TestEqual(TEXT("explicit player id"), BuildFinalScores({ ById }, Room)[0].PlayerId, FString(TEXT("p9")));
	TestTrue(TEXT("valid live scores"), AreScoresValid({ { TEXT("p1"), 250 }, { TEXT("p2"), 9999 } }));
	TestFalse(TEXT("duplicate ids are invalid"), AreScoresValid({ { TEXT("p1"), 1 }, { TEXT("p1"), 2 } }));
	TestFalse(TEXT("negative score is invalid"), AreScoresValid({ { TEXT("p1"), -1 } }));
	TestFalse(TEXT("bad id is invalid"), AreScoresValid({ { TEXT("p0"), 1 } }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetQrFormatTest, "MovecadeKart.Net.QrFormatAndVersionBits", NetTestFlags)
bool FNetQrFormatTest::RunTest(const FString&)
{
	// ISO/IEC 18004 Annex C table C.1 (level M) and Annex D table D.1.
	const uint32 FormatM[8] = { 0x5412, 0x5125, 0x5E7C, 0x5B4B, 0x45F9, 0x40CE, 0x4F97, 0x4AA0 };
	for (int32 Mask = 0; Mask < 8; ++Mask)
	{
		TestEqual(FString::Printf(TEXT("format bits M, mask %d"), Mask), Qr::FormatBits(Mask), FormatM[Mask]);
	}
	TestEqual(TEXT("version 7 bits"), Qr::VersionBits(7), 0x07C94u);
	TestEqual(TEXT("version 8 bits"), Qr::VersionBits(8), 0x085BCu);
	TestEqual(TEXT("version 9 bits"), Qr::VersionBits(9), 0x09A99u);
	TestEqual(TEXT("version 10 bits"), Qr::VersionBits(10), 0x0A4D3u);
	TestEqual(TEXT("v1 byte capacity at M"), Qr::ByteCapacity(1), 14);
	TestEqual(TEXT("v5 byte capacity at M"), Qr::ByteCapacity(5), 84);
	TestEqual(TEXT("v10 byte capacity at M"), Qr::ByteCapacity(10), 213);
	TestFalse(TEXT("214 bytes does not fit"), Qr::EncodeText(FString::ChrN(214, TEXT('a'))).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetQrReferenceTest, "MovecadeKart.Net.QrReferenceMatrices", NetTestFlags)
bool FNetQrReferenceTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> F = LoadFixtures(*this);
	if (!F) return false;
	for (const TSharedPtr<FJsonValue>& Value : F->GetArrayField(TEXT("qr")))
	{
		const FJsonObject& Case = *Value->AsObject();
		const FString Text = Case.GetStringField(TEXT("text"));
		const TArray<TSharedPtr<FJsonValue>>& Rows = Case.GetArrayField(TEXT("rows"));
		Qr::FQrMatrix Reference;
		Reference.Size = Rows.Num();
		Reference.Version = static_cast<int32>(Case.GetNumberField(TEXT("version")));
		for (const TSharedPtr<FJsonValue>& Row : Rows)
		{
			for (const TCHAR C : Row->AsString())
			{
				Reference.Modules.Add(C == TEXT('1') ? 1 : 0);
			}
		}
		// The reference library picks its own mask; read it back from the format bits and force it.
		const uint32 Format = Qr::ReadFormatBits(Reference) ^ 0x5412;
		const int32 Ecc = (Format >> 13) & 3;
		const int32 Mask = (Format >> 10) & 7;
		const FString Label = FString::Printf(TEXT("v%d \"%s\""), Reference.Version, *Text.Left(24));
		TestEqual(Label + TEXT(": reference is level M"), Ecc, 0);
		TestEqual(Label + TEXT(": reference format bits are well formed"), Qr::ReadFormatBits(Reference), Qr::FormatBits(Mask));

		const Qr::FQrMatrix Ours = Qr::EncodeText(Text, Mask);
		TestEqual(Label + TEXT(": version"), Ours.Version, Reference.Version);
		TestEqual(Label + TEXT(": size"), Ours.Size, Reference.Size);
		if (Ours.Size != Reference.Size)
		{
			continue;
		}
		int32 Mismatches = 0;
		FString First;
		for (int32 i = 0; i < Ours.Modules.Num(); ++i)
		{
			if (Ours.Modules[i] != Reference.Modules[i])
			{
				if (Mismatches++ == 0) First = FString::Printf(TEXT(" (first at x=%d y=%d)"), i % Ours.Size, i / Ours.Size);
			}
		}
		TestEqual(Label + TEXT(": every module matches the reference") + First, Mismatches, 0);

		// Our own mask choice must yield a consistent symbol too.
		const Qr::FQrMatrix Auto = Qr::EncodeText(Text);
		TestTrue(Label + TEXT(": auto mask in range"), Auto.Mask >= 0 && Auto.Mask < 8);
		TestEqual(Label + TEXT(": auto mask format bits"), Qr::ReadFormatBits(Auto), Qr::FormatBits(Auto.Mask));
		TestTrue(Label + TEXT(": auto mask equals the forced encode of that mask"), Auto.Modules == Qr::EncodeText(Text, Auto.Mask).Modules);
	}
	return true;
}

#endif

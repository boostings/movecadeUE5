// Pentest fuzz + boundary tests for the Party Mode inbound parsers (pentest-net branch).
//
// Scope: a malicious phone or a malicious / compromised room server must not be able to crash, hang or
// exhaust the UE host through the pure wire parsers. Every server text frame reaches these functions
// (RoomHostInbound.cpp), and an honest server's validation (movecade-v2 protocol/) does NOT bind a
// compromised server, so the host must stand on its own.
//
// These tests assert robustness (no crash, bounded/finite outputs) and pin two current vulnerabilities as
// documented behaviour so a fix flips the marked assertions (see Docs/Agents/PENTEST-NET.md):
//   * NET-01  ParseRoomState accepts an unbounded, non-deduplicated player roster.
//   * NET-02  ParseRoomState does not bound or sanitise attacker-controlled seat strings (name/emoji/...).

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Net/RoomProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Net;

namespace
{
	constexpr EAutomationTestFlags FuzzFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// A small deterministic PRNG so a failure reproduces from the seed printed in the test name.
	struct FXorShift
	{
		uint64 S;
		explicit FXorShift(uint64 Seed) : S(Seed ? Seed : 0x9E3779B97F4A7C15ull) {}
		uint64 Next() { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return S; }
		uint32 U32() { return static_cast<uint32>(Next()); }
		int32 Range(int32 LoInc, int32 HiInc) { return LoInc + static_cast<int32>(U32() % static_cast<uint32>(HiInc - LoInc + 1)); }
		uint8 Byte() { return static_cast<uint8>(U32()); }
	};

	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Object);
		return Object;
	}
}

// ───────────────────────────── binary sensor frames ─────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetFuzzSensorTest, "MovecadeKart.Net.Fuzz.SensorFrame", FuzzFlags)
bool FNetFuzzSensorTest::RunTest(const FString&)
{
	FXorShift Rng(0xBADC0FFEE0DDF00Dull);
	for (int32 Iter = 0; Iter < 200000; ++Iter)
	{
		// Random size around the four legal sizes (30/31/38/39), plus 0 and large.
		int32 Size;
		switch (Rng.Range(0, 6))
		{
		case 0: Size = 0; break;
		case 1: Size = Rng.Range(1, 29); break;
		case 2: Size = 30; break;
		case 3: Size = 31; break;
		case 4: Size = 38; break;
		case 5: Size = 39; break;
		default: Size = Rng.Range(40, 512); break;
		}
		TArray<uint8> Buf;
		Buf.SetNumUninitialized(FMath::Max(1, Size));
		for (int32 i = 0; i < Buf.Num(); ++i)
		{
			Buf[i] = Rng.Byte();
		}
		// Sometimes force the header toward valid so the decode path (not just validation) runs.
		if (Size >= 3 && (Rng.U32() & 1))
		{
			Buf[0] = SensorFrameType;
			Buf[1] = static_cast<uint8>(Rng.U32() & SensorFlagMask);
			Buf[2] = static_cast<uint8>(Rng.Range(0, 3));
		}

		FSensorFrame Frame;
		ESensorError Err = ESensorError::None;
		const bool bOk = DecodeSensorFrame(Size == 0 ? nullptr : Buf.GetData(), Size, Frame, &Err);
		if (bOk)
		{
			const bool bValidSize = Size == SensorFrameBytes || Size == SensorFrameWithPlayerBytes
				|| Size == SensorFrameV2Bytes || Size == SensorFrameV2WithPlayerBytes;
			TestTrue(TEXT("accepted frame has a legal size"), bValidSize);
			// Every decoded scalar is finite and bounded (int16 / fixed divisor), so no NaN quaternion or
			// Inf can enter a game. This is the property the per-game quaternion maths relies on.
			const bool bFinite = FMath::IsFinite(Frame.Alpha) && FMath::IsFinite(Frame.Beta) && FMath::IsFinite(Frame.Gamma)
				&& Frame.Gyro.ContainsNaN() == false && Frame.Accel.ContainsNaN() == false && Frame.Gravity.ContainsNaN() == false;
			TestTrue(TEXT("decoded values are finite"), bFinite);
			if (Frame.bHasQuaternion)
			{
				const double MaxComp = FMath::Max(FMath::Max(FMath::Abs(Frame.Quaternion.X), FMath::Abs(Frame.Quaternion.Y)),
					FMath::Max(FMath::Abs(Frame.Quaternion.Z), FMath::Abs(Frame.Quaternion.W)));
				TestTrue(TEXT("quaternion components bounded to +/-2"), MaxComp <= 2.0 + KINDA_SMALL_NUMBER);
			}
		}
	}
	// SteeringFromGamma with hostile inputs (NaN/Inf/huge) must stay finite and clamped.
	for (double G : { 0.0, 90.0, -90.0, 1e18, -1e18, std::numeric_limits<double>::infinity(),
		-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
	{
		for (double Sens : { 0.0, 1.0, 1e9, std::numeric_limits<double>::infinity() })
		{
			const double S = SteeringFromGamma(G, Sens);
			TestTrue(TEXT("steering is finite"), FMath::IsFinite(S));
			TestTrue(TEXT("steering is clamped to [-1,1]"), S >= -1.0 - KINDA_SMALL_NUMBER && S <= 1.0 + KINDA_SMALL_NUMBER);
		}
	}
	return true;
}

// ───────────────────────────── JSON room-state / messages ─────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetFuzzJsonTest, "MovecadeKart.Net.Fuzz.Json", FuzzFlags)
bool FNetFuzzJsonTest::RunTest(const FString&)
{
	// Hostile JSON fragments the server could send. No crash is the pass condition; invariants checked on success.
	const TCHAR* Fragments[] = {
		TEXT("{}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":-5}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":1e308}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":\"NaN\"}"),
		TEXT("{\"roomId\":\"\",\"stateVersion\":1}"),
		TEXT("{\"roomId\":123,\"stateVersion\":1}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"players\":\"notarray\"}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"players\":[null,1,\"x\",{}]}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"players\":[{\"id\":\"p1\",\"seat\":2.9}]}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"players\":[{\"id\":\"p1\",\"seat\":-2147483648}]}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"players\":[{\"id\":\"p1\",\"seat\":9999999999}]}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"game\":{\"status\":42}}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"game\":{\"status\":\"in-progress\",\"startedAt\":-1}}"),
		TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"countdown\":{\"endsAt\":1e400}}"),
	};
	for (const TCHAR* Frag : Fragments)
	{
		if (const TSharedPtr<FJsonObject> Obj = Parse(Frag))
		{
			FMcRoomState Out;
			ParseRoomState(*Obj, Out); // must not crash; result may be false
		}
	}

	// kart-input with wrong types / NaN / Inf: steering must come out finite and clamped, booleans ignored unless bool.
	const TCHAR* Inputs[] = {
		TEXT("{\"steeringInput\":1e308}"),
		TEXT("{\"steeringInput\":\"left\"}"),
		TEXT("{\"steeringInput\":null}"),
		TEXT("{\"isAccelerating\":\"yes\"}"),
		TEXT("{\"isAccelerating\":1}"),
		TEXT("{\"useItem\":\"true\"}"),
		TEXT("{\"steeringInput\":-99.0,\"isDrifting\":true}"),
	};
	for (const TCHAR* In : Inputs)
	{
		const TSharedPtr<FJsonObject> Obj = Parse(In);
		if (!Obj) continue;
		const FKartInputEvent Ev = ParseKartInput(*Obj);
		if (Ev.Steering.IsSet())
		{
			TestTrue(TEXT("kart steering finite"), FMath::IsFinite(*Ev.Steering));
			TestTrue(TEXT("kart steering clamped"), *Ev.Steering >= -1.0 && *Ev.Steering <= 1.0);
		}
	}

	// Random bytes as JSON text: the deserialiser must never crash.
	FXorShift Rng(0x1234567890ABCDEFull);
	for (int32 Iter = 0; Iter < 20000; ++Iter)
	{
		const int32 Len = Rng.Range(0, 200);
		FString S;
		for (int32 i = 0; i < Len; ++i)
		{
			S.AppendChar(static_cast<TCHAR>(Rng.Range(32, 126)));
		}
		if (const TSharedPtr<FJsonObject> Obj = Parse(S))
		{
			FMcRoomState Out;
			ParseRoomState(*Obj, Out);
		}
	}

	// Deep nesting (the engine reader is iterative/heap-based, so this must not stack-overflow).
	{
		FString Deep;
		for (int32 i = 0; i < 20000; ++i) Deep += TEXT("{\"a\":");
		Deep += TEXT("1");
		for (int32 i = 0; i < 20000; ++i) Deep += TEXT("}");
		Parse(Deep); // no crash
		TestTrue(TEXT("deep nesting parsed without crashing"), true);
	}
	return true;
}

// ───────────────────────────── NET-01: unbounded roster ─────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetFuzzRosterTest, "MovecadeKart.Net.Fuzz.UnboundedRoster", FuzzFlags)
bool FNetFuzzRosterTest::RunTest(const FString&)
{
	// A compromised room server sends thousands of seats, all on seat 1 with the same id. The host caps each
	// seat index to [1,4] but never caps the ARRAY length and never deduplicates, so Room.Players grows without
	// bound. ApplySeatChanges then does O(n*m) FindByPlayerId lookups per state message (RoomHostInbound.cpp:356).
	const int32 N = 20000;
	FString Json = TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"players\":[");
	for (int32 i = 0; i < N; ++i)
	{
		Json += FString::Printf(TEXT("%s{\"id\":\"p1\",\"seat\":1,\"name\":\"x\"}"), i ? TEXT(",") : TEXT(""));
	}
	Json += TEXT("]}");

	const TSharedPtr<FJsonObject> Obj = Parse(Json);
	TestTrue(TEXT("roster JSON parses"), Obj.IsValid());
	FMcRoomState Out;
	const bool bOk = Obj.IsValid() && ParseRoomState(*Obj, Out);
	TestTrue(TEXT("state accepted"), bOk);

	// VULN NET-01 (current behaviour): the roster is NOT clamped to MaxPlayers and duplicates are kept.
	// When this is fixed (clamp + dedupe in ParseRoomState), change this to: TestTrue(Out.Players.Num() <= MaxPlayers).
	AddInfo(FString::Printf(TEXT("NET-01: ParseRoomState kept %d seats (MaxPlayers=%d) from a malicious roster"), Out.Players.Num(), MaxPlayers));
	TestTrue(TEXT("NET-01 reproduces: more seats than MaxPlayers were accepted"), Out.Players.Num() > MaxPlayers);
	return true;
}

// ───────────────────────────── NET-02: attacker-controlled strings ─────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetFuzzNameTest, "MovecadeKart.Net.Fuzz.UnboundedSeatStrings", FuzzFlags)
bool FNetFuzzNameTest::RunTest(const FString&)
{
	// A compromised room server bypasses the web app's sanitizePlayerName (20 chars, [a-zA-Z0-9\s\-_]). The host
	// copies name/emoji verbatim and renders them in the UMG lobby and the Slate HUDs.
	FString Evil;
	for (int32 i = 0; i < 100000; ++i) Evil.AppendChar(TEXT('Z'));
	Evil += TEXT("\n\n\n\r\t");                       // newline / control floods
	Evil += TEXT("‮\u0007\u0000 GAME OVER");     // RTL override, bell, NUL, spoof text

	const FString Json = FString::Printf(TEXT("{\"roomId\":\"r-x\",\"stateVersion\":0,\"players\":[{\"id\":\"p1\",\"seat\":1,\"name\":%s,\"emoji\":\"\\ud83d\\ude00\"}]}"),
		*QuoteJson(Evil));
	const TSharedPtr<FJsonObject> Obj = Parse(Json);
	TestTrue(TEXT("evil-name JSON parses"), Obj.IsValid());
	FMcRoomState Out;
	if (Obj.IsValid() && ParseRoomState(*Obj, Out) && Out.Players.Num() == 1)
	{
		AddInfo(FString::Printf(TEXT("NET-02: seat name length after parse = %d (web cap is 20)"), Out.Players[0].Name.Len()));
		// VULN NET-02 (current behaviour): no length or charset bound. When fixed, expect Name.Len() <= 20 and no
		// control characters.
		TestTrue(TEXT("NET-02 reproduces: oversized name accepted unchanged"), Out.Players[0].Name.Len() > 20);
	}
	else
	{
		AddError(TEXT("expected exactly one parsed seat"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

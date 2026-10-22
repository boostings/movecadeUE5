// The host's own defences against a hostile or compromised room server (Party Mode pentest findings NET-01..04,
// F-09, F-10). The fuzz side lives in NetFuzzTests.cpp; these pin each fix's exact behaviour.

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Net/RoomProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Net;

namespace
{
	constexpr EAutomationTestFlags HardeningTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	/** A state frame around the given players array (JSON text). */
	bool ParseRoster(const FString& PlayersJson, FMcRoomState& Out, FRoomStateIssues& Issues)
	{
		const TSharedPtr<FJsonObject> State = ParseJson(FString::Printf(TEXT("{\"roomId\":\"r-x\",\"stateVersion\":3,\"players\":[%s]}"), *PlayersJson));
		return State.IsValid() && ParseRoomState(*State, Out, &Issues);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetHardeningRosterTest, "MovecadeKart.Net.Hardening.RosterBounds", HardeningTestFlags)
bool FNetHardeningRosterTest::RunTest(const FString&)
{
	// An honest four-seat room with an eliminated seat comes through whole and clean.
	{
		FMcRoomState Room;
		FRoomStateIssues Issues;
		TestTrue(TEXT("honest roster parses"), ParseRoster(TEXT(
			"{\"id\":\"p3\",\"seat\":3,\"name\":\"Cy\",\"eliminated\":true},{\"id\":\"p1\",\"seat\":1,\"name\":\"Ann\"},"
			"{\"id\":\"p2\",\"seat\":2,\"name\":\"Bo\"},{\"id\":\"p4\",\"seat\":4,\"name\":\"Di\",\"bot\":true}"), Room, Issues));
		TestEqual(TEXT("all four seats kept"), Room.Players.Num(), 4);
		TestFalse(TEXT("an honest roster reports no issues"), Issues.HasAny());
		TestTrue(TEXT("sorted by seat, the eliminated seat kept"), Room.Players.Num() == 4 && Room.Players[0].Seat == 1 && Room.Players[2].bEliminated);
	}
	// Duplicate seats and ids: the first one wins; an over-long id is no seat at all.
	{
		FMcRoomState Room;
		FRoomStateIssues Issues;
		TestTrue(TEXT("hostile roster parses"), ParseRoster(TEXT(
			"{\"id\":\"p1\",\"seat\":1,\"name\":\"First\"},{\"id\":\"p9\",\"seat\":1,\"name\":\"SameSeat\"},"
			"{\"id\":\"p1\",\"seat\":2,\"name\":\"SameId\"},{\"id\":\"p12345678901234567\",\"seat\":3},{\"id\":\"p3\",\"seat\":3}"), Room, Issues));
		TestEqual(TEXT("two distinct seats kept"), Room.Players.Num(), 2);
		TestEqual(TEXT("seat 1 is the first claim"), Room.FindBySeat(1) ? Room.FindBySeat(1)->Name : FString(), FString(TEXT("First")));
		TestTrue(TEXT("seat 3 is p3, not the 18-char id"), Room.FindBySeat(3) && Room.FindBySeat(3)->PlayerId == TEXT("p3"));
		TestEqual(TEXT("three entries skipped"), Issues.SkippedEntries, 3);
		TestEqual(TEXT("nothing unread"), Issues.UnreadEntries, 0);
	}
	// Reading stops after MaxPlayers seats, and never goes past MaxRosterEntries.
	{
		FString Players;
		for (int32 i = 0; i < 10; ++i)
		{
			Players += FString::Printf(TEXT("%s{\"id\":\"p%d\",\"seat\":%d}"), i ? TEXT(",") : TEXT(""), i + 1, i % MaxPlayers + 1);
		}
		FMcRoomState Room;
		FRoomStateIssues Issues;
		TestTrue(TEXT("ten-entry roster parses"), ParseRoster(Players, Room, Issues));
		TestEqual(TEXT("four seats"), Room.Players.Num(), MaxPlayers);
		TestEqual(TEXT("the six after the fourth seat are not read"), Issues.UnreadEntries, 6);

		FString Junk;
		for (int32 i = 0; i < 100; ++i)
		{
			Junk += i ? TEXT(",null") : TEXT("null");
		}
		Junk += TEXT(",{\"id\":\"p1\",\"seat\":1}");
		FMcRoomState JunkRoom;
		FRoomStateIssues JunkIssues;
		TestTrue(TEXT("junk roster parses"), ParseRoster(Junk, JunkRoom, JunkIssues));
		TestEqual(TEXT("a seat past MaxRosterEntries is never reached"), JunkRoom.Players.Num(), 0);
		TestEqual(TEXT("MaxRosterEntries junk entries skipped"), JunkIssues.SkippedEntries, MaxRosterEntries);
		TestEqual(TEXT("the rest unread"), JunkIssues.UnreadEntries, 101 - MaxRosterEntries);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetHardeningNameTest, "MovecadeKart.Net.Hardening.PlayerNames", HardeningTestFlags)
bool FNetHardeningNameTest::RunTest(const FString&)
{
	// Expected values are movecade-v2 protocol/src/validate.ts sanitizePlayerName run under node on the same inputs.
	struct FCase
	{
		const TCHAR* In;
		const TCHAR* Out;
	};
	const FCase Cases[] = {
		{ TEXT("Jimmy"), TEXT("Jimmy") },
		{ TEXT("Player_1"), TEXT("Player_1") },
		{ TEXT("a-b c"), TEXT("a-b c") },
		{ TEXT("  Jo\tAnn  "), TEXT("Jo Ann") },
		{ TEXT("Zoë"), TEXT("Zo") },
		{ TEXT("a b"), TEXT("a b") },
		{ TEXT("a ! b"), TEXT("a b") },
		{ TEXT("a!b"), TEXT("ab") },
		{ TEXT("abcdefghijklmnopqrs tuv"), TEXT("abcdefghijklmnopqrs") },
		{ TEXT("abcdefghijklmnopqrstuvwxyz"), TEXT("abcdefghijklmnopqrst") },
		{ TEXT("‮GAME OVER\u0007"), TEXT("GAME OVER") },
		{ TEXT("\U0001F600Ace"), TEXT("Ace") },
		{ TEXT("a 　b"), TEXT("a b") },
		{ TEXT(""), TEXT("") },
		{ TEXT("   "), TEXT("") },
		{ TEXT("A​B"), TEXT("AB") },
		{ TEXT("line1\nline2\rline3"), TEXT("line1 line2 line3") },
		{ TEXT("a\u0085b"), TEXT("a b") },
	};
	for (const FCase& Case : Cases)
	{
		const FString Sanitized = SanitizePlayerName(Case.In);
		TestEqual(FString::Printf(TEXT("sanitizePlayerName parity for \"%s\""), Case.Out), Sanitized, FString(Case.Out));
		TestEqual(TEXT("a sanitised name is a fixed point"), SanitizePlayerName(Sanitized), Sanitized);
	}
	// A control character in the middle is a space, never a NUL that ends the string early.
	const FString WithNul = FString::ConstructFromPtrSize(TEXT("x\0y"), 3);
	TestEqual(TEXT("NUL becomes a space"), SanitizePlayerName(WithNul), FString(TEXT("x y")));

	// The other seat strings: bounded, dropped (not cut) when too long or carrying control characters.
	const FString Long33 = FString::ChrN(MaxSeatIdLength + 1, TEXT('k'));
	const TSharedPtr<FJsonObject> State = ParseJson(FString::Printf(TEXT("{\"roomId\":\"r-x\",\"stateVersion\":1,\"players\":["
		"{\"id\":\"p1\",\"seat\":1,\"name\":\"Ann\",\"emoji\":\"%s\",\"screen\":\"%s\",\"avatar\":{\"kartPaint\":\"cyan\",\"vehicle\":\"%s\",\"face\":\"mi\\u0007ra\"}},"
		"{\"id\":\"p2\",\"seat\":2,\"name\":\"Bo\",\"emoji\":\"\\ud83c\\udfce\\ufe0f\",\"screen\":\"ready\"}],"
		"\"game\":{\"status\":\"paused\",\"pauseReason\":\"%s\",\"pausedForPlayerId\":\"p2\"}}"),
		*FString::ChrN(MaxEmojiLength + 1, TEXT('e')), *Long33, *Long33, *Long33));
	FMcRoomState Room;
	FRoomStateIssues Issues;
	TestTrue(TEXT("state parses"), State.IsValid() && ParseRoomState(*State, Room, &Issues));
	const FMcSeat* Ann = Room.FindBySeat(1);
	const FMcSeat* Bo = Room.FindBySeat(2);
	if (TestTrue(TEXT("both seats"), Ann && Bo))
	{
		TestTrue(TEXT("over-long emoji, screen and vehicle dropped"), Ann->Emoji.IsEmpty() && Ann->Screen.IsEmpty() && Ann->Vehicle.IsEmpty());
		TestTrue(TEXT("a face id with a control character dropped"), Ann->Face.IsEmpty());
		TestEqual(TEXT("a clean paint id kept"), Ann->KartPaint, FString(TEXT("cyan")));
		TestEqual(TEXT("a real emoji kept as it is"), Bo->Emoji, FString(TEXT("\U0001F3CE️")));
		TestEqual(TEXT("a real screen kept"), Bo->Screen, FString(TEXT("ready")));
	}
	TestTrue(TEXT("an over-long pause reason dropped, the player kept"), Room.PauseReason.IsEmpty() && Room.PausedForPlayerId == TEXT("p2"));
	TestEqual(TEXT("five strings cleaned"), Issues.CleanedStrings, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetHardeningServerTextTest, "MovecadeKart.Net.Hardening.ServerText", HardeningTestFlags)
bool FNetHardeningServerTextTest::RunTest(const FString&)
{
	TestEqual(TEXT("plain text unchanged"), SanitizeServerText(TEXT("Room is full")), FString(TEXT("Room is full")));
	TestEqual(TEXT("control characters become spaces, bidi overrides go"), SanitizeServerText(TEXT("‮bad\nnews\u0007")), FString(TEXT("bad news")));
	TestEqual(TEXT("cut to MaxServerTextLength"), SanitizeServerText(FString::ChrN(100000, TEXT('Z'))).Len(), MaxServerTextLength);
	// A surrogate pair straddling the cut is dropped whole.
	const FString Straddle = FString::ChrN(MaxServerTextLength - 1, TEXT('a')) + TEXT("\U0001F600");
	TestEqual(TEXT("no half surrogate at the cut"), SanitizeServerText(Straddle), FString::ChrN(MaxServerTextLength - 1, TEXT('a')));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

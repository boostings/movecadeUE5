// The shared arcade helpers (Games/ArcadeGameModeBase.h, ArcadeHud.h, ArcadeTestHooks.h, ArcadeVenueKit.h) and the
// room host's pure rules (start checks, server message dispatch, the QR raster).

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/ArcadeHud.h"
#include "Games/ArcadeTestHooks.h"
#include "Games/ArcadeVenueKit.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Net/QrCode.h"
#include "Net/RoomProtocol.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ArcadeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A bare game world for component tests: no level, no BeginPlay. */
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false, TEXT("ArcadeFrameworkTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}

		~FScopedTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(/*bInformEngineOfWorld*/ false);
		}
	};

	FMcSeat Seat(int32 Number, bool bReady, bool bBot = false, bool bConnected = true)
	{
		FMcSeat S;
		S.PlayerId = FString::Printf(TEXT("p%d"), Number);
		S.Seat = Number;
		S.bReady = bReady;
		S.bBot = bBot;
		S.bConnected = bConnected;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcadeLaunchOptionsTest, "MovecadeKart.Arcade.LaunchOptions", ArcadeTestFlags)
bool FArcadeLaunchOptionsTest::RunTest(const FString&)
{
	const MovecadeKart::Arcade::FGameInfo* Bowling = MovecadeKart::Arcade::Find(TEXT("bowling"));
	const MovecadeKart::Arcade::FGameInfo* Stacker = MovecadeKart::Arcade::Find(TEXT("stacker"));
	if (!TestTrue(TEXT("catalog has bowling and stacker"), Bowling && Stacker))
	{
		return false;
	}
	FArcadeLaunchOptions L = FArcadeLaunchOptions::Parse(*Bowling, TEXT(""), TEXT(""), TEXT("Bowl"), 1, 1);
	TestEqual(TEXT("defaults: one local"), L.Locals, 1);
	TestEqual(TEXT("defaults: one bot"), L.Bots, 1);
	TestFalse(TEXT("defaults: not auto"), L.bAutoPlay);

	L = FArcadeLaunchOptions::Parse(*Bowling, TEXT("?Players=2?Bots=1"), TEXT(""), TEXT("Bowl"), 1, 1);
	TestEqual(TEXT("travel URL locals"), L.Locals, 2);
	TestEqual(TEXT("travel URL bots"), L.Bots, 1);

	L = FArcadeLaunchOptions::Parse(*Bowling, TEXT("?Players=2?Bots=1"), TEXT("-BowlPlayers=3 -BowlBots=0 -BowlAuto"), TEXT("Bowl"), 1, 1);
	TestEqual(TEXT("command line overrides locals"), L.Locals, 3);
	TestEqual(TEXT("command line overrides bots"), L.Bots, 0);
	TestTrue(TEXT("-BowlAuto"), L.bAutoPlay);

	L = FArcadeLaunchOptions::Parse(*Stacker, TEXT("?Players=3?Bots=4"), TEXT(""), TEXT("Stacker"), 1, 1);
	TestEqual(TEXT("a simultaneous game takes one local"), L.Locals, Stacker->MaxLocal);
	TestEqual(TEXT("seats never pass the catalog's max"), L.Locals + L.Bots, Stacker->MaxPlayers);

	L = FArcadeLaunchOptions::Parse(*Stacker, TEXT("?Players=0?Bots=0"), TEXT(""), TEXT("Stacker"), 1, 1);
	TestEqual(TEXT("an empty launch gets one house player"), L.Locals + L.Bots, 1);

	L = FArcadeLaunchOptions::Parse(*Stacker, TEXT(""), TEXT("-BowlPlayers=0"), TEXT("Stacker"), 1, 2);
	TestEqual(TEXT("another game's flags are ignored"), L.Locals, 1);
	TestEqual(TEXT("the game's own default bots"), L.Bots, 2);

	// A game's own minimum (Sumo: two wrestlers) raises the bots, and that is what the session records.
	L = FArcadeLaunchOptions::Parse(*Stacker, TEXT("?Players=1?Bots=0"), TEXT(""), TEXT("Sumo"), 1, 2);
	TestEqual(TEXT("solo launch: no bots asked"), L.Bots, 0);
	L.EnsureMinimumSeats(2);
	TestEqual(TEXT("a sparring partner added"), L.Bots, 1);
	TestEqual(TEXT("the local is kept"), L.Locals, 1);
	L.EnsureMinimumSeats(2);
	TestEqual(TEXT("idempotent"), L.Bots, 1);
	L.Bots = 3;
	L.EnsureMinimumSeats(2);
	TestEqual(TEXT("never lowers"), L.Bots, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcadeTestHooksTest, "MovecadeKart.Arcade.TestHooks", ArcadeTestFlags)
bool FArcadeTestHooksTest::RunTest(const FString&)
{
	const FArcadeTestHooks None = FArcadeTestHooks::Parse(TEXT("-game -KartRoom"));
	TestTrue(TEXT("no hooks"), None.IsEmpty());

	const FArcadeTestHooks Hooks = FArcadeTestHooks::Parse(TEXT("-game -KartShots=2,8.5,20 -KartQuitAfter=25"));
	if (TestEqual(TEXT("three shots"), Hooks.ShotTimes.Num(), 3))
	{
		TestEqual(TEXT("first shot"), Hooks.ShotTimes[0], 2.f);
		TestEqual(TEXT("second shot"), Hooks.ShotTimes[1], 8.5f);
		TestEqual(TEXT("third shot"), Hooks.ShotTimes[2], 20.f);
	}
	TestEqual(TEXT("quit after"), Hooks.QuitAfterSeconds, 25.f);
	TestFalse(TEXT("not empty"), Hooks.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcadeHudViewTest, "MovecadeKart.Arcade.HudView", ArcadeTestFlags)
bool FArcadeHudViewTest::RunTest(const FString&)
{
	// An identity view-projection: clip space is world space, so (0, 0, 0.5) is the middle of the view rect.
	const FIntRect Rect(FIntPoint(100, 0), FIntPoint(300, 100));
	const FArcadeHudView View = FArcadeHudView::FromMatrix(FMatrix::Identity, Rect, FVector2D(400.0, 100.0));
	TestTrue(TEXT("valid"), View.bValid);
	FVector2D Out;
	TestTrue(TEXT("projects the centre"), View.Project(FVector(0.0, 0.0, 0.5), Out));
	TestTrue(TEXT("viewport 0..1: the rect's centre is at x 0.5 of a 400 px viewport"), Out.Equals(FVector2D(0.5, 0.5), 1e-6));
	TestTrue(TEXT("rect 0..1"), View.ProjectToViewRect(FVector(0.0, 0.0, 0.5), Out) && Out.Equals(FVector2D(0.5, 0.5), 1e-6));
	TestTrue(TEXT("the right edge of the rect"), View.ProjectToViewRect(FVector(1.0, 0.0, 0.5), Out) && Out.Equals(FVector2D(1.0, 0.5), 1e-6));

	const FArcadeHudView Empty = FArcadeHudView::FromMatrix(FMatrix::Identity, FIntRect(), FVector2D::ZeroVector);
	TestFalse(TEXT("an empty view never projects"), Empty.Project(FVector::ZeroVector, Out));
	TestFalse(TEXT("no viewer, no view"), FArcadeHudView::Capture(nullptr).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcadeCachedTextTest, "MovecadeKart.Arcade.CachedText", ArcadeTestFlags)
bool FArcadeCachedTextTest::RunTest(const FString&)
{
	FCachedText Text;
	int32 Formats = 0;
	auto Format = [&Formats](int32 Value) { ++Formats; return FString::Printf(TEXT("ROW %d"), Value); };
	TestEqual(TEXT("first use formats"), Text.Get(0, Format), FString(TEXT("ROW 0")));
	TestEqual(TEXT("same value: cached"), Text.Get(0, Format), FString(TEXT("ROW 0")));
	TestEqual(TEXT("one format so far"), Formats, 1);
	TestEqual(TEXT("a change formats again"), Text.Get(7, Format), FString(TEXT("ROW 7")));
	TestEqual(TEXT("two formats"), Formats, 2);
	Text.Invalidate();
	Text.Get(7, Format);
	TestEqual(TEXT("invalidate forces a format"), Formats, 3);

	FCachedFloatText Speed;
	TestEqual(TEXT("float text"), Speed.Get(2.5f, [](float V) { return FString::Printf(TEXT("%.1f m/s"), V); }), FString(TEXT("2.5 m/s")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcadeStandingsOrderTest, "MovecadeKart.Arcade.StandingsOrder", ArcadeTestFlags)
bool FArcadeStandingsOrderTest::RunTest(const FString&)
{
	FArcadeStandingsOrder Ranks;
	TArray<int32> Scores = { 10, 30, 30, 5 };
	const TArray<int32>& Order = Ranks.Update(Scores.Num(), [&Scores](int32 i) { return Scores[i]; });
	TestTrue(TEXT("best first, ties in seat order"), Order == TArray<int32>({ 1, 2, 0, 3 }));
	Scores[3] = 50;
	TestTrue(TEXT("a changed score re-ranks"), Ranks.Update(Scores.Num(), [&Scores](int32 i) { return Scores[i]; }) == TArray<int32>({ 3, 1, 2, 0 }));
	Scores.Add(40);
	TestTrue(TEXT("a new seat ranks in"), Ranks.Update(Scores.Num(), [&Scores](int32 i) { return Scores[i]; }) == TArray<int32>({ 3, 4, 1, 2, 0 }));
	Ranks.Reset();
	TestEqual(TEXT("reset empties"), Ranks.Get().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcadeVenueKitTest, "MovecadeKart.Arcade.VenueKit", ArcadeTestFlags)
bool FArcadeVenueKitTest::RunTest(const FString&)
{
	UMaterialInterface* Wood = NewObject<UMaterialInstanceConstant>(GetTransientPackage());
	FScopedTestWorld TestWorld;
	AActor* Venue = TestWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("venue actor"), Venue))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Venue, TEXT("Root"));
	// Venues are static: a static group under a movable root would refuse to attach.
	Root->SetMobility(EComponentMobility::Static);
	Venue->SetRootComponent(Root);
	Root->RegisterComponent();

	FArcadeInstanceGroups Groups;
	Groups.Init(Venue, TEXT("TestGroup"), TEXT("TestVenue"));
	UStaticMesh* Cube = NewObject<UStaticMesh>(GetTransientPackage());
	UInstancedStaticMeshComponent* A = Groups.Find(Cube, Wood, true);
	UInstancedStaticMeshComponent* B = Groups.Find(Cube, Wood, true);
	UInstancedStaticMeshComponent* C = Groups.Find(Cube, Wood, false);
	TestNotNull(TEXT("a group"), A);
	TestTrue(TEXT("same look, same group"), A == B);
	TestTrue(TEXT("shadow casting splits groups"), A != C);
	TestEqual(TEXT("two groups"), Groups.Num(), 2);
	if (A && C)
	{
		TestTrue(TEXT("tagged"), A->ComponentHasTag(TEXT("TestVenue")));
		TestTrue(TEXT("registered"), A->IsRegistered());
		TestFalse(TEXT("no shadow on the second"), C->CastShadow);
		TestTrue(TEXT("attached to the root"), A->GetAttachParent() == Root);
		TestTrue(TEXT("no collision"), A->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomStartRulesTest, "MovecadeKart.Net.StartRules", ArcadeTestFlags)
bool FRoomStartRulesTest::RunTest(const FString&)
{
	using namespace MovecadeKart::Net;
	FMcRoomState Room;
	Room.Status = EMcRoomStatus::Lobby;

	auto Block = [&](bool bOpen, bool bHost, bool bPrepared) { return EvaluateStart(Room, bOpen, bHost, bPrepared).Block; };
	TestTrue(TEXT("not connected"), Block(false, true, true) == EStartBlock::NotConnected);
	TestEqual(TEXT("connecting line"), DescribeStartBlock(EvaluateStart(Room, false, true, true), FString()), FString(TEXT("Connecting to the room server...")));
	TestEqual(TEXT("the last error wins while not connected"), DescribeStartBlock(EvaluateStart(Room, false, true, true), TEXT("boom")), FString(TEXT("boom")));
	TestTrue(TEXT("standby"), Block(true, false, true) == EStartBlock::NotHost);
	TestTrue(TEXT("nobody"), Block(true, true, true) == EStartBlock::NobodySeated);
	TestEqual(TEXT("nobody line"), DescribeStartBlock(EvaluateStart(Room, true, true, true), FString()), FString(TEXT("Scan the code with a phone to join.")));

	Room.Players = { Seat(1, true, /*bBot*/ true) };
	TestTrue(TEXT("bots only"), Block(true, true, true) == EStartBlock::OnlyBots);

	Room.Players = { Seat(1, true), Seat(2, false), Seat(3, true, false, /*bConnected*/ false) };
	TestTrue(TEXT("preparing"), Block(true, true, false) == EStartBlock::Preparing);
	const FStartCheck Waiting = EvaluateStart(Room, true, true, true);
	TestEqual(TEXT("two not ready"), Waiting.NotReady, 2);
	TestEqual(TEXT("waiting line"), DescribeStartBlock(Waiting, FString()), FString(TEXT("Waiting for 2 players to get ready.")));

	Room.Players = { Seat(1, true), Seat(2, true) };
	TestTrue(TEXT("everyone ready"), EvaluateStart(Room, true, true, true).CanStart());
	TestEqual(TEXT("no line when it can start"), DescribeStartBlock(EvaluateStart(Room, true, true, true), FString()), FString());
	Room.Status = EMcRoomStatus::InProgress;
	TestTrue(TEXT("a match runs"), Block(true, true, true) == EStartBlock::MatchRunning);

	TestTrue(TEXT("ping"), ParseServerMessageType(TEXT("ping")) == EServerMessage::Ping);
	TestTrue(TEXT("signed scores"), ParseServerMessageType(TEXT("signed-scores")) == EServerMessage::SignedScores);
	TestTrue(TEXT("the wire is case-sensitive"), ParseServerMessageType(TEXT("Welcome")) == EServerMessage::Unknown);
	TestTrue(TEXT("unknown"), ParseServerMessageType(TEXT("lobby-visible")) == EServerMessage::Unknown);
	TestTrue(TEXT("a refused start"), IsStartRejectedError(TEXT("PLAYERS_NOT_READY")));
	TestFalse(TEXT("other errors"), IsStartRejectedError(TEXT("UNAUTHORIZED_ORIGIN")));
	TestTrue(TEXT("status is case-sensitive"), ParseStatus(TEXT("Lobby")) == EMcRoomStatus::Unknown);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcadeLabelsTest, "MovecadeKart.Arcade.LabelsAndHexColor", ArcadeTestFlags)
bool FArcadeLabelsTest::RunTest(const FString&)
{
	auto Label = []() -> const FText& { return ARCADE_LABEL("ArcadeTests", "Wind", "WIND"); };
	TestEqual(TEXT("label text"), Label().ToString(), FString(TEXT("WIND")));
	TestTrue(TEXT("built once: the same FText every call"), &Label() == &Label());

	using MovecadeKart::Net::HexColor;
	TestEqual(TEXT("white"), HexColor(FLinearColor::White), FString(TEXT("#ffffff")));
	TestEqual(TEXT("black"), HexColor(FLinearColor::Black), FString(TEXT("#000000")));
	TestEqual(TEXT("round trip through sRGB, lower case"), HexColor(FLinearColor::FromSRGBColor(FColor(0x2e, 0xc4, 0xb6))), FString(TEXT("#2ec4b6")));
	TestTrue(TEXT("a valid pad accent"), MovecadeKart::Net::PadPromptData(TEXT("p1"), TEXT("t"), TEXT("s"), TEXT("A"), TEXT(""),
		HexColor(FLinearColor::Red), TEXT("tilt")).Contains(TEXT("\"accent\":\"#ff0000\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomQrRasterTest, "MovecadeKart.Net.QrRaster", ArcadeTestFlags)
bool FRoomQrRasterTest::RunTest(const FString&)
{
	const MovecadeKart::Qr::FQrMatrix Matrix = MovecadeKart::Qr::EncodeText(TEXT("http://localhost:5173/join/ABCDEF#v=2"));
	if (!TestTrue(TEXT("encodes"), Matrix.IsValid()))
	{
		return false;
	}
	TArray64<uint8> Pixels;
	int32 Size = 0;
	MovecadeKart::Qr::RasterizeBgra(Matrix, 4, 8, Pixels, Size);
	TestEqual(TEXT("side in pixels"), Size, (Matrix.Size + 8) * 8);
	TestEqual(TEXT("BGRA bytes"), Pixels.Num(), int64(Size) * Size * 4);
	auto At = [&](int32 X, int32 Y) { return Pixels[(int64(Y) * Size + X) * 4]; };
	TestTrue(TEXT("the quiet zone is white"), At(0, 0) == uint8(255));
	// The top-left finder pattern's corner module is dark: the first pixel after the quiet zone.
	TestTrue(TEXT("the finder's corner is black"), At(4 * 8, 4 * 8) == uint8(0));
	TestTrue(TEXT("opaque"), Pixels[3] == uint8(255));
	return true;
}

#endif

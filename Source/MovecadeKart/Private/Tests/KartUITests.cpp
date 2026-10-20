#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Race/SKartRaceOverlay.h"
#include "UI/KartMenuInput.h"
#include "UI/KartRoster.h"
#include "UI/KartScreen.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags UITestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartTrackCatalogTest, "MovecadeKart.UI.TrackCatalog", UITestFlags)

bool FKartTrackCatalogTest::RunTest(const FString& Parameters)
{
	const TArray<FKartTrackInfo>& Tracks = FKartTrackCatalog::All();
	TestTrue(TEXT("at least the two shipped tracks"), Tracks.Num() >= 2);
	TSet<FString> Ids;
	for (const FKartTrackInfo& Track : Tracks)
	{
		TestFalse(TEXT("ids are unique"), Ids.Contains(Track.Id));
		Ids.Add(Track.Id);
		TestFalse(*FString::Printf(TEXT("%s has a name"), *Track.Id), Track.Name.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s has an opaque accent"), *Track.Id), Track.Accent.A > 0.99f);
		TestTrue(*FString::Printf(TEXT("%s level %s exists"), *Track.Id, *Track.Level), FPackageName::DoesPackageExist(Track.Level));
		TestTrue(TEXT("FindByLevel finds it by short name"), FKartTrackCatalog::FindByLevel(FPackageName::GetShortName(Track.Level)) == &Track);
	}
	TestEqual(TEXT("sunset-harbor is first"), FKartTrackCatalog::IndexOf(TEXT("sunset-harbor")), 0);
	TestEqual(TEXT("violet loop level"), FKartTrackCatalog::LevelFor(TEXT("violet-loop")), FString(TEXT("/Game/Maps/L_VioletLoop")));
	TestNull(TEXT("unknown id"), FKartTrackCatalog::Find(TEXT("rainbow-road")));
	TestEqual(TEXT("unknown id index"), FKartTrackCatalog::IndexOf(TEXT("rainbow-road")), INDEX_NONE);
	TestTrue(TEXT("unknown id has no level"), FKartTrackCatalog::LevelFor(TEXT("nope")).IsEmpty());

	// A 100 x 50 m rectangle: normalised on its longer side, lap length kept in metres.
	const TArray<FVector2D> Box = { { 0, 0 }, { 100, 0 }, { 100, 50 }, { 0, 50 } };
	const FKartTrackPreview Preview = FKartTrackCatalog::MakePreview(Box, 220);
	TestEqual(TEXT("all points kept"), Preview.Outline.Num(), 4);
	TestEqual(TEXT("lap length"), Preview.LapMetres, 300.0);
	TestEqual(TEXT("aspect"), Preview.Aspect, 2.0);
	TestTrue(TEXT("normalised to the longer side"), Preview.Outline[2].Equals(FVector2D(1.0, 0.5)));
	TArray<FVector2D> Dense;
	for (int32 i = 0; i < 1000; ++i)
	{
		Dense.Add(FVector2D(FMath::Cos(i * 0.00628), FMath::Sin(i * 0.00628)));
	}
	TestTrue(TEXT("dense outlines are simplified"), FKartTrackCatalog::MakePreview(Dense, 200).Outline.Num() <= 200);
	TestEqual(TEXT("degenerate outlines are empty"), FKartTrackCatalog::MakePreview({ FVector2D::ZeroVector }).Outline.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartFrontEndFlagsTest, "MovecadeKart.UI.FrontEndFlags", UITestFlags)

bool FKartFrontEndFlagsTest::RunTest(const FString& Parameters)
{
	using namespace KartFrontEndFlags;
	// Every existing test and race flag skips the menus, so automated runs never wait on a title screen.
	const TCHAR* Skips[] = {
		TEXT("-KartAutoDrive"), TEXT("-KartSkipIntro"), TEXT("-KartShots=4,10"), TEXT("-KartQuitAfter=20"), TEXT("-KartRoom"),
		TEXT("-KartBots=2"), TEXT("-KartLocalPlayers=2"), TEXT("-KartSoloView"), TEXT("-KartLaps=1"), TEXT("-KartNoFrontEnd"),
		TEXT("-KartLobby"), TEXT("-KartBotSkill=hard"), TEXT("-KartLegacyWalls"),
	};
	for (const TCHAR* Flag : Skips)
	{
		const FString Cmd = FString::Printf(TEXT("MyProject.uproject /Game/Maps/L_SunsetHarbor -game -windowed %s"), Flag);
		TestTrue(*FString::Printf(TEXT("%s skips the front end"), Flag), ShouldSkipFrontEnd(*Cmd));
		TestTrue(*FString::Printf(TEXT("%s boots past the menus"), Flag), DecideBoot(EKartSessionMode::None, true, *Cmd) != EKartBoot::FrontEnd);
	}
	TestFalse(TEXT("a plain launch shows the menus"), ShouldSkipFrontEnd(TEXT("MyProject.uproject -game -windowed -ResX=1920 -ResY=1080")));
	TestTrue(TEXT("plain launch"), DecideBoot(EKartSessionMode::None, true, TEXT("-game")) == EKartBoot::FrontEnd);
	TestTrue(TEXT("-KartRoom opens the lobby"), DecideBoot(EKartSessionMode::None, true, TEXT("-game -KartRoom -KartBots=1")) == EKartBoot::Room);
	TestTrue(TEXT("race flags race"), DecideBoot(EKartSessionMode::None, true, TEXT("-KartAutoDrive -KartQuitAfter=30")) == EKartBoot::LocalRace);
	// The screenshot tour drives the menus itself; its race flags still shape the race it starts.
	TestTrue(TEXT("-KartUiShots shows the menus"), DecideBoot(EKartSessionMode::None, true, TEXT("-KartUiShots=title,menu -KartAutoDrive -KartLaps=1")) == EKartBoot::FrontEnd);
	// After the first level, the session decides, not the command line.
	TestTrue(TEXT("back to the menu despite flags"), DecideBoot(EKartSessionMode::None, false, TEXT("-KartAutoDrive")) == EKartBoot::FrontEnd);
	TestTrue(TEXT("menu race"), DecideBoot(EKartSessionMode::LocalRace, false, TEXT("-game")) == EKartBoot::LocalRace);
	TestTrue(TEXT("party mode"), DecideBoot(EKartSessionMode::Party, false, TEXT("-game")) == EKartBoot::Room);
	TestTrue(TEXT("party mode wins over race flags"), DecideBoot(EKartSessionMode::Party, true, TEXT("-KartAutoDrive")) == EKartBoot::Room);
	TestFalse(TEXT("no partial matches"), ShouldSkipFrontEnd(TEXT("-KartRoomy -NotAFlag")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartRacePlanTest, "MovecadeKart.UI.RacePlan", UITestFlags)

bool FKartRacePlanTest::RunTest(const FString& Parameters)
{
	// No menu setup: exactly the flag-driven race (3 bots by default, seat paints, varied looks).
	{
		const FKartRacePlan Plan = FKartRacePlan::Resolve(nullptr, TEXT("-game"), 8, 3);
		TestEqual(TEXT("1 + 3 seats"), Plan.Seats.Num(), 4);
		TestEqual(TEXT("one local player"), Plan.LocalPlayers, 1);
		TestEqual(TEXT("track laps"), Plan.Laps, 0);
		TestFalse(TEXT("a view per kart"), Plan.bSoloView);
		TestEqual(TEXT("seat 0 name"), Plan.Seats[0].Name, FString(TEXT("Player 1")));
		TestEqual(TEXT("first bot is Dante"), Plan.Seats[1].Name, FString(TEXT("Dante")));
		TestTrue(TEXT("seat 0 is local"), Plan.Seats[0].Kind == EKartSeatKind::Local);
		TestTrue(TEXT("seat 1 is a bot"), Plan.Seats[1].Kind == EKartSeatKind::Bot);
		TestFalse(TEXT("varied looks"), Plan.Seats[0].bHasLook);
		TestTrue(TEXT("v2 pink first"), Plan.Seats[0].Paint.Equals(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("f0508e")))));
		TestTrue(TEXT("normal bots"), Plan.Seats[1].BotSkill == EKartBotSkill::Normal);
	}
	// Flags.
	{
		const FKartRacePlan Plan = FKartRacePlan::Resolve(nullptr, TEXT("-KartBots=1 -KartLocalPlayers=2 -KartLaps=2 -KartSoloView -KartAutoDrive -KartSkipIntro -KartBotSkill=Hard"), 8, 3);
		TestEqual(TEXT("2 + 1 seats"), Plan.Seats.Num(), 3);
		TestEqual(TEXT("two players"), Plan.LocalPlayers, 2);
		TestEqual(TEXT("laps"), Plan.Laps, 2);
		TestTrue(TEXT("solo view"), Plan.bSoloView);
		TestTrue(TEXT("autodrive"), Plan.bAutoDrive);
		TestTrue(TEXT("skip intro"), Plan.bSkipIntro);
		TestTrue(TEXT("hard bots (case-insensitive)"), Plan.Seats[2].BotSkill == EKartBotSkill::Hard);
	}
	// Clamped to the grid.
	{
		const FKartRacePlan Plan = FKartRacePlan::Resolve(nullptr, TEXT("-KartBots=20 -KartLocalPlayers=9"), 4, 3);
		TestEqual(TEXT("grid-sized"), Plan.Seats.Num(), 4);
		TestEqual(TEXT("players fill the grid"), Plan.LocalPlayers, 4);
	}
	// Menu setup, then flags on top.
	FKartRaceSetup Setup;
	Setup.LocalPlayers = 2;
	Setup.Bots = 3;
	Setup.Laps = 5;
	Setup.BotSkill = 0;
	Setup.bViewEveryKart = false;
	Setup.Sanitize(8);
	Setup.Racers[0].Paint = 4; // lime
	Setup.Racers[0].Vehicle = EKartVehicle::Moto;
	Setup.Racers[1].Paint = 0;
	{
		const FKartRacePlan Plan = FKartRacePlan::Resolve(&Setup, TEXT("-game"), 8, 3);
		TestEqual(TEXT("2 + 3 seats"), Plan.Seats.Num(), 5);
		TestEqual(TEXT("menu laps"), Plan.Laps, 5);
		TestTrue(TEXT("one view per player"), Plan.bSoloView);
		TestTrue(TEXT("players wear their picks"), Plan.Seats[0].bHasLook && Plan.Seats[0].Look.Vehicle == EKartVehicle::Moto);
		TestTrue(TEXT("player paint"), Plan.Seats[0].Paint.Equals(FKartRoster::PaintColor(4)));
		TestTrue(TEXT("easy bots"), Plan.Seats[2].BotSkill == EKartBotSkill::Easy);
		TSet<FString> Names;
		for (int32 i = 2; i < Plan.Seats.Num(); ++i)
		{
			const FKartSeatSpec& Bot = Plan.Seats[i];
			TestTrue(TEXT("bots have looks"), Bot.bHasLook);
			TestFalse(TEXT("bots never take a player's paint"), Bot.Paint.Equals(FKartRoster::PaintColor(4)) || Bot.Paint.Equals(FKartRoster::PaintColor(0)));
			TestFalse(TEXT("bot names are unique"), Names.Contains(Bot.Name));
			Names.Add(Bot.Name);
		}
	}
	{
		const FKartRacePlan Plan = FKartRacePlan::Resolve(&Setup, TEXT("-KartLaps=1 -KartBots=0 -KartBotSkill=normal"), 8, 3);
		TestEqual(TEXT("-KartLaps overrides the menu"), Plan.Laps, 1);
		TestEqual(TEXT("-KartBots overrides the menu"), Plan.Seats.Num(), 2);
		TestTrue(TEXT("-KartBotSkill overrides the menu"), Plan.BotSkill == EKartBotSkill::Normal);
	}
	Setup.bViewEveryKart = true;
	TestFalse(TEXT("TV mode: a view per kart"), FKartRacePlan::Resolve(&Setup, TEXT(""), 8, 3).bSoloView);
	TestTrue(TEXT("-KartSoloView beats TV mode"), FKartRacePlan::Resolve(&Setup, TEXT("-KartSoloView"), 8, 3).bSoloView);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartRaceSetupTest, "MovecadeKart.UI.RaceSetup", UITestFlags)

bool FKartRaceSetupTest::RunTest(const FString& Parameters)
{
	FKartRaceSetup Setup;
	Setup.TrackId = TEXT("no-such-track");
	Setup.Laps = 40;
	Setup.Bots = 9;
	Setup.LocalPlayers = 0;
	Setup.BotSkill = 7;
	Setup.Racers.Reset();
	Setup.Sanitize(4);
	TestEqual(TEXT("unknown track falls back"), Setup.TrackId, FKartTrackCatalog::All()[0].Id);
	TestEqual(TEXT("laps clamp"), Setup.Laps, FKartRaceSetup::MaxLaps);
	TestEqual(TEXT("at least one player"), Setup.LocalPlayers, 1);
	TestEqual(TEXT("bots fit the grid"), Setup.Bots, 3);
	TestEqual(TEXT("skill clamps"), Setup.BotSkill, 2);
	TestEqual(TEXT("four racer slots"), Setup.Racers.Num(), FKartRaceSetup::MaxLocalPlayers);

	// Defaults give each player a different driver and their seat's v2 paint.
	TSet<int32> Characters;
	for (int32 i = 0; i < 4; ++i)
	{
		const FKartRacerChoice Choice = FKartRoster::DefaultChoice(i);
		Characters.Add(Choice.Character);
		TestEqual(TEXT("seat paint"), Choice.Paint, i);
	}
	TestEqual(TEXT("four different drivers"), Characters.Num(), 4);
	FKartRacerChoice Wild;
	Wild.Character = 99;
	Wild.Paint = -3;
	const FKartRacerChoice Clean = FKartRoster::Sanitize(Wild);
	TestTrue(TEXT("sanitised character"), FKartRoster::Characters().IsValidIndex(Clean.Character));
	TestTrue(TEXT("sanitised paint"), FKartRoster::Paints().IsValidIndex(Clean.Paint));
	const FKartLook Look = FKartRoster::MakeLook(FKartRoster::DefaultChoice(1), TEXT("P2"));
	TestEqual(TEXT("look name"), Look.Name, FString(TEXT("P2")));
	TestEqual(TEXT("character name from the look"), FKartRoster::CharacterNameOf(Look).ToString(), FKartRoster::Characters()[1].Name.ToString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartMenuLogicTest, "MovecadeKart.UI.MenuLogic", UITestFlags)

bool FKartMenuLogicTest::RunTest(const FString& Parameters)
{
	FKartMenuCursor Cursor;
	Cursor.Count = 3;
	TestTrue(TEXT("moves"), Cursor.Move(1));
	TestEqual(TEXT("index 1"), Cursor.Index, 1);
	Cursor.Move(2);
	TestEqual(TEXT("wraps forward"), Cursor.Index, 0);
	Cursor.Move(-1);
	TestEqual(TEXT("wraps back"), Cursor.Index, 2);
	Cursor.bWrap = false;
	TestFalse(TEXT("clamped at the end"), Cursor.Move(1));
	TestTrue(TEXT("set"), Cursor.Set(0));
	TestFalse(TEXT("set to the same"), Cursor.Set(0));
	TestEqual(TEXT("cycle negative"), FKartMenuCursor::Cycle(0, -1, 4), 3);
	TestEqual(TEXT("cycle empty"), FKartMenuCursor::Cycle(5, 1, 0), 0);

	TestEqual(TEXT("1st"), SKartRaceOverlay::Ordinal(1), FString(TEXT("1st")));
	TestEqual(TEXT("2nd"), SKartRaceOverlay::Ordinal(2), FString(TEXT("2nd")));
	TestEqual(TEXT("3rd"), SKartRaceOverlay::Ordinal(3), FString(TEXT("3rd")));
	TestEqual(TEXT("4th"), SKartRaceOverlay::Ordinal(4), FString(TEXT("4th")));
	TestEqual(TEXT("11th"), SKartRaceOverlay::Ordinal(11), FString(TEXT("11th")));
	TestEqual(TEXT("12th"), SKartRaceOverlay::Ordinal(12), FString(TEXT("12th")));
	TestEqual(TEXT("13th"), SKartRaceOverlay::Ordinal(13), FString(TEXT("13th")));
	TestEqual(TEXT("21st"), SKartRaceOverlay::Ordinal(21), FString(TEXT("21st")));
	TestEqual(TEXT("112th"), SKartRaceOverlay::Ordinal(112), FString(TEXT("112th")));
	TestEqual(TEXT("race time"), SKartRaceOverlay::FormatRaceTime(83250.0), FString(TEXT("1:23.250")));
	TestEqual(TEXT("race clock"), SKartRaceOverlay::FormatRaceTime(83250.0, false), FString(TEXT("1:23")));
	TestEqual(TEXT("negative clamps"), SKartRaceOverlay::FormatRaceTime(-5.0), FString(TEXT("0:00.000")));

	// The same action from the keyboard and any pad; pause keys don't include a pad's B (that's brake).
	TestTrue(TEXT("Enter accepts"), FKartMenuInputProcessor::MapKey(EKeys::Enter) == EKartMenuAction::Accept);
	TestTrue(TEXT("A accepts"), FKartMenuInputProcessor::MapKey(EKeys::Gamepad_FaceButton_Bottom) == EKartMenuAction::Accept);
	TestTrue(TEXT("Esc backs"), FKartMenuInputProcessor::MapKey(EKeys::Escape) == EKartMenuAction::Back);
	TestTrue(TEXT("B backs"), FKartMenuInputProcessor::MapKey(EKeys::Gamepad_FaceButton_Right) == EKartMenuAction::Back);
	TestTrue(TEXT("stick navigates"), FKartMenuInputProcessor::MapKey(EKeys::Gamepad_LeftStick_Down) == EKartMenuAction::Down);
	TestTrue(TEXT("Start"), FKartMenuInputProcessor::MapKey(EKeys::Gamepad_Special_Right) == EKartMenuAction::Start);
	TestTrue(TEXT("shoulders switch tabs"), FKartMenuInputProcessor::MapKey(EKeys::Gamepad_RightShoulder) == EKartMenuAction::NextTab);
	TestTrue(TEXT("unmapped"), FKartMenuInputProcessor::MapKey(EKeys::F7) == EKartMenuAction::None);
	TestTrue(TEXT("Esc pauses"), FKartMenuInputProcessor::IsPauseKey(EKeys::Escape));
	TestTrue(TEXT("Start pauses"), FKartMenuInputProcessor::IsPauseKey(EKeys::Gamepad_Special_Right));
	TestFalse(TEXT("B never pauses"), FKartMenuInputProcessor::IsPauseKey(EKeys::Gamepad_FaceButton_Right));
	return true;
}

#endif

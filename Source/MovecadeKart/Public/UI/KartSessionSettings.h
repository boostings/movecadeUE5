#pragma once

#include "CoreMinimal.h"
#include "Race/KartRaceManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/KartRoster.h"
#include "KartSessionSettings.generated.h"

/** Everything the race setup screens choose for a local race. Persisted in UKartPlayerPrefs. */
USTRUCT(BlueprintType)
struct MOVECADEKART_API FKartRaceSetup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	FString TrackId = TEXT("sunset-harbor");

	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	int32 Laps = 3;

	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	int32 Bots = 3;

	/** 0 Easy, 1 Normal, 2 Hard (EKartBotSkill order). */
	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	int32 BotSkill = 1;

	/** 1..4; players 2..4 use gamepads 2..4. */
	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	int32 LocalPlayers = 1;

	/** "TV mode": a view for every kart, bots included (the web host's layout); otherwise one per player. */
	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	bool bViewEveryKart = false;

	/** One per local player slot (always four). */
	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	TArray<FKartRacerChoice> Racers;

	static constexpr int32 MaxLocalPlayers = 4;
	static constexpr int32 MaxLaps = 9;

	/** Fills the racer slots and clamps every field (GridSlots: the track's grid size). */
	void Sanitize(int32 GridSlots = 8);
};

/** What a level does when it starts. */
UENUM()
enum class EKartBoot : uint8
{
	FrontEnd,
	LocalRace,
	/** The phone room lobby (-KartRoom, or Party Mode from the menu). */
	Room,
};

UENUM()
enum class EKartSessionMode : uint8
{
	/** In the menus (or not started yet). */
	None,
	LocalRace,
	Party,
};

/** Which front-end screen opens when the menus come back. */
UENUM()
enum class EKartMenuEntry : uint8
{
	Title,
	MainMenu,
	TrackSelect,
};

/** The race AKartGameMode builds: the seats, views and switches, from the menu setup and the command line. */
struct MOVECADEKART_API FKartRacePlan
{
	TArray<FKartSeatSpec> Seats;
	int32 LocalPlayers = 1;
	/** 0 keeps the track's own lap count. */
	int32 Laps = 0;
	/** Every bot seat's skill (-KartBotSkill=easy|normal|hard overrides the menu). Already applied to Seats. */
	EKartBotSkill BotSkill = EKartBotSkill::Normal;
	bool bSoloView = false;
	bool bAutoDrive = false;
	bool bSkipIntro = false;
	bool bLegacyWalls = false;

	/**
	 * MenuSetup null: exactly the flag-driven race (DefaultBots bots, -KartBots, -KartLocalPlayers, -KartLaps,
	 * -KartSoloView, varied looks). With a menu setup, its choices apply and any of those flags still override.
	 */
	static FKartRacePlan Resolve(const FKartRaceSetup* MenuSetup, const TCHAR* CommandLine, int32 GridSlots, int32 DefaultBots);
};

namespace KartFrontEndFlags
{
	/** Any race or test flag (-KartAutoDrive, -KartShots, -KartBots, ...) or -KartNoFrontEnd: no menus. */
	MOVECADEKART_API bool ShouldSkipFrontEnd(const TCHAR* CommandLine);
	/** -KartRoom / -KartLobby. */
	MOVECADEKART_API bool WantsRoom(const TCHAR* CommandLine);
	/** The pure boot decision: the session mode wins; the command line only decides the first level. */
	MOVECADEKART_API EKartBoot DecideBoot(EKartSessionMode Mode, bool bFirstBoot, const TCHAR* CommandLine);
}

/**
 * The game's flow across level loads: front end, local race or party (phone room). AKartGameMode asks
 * DecideBoot at StartPlay; the menus fill Setup and call StartLocalRace / StartParty / ReturnToMenu before
 * they travel. Command-line test flags skip the menus on the first level, as before the front end existed.
 */
UCLASS()
class MOVECADEKART_API UKartSessionSettings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UKartSessionSettings* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	EKartBoot DecideBoot(const TCHAR* CommandLine);

	/** The menu's race setup, or null when the race came from the command line alone. */
	const FKartRaceSetup* GetMenuSetup() const { return bRaceFromMenu ? &Setup : nullptr; }
	const FKartRaceSetup& GetSetup() const { return Setup; }
	FKartRaceSetup& MutableSetup() { return Setup; }
	/** Writes the setup to the player prefs (GameUserSettings.ini). */
	void SaveSetup();

	EKartSessionMode GetMode() const { return Mode; }
	void StartLocalRace();
	void StartParty();
	void ReturnToMenu(EKartMenuEntry Entry);
	/** The screen the front end should open, once (then Title again). */
	EKartMenuEntry ConsumeMenuEntry();

private:
	UPROPERTY()
	FKartRaceSetup Setup;

	EKartSessionMode Mode = EKartSessionMode::None;
	EKartMenuEntry Entry = EKartMenuEntry::Title;
	bool bBooted = false;
	bool bRaceFromMenu = false;
};

#include "Games/ArcadeCatalog.h"

#include "UI/KartUIStyle.h"

#define LOCTEXT_NAMESPACE "ArcadeCatalog"

namespace MovecadeKart::Arcade
{
	const TArray<FGameInfo>& Games()
	{
		namespace KC = KartUI::Color;
		static const TArray<FGameInfo> List = {
			{ TEXT("bowling"), LOCTEXT("Bowling", "Bowling"), LOCTEXT("BowlingTag", "Ten frames down a neon lane. Swing, hook and chase the turkey."),
				LOCTEXT("BowlingHow", "A/D set your line, Q/E your hook. Hold Space, RT or the mouse to grip, swing and let go. Strikes and spares carry bonus pins."), TEXT("L_Bowling"), KC::Sun, 4, 4, 2.0,
				LOCTEXT("BowlingPhone", "Phones are the ball: scan the code, swing and let go to bowl."), EArcadeOrientation::Portrait },
			{ TEXT("darts"), LOCTEXT("Darts", "Bullseye Bash"), LOCTEXT("DartsTag", "Pub darts on one board. Three darts a visit; hit the called sector for double."),
				LOCTEXT("DartsHow", "WASD, stick or mouse aim the dart; hold Space, RT or click, then let go to throw. Three darts a visit; the called sector scores double."), TEXT("L_Darts"), KC::Hot, 4, 4, 2.0,
				LOCTEXT("DartsPhone", "Phones are the darts: scan the code, aim, pinch and flick."), EArcadeOrientation::Portrait },
			{ TEXT("samurai"), LOCTEXT("Samurai", "Samurai Slice"), LOCTEXT("SamuraiTag", "Swing the katana through the fruit, never the bombs. Three precise cuts: Focus."),
				LOCTEXT("SamuraiHow", "Mouse or stick: sweep the katana through the fruit at the top of its arc. Never cut a bomb. Three precise cuts earn Focus: double points."), TEXT("L_Samurai"), KC::Mint, 4, 1, 2.0,
				LOCTEXT("SamuraiPhone", "Phones are katanas: scan the code, then swing through the fruit."), EArcadeOrientation::Portrait },
			{ TEXT("stacker"), LOCTEXT("Stacker", "Sky Stacker"), LOCTEXT("StackerTag", "Drop each block on the last, trim the overhang, lean the tower against the wind."),
				LOCTEXT("StackerHow", "A, Space or click drops the sweeping block; the overhang is cut away. Stick, A/D or mouse lean the tower against the wind."), TEXT("L_Stacker"), KC::Sky, 4, 1, 3.0,
				LOCTEXT("StackerPhone", "Phones are your palm: scan the code, keep it level and tap DROP."), EArcadeOrientation::Landscape },
			{ TEXT("sumo"), LOCTEXT("Sumo", "Sumo Bumpers"), LOCTEXT("SumoTag", "Bumper-car sumo on a ring over the night sea. Tilt to roll, dash to shove, last one standing."),
				LOCTEXT("SumoHow", "Stick or WASD roll your puck; A, Space or click dashes into whoever is in front. Knock them off the edge; the ring shrinks. Two rounds win."), TEXT("L_Sumo"), KC::Grape, 4, 1, 2.5,
				LOCTEXT("SumoPhone", "Phones are your bumper car: scan the code, tilt to roll, tap A to dash."), EArcadeOrientation::Landscape },
			{ TEXT("derby"), LOCTEXT("Derby", "Home Run Derby"), LOCTEXT("DerbyTag", "Night lights, ten pitches, one bat. Time the swing and send it into the seats."),
				LOCTEXT("DerbyHow", "Hold Space, RT or click to load, let go to swing as the ball arrives. Mouse, A/D or stick aim pull or opposite field. Phones: swing it like a bat."), TEXT("L_Derby"), KC::Hot, 4, 4, 3.0,
				LOCTEXT("DerbyPhone", "Phones are the bat: scan the code, then swing it as the pitch arrives."), EArcadeOrientation::Landscape },
			{ TEXT("fishing"), LOCTEXT("Fishing", "Fishing Frenzy"), LOCTEXT("FishingTag", "Cast, hook and reel off a golden-hour dock. The far water hides the giants."),
				LOCTEXT("FishingHow", "Hold Space, A or click and let go to cast: power is distance. Press again when the bobber dips. Reel by spinning the right stick, alternating A/D or scrolling; keep the tension in the green."),
				TEXT("L_Fishing"), FLinearColor::FromSRGBColor(FColor(0x2e, 0xc4, 0xb6)), 4, 1, 3.0,
				LOCTEXT("FishingPhone", "Phones are your rod: flick forward to cast, jerk up to hook, roll your wrist to reel."), EArcadeOrientation::Landscape },
		};
		return List;
	}

	const FGameInfo* Find(const FString& Id)
	{
		return Games().FindByPredicate([&Id](const FGameInfo& G) { return G.Id == Id; });
	}

	const FGameInfo* FindByMap(const FString& Level)
	{
		FString Name = Level;
		Name.Split(TEXT("?"), &Name, nullptr);
		int32 Slash = INDEX_NONE;
		if (Name.FindLastChar(TEXT('/'), Slash))
		{
			Name.RightChopInline(Slash + 1);
		}
		Name.Split(TEXT("."), &Name, nullptr);
		return Games().FindByPredicate([&Name](const FGameInfo& G) { return G.Map.Equals(Name, ESearchCase::IgnoreCase); });
	}

	const FGameInfo& NextAfter(const FString& Id)
	{
		const TArray<FGameInfo>& List = Games();
		const int32 i = List.IndexOfByPredicate([&Id](const FGameInfo& G) { return G.Id == Id; });
		return List[(i + 1) % List.Num()];
	}

	void ClampSeats(const FGameInfo& Game, int32& Locals, int32& Bots)
	{
		Locals = FMath::Clamp(Locals, 0, Game.MaxLocal);
		Bots = FMath::Clamp(Bots, 0, Game.MaxPlayers - Locals);
		if (Locals + Bots == 0)
		{
			Bots = 1;
		}
	}

	FString TravelUrl(const FGameInfo& Game, int32 Locals, int32 Bots)
	{
		ClampSeats(Game, Locals, Bots);
		return FString::Printf(TEXT("%s?Players=%d?Bots=%d"), *Game.Map, Locals, Bots);
	}

	const TCHAR* LexToString(EArcadeOrientation Orientation)
	{
		switch (Orientation)
		{
		case EArcadeOrientation::Portrait: return TEXT("portrait");
		case EArcadeOrientation::Any: return TEXT("any");
		case EArcadeOrientation::Landscape:
		default: return TEXT("landscape");
		}
	}

	bool LexFromString(EArcadeOrientation& Out, const TCHAR* Text)
	{
		for (const EArcadeOrientation Candidate : { EArcadeOrientation::Landscape, EArcadeOrientation::Portrait, EArcadeOrientation::Any })
		{
			if (FCString::Stricmp(Text, LexToString(Candidate)) == 0)
			{
				Out = Candidate;
				return true;
			}
		}
		return false;
	}

	EArcadeOrientation OrientationForLevel(const FString& Level)
	{
		const FGameInfo* Game = FindByMap(Level);
		return Game ? Game->Orientation : KartOrientation;
	}
}

#undef LOCTEXT_NAMESPACE

#pragma once

#include "CoreMinimal.h"

/** One playable track as the menus present it. The sim data itself lives in Data/Tracks/<Id>.track.json. */
struct MOVECADEKART_API FKartTrackInfo
{
	/** The track id the sim, the room protocol and ATrackBuilder::TrackId use ("sunset-harbor"). */
	FString Id;
	FText Name;
	FText Tagline;
	/** Long package name of the track's level ("/Game/Maps/L_SunsetHarbor"). */
	FString Level;
	/** The card, banner and intro accent. */
	FLinearColor Accent;
};

/** The track's outline for menu cards: the centreline normalised into [0, 1] (aspect kept), and its length. */
struct MOVECADEKART_API FKartTrackPreview
{
	TArray<FVector2D> Outline;
	/** Width / height of the outline's bounds. */
	double Aspect = 1.0;
	double LapMetres = 0.0;
	int32 Laps = 3;
	int32 GridSlots = 8;
};

/**
 * The tracks the front end offers. To add a track: export its JSON (Tools/track-export), make its level
 * (Tools/ue/make_track_map.py), then add one row to the table in KartTrackCatalog.cpp. The menus, the lobby
 * and level travel all read from here.
 */
class MOVECADEKART_API FKartTrackCatalog
{
public:
	static const TArray<FKartTrackInfo>& All();
	static const FKartTrackInfo* Find(const FString& Id);
	/** INDEX_NONE if unknown. */
	static int32 IndexOf(const FString& Id);
	/** By level short name ("L_VioletLoop") or long package name. */
	static const FKartTrackInfo* FindByLevel(const FString& LevelName);
	/** The level for a track id, or an empty string if the id is unknown. */
	static FString LevelFor(const FString& Id);

	/**
	 * The menu outline, loaded from the track JSON on a worker thread the first time it is asked for.
	 * Null until it is ready (or if the JSON is missing); call again next frame.
	 */
	static TSharedPtr<const FKartTrackPreview> GetPreview(const FString& Id);

	/** Simplifies a closed centreline to at most MaxPoints, normalised to [0, 1] in its longer axis. */
	static FKartTrackPreview MakePreview(const TArray<FVector2D>& Centre, int32 MaxPoints = 220);
};

#include "UI/KartTrackCatalog.h"

#include "Async/Async.h"
#include "Misc/PackageName.h"
#include "Sim/KartTrack.h"
#include "TrackBuilder.h"

#define LOCTEXT_NAMESPACE "KartTrackCatalog"

namespace
{
	FLinearColor Hex(const TCHAR* Code)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Code));
	}

	TArray<FKartTrackInfo> MakeCatalog()
	{
		// Names and taglines follow app/src/games/kart/tracks/catalog.ts and TrackPicker.tsx.
		TArray<FKartTrackInfo> Tracks;
		Tracks.Add({ TEXT("sunset-harbor"), LOCTEXT("HarborName", "Sunset Harbor"),
			LOCTEXT("HarborTagline", "Lighthouse bends, a coral tunnel and the marina at golden hour."),
			TEXT("/Game/Maps/L_SunsetHarbor"), Hex(TEXT("ff9a4d")) });
		Tracks.Add({ TEXT("violet-loop"), LOCTEXT("VioletName", "Violet Loop"),
			LOCTEXT("VioletTagline", "The original figure-eight circuit, fast and flowing."),
			TEXT("/Game/Maps/L_VioletLoop"), Hex(TEXT("a67cf0")) });
		// Ours, not the web's: the room server's maps.ts doesn't know it yet, so Party Mode can't select it.
		Tracks.Add({ TEXT("neon-city"), LOCTEXT("NeonName", "Neon City"),
			LOCTEXT("NeonTagline", "A night street circuit through a neon downtown. Local play for now."),
			TEXT("/Game/Maps/L_NeonCity"), Hex(TEXT("ff3fd2")) });
		return Tracks;
	}

	struct FPreviewCache
	{
		FCriticalSection Lock;
		TMap<FString, TSharedPtr<const FKartTrackPreview>> Ready;
		TSet<FString> Loading;
	};

	FPreviewCache& Cache()
	{
		static FPreviewCache Instance;
		return Instance;
	}
}

const TArray<FKartTrackInfo>& FKartTrackCatalog::All()
{
	static const TArray<FKartTrackInfo> Tracks = MakeCatalog();
	return Tracks;
}

const FKartTrackInfo* FKartTrackCatalog::Find(const FString& Id)
{
	return All().FindByPredicate([&Id](const FKartTrackInfo& Track) { return Track.Id == Id; });
}

int32 FKartTrackCatalog::IndexOf(const FString& Id)
{
	return All().IndexOfByPredicate([&Id](const FKartTrackInfo& Track) { return Track.Id == Id; });
}

const FKartTrackInfo* FKartTrackCatalog::FindByLevel(const FString& LevelName)
{
	const FString Short = FPackageName::GetShortName(LevelName);
	return All().FindByPredicate([&Short](const FKartTrackInfo& Track)
	{
		return FPackageName::GetShortName(Track.Level).Equals(Short, ESearchCase::IgnoreCase);
	});
}

FString FKartTrackCatalog::LevelFor(const FString& Id)
{
	const FKartTrackInfo* Track = Find(Id);
	return Track ? Track->Level : FString();
}

FKartTrackPreview FKartTrackCatalog::MakePreview(const TArray<FVector2D>& Centre, int32 MaxPoints)
{
	FKartTrackPreview Preview;
	if (Centre.Num() < 3)
	{
		return Preview;
	}
	FBox2D Bounds(ForceInit);
	for (int32 i = 0; i < Centre.Num(); ++i)
	{
		Bounds += Centre[i];
		Preview.LapMetres += FVector2D::Distance(Centre[i], Centre[(i + 1) % Centre.Num()]);
	}
	const FVector2D Size = Bounds.GetSize();
	const double Longest = FMath::Max3(Size.X, Size.Y, 1.0);
	Preview.Aspect = FMath::Max(Size.X, 1.0) / FMath::Max(Size.Y, 1.0);
	const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(Centre.Num(), FMath::Max(3, MaxPoints)));
	for (int32 i = 0; i < Centre.Num(); i += Stride)
	{
		Preview.Outline.Add((Centre[i] - Bounds.Min) / Longest);
	}
	return Preview;
}

TSharedPtr<const FKartTrackPreview> FKartTrackCatalog::GetPreview(const FString& Id)
{
	FPreviewCache& C = Cache();
	{
		FScopeLock Guard(&C.Lock);
		if (const TSharedPtr<const FKartTrackPreview>* Found = C.Ready.Find(Id))
		{
			return *Found;
		}
		if (C.Loading.Contains(Id) || !Find(Id))
		{
			return nullptr;
		}
		C.Loading.Add(Id);
	}
	const FString Path = ATrackBuilder::TrackPath(Id);
	Async(EAsyncExecution::ThreadPool, [Id, Path]()
	{
		MovecadeKart::FTrack Track;
		FString Error;
		TSharedPtr<FKartTrackPreview> Preview;
		if (MovecadeKart::FTrack::LoadFromJsonFile(Path, Track, Error))
		{
			Preview = MakeShared<FKartTrackPreview>(MakePreview(Track.Centre));
			Preview->Laps = Track.Laps;
			Preview->GridSlots = Track.Grid.Num();
		}
		FPreviewCache& Done = Cache();
		FScopeLock Guard(&Done.Lock);
		Done.Ready.Add(Id, Preview);
		Done.Loading.Remove(Id);
	});
	return nullptr;
}

#undef LOCTEXT_NAMESPACE

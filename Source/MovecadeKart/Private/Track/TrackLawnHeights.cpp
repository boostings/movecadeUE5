#include "Track/TrackLawnHeights.h"

#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sim/KartJsonFields.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrackLawn, Log, All);

namespace MovecadeKart::LifeShared
{
	namespace
	{
		/** The bake's cell size and corner: anything outside these is not a lawn of a real track. */
		constexpr double MaxCellM = 10.0;
		constexpr double MaxCornerM = 1e5;
		/** Sample's grid coordinates beyond this are far off the lawn (and would overflow the int32 conversion). */
		constexpr double MaxGridCoordinate = 1e9;
	}

	bool FLawnHeights::Load(const FString& TrackId)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks"), TrackId + TEXT(".lawn.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json))
		{
			return false;
		}
		return Parse(Json);
	}

	bool FLawnHeights::Parse(const TSharedPtr<FJsonObject>& Root)
	{
		*this = FLawnHeights();
		if (!Root.IsValid())
		{
			return false;
		}
		double CellM = 0.0, MinX = 0.0, MinZ = 0.0;
		FString Encoded;
		const bool bFields = Root->TryGetNumberField(TEXT("cellM"), CellM) && Root->TryGetNumberField(TEXT("minX"), MinX)
			&& Root->TryGetNumberField(TEXT("minZ"), MinZ) && Root->TryGetStringField(TEXT("heightsCm"), Encoded);
		const int32 InCols = Json::IntField(Root, TEXT("cols"), 0, 0, MaxSide);
		const int32 InRows = Json::IntField(Root, TEXT("rows"), 0, 0, MaxSide);
		const int64 CellCount = static_cast<int64>(InCols) * InRows;
		// The comparisons are false for NaN, so a non-finite header fails them too.
		if (!bFields || !(CellM > 0.0 && CellM <= MaxCellM) || !(FMath::Abs(MinX) <= MaxCornerM && FMath::Abs(MinZ) <= MaxCornerM)
			|| CellCount == 0 || CellCount > MaxCells)
		{
			UE_LOG(LogTrackLawn, Warning, TEXT("Ignoring a lawn bake of %d x %d cells of %g m at (%g, %g)"), InCols, InRows, CellM, MinX, MinZ);
			return false;
		}
		TArray<uint8> Bytes;
		if (!FBase64::Decode(Encoded, Bytes) || Bytes.Num() != CellCount * static_cast<int64>(sizeof(int16)))
		{
			UE_LOG(LogTrackLawn, Warning, TEXT("Ignoring a lawn bake whose heights are not %d x %d int16s"), InCols, InRows);
			return false;
		}
		Cell = CellM;
		Min = FVector2D(MinX, MinZ);
		Cols = InCols;
		Rows = InRows;
		Heights.SetNumUninitialized(static_cast<int32>(CellCount));
		FMemory::Memcpy(Heights.GetData(), Bytes.GetData(), Bytes.Num()); // little-endian int16, like every target
		return true;
	}

	bool FLawnHeights::Sample(const FVector2D& P, double& OutCm) const
	{
		if (Heights.Num() == 0)
		{
			return false;
		}
		const FVector2D G = (P - Min) / Cell - FVector2D(0.5);
		if (!(FMath::Abs(G.X) < MaxGridCoordinate && FMath::Abs(G.Y) < MaxGridCoordinate))
		{
			return false;
		}
		const int32 X0 = FMath::FloorToInt32(G.X), Y0 = FMath::FloorToInt32(G.Y);
		const double FX = G.X - X0, FY = G.Y - Y0;
		double Sum = 0.0, Weight = 0.0;
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 X = X0 + (k & 1), Y = Y0 + (k >> 1);
			if (X < 0 || Y < 0 || X >= Cols || Y >= Rows)
			{
				continue;
			}
			const int64 Index = static_cast<int64>(Y) * Cols + X;
			if (Index >= Heights.Num() || Heights[static_cast<int32>(Index)] == NoData)
			{
				continue;
			}
			const double W = ((k & 1) ? FX : 1.0 - FX) * ((k >> 1) ? FY : 1.0 - FY) + 1e-4;
			Sum += Heights[static_cast<int32>(Index)] * W;
			Weight += W;
		}
		if (Weight <= 0.0)
		{
			return false;
		}
		OutCm = Sum / Weight;
		return true;
	}
}

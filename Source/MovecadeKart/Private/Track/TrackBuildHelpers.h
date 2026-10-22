#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "FX/KartFx.h"
#include "KartCoords.h"
#include "Misc/Paths.h"

/**
 * Shared by ATrackBuilder's source files: tolerant readers for the track JSON (a missing field reads as its default),
 * asset loading, and the three.js-space transforms the exported scenery is placed with.
 */
namespace MovecadeKart::TrackBuild
{
	/** Every imported scenery component carries this tag (TakeOverPrefab and ClearScenery find them by it). */
	inline const TCHAR* const SceneryTag = TEXT("TrackScenery");
	inline const TCHAR* const SignPlaneAsset = TEXT("/Game/Tracks/Shared/violet_prefabs/StaticMeshes/sign_plane");

	inline double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default = 0.0)
	{
		double V = Default;
		return O.IsValid() && O->TryGetNumberField(Key, V) ? V : Default;
	}

	inline FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		FString V;
		if (O.IsValid())
		{
			O->TryGetStringField(Key, V);
		}
		return V;
	}

	inline TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return O.IsValid() && O->TryGetObjectField(Key, Out) ? *Out : nullptr;
	}

	inline const TArray<TSharedPtr<FJsonValue>>& Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		return O.IsValid() && O->TryGetArrayField(Key, Out) ? *Out : Empty;
	}

	inline FVector Vec3(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		return A.Num() >= 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : FVector::ZeroVector;
	}

	/** O[Key] as a vector, or Default when it is missing or shorter than three. */
	inline FVector VecOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVector& Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return O.IsValid() && O->TryGetArrayField(Key, A) && A->Num() >= 3 ? FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber()) : Default;
	}

	/** A "#rrggbb" as written by the content tools (every '#' dropped) in linear colour; Default when empty. */
	inline FLinearColor HexColor(const FString& Hex, const FLinearColor& Default = FLinearColor::White)
	{
		if (Hex.IsEmpty())
		{
			return Default;
		}
		TStringBuilder<16> Digits;
		for (const TCHAR C : Hex)
		{
			if (C != TEXT('#'))
			{
				Digits.AppendChar(C);
			}
		}
		return Fx::Hex(Digits.ToView());
	}

	/** O[Key] as a colour, or DefaultHex when the field is missing (a present but empty field reads as transparent). */
	inline FLinearColor ColorField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const TCHAR* DefaultHex)
	{
		FString V;
		if (!O.IsValid() || !O->TryGetStringField(Key, V))
		{
			V = DefaultHex;
		}
		return Fx::Hex(*V.Replace(TEXT("#"), TEXT("")));
	}

	/** "/Game/A/B" or "/Game/A/B.B", quietly (null when the asset isn't imported). */
	template <typename T>
	T* LoadAsset(const FString& PackagePath)
	{
		FString Path = PackagePath;
		if (!Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPaths::GetBaseFilename(PackagePath);
		}
		return LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** The master material for a JSON hint's shader (Content/Tracks/Shared/Materials); vertex colour when unknown. */
	inline FString MaterialPath(const FString& Shader)
	{
		static const TMap<FString, FString> Paths = {
			{ TEXT("vertexColor"), TEXT("M_TrackVertexColor") },
			{ TEXT("textured"), TEXT("M_TrackTextured") },
			{ TEXT("unlitTexture"), TEXT("M_TrackUnlitTexture") },
			{ TEXT("translucent"), TEXT("M_TrackTranslucent") },
			{ TEXT("additive"), TEXT("M_TrackAdditive") },
			{ TEXT("surface"), TEXT("M_TrackSurface") },
			{ TEXT("surfaceWall"), TEXT("M_TrackSurfaceWall") },
			{ TEXT("boostPad"), TEXT("M_TrackBoostPad") },
			{ TEXT("foliage"), TEXT("M_TrackFoliage") },
			{ TEXT("foliageWind"), TEXT("M_TrackFoliageWind") },
			{ TEXT("crystal"), TEXT("M_TrackCrystal") },
			{ TEXT("glass"), TEXT("M_TrackGlass") },
			{ TEXT("water"), TEXT("M_TrackWater") },
		};
		const FString* Name = Paths.Find(Shader);
		const FString Asset = Name ? *Name : TEXT("M_TrackVertexColor");
		return FString::Printf(TEXT("/Game/Tracks/Shared/Materials/%s.%s"), *Asset, *Asset);
	}

	// ---- three.js space -> UE ----------------------------------------------------------------
	// UE's glTF import maps glTF (x, y, z) to UE (x, z, y); the port's convention is UE (-z, x, y) (KartCoords.h), so
	// every imported mesh sits under a +90 degree yaw.

	inline const FQuat& ImportFix()
	{
		static const FQuat Fix = FRotator(0.0, 90.0, 0.0).Quaternion();
		return Fix;
	}

	/** A rotation built in three.js space (right-handed, Y up), as a UE rotation. */
	inline FQuat QuatToUE(const FQuat& Q)
	{
		return FQuat(Q.Z, -Q.X, -Q.Y, Q.W);
	}

	/** three.js Euler 'XYZ' (Object3D.rotation) as a quaternion in three.js space. */
	inline FQuat Euler(double X, double Y, double Z)
	{
		return FQuat(FVector::XAxisVector, X) * FQuat(FVector::YAxisVector, Y) * FQuat(FVector::ZAxisVector, Z);
	}

	/** A three.js-space transform (position in metres) as a component transform for an imported mesh. */
	inline FTransform ToUETransform(const FTransform& Three, double RebaseY)
	{
		const FVector P = Three.GetTranslation();
		const FVector S = Three.GetScale3D();
		return FTransform(QuatToUE(Three.GetRotation()) * ImportFix(),
			Coords::ToUE(FVector(P.X, P.Y + RebaseY, P.Z)),
			FVector(S.X, S.Z, S.Y)); // imported mesh axes are (x, z, y)
	}

	/** A transform in three.js space. */
	inline FTransform Three(const FVector& Position, const FQuat& Rotation = FQuat::Identity, const FVector& Scale = FVector::OneVector)
	{
		return FTransform(Rotation, Position, Scale);
	}
}

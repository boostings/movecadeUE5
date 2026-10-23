// ATrackBuilder's moving scenery: Harbor's animated actors (scenery.animated, HarborMotion.tsx) as one component per
// moving part, posed every frame from the world clock. Line references are HarborMotion.tsx.
#include "TrackBuilder.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Track/TrackBuildHelpers.h"

using namespace MovecadeKart;
using namespace MovecadeKart::TrackBuild;

namespace
{
	/** Every moving part carries this tag, and "<part>:<index>:<animated entry>" to find its motion again. */
	const TCHAR* const MotionTag = TEXT("Motion");
	constexpr int32 FerrisGondolas = 10;
	/** Gondola meshes come in this many colours ("ferris_gondola_0".."_4"). */
	constexpr int32 FerrisGondolaColours = 5;
	constexpr int32 JetSkis = 3;
	constexpr int32 Gulls = 9;

	/** Only the big movers cast: the ferris wheel and the ships. Gulls, jet skis, buoys and gondolas are small or far
	 *  out on the water, and every moving caster re-renders its shadow each frame. */
	bool CastsShadow(const FString& Part)
	{
		return Part == TEXT("ferris_frame") || Part == TEXT("ferris_wheel") || Part == TEXT("ferry") || Part == TEXT("cargo_ship");
	}
}

ATrackBuilder::EMotion ATrackBuilder::MotionOf(FName Part)
{
	// An FName compare is an integer compare; this runs once per part when the motion is gathered.
	static const TPair<FName, EMotion> Parts[] = {
		{ TEXT("lighthouse_beams"), EMotion::LighthouseBeams },
		{ TEXT("ferris_frame"), EMotion::FerrisFrame },
		{ TEXT("ferris_wheel"), EMotion::FerrisWheel },
		{ TEXT("ferris_gondola"), EMotion::FerrisGondola },
		{ TEXT("ferry"), EMotion::Ferry },
		{ TEXT("ferry_smoke"), EMotion::Ferry },
		{ TEXT("cargo_ship"), EMotion::CargoShip },
		{ TEXT("jet_ski"), EMotion::JetSki },
		{ TEXT("jet_wake"), EMotion::JetWake },
		{ TEXT("gull_body"), EMotion::GullBody },
		{ TEXT("gull_wing_l"), EMotion::GullWingL },
		{ TEXT("gull_wing_r"), EMotion::GullWingR },
		{ TEXT("fountain_jet"), EMotion::FountainJet },
		{ TEXT("fountain_spray"), EMotion::FountainSpray },
		{ TEXT("buoy"), EMotion::Buoy },
		{ TEXT("buoy_lamp"), EMotion::BuoyLamp },
	};
	for (const TPair<FName, EMotion>& Entry : Parts)
	{
		if (Entry.Key == Part)
		{
			return Entry.Value;
		}
	}
	return EMotion::Fixed;
}

void ATrackBuilder::BuildMotion()
{
	const TSharedPtr<FJsonObject> Scenery = Obj(Json, TEXT("scenery"));
	const TSharedPtr<FJsonObject> Parts = Obj(Scenery, TEXT("motionParts"));
	if (!Parts.IsValid())
	{
		return;
	}
	auto Spawn = [&](const FString& Part, int32 Index, int32 Entry)
	{
		const TSharedPtr<FJsonObject> Def = Obj(Parts, *Part);
		UStaticMesh* Mesh = Def.IsValid() ? LoadAsset<UStaticMesh>(Str(Def, TEXT("ueAsset"))) : nullptr;
		if (!Mesh)
		{
			return;
		}
		UMaterialInterface* Material = MaterialFor(Obj(Def, TEXT("material")));
		if (Part == TEXT("buoy_lamp") && Material)
		{
			// Each lamp blinks on its own, so it gets its own instance of the master.
			UMaterialInstanceDynamic* Own = UMaterialInstanceDynamic::Create(Material->GetMaterial(), this);
			Own->CopyParameterOverrides(Cast<UMaterialInstance>(Material));
			Material = Own;
		}
		UStaticMeshComponent* C = AddMeshComponent(Part, Mesh, FTransform::Identity, Material, MotionTag);
		C->ComponentTags.Add(*FString::Printf(TEXT("%s:%d:%d"), *Part, Index, Entry));
		C->SetCastShadow(CastsShadow(Part));
	};
	const TArray<TSharedPtr<FJsonValue>>& Animated = Arr(Scenery, TEXT("animated"));
	for (int32 E = 0; E < Animated.Num(); ++E)
	{
		const TSharedPtr<FJsonObject> A = Animated[E]->AsObject();
		const FString Type = Str(A, TEXT("type"));
		if (Type == TEXT("lighthouseBeam"))
		{
			Spawn(TEXT("lighthouse_beams"), 0, E);
			Spawn(TEXT("lighthouse_lamp"), 0, E);
		}
		else if (Type == TEXT("ferrisWheel"))
		{
			Spawn(TEXT("ferris_frame"), 0, E);
			Spawn(TEXT("ferris_wheel"), 0, E);
			for (int32 i = 0; i < FerrisGondolas; ++i)
			{
				Spawn(FString::Printf(TEXT("ferris_gondola_%d"), i % FerrisGondolaColours), i, E);
			}
		}
		else if (Type == TEXT("ferry"))
		{
			Spawn(TEXT("ferry"), 0, E);
			Spawn(TEXT("ferry_smoke"), 0, E);
		}
		else if (Type == TEXT("cargoShip"))
		{
			Spawn(TEXT("cargo_ship"), 0, E);
		}
		else if (Type == TEXT("jetSki"))
		{
			for (int32 i = 0; i < JetSkis; ++i)
			{
				Spawn(FString::Printf(TEXT("jet_ski_%d"), i), i, E);
				Spawn(TEXT("jet_wake"), i, E);
			}
		}
		else if (Type == TEXT("gullFlock"))
		{
			for (int32 i = 0; i < Gulls; ++i)
			{
				Spawn(TEXT("gull_body"), i, E);
				Spawn(TEXT("gull_wing_l"), i, E);
				Spawn(TEXT("gull_wing_r"), i, E);
			}
		}
		else if (Type == TEXT("fountain"))
		{
			Spawn(TEXT("fountain_jet"), 0, E);
			Spawn(TEXT("fountain_spray"), 0, E);
		}
		else if (Type == TEXT("buoy"))
		{
			Spawn(FString::Printf(TEXT("buoy_%d"), VariantOf(A)), 0, E);
			Spawn(TEXT("buoy_lamp"), 0, E);
		}
	}
	GatherMotion();
	TickMotion(0.0); // pose them for the editor viewport
}

void ATrackBuilder::GatherMotion()
{
	Motion.Reset();
	const TArray<TSharedPtr<FJsonValue>>& Animated = Arr(Obj(Json, TEXT("scenery")), TEXT("animated"));
	for (UActorComponent* Component : GetComponentsByTag(UStaticMeshComponent::StaticClass(), MotionTag))
	{
		for (const FName& Tag : Component->ComponentTags)
		{
			TArray<FString> Bits;
			if (Tag.ToString().ParseIntoArray(Bits, TEXT(":")) != 3)
			{
				continue;
			}
			const int32 Entry = FCString::Atoi(*Bits[2]);
			if (!Animated.IsValidIndex(Entry))
			{
				continue;
			}
			const TSharedPtr<FJsonObject> A = Animated[Entry]->AsObject();
			FMotionPart& M = Motion.AddDefaulted_GetRef();
			M.Component = Cast<UStaticMeshComponent>(Component);
			// Colour variants share a motion: "ferris_gondola_3" moves as "ferris_gondola".
			FString Part = Bits[0];
			int32 Underscore = INDEX_NONE;
			if (Part.FindLastChar(TEXT('_'), Underscore) && Part.Mid(Underscore + 1).IsNumeric())
			{
				Part.LeftInline(Underscore);
			}
			M.Kind = MotionOf(*Part);
			M.Index = FCString::Atoi(*Bits[1]);
			M.Base = Vec3(Arr(A, TEXT("position")));
			M.Yaw = Num(A, TEXT("yaw"));
			M.Phase = Num(Obj(A, TEXT("params")), TEXT("phase"));
			M.Lamp = Cast<UMaterialInstanceDynamic>(M.Component->GetMaterial(0));
		}
	}
}

void ATrackBuilder::TickMotion(double T)
{
	if (Motion.Num() == 0 || !Track.IsValid())
	{
		return;
	}
	static const FName OpacityName(TEXT("Opacity"));
	const double Rebase = Track->SuggestedRebaseY;
	for (FMotionPart& M : Motion)
	{
		UStaticMeshComponent* C = M.Component.Get();
		if (!C)
		{
			continue;
		}
		if (M.Kind == EMotion::BuoyLamp && M.Lamp.IsValid())
		{
			// :278-289: the lamp shows when sin(2.5 t + phase) > 0.6 (a MID ignores an unchanged value).
			M.Lamp->SetScalarParameterValue(OpacityName, FMath::Sin(T * 2.5 + M.Phase) > 0.6 ? 1.0 : 0.15);
		}
		C->SetRelativeTransform(ToUETransform(MotionPose(M, T), Rebase));
	}
}

FTransform ATrackBuilder::MotionPose(const FMotionPart& M, double T) const
{
	const int32 i = M.Index;
	switch (M.Kind)
	{
	case EMotion::LighthouseBeams:
		return Three(M.Base, Euler(0.0, 0.7 * T, 0.0)); // :23-25
	case EMotion::FerrisFrame:
	case EMotion::FerrisWheel:
	case EMotion::FerrisGondola:
	{
		// :52-62: the rim rolls 0.25 rad/s about the hub at y 11.6; gondolas hang 9 m out and counter-rotate to stay level.
		const FTransform BaseX = Three(M.Base, Euler(0.0, M.Yaw, 0.0));
		const double Roll = 0.25 * T;
		const FTransform Wheel = Three(FVector(0.0, 11.6, 0.0), Euler(0.0, 0.0, Roll));
		if (M.Kind == EMotion::FerrisFrame)
		{
			return BaseX;
		}
		if (M.Kind == EMotion::FerrisWheel)
		{
			return Wheel * BaseX;
		}
		const double Angle = i / static_cast<double>(FerrisGondolas) * UE_DOUBLE_TWO_PI;
		return Three(FVector(FMath::Cos(Angle) * 9.0, FMath::Sin(Angle) * 9.0, 0.0), Euler(0.0, 0.0, -Roll)) * Wheel * BaseX;
	}
	case EMotion::Ferry:
	{
		// :124-128: a slow loop round the bay, bobbing and rocking.
		const double A = T * 0.02;
		return Three(FVector(130.0 + FMath::Cos(A) * 330.0, -3.5 + FMath::Sin(T * 0.9) * 0.25, -35.0 + FMath::Sin(A) * 300.0),
			Euler(FMath::Sin(T * 0.7) * 0.015, -A, FMath::Sin(T * 0.9) * 0.02));
	}
	case EMotion::CargoShip:
	{
		// :129-133: a wider loop the other way.
		const double A = -T * 0.012 + 2.2;
		return Three(FVector(130.0 + FMath::Cos(A) * 420.0, -3.5 + FMath::Sin(T * 0.6) * 0.2, -35.0 + FMath::Sin(A) * 400.0),
			Euler(0.0, -A + UE_DOUBLE_PI, FMath::Sin(T * 0.6) * 0.015));
	}
	case EMotion::JetSki:
	case EMotion::JetWake:
	{
		// :165-178. The web yaws the wake after laying it flat (Euler XYZ), which rolls it about its length; here the
		// wake stays flat and yaws with its ski.
		const double A = T * 0.55 + i * (UE_DOUBLE_TWO_PI / 3.0);
		const double R = 24.0 + FMath::Sin(T * 0.3 + i) * 5.0;
		const double PX = 110.0 + FMath::Cos(A) * R, PZ = -45.0 + FMath::Sin(A) * R * 0.75;
		return M.Kind == EMotion::JetSki
			? Three(FVector(PX, -6.6 + FMath::Sin(T * 4.0 + i) * 0.12, PZ), Euler(FMath::Sin(T * 4.0 + i) * 0.05, -A, -0.25))
			: Three(FVector(PX, -6.92, PZ), Euler(0.0, -A, 0.0));
	}
	case EMotion::GullBody:
	case EMotion::GullWingL:
	case EMotion::GullWingR:
	{
		// :206-220: circling at their own speeds and heights, wings flapping.
		const double A = T * (0.35 + (i % 3) * 0.08) + i * 0.8;
		const double R = 14.0 + (i % 4) * 5.0;
		const FTransform Bird = Three(FVector(M.Base.X + FMath::Cos(A) * R, 18.0 + (i % 5) * 2.5 + FMath::Sin(T * 1.3 + i) * 0.8, M.Base.Z + FMath::Sin(A) * R), Euler(0.0, -A, 0.0));
		const double Flap = FMath::Sin(T * 6.0 + i) * 0.55;
		return M.Kind == EMotion::GullBody ? Bird : Three(FVector::ZeroVector, Euler(0.0, 0.0, M.Kind == EMotion::GullWingL ? Flap : -Flap)) * Bird;
	}
	case EMotion::FountainJet:
	case EMotion::FountainSpray:
	{
		// :246-257: the jet pulses; the spray turns and swells with it.
		const double Pulse = 0.7 + FMath::Sin(T * 1.6) * 0.3;
		return M.Kind == EMotion::FountainJet
			? Three(M.Base + FVector(0.0, 4.6 + Pulse * 1.4, 0.0), FQuat::Identity, FVector(1.0, Pulse, 1.0))
			: Three(M.Base + FVector(0.0, 6.4, 0.0), Euler(0.0, T * 0.8, 0.0), FVector(0.8 + Pulse * 0.5));
	}
	case EMotion::Buoy:
	case EMotion::BuoyLamp:
		// :278-289: bob and rock.
		return Three(FVector(M.Base.X, -6.6 + FMath::Sin(T * 1.4 + M.Phase) * 0.35, M.Base.Z),
			Euler(FMath::Sin(T * 1.1 + M.Phase) * 0.12, 0.0, FMath::Cos(T * 0.9 + M.Phase) * 0.12));
	case EMotion::Fixed:
	default:
		return Three(M.Base);
	}
}

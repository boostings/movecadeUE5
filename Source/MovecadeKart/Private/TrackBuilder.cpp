#include "TrackBuilder.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrackBuilder, Log, All);

namespace
{
	FLinearColor Hex(const TCHAR* Value)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Value));
	}

	/** Graybox colours per ribbon material (from HarborTrack.tsx where the web uses a flat colour). */
	FLinearColor RibbonColor(const FString& Material)
	{
		if (Material == TEXT("asphalt")) return Hex(TEXT("4a4f57"));
		if (Material == TEXT("edgeLine") || Material == TEXT("wallCap")) return Hex(TEXT("fff0cc"));
		if (Material == TEXT("dirtShoulder")) return Hex(TEXT("b89a6a"));
		if (Material == TEXT("barrier")) return Hex(TEXT("8fb0ad"));
		if (Material == TEXT("sand")) return Hex(TEXT("e6cc9e"));
		if (Material == TEXT("surf")) return Hex(TEXT("f4f7ec"));
		if (Material == TEXT("shallows")) return Hex(TEXT("7fd0cc"));
		return Hex(TEXT("808080"));
	}

	/** Emits a quad with both windings and one normal, so it shows from its intended side whichever
	 *  winding the renderer treats as front-facing. */
	void AddQuad(TArray<FVector>& V, TArray<int32>& Tri, TArray<FVector>& N, const FVector& A0, const FVector& B0, const FVector& A1, const FVector& B1, const FVector& Normal)
	{
		const int32 Base = V.Add(A0);
		V.Add(B0);
		V.Add(A1);
		V.Add(B1);
		N.Append({ Normal, Normal, Normal, Normal });
		Tri.Append({ Base, Base + 1, Base + 2, Base + 1, Base + 3, Base + 2 });
		Tri.Append({ Base, Base + 2, Base + 1, Base + 1, Base + 2, Base + 3 });
	}

	double JsonNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default = 0.0)
	{
		double V = Default;
		return O.IsValid() && O->TryGetNumberField(Key, V) ? V : Default;
	}
}

ATrackBuilder::ATrackBuilder()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(RootComponent);
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ground->bUseAsyncCooking = true;
	// Flat ribbons and the 5 km sea gain nothing from casting shadows and overflow the VSM non-Nanite queue.
	Ground->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = ShapeMaterial.Object;

	ItemBoxes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ItemBoxes"));
	ItemBoxes->SetupAttachment(RootComponent);
	ItemBoxes->SetStaticMesh(CubeMesh.Object);
	ItemBoxes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ItemBoxes->SetMobility(EComponentMobility::Movable);

	BoostPads = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoostPads"));
	BoostPads->SetupAttachment(RootComponent);
	BoostPads->SetStaticMesh(PlaneMesh.Object);
	BoostPads->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetMobility(EComponentMobility::Movable);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	// Movable, so ApplyLook's setters take effect at BeginPlay too (static components ignore them).
	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(RootComponent);
	Atmosphere->SetMobility(EComponentMobility::Movable);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetMobility(EComponentMobility::Movable);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(RootComponent);
	PostProcess->bUnbound = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	SkySphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkySphere"));
	SkySphere->SetupAttachment(RootComponent);
	SkySphere->SetStaticMesh(SphereMesh.Object);
	SkySphere->SetRelativeScale3D(FVector(20000.0)); // the Sphere is 1 m across: 10 km radius
	SkySphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkySphere->SetCastShadow(false);
	SkySphere->SetReceivesDecals(false);
	SkySphere->bAffectDistanceFieldLighting = false;
	SkySphere->SetMobility(EComponentMobility::Movable);
}

FString ATrackBuilder::TrackPath(const FString& Id)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks"), Id + TEXT(".track.json"));
}

TSharedPtr<const MovecadeKart::FTrack> ATrackBuilder::GetTrack()
{
	if (!Track.IsValid() || LoadedId != TrackId)
	{
		Load();
	}
	return Track;
}

bool ATrackBuilder::Load()
{
	Track.Reset();
	Json.Reset();
	LoadedId = TrackId;

	FString Text;
	const FString Path = TrackPath(TrackId);
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogTrackBuilder, Error, TEXT("Track file not found: %s. Run Tools/track-export (npm run export)."), *Path);
		return false;
	}
	TSharedPtr<MovecadeKart::FTrack> Parsed = MakeShared<MovecadeKart::FTrack>();
	FString Error;
	if (!MovecadeKart::FTrack::LoadFromJsonString(Text, *Parsed, Error))
	{
		UE_LOG(LogTrackBuilder, Error, TEXT("%s: %s"), *Path, *Error);
		return false;
	}
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
	Track = Parsed;
	return true;
}

void ATrackBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The track is authored in world space.
	SetActorTransform(FTransform::Identity);
	if (GetTrack())
	{
		Build();
	}
}

void ATrackBuilder::AddSection(const FString& Name, const FLinearColor& Color, TFunctionRef<void(TArray<FVector>&, TArray<int32>&, TArray<FVector>&)> Emit)
{
	TArray<FVector> Vertices, Normals;
	TArray<int32> Triangles;
	Emit(Vertices, Triangles, Normals);
	if (Triangles.Num() == 0)
	{
		return;
	}
	const int32 Index = SectionCount++;
	Ground->CreateMeshSection_LinearColor(Index, Vertices, Triangles, Normals, TArray<FVector2D>(), TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this, *FString::Printf(TEXT("MID_%s"), *Name));
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Ground->SetMaterial(Index, Material);
	}
}

void ATrackBuilder::Build()
{
	using namespace MovecadeKart;
	const FTrack& T = *Track;
	const double Rebase = T.SuggestedRebaseY;
	const int32 Rows = T.Centre.Num();

	Ground->ClearAllMeshSections();
	SectionCount = 0;

	// Imported scenery first: an imported ribbon set replaces the procedural one below.
	ClearScenery();
	const bool bRibbonsImported = BuildImportedMeshes();
	BuildSea();
	const bool bSeaImported = GetComponentsByTag(UStaticMeshComponent::StaticClass(), TEXT("TrackSea")).Num() > 0;
	BuildPrefabInstances();
	BuildSigns();
	BuildMotion();

	auto Right = [&T](int32 i) { const FVector2D Tg = T.CentreTangent[i % T.Centre.Num()]; return FVector2D(-Tg.Y, Tg.X); };
	auto At = [&](int32 i, double Offset, double Y) { const int32 k = i % Rows; return Coords::ToUE(T.Centre[k] + Right(k) * Offset, Y + Rebase); };

	// Ribbons: one section per ribbon, curbs split into their red and cream stripes (every 4 rows).
	const TArray<TSharedPtr<FJsonValue>>* Ribbons = nullptr;
	if (!bRibbonsImported && Json.IsValid() && Json->TryGetArrayField(TEXT("ribbons"), Ribbons))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Ribbons)
		{
			const TSharedPtr<FJsonObject> R = Value->AsObject();
			FString Name, Material;
			R->TryGetStringField(TEXT("name"), Name);
			R->TryGetStringField(TEXT("material"), Material);
			const double Inner = JsonNum(R, TEXT("inner")), Outer = JsonNum(R, TEXT("outer"));
			const double InnerY = JsonNum(R, TEXT("innerY")), OuterY = JsonNum(R, TEXT("outerY"));
			const bool bWall = FMath::IsNearlyEqual(Inner, Outer);
			const int32 StripePeriod = static_cast<int32>(JsonNum(R, TEXT("stripePeriodSamples"), 0));

			// B14: skip quads where the offset curve folds back inside a bend tighter than the offset.
			TArray<bool> Folded;
			Folded.Init(false, Rows);
			for (int32 i = 0; i < Rows; ++i)
			{
				for (const double Offset : { Inner, Outer })
				{
					FVector2D Nearest;
					double Lateral = 0.0;
					const FVector2D P = T.Centre[i] + Right(i) * Offset;
					if (FMath::Abs(Offset) > 1.0 && T.NearestCentre(P, Nearest, Lateral) < FMath::Abs(Offset) - 0.25)
					{
						Folded[i] = true;
					}
				}
			}

			auto Emit = [&](int32 Stripe, TArray<FVector>& V, TArray<int32>& Tri, TArray<FVector>& N)
			{
				for (int32 i = 0; i < Rows; ++i)
				{
					if ((StripePeriod > 0 && (i / StripePeriod) % 2 != Stripe) || Folded[i] || Folded[(i + 1) % Rows])
					{
						continue;
					}
					FVector Normal = FVector::UpVector;
					if (bWall)
					{
						// Face the road: a left wall (negative offset) faces right of travel.
						const FVector RightUE = Coords::DirToUE(FVector(Right(i).X, 0.0, Right(i).Y));
						Normal = Inner < 0.0 ? RightUE : -RightUE;
					}
					AddQuad(V, Tri, N, At(i, Inner, InnerY), At(i, Outer, OuterY), At(i + 1, Inner, InnerY), At(i + 1, Outer, OuterY), Normal);
				}
			};

			if (StripePeriod > 0)
			{
				AddSection(Name + TEXT("_red"), Hex(TEXT("d7312a")), [&](auto& V, auto& Tri, auto& N) { Emit(0, V, Tri, N); });
				AddSection(Name + TEXT("_cream"), Hex(TEXT("f7f4ec")), [&](auto& V, auto& Tri, auto& N) { Emit(1, V, Tri, N); });
			}
			else
			{
				AddSection(Name, RibbonColor(Material), [&](auto& V, auto& Tri, auto& N) { Emit(-1, V, Tri, N); });
			}
		}
	}

	// Sea and other flat scenery instances.
	const TSharedPtr<FJsonObject>* Scenery = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
	if (Json.IsValid() && Json->TryGetObjectField(TEXT("scenery"), Scenery) && (*Scenery)->TryGetArrayField(TEXT("instances"), Instances))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Instances)
		{
			const TSharedPtr<FJsonObject> I = Value->AsObject();
			FString Prefab, Tint;
			I->TryGetStringField(TEXT("prefab"), Prefab);
			if (Prefab != TEXT("sea_plane") || bSeaImported)
			{
				continue; // Prefabs are instanced by BuildPrefabInstances; this is the graybox sea.
			}
			I->TryGetStringField(TEXT("tint"), Tint);
			const TArray<TSharedPtr<FJsonValue>>* Scale = nullptr;
			I->TryGetArrayField(TEXT("scale"), Scale);
			const double HalfX = Scale ? (*Scale)[0]->AsNumber() / 2.0 : 2500.0;
			const double HalfZ = Scale ? (*Scale)[2]->AsNumber() / 2.0 : 2500.0;
			const FVector2D C(JsonNum(I, TEXT("x")), JsonNum(I, TEXT("z")));
			const double Y = JsonNum(I, TEXT("y")) + Rebase;
			AddSection(TEXT("sea"), Hex(*Tint.Replace(TEXT("#"), TEXT(""))), [&](auto& V, auto& Tri, auto& N)
			{
				AddQuad(V, Tri, N,
					Coords::ToUE(C + FVector2D(-HalfX, -HalfZ), Y), Coords::ToUE(C + FVector2D(HalfX, -HalfZ), Y),
					Coords::ToUE(C + FVector2D(-HalfX, HalfZ), Y), Coords::ToUE(C + FVector2D(HalfX, HalfZ), Y), FVector::UpVector);
			});
		}
	}

	// Start/finish line across the road at the finish checkpoint, 1.5 m deep (both tracks paint their own
	// chequers, so only the graybox needs it).
	if (SceneryCount == 0 && T.Checkpoints.Num() > 0)
	{
		const FVector2D F = T.Checkpoints[0].Position;
		double HalfWidth = 14.0;
		const TSharedPtr<FJsonObject>* StartLine = nullptr;
		if (Json.IsValid() && Json->TryGetObjectField(TEXT("startLine"), StartLine))
		{
			HalfWidth = JsonNum(*StartLine, TEXT("width"), 28.0) / 2.0;
		}
		const double Y = 0.03 + Rebase;
		AddSection(TEXT("startLine"), Hex(TEXT("ffffff")), [&](auto& V, auto& Tri, auto& N)
		{
			AddQuad(V, Tri, N,
				Coords::ToUE(F + FVector2D(-HalfWidth, -0.75), Y), Coords::ToUE(F + FVector2D(HalfWidth, -0.75), Y),
				Coords::ToUE(F + FVector2D(-HalfWidth, 0.75), Y), Coords::ToUE(F + FVector2D(HalfWidth, 0.75), Y), FVector::UpVector);
		});
	}

	// Boost pads: the emissive chevron prefab, or the graybox plane.
	BuildBoostPads();

	// Item boxes: 2 m cubes at y 1 (pickup arrives with the items phase).
	ItemBoxes->ClearInstances();
	ItemBoxBase.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
	if (Json.IsValid() && Json->TryGetArrayField(TEXT("itemBoxes"), Boxes))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Boxes)
		{
			const TSharedPtr<FJsonObject> B = Value->AsObject();
			const FVector P = Coords::ToUE(FVector(JsonNum(B, TEXT("x")), JsonNum(B, TEXT("y")) + Rebase, JsonNum(B, TEXT("z"))));
			ItemBoxBase.Add(P);
			ItemBoxes->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(2.0)), true);
		}
	}
	if (BaseMaterial)
	{
		if (BoostPads->GetStaticMesh() && BoostPads->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/")))
		{
			UMaterialInstanceDynamic* PadMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this, TEXT("MID_BoostPad"));
			PadMaterial->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("ff6a1a")));
			BoostPads->SetMaterial(0, PadMaterial);
		}
		UMaterialInstanceDynamic* BoxMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this, TEXT("MID_ItemBox"));
		BoxMaterial->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("ffcc00")));
		ItemBoxes->SetMaterial(0, BoxMaterial);
	}

	ApplyLook();
}

namespace
{
	TSharedPtr<FJsonObject> JsonObj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return O.IsValid() && O->TryGetObjectField(Key, Out) ? *Out : nullptr;
	}

	FLinearColor JsonColor(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const TCHAR* Default)
	{
		FString V;
		if (!O.IsValid() || !O->TryGetStringField(Key, V))
		{
			V = Default;
		}
		return Hex(*V.Replace(TEXT("#"), TEXT("")));
	}

	FVector JsonVec(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVector& Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		return O.IsValid() && O->TryGetArrayField(Key, A) && A->Num() >= 3 ? FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber()) : Default;
	}
}

void ATrackBuilder::ApplyLook()
{
	using namespace MovecadeKart;
	Sun->SetVisibility(bAddLighting);
	SkyLight->SetVisibility(bAddLighting);
	Atmosphere->SetVisibility(bAddLighting);
	Fog->SetVisibility(bAddLighting);
	PostProcess->bEnabled = bAddLighting;
	if (!Json.IsValid())
	{
		return;
	}
	// kartLook.ts's record (Docs/Port/03 §5.7). The scene is lit in three.js units: with manual exposure at
	// EV 0, a sun of `sunIntensity` lux on a white Lambert surface gives the web's 1/pi-scaled radiance, so
	// colours land where the web's neutral tone mapping puts them. Ambient comes from the real-time sky
	// light (hemisphere + fill), scaled up because Lumen's sky occlusion darkens it.
	const TSharedPtr<FJsonObject> Look = JsonObj(Json, TEXT("look"));
	const TSharedPtr<FJsonObject> Mood = JsonObj(Look, TEXT("mood"));
	const TSharedPtr<FJsonObject> Grade = JsonObj(Look, TEXT("grade"));
	const TSharedPtr<FJsonObject> Bloom = JsonObj(Look, TEXT("bloom"));
	const TSharedPtr<FJsonObject> FogLook = JsonObj(Look, TEXT("fog"));
	const TSharedPtr<FJsonObject> Hemi = JsonObj(Look, TEXT("hemisphere"));
	const TSharedPtr<FJsonObject> Fill = JsonObj(Look, TEXT("fill"));
	// UE-only tuning: look.ue in the JSON, overridden by -KartLookUE="key=value,..." for quick look passes.
	const TSharedPtr<FJsonObject> UELook = JsonObj(Look, TEXT("ue"));
	TMap<FString, double> Overrides;
	FString OverrideText;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartLookUE="), OverrideText, /*bShouldStopOnSeparator*/ false))
	{
		TArray<FString> Pairs;
		OverrideText.ParseIntoArray(Pairs, TEXT(","));
		for (const FString& Pair : Pairs)
		{
			FString Key, Value;
			if (Pair.Split(TEXT("="), &Key, &Value))
			{
				Overrides.Add(Key.TrimStartAndEnd(), FCString::Atod(*Value));
			}
		}
	}
	auto Tune = [&UELook, &Overrides](const TCHAR* Key, double Default)
	{
		const double* Override = Overrides.Find(Key);
		return Override ? *Override : JsonNum(UELook, Key, Default);
	};

	// Sun: direction toward the sun in three.js axes.
	const FVector SunDir = JsonVec(Look, TEXT("sunDirection"), Track.IsValid() ? Track->SunDirection : FVector(0.5, 0.6, 0.6));
	Sun->SetWorldRotation((-Coords::DirToUE(SunDir).GetSafeNormal()).Rotation());
	Sun->SetLightColor(JsonColor(Look, TEXT("sunColor"), TEXT("ffffff")));
	Sun->SetIntensity(static_cast<float>(JsonNum(Look, TEXT("sunIntensity"), 3.0) * Tune(TEXT("sunScale"), 1.0)));
	Sun->SetAtmosphereSunLight(true);
	Sun->SetCastShadows(true);
	Sun->SetDynamicShadowDistanceMovableLight(20000.f);

	// Sky light: real-time capture of the atmosphere, tinted toward the hemisphere sky colour.
	const double Ambient = JsonNum(Hemi, TEXT("intensity"), 0.6) + JsonNum(Fill, TEXT("intensity"), 0.3);
	SkyLight->SetIntensity(static_cast<float>(Ambient * Tune(TEXT("skyLightScale"), 1.5)));
	SkyLight->SetLightColor(FMath::Lerp(FLinearColor::White, JsonColor(Hemi, TEXT("sky"), TEXT("ffffff")), 0.35f));
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(JsonColor(Hemi, TEXT("ground"), TEXT("6e5a48")) * 0.5f);
	SkyLight->RecaptureSky();

	// Sky: the web's dome (kartLook mood: three-stop gradient, sun disc and glow) on M_TrackSky. The physical
	// atmosphere stays off: at the web's light levels it renders a dim grey sky, and it would tint the sun.
	Atmosphere->SetVisibility(false);
	Sun->SetAtmosphereSunLight(false);
	SkySphere->SetVisibility(bAddLighting);
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		const TSharedPtr<FJsonObject> SkyLook = JsonObj(Mood, TEXT("sky"));
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this, TEXT("MID_Sky"));
		const float Brightness = static_cast<float>(Tune(TEXT("skyBrightness"), 1.0));
		Sky->SetVectorParameterValue(TEXT("SkyTop"), JsonColor(Mood, TEXT("skyTop"), TEXT("4f78b8")));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), JsonColor(Mood, TEXT("skyHorizon"), TEXT("d4e6f5")));
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), JsonColor(Mood, TEXT("skyGround"), TEXT("a9bfd6")));
		const FVector SkySun = Coords::DirToUE(JsonVec(SkyLook, TEXT("sunDirection"), SunDir)).GetSafeNormal();
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SkySun.X, SkySun.Y, SkySun.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), JsonColor(SkyLook, TEXT("cloudColor"), TEXT("fff6ee")));
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.6 * JsonNum(SkyLook, TEXT("sunSize"), 0.03)));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), JsonNum(SkyLook, TEXT("sunGlow"), 0.8) * 0.6);
		Sky->SetScalarParameterValue(TEXT("Brightness"), Brightness);
		SkySphere->SetMaterial(0, Sky);
	}
	Fog->SetFogCutoffDistance(900000.f); // the dome at 10 km stays unfogged

	// Fog: the web's linear fog runs from near to far in the sky horizon colour. Height fog starts at near
	// and is dense enough to read as haze at far.
	const double Near = JsonNum(FogLook, TEXT("near"), 260.0), Far = JsonNum(FogLook, TEXT("far"), 2000.0);
	Fog->SetStartDistance(static_cast<float>(Near * 100.0));
	Fog->SetFogDensity(static_cast<float>(Tune(TEXT("fogDensity"), 5.0 / FMath::Max(Far - Near, 100.0))));
	Fog->SetFogHeightFalloff(static_cast<float>(Tune(TEXT("fogFalloff"), 0.02)));
	Fog->SetFogMaxOpacity(static_cast<float>(Tune(TEXT("fogMaxOpacity"), 0.85)));
	Fog->SetFogInscatteringColor(JsonColor(Mood, TEXT("skyHorizon"), TEXT("d4e6f5")) * static_cast<float>(Tune(TEXT("fogBrightness"), 1.0)));
	Fog->SetDirectionalInscatteringColor(JsonColor(Look, TEXT("sunColor"), TEXT("ffffff")) * 0.25f);

	// Post: manual exposure, the grade, bloom, vignette, Lumen.
	FPostProcessSettings& S = PostProcess->Settings;
	S = FPostProcessSettings();
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = static_cast<float>(FMath::Log2(FMath::Max(JsonNum(Grade, TEXT("exposure"), 1.0), 0.01)) + Tune(TEXT("exposureBias"), 0.0));
	// Vibrance has no UE twin; a quarter of it goes into saturation.
	const double Contrast = JsonNum(Grade, TEXT("contrast"), 1.0), Saturation = JsonNum(Grade, TEXT("saturation"), 1.0) + 0.25 * JsonNum(Grade, TEXT("vibrance"), 0.0);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(Contrast, Contrast, Contrast, 1.0);
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0);
	const FVector Gain = JsonVec(Grade, TEXT("gain"), FVector::OneVector), Lift = JsonVec(Grade, TEXT("lift"), FVector::ZeroVector);
	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(Gain.X, Gain.Y, Gain.Z, 1.0);
	// The web lifts after tone mapping; UE's offset acts on scene colour, so a third of it gives the same tint.
	S.bOverride_ColorOffset = true;
	S.ColorOffset = FVector4(Lift.X / 3.0, Lift.Y / 3.0, Lift.Z / 3.0, 0.0);
	// The web tone maps with three.js Neutral; soften the filmic curve toward it.
	S.bOverride_ToneCurveAmount = true;
	S.ToneCurveAmount = static_cast<float>(Tune(TEXT("toneCurve"), 0.6));
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = static_cast<float>(JsonNum(Bloom, TEXT("strength"), 0.3) * 1.5);
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = static_cast<float>(JsonNum(Bloom, TEXT("threshold"), 1.0));
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = static_cast<float>(JsonNum(Look, TEXT("vignette"), 0.3));
	S.bOverride_DynamicGlobalIlluminationMethod = true;
	S.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	S.bOverride_ReflectionMethod = true;
	S.ReflectionMethod = EReflectionMethod::Lumen;
	// Split-screen budget: four views share the frame, so Lumen runs at its cheaper settings.
	S.bOverride_LumenSceneLightingQuality = true;
	S.LumenSceneLightingQuality = 0.75f;
	S.bOverride_LumenFinalGatherQuality = true;
	S.LumenFinalGatherQuality = 0.75f;
	S.bOverride_LumenMaxTraceDistance = true;
	S.LumenMaxTraceDistance = 20000.f;
	S.bOverride_AmbientOcclusionIntensity = true;
	S.AmbientOcclusionIntensity = 0.4f;
}

void ATrackBuilder::BeginPlay()
{
	Super::BeginPlay();
	// -KartTrack=<id>: race another exported track on this level (local play; the room server only knows
	// violet-loop and sunset-harbor). The whole track, scenery and look included, is rebuilt for it.
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartTrack="), Override) && !Override.IsEmpty() && Override != TrackId)
	{
		if (FPaths::FileExists(TrackPath(Override)))
		{
			UE_LOG(LogTrackBuilder, Log, TEXT("-KartTrack: %s replaces %s"), *Override, *TrackId);
			TrackId = Override;
			if (GetTrack())
			{
				Build();
			}
		}
		else
		{
			UE_LOG(LogTrackBuilder, Error, TEXT("-KartTrack=%s: no %s"), *Override, *TrackPath(Override));
		}
	}
	if (GetTrack() && SceneryCount == 0 && GetComponentsByTag(UStaticMeshComponent::StaticClass(), TEXT("TrackScenery")).Num() == 0)
	{
		Build(); // A level saved before the assets were imported.
	}
	else if (Json.IsValid())
	{
		ApplyLook(); // the look as the code has it now, even on a level saved earlier
	}
	GatherMotion();
}

void ATrackBuilder::SetPreviewItemBoxesVisible(bool bVisible)
{
	ItemBoxes->SetVisibility(bVisible);
}

void ATrackBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Time = GetWorld()->GetTimeSeconds();
	TickMotion(Time);

	// MultiplayerPowerups.tsx:159-164: yaw +1.5 rad/s, bob 0.1 * sin(2.5 t).
	for (int32 i = 0; i < ItemBoxBase.Num(); ++i)
	{
		const FVector P = ItemBoxBase[i] + FVector(0.0, 0.0, 10.0 * FMath::Sin(2.5 * Time));
		ItemBoxes->UpdateInstanceTransform(i, FTransform(FRotator(0.0, -FMath::RadiansToDegrees(1.5 * Time), 0.0), P, FVector(2.0)), true, i == ItemBoxBase.Num() - 1);
	}

	if (bShowCheckpoints && Track.IsValid())
	{
		for (const MovecadeKart::FCheckpoint& Cp : Track->Checkpoints)
		{
			DrawDebugCircle(GetWorld(), MovecadeKart::Coords::ToUE(Cp.Position, 0.3 + Track->SuggestedRebaseY), Cp.Radius * 100.0, 48,
				Cp.bFinish ? FColor::Green : FColor::Cyan, false, -1.f, 0, 12.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
		}
	}
}

#include "Avatar/AvatarDressing.h"
#include "Engine/SkeletalMesh.h"
#include "KartLook.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITORONLY_DATA
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	FLinearColor Hex(const TCHAR* Code)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Code));
	}

	/** Least-squares sphere through Points (|p - c|^2 = r^2, linear in c and r^2 - |c|^2): its centre and radius. */
	bool FitSphere(const TArray<FVector3f>& Points, FVector& OutCentre, double& OutRadius)
	{
		double M[4][5] = {};
		for (const FVector3f& P : Points)
		{
			const double Row[4] = { 2.0 * P.X, 2.0 * P.Y, 2.0 * P.Z, 1.0 };
			const double B = static_cast<double>(P.SizeSquared());
			for (int32 i = 0; i < 4; ++i)
			{
				for (int32 j = 0; j < 4; ++j)
				{
					M[i][j] += Row[i] * Row[j];
				}
				M[i][4] += Row[i] * B;
			}
		}
		// Gauss-Jordan with partial pivoting on the 4x4 normal equations.
		for (int32 Col = 0; Col < 4; ++Col)
		{
			int32 Pivot = Col;
			for (int32 Row = Col + 1; Row < 4; ++Row)
			{
				Pivot = FMath::Abs(M[Row][Col]) > FMath::Abs(M[Pivot][Col]) ? Row : Pivot;
			}
			if (FMath::Abs(M[Pivot][Col]) < 1e-9)
			{
				return false;
			}
			for (int32 k = 0; k < 5; ++k)
			{
				Swap(M[Col][k], M[Pivot][k]);
			}
			for (int32 Row = 0; Row < 4; ++Row)
			{
				if (Row != Col)
				{
					const double F = M[Row][Col] / M[Col][Col];
					for (int32 k = Col; k < 5; ++k)
					{
						M[Row][k] -= F * M[Col][k];
					}
				}
			}
		}
		OutCentre = FVector(M[0][4] / M[0][0], M[1][4] / M[1][1], M[2][4] / M[2][2]);
		OutRadius = FMath::Sqrt(M[3][4] / M[3][3] + OutCentre.SizeSquared());
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarSkinTuningTest, "MovecadeKart.Avatar.SkinTuning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarSkinTuningTest::RunTest(const FString& Parameters)
{
	using Avatar::ESkinTone;
	// The stock tones (KartLook.cpp) fall into the three profile families.
	const TCHAR* const Light[] = { TEXT("f9e4d2"), TEXT("f3d0b8"), TEXT("e0a888") };
	const TCHAR* const Medium[] = { TEXT("c9a27c"), TEXT("d08a68"), TEXT("a86a48") };
	const TCHAR* const Deep[] = { TEXT("8a5236"), TEXT("6b3d28") };
	for (const TCHAR* Code : Light)
	{
		TestTrue(FString::Printf(TEXT("%s is light"), Code), Avatar::SkinTuning(Hex(Code)).Tone == ESkinTone::Light);
	}
	for (const TCHAR* Code : Medium)
	{
		TestTrue(FString::Printf(TEXT("%s is medium"), Code), Avatar::SkinTuning(Hex(Code)).Tone == ESkinTone::Medium);
	}
	for (const TCHAR* Code : Deep)
	{
		TestTrue(FString::Printf(TEXT("%s is deep"), Code), Avatar::SkinTuning(Hex(Code)).Tone == ESkinTone::Deep);
	}
	// Lighter skin scatters further and flushes more; deeper skin is held back from over-saturating.
	const Avatar::FSkinTuning Pale = Avatar::SkinTuning(Hex(TEXT("f9e4d2")));
	const Avatar::FSkinTuning Dark = Avatar::SkinTuning(Hex(TEXT("6b3d28")));
	TestTrue(TEXT("scatter follows lightness"), Pale.Scatter > Dark.Scatter);
	TestTrue(TEXT("flush follows lightness"), Pale.Flush > Dark.Flush);
	TestTrue(TEXT("deep tones desaturated more"), Dark.Saturation < Pale.Saturation && Dark.Saturation > 0.5f);

	// Brows deepen the hair; lips sit between the skin and a muted rose.
	const FLinearColor Hair = Hex(TEXT("c8a050"));
	TestTrue(TEXT("brows darker than the hair"), Avatar::BrowColour(Hair).GetLuminance() < Hair.GetLuminance());
	const FLinearColor Lips = Avatar::LipColour(Hex(TEXT("d08a68")));
	TestTrue(TEXT("lips redder than they are green"), Lips.R > Lips.G * 1.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarEyeRigTest, "MovecadeKart.Avatar.EyeRigMatchesMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarEyeRigTest::RunTest(const FString& Parameters)
{
#if WITH_EDITORONLY_DATA
	// The eye material turns the irises about FEyeRig's centres: they must be the eyeballs' own (fitted to each
	// face's eyeWhite sphere, right eye).
	static const FName EyeWhite(TEXT("eyeWhite"));
	for (uint8 Face = 0; Face <= static_cast<uint8>(EKartFace::Rose); ++Face)
	{
		const EKartFace KartFace = static_cast<EKartFace>(Face);
		const FString Key = FString::Printf(TEXT("%s_female"), FKartLook::FaceId(KartFace));
		const FString Path = FString::Printf(TEXT("/Game/Avatars/Drivers/%s/SK_Driver_%s.SK_Driver_%s"), *Key, *Key, *Key);
		const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		const FSkeletalMeshModel* Model = Mesh ? Mesh->GetImportedModel() : nullptr;
		if (!Model || Model->LODModels.Num() == 0)
		{
			AddError(FString::Printf(TEXT("%s: no driver mesh"), *Key));
			continue;
		}
		TArray<FVector3f> Points;
		for (const FSkelMeshSection& Section : Model->LODModels[0].Sections)
		{
			const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
			if (Materials.IsValidIndex(Section.MaterialIndex) && Materials[Section.MaterialIndex].MaterialSlotName == EyeWhite)
			{
				for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
				{
					if (Vertex.Position.X > 0.f)
					{
						Points.Add(Vertex.Position);
					}
				}
			}
		}
		FVector Centre;
		double Radius = 0.0;
		const Avatar::FEyeRig Rig = Avatar::EyeRig(KartFace);
		if (!TestTrue(FString::Printf(TEXT("%s: eye white found"), *Key), Points.Num() > 16 && FitSphere(Points, Centre, Radius)))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s: eye centre %s vs table %s"), *Key, *Centre.ToString(), *Rig.Centre.ToString()), Centre.Equals(Rig.Centre, 0.05));
		TestTrue(FString::Printf(TEXT("%s: eye radius %.3f vs table %.3f"), *Key, Radius, Rig.Radius), FMath::IsNearlyEqual(Radius, static_cast<double>(Rig.Radius), 0.05));
	}
#endif
	return true;
}

#endif

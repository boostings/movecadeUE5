#include "Games/Sumo/SumoPresentation.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY(LogSumo);

namespace MovecadeKart::Sumo::Presentation
{
	namespace
	{
		const TCHAR* const AdditivePath = TEXT("/Game/Tracks/Shared/Materials/M_TrackAdditive.M_TrackAdditive");
	}

	FLinearColor PowerColor(EPower Power)
	{
		// Parsed once: the HUD and the pucks ask every frame.
		static const FLinearColor Heavy = Art::Hex(TEXT("#ffcf5a"));
		static const FLinearColor Shield = Art::Hex(TEXT("#7ff6ff"));
		static const FLinearColor Turbo = Art::Hex(TEXT("#ff6be6"));
		switch (Power)
		{
		case EPower::Heavy: return Heavy;
		case EPower::Shield: return Shield;
		case EPower::Turbo: return Turbo;
		default: return FLinearColor::White;
		}
	}

	const TCHAR* PowerCallout(EPower Power)
	{
		switch (Power)
		{
		case EPower::Heavy: return TEXT("HEAVY!");
		case EPower::Shield: return TEXT("SHIELD!");
		case EPower::Turbo: return TEXT("TURBO!");
		default: return TEXT("");
		}
	}

	UMaterialInterface* AdditiveMaster()
	{
		return LoadObject<UMaterialInterface>(nullptr, AdditivePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UMaterialInstanceDynamic* MakeAdditive(UObject* Outer, const FLinearColor& Color, float Opacity)
	{
		UMaterialInterface* Master = AdditiveMaster();
		if (!Master)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Master, Outer);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Mid->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		return Mid;
	}

	void AddAnnulus(FMeshBuilder& Builder, int32 Section, double Inner, double Outer, double Z, int32 Steps, double UnitCm)
	{
		auto At = [Z, UnitCm](double Radius, double Angle) { return FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), Z) * UnitCm; };
		for (int32 i = 0; i < Steps; ++i)
		{
			const double A0 = 2.0 * PI * i / Steps;
			const double A1 = 2.0 * PI * (i + 1) / Steps;
			const int32 a = Builder.Vert(Section, At(Inner, A0), FVector::UpVector, FVector2D(0, 0), FVector2D::ZeroVector);
			const int32 b = Builder.Vert(Section, At(Outer, A0), FVector::UpVector, FVector2D(1, 0), FVector2D::ZeroVector);
			const int32 c = Builder.Vert(Section, At(Outer, A1), FVector::UpVector, FVector2D(1, 1), FVector2D::ZeroVector);
			const int32 d = Builder.Vert(Section, At(Inner, A1), FVector::UpVector, FVector2D(0, 1), FVector2D::ZeroVector);
			Builder.Tri(Section, a, b, c);
			Builder.Tri(Section, a, c, d);
		}
	}
}

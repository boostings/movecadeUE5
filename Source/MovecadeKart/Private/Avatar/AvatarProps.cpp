#include "Avatar/AvatarProps.h"

#include "Art/KartMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace MovecadeKart::Avatar
{
	namespace
	{
		const FName SlotNames[] = { TEXT("main"), TEXT("trim"), TEXT("accent") };

		// The avatar4 head in head-bone space (Tools: the MovecadeKart.Avatar rig dump): the skull's centre sits
		// 29 cm above the bone and 1 cm behind it, the hair's crown about 49 cm up, the head about 33 cm wide.
		const FVector HeadCentre(0.0, 1.0, 29.0);
		const FVector2D UV1(0.0, 0.0);

		/** A thin ring (a band of a crown, a hat's brim) about Axis through Origin: radii R0..R1, height H. */
		void Band(FMeshBuilder& M, FName Slot, const FVector& Origin, const FVector& Axis, double R0, double R1, double H)
		{
			const TArray<FVector2D> Profile = { { R0, 0.0 }, { R1, 0.0 }, { R1, H }, { R0, H }, { R0, 0.0 } };
			M.Lathe(Slot, Profile, 32, Origin, Axis, FVector::OneVector, UV1);
		}

		/** A torus about Axis through Origin (radius R, tube radius T). */
		void Torus(FMeshBuilder& M, FName Slot, const FVector& Origin, const FVector& Axis, double R, double T)
		{
			TArray<FVector2D> Profile;
			for (int32 i = 0; i <= 10; ++i)
			{
				const double A = -HALF_PI - 2.0 * PI * i / 10.0;
				Profile.Add(FVector2D(R + T * FMath::Cos(A), T * FMath::Sin(A)));
			}
			M.Lathe(Slot, Profile, 28, Origin, Axis, FVector::OneVector, UV1);
		}

		UStaticMesh* BuildCrown()
		{
			FMeshBuilder M;
			const FName Gold = SlotNames[0], Gem = SlotNames[2];
			M.Section(Gold);
			M.Section(SlotNames[1]);
			M.Section(Gem);
			// Set on the crown, tipped a little to one side.
			const FQuat Tip(FVector::YAxisVector, FMath::DegreesToRadians(-9.0));
			const FVector Base = FVector(1.5, 1.0, 45.5);
			const FVector Axis = Tip.RotateVector(FVector::UpVector);
			Band(M, Gold, Base, Axis, 9.4, 10.6, 6.0);
			Torus(M, Gold, Base + Axis * 0.4, Axis, 10.5, 0.9);
			for (int32 i = 0; i < 5; ++i)
			{
				const double A = 2.0 * PI * i / 5.0 + HALF_PI;
				const FVector Out = Tip.RotateVector(FVector(FMath::Cos(A), -FMath::Sin(A), 0.0));
				const FVector Foot = Base + Axis * 5.5 + Out * 10.0;
				const TArray<FVector2D> Spike = { { 2.4, 0.0 }, { 1.7, 2.5 }, { 0.6, 6.0 }, { 0.0, 6.6 } };
				M.Lathe(Gold, Spike, 12, Foot, Axis + Out * 0.18, FVector::OneVector, UV1);
				M.Ellipsoid(Gem, Foot + (Axis + Out * 0.18).GetSafeNormal() * 7.2, FVector(1.25), 6, 10, UV1);
			}
			// The front jewel.
			const FVector Front = Tip.RotateVector(FVector(0.0, -1.0, 0.0));
			M.Ellipsoid(Gem, Base + Axis * 3.0 + Front * 10.7, FVector(1.9, 1.0, 1.9), 8, 12, UV1);
			return M.Build(GetTransientPackage(), TEXT("SM_AvatarProp_Crown"));
		}

		UStaticMesh* BuildPartyHat()
		{
			FMeshBuilder M;
			const FName Paper = SlotNames[0], Pom = SlotNames[1], Stripe = SlotNames[2];
			M.Section(Paper);
			M.Section(Pom);
			M.Section(Stripe);
			const FQuat Tip(FVector::YAxisVector, FMath::DegreesToRadians(-14.0));
			const FVector Base(3.0, 1.5, 44.0);
			const FVector Axis = Tip.RotateVector(FVector::UpVector);
			const double Height = 21.0, Radius = 8.2;
			const TArray<FVector2D> Cone = { { 0.0, 0.0 }, { Radius, 0.0 }, { Radius, 0.01 }, { 0.0, Height } };
			M.Lathe(Paper, Cone, 28, Base, Axis, FVector::OneVector, UV1);
			// Two stripes round the cone.
			for (const double At : { 0.3, 0.62 })
			{
				const double R = Radius * (1.0 - At) + 0.15;
				const TArray<FVector2D> Ring = { { R + 0.05, Height * At - 1.0 }, { R - 0.3, Height * At + 1.0 } };
				M.Lathe(Stripe, Ring, 28, Base, Axis, FVector::OneVector, UV1);
			}
			M.Ellipsoid(Pom, Base + Axis * (Height + 1.8), FVector(3.0), 8, 12, UV1);
			Torus(M, Pom, Base + Axis * 0.6, Axis, Radius - 0.2, 1.1);
			return M.Build(GetTransientPackage(), TEXT("SM_AvatarProp_PartyHat"));
		}

		UStaticMesh* BuildHeadphones()
		{
			FMeshBuilder M;
			const FName Shell = SlotNames[0], Cushion = SlotNames[1], Light = SlotNames[2];
			M.Section(Shell);
			M.Section(Cushion);
			M.Section(Light);
			// The band arcs over the crown from ear to ear.
			const FVector Centre = HeadCentre + FVector(0.0, 0.5, -2.0);
			const double R = 21.5;
			FVector Last = FVector::ZeroVector;
			for (int32 i = 0; i <= 14; ++i)
			{
				const double A = FMath::DegreesToRadians(-78.0 + 156.0 * i / 14.0);
				const FVector P = Centre + FVector(FMath::Sin(A) * R * 0.86, 0.0, FMath::Cos(A) * R);
				if (i > 0)
				{
					M.Limb(Shell, Last, P, 1.5, 1.5, 10, UV1);
				}
				Last = P;
			}
			for (const double Side : { -1.0, 1.0 })
			{
				const FVector Ear = HeadCentre + FVector(Side * 16.6, 0.5, -3.5);
				M.Cylinder(Shell, Ear + FVector(Side * 1.2, 0.0, 0.0), Ear + FVector(Side * 5.4, 0.0, 0.0), 6.6, 24, UV1);
				M.Ellipsoid(Cushion, Ear + FVector(Side * 0.9, 0.0, 0.0), FVector(1.8, 6.0, 6.0), 8, 16, UV1);
				// A glowing ring on each cup.
				Torus(M, Light, Ear + FVector(Side * 5.5, 0.0, 0.0), FVector(Side, 0.0, 0.0), 4.2, 0.7);
			}
			return M.Build(GetTransientPackage(), TEXT("SM_AvatarProp_Headphones"));
		}

		UStaticMesh* BuildBeanie()
		{
			FMeshBuilder M;
			const FName Knit = SlotNames[0], Cuff = SlotNames[1], Pom = SlotNames[2];
			M.Section(Knit);
			M.Section(Cuff);
			M.Section(Pom);
			const FVector Rim(0.0, 1.5, 35.0);
			const double R = 18.0, H = 19.5;
			TArray<FVector2D> Dome;
			for (int32 i = 0; i <= 10; ++i)
			{
				const double A = HALF_PI * i / 10.0;
				Dome.Add(FVector2D(R * FMath::Sin(A), H * FMath::Cos(A)));
			}
			M.Lathe(Knit, Dome, 32, Rim, FVector::UpVector, FVector(1.0, 1.06, 1.0), UV1);
			const TArray<FVector2D> Fold = { { R - 0.6, -2.5 }, { R + 1.2, -2.2 }, { R + 1.4, 2.6 }, { R - 0.2, 3.4 } };
			M.Lathe(Cuff, Fold, 32, Rim, FVector::UpVector, FVector(1.0, 1.06, 1.0), UV1);
			M.Ellipsoid(Pom, Rim + FVector(0.0, 0.0, H + 3.2), FVector(5.0), 8, 14, UV1);
			return M.Build(GetTransientPackage(), TEXT("SM_AvatarProp_Beanie"));
		}
	}

	bool IsBuiltProp(EKartAccessory Accessory)
	{
		return Accessory == EKartAccessory::Crown || Accessory == EKartAccessory::PartyHat || Accessory == EKartAccessory::Headphones
			|| Accessory == EKartAccessory::Beanie;
	}

	bool HidesHair(EKartAccessory Accessory)
	{
		return Accessory == EKartAccessory::Cap || Accessory == EKartAccessory::Beanie;
	}

	UStaticMesh* PropMesh(EKartAccessory Accessory)
	{
		if (!IsBuiltProp(Accessory))
		{
			return nullptr;
		}
		// One mesh per kind, kept alive for the session (they are small and shared by every avatar).
		static TStrongObjectPtr<UStaticMesh> Meshes[8];
		TStrongObjectPtr<UStaticMesh>& Slot = Meshes[static_cast<int32>(Accessory) % UE_ARRAY_COUNT(Meshes)];
		if (!Slot.IsValid())
		{
			switch (Accessory)
			{
			case EKartAccessory::Crown: Slot.Reset(BuildCrown()); break;
			case EKartAccessory::PartyHat: Slot.Reset(BuildPartyHat()); break;
			case EKartAccessory::Headphones: Slot.Reset(BuildHeadphones()); break;
			default: Slot.Reset(BuildBeanie()); break;
			}
		}
		return Slot.Get();
	}

	FLinearColor PropColour(EKartAccessory Accessory, EPropSlot Slot, const FKartLook& Look)
	{
		const FLinearColor Jersey = Look.JerseyColor();
		const FLinearColor White = FLinearColor::FromSRGBColor(FColor(0xff, 0xf6, 0xf2));
		switch (Accessory)
		{
		case EKartAccessory::Crown:
			return Slot == EPropSlot::Accent ? FLinearColor::FromSRGBColor(FColor(0xe8, 0x39, 0x4d)) : FLinearColor(1.f, 0.62f, 0.1f);
		case EKartAccessory::PartyHat:
			return Slot == EPropSlot::Main ? Jersey : (Slot == EPropSlot::Accent ? FLinearColor::FromSRGBColor(FColor(0xf7, 0xc9, 0x48)) : White);
		case EKartAccessory::Headphones:
			return Slot == EPropSlot::Main ? FLinearColor::FromSRGBColor(FColor(0x26, 0x26, 0x30)) : (Slot == EPropSlot::Trim ? Jersey * 0.8f : Jersey);
		case EKartAccessory::Beanie:
			return Slot == EPropSlot::Trim ? Jersey * 0.55f : (Slot == EPropSlot::Accent ? White : Jersey);
		default:
			return Jersey;
		}
	}

	bool PropIsCloth(EKartAccessory Accessory, EPropSlot Slot)
	{
		return Accessory == EKartAccessory::Beanie || (Accessory == EKartAccessory::PartyHat && Slot == EPropSlot::Trim)
			|| (Accessory == EKartAccessory::Headphones && Slot == EPropSlot::Trim);
	}
}

#include "StackerBlockMesh.h"

#include "Art/KartMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "StackerScene.h"

namespace MovecadeKart::Stacker
{
	namespace
	{
		/** The corner radius (cm) and how many segments each quarter arc gets. */
		constexpr double CornerRadius = 11.0;
		constexpr int32 ArcSteps = 4;
		/** The trim stands this far (cm) proud of each broad face. */
		constexpr double FaceInset = 0.5;
		constexpr int32 RibCount = 5;

		/** One axis of the rounded box: the flat run and the quarter arcs at each end, as coordinates. */
		TArray<double> RoundedAxis(double Half, double Radius, int32 Steps)
		{
			TArray<double> Out;
			for (int32 i = 0; i <= Steps; ++i)
			{
				Out.Add(-(Half - Radius) - Radius * FMath::Cos(HALF_PI * i / Steps));
			}
			for (int32 i = Steps; i >= 0; --i)
			{
				Out.Add((Half - Radius) + Radius * FMath::Cos(HALF_PI * i / Steps));
			}
			return Out;
		}

		/** The rounded panel: each of the six faces as a grid over the two rounded axes, pushed out onto the corners. */
		void AddPanel(FMeshBuilder& B, int32 Panel, const FVector& Half)
		{
			const TArray<double> Axes[3] = { RoundedAxis(Half.X, CornerRadius, ArcSteps), RoundedAxis(Half.Y, CornerRadius, ArcSteps),
				RoundedAxis(Half.Z, CornerRadius, ArcSteps) };
			const FVector Inner = Half - FVector(CornerRadius);
			auto Round = [&Inner](const FVector& P, FVector& OutNormal)
			{
				const FVector Q(FMath::Clamp(P.X, -Inner.X, Inner.X), FMath::Clamp(P.Y, -Inner.Y, Inner.Y), FMath::Clamp(P.Z, -Inner.Z, Inner.Z));
				OutNormal = (P - Q).GetSafeNormal();
				return Q + OutNormal * CornerRadius;
			};
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
				const TArray<double>& Us = Axes[U];
				const TArray<double>& Vs = Axes[V];
				for (const double Side : { -1.0, 1.0 })
				{
					TArray<int32> Index;
					for (int32 j = 0; j < Vs.Num(); ++j)
					{
						for (int32 i = 0; i < Us.Num(); ++i)
						{
							FVector P;
							P[Axis] = Side * Half[Axis];
							P[U] = Us[i];
							P[V] = Vs[j];
							FVector Normal;
							const FVector Out = Round(P, Normal);
							const FVector2D Uv(Us[i] / (2.0 * Half[U]) + 0.5, Vs[j] / (2.0 * Half[V]) + 0.5);
							Index.Add(B.Vert(Panel, Out, Normal, Uv, FVector2D::ZeroVector));
						}
					}
					for (int32 j = 0; j + 1 < Vs.Num(); ++j)
					{
						for (int32 i = 0; i + 1 < Us.Num(); ++i)
						{
							const int32 A = Index[j * Us.Num() + i], Bv = Index[j * Us.Num() + i + 1];
							const int32 C = Index[(j + 1) * Us.Num() + i + 1], D = Index[(j + 1) * Us.Num() + i];
							B.Tri(Panel, A, Bv, C);
							B.Tri(Panel, A, C, D);
						}
					}
				}
			}
		}

		/** One broad face (toward or away from the camera): the frame, the ribs, the bolts and the lit top edge. */
		void AddFaceTrim(FMeshBuilder& B, const FVector& Half, double Side)
		{
			const double X = Side * (Half.X + FaceInset);
			const double FlatY = Half.Y - CornerRadius + 4.0, FlatZ = Half.Z - 3.0;
			// Chunky enough to stay crisp at a distance (fine trim shimmers under TSR at a quarter of the screen).
			B.Box(TEXT("Trim"), FVector(X, 0.0, FlatZ - 1.6), FVector(0.8, FlatY, 1.6), FVector2D::ZeroVector);
			B.Box(TEXT("Trim"), FVector(X, 0.0, -FlatZ + 1.6), FVector(0.8, FlatY, 1.6), FVector2D::ZeroVector);
			B.Box(TEXT("Trim"), FVector(X, FlatY - 1.8, 0.0), FVector(0.8, 1.8, FlatZ), FVector2D::ZeroVector);
			B.Box(TEXT("Trim"), FVector(X, -FlatY + 1.8, 0.0), FVector(0.8, 1.8, FlatZ), FVector2D::ZeroVector);
			for (int32 k = 0; k < RibCount; ++k)
			{
				const double Y = FMath::Lerp(-FlatY + 11.0, FlatY - 11.0, k / static_cast<double>(RibCount - 1));
				B.Box(TEXT("Trim"), FVector(X, Y, 0.0), FVector(0.55, 1.6, FlatZ - 5.5), FVector2D::ZeroVector);
				B.Box(TEXT("Light"), FVector(X, Y + 2.4, 0.0), FVector(0.45, 0.8, FlatZ - 5.5), FVector2D::ZeroVector);
			}
			for (const double By : { -1.0, 1.0 })
			{
				for (const double Bz : { -1.0, 1.0 })
				{
					const FVector At(X, By * (FlatY - 6.0), Bz * (FlatZ - 6.0));
					B.Cylinder(TEXT("Trim"), At, At + FVector(Side * 1.6, 0.0, 0.0), 2.4, 10, FVector2D::ZeroVector);
				}
			}
			B.Box(TEXT("Light"), FVector(X, 0.0, FlatZ + 1.4), FVector(0.4, FlatY - 2.0, 0.9), FVector2D::ZeroVector);
		}
	}

	UStaticMesh* BuildBlockMesh(UObject* Outer)
	{
		const FVector& Size = Scene::BlockSize;
		// Half extents in UE cm: X deep (web z), Y across (web x), Z up (web y).
		const FVector Half(Size.Z * 50.0, Size.X * 50.0, Size.Y * 50.0);
		FMeshBuilder B;
		// Slot order is the order of the first Section() calls.
		const int32 Panel = B.Section(TEXT("Panel"));
		B.Section(TEXT("Trim"));
		B.Section(TEXT("Light"));
		AddPanel(B, Panel, Half);
		for (const double Side : { -1.0, 1.0 })
		{
			AddFaceTrim(B, Half, Side);
		}
		return B.Build(Outer, TEXT("StackerBlock"));
	}
}

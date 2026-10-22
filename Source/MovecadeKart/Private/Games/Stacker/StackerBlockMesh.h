#pragma once

#include "CoreMinimal.h"

class UObject;
class UStaticMesh;

namespace MovecadeKart::Stacker
{
	/**
	 * blockSurface.ts as geometry: Scene::BlockSize as a rounded panel (UE cm: X deep, Y wide, Z tall) with a frame,
	 * five ribs and four corner bolts on its two broad faces in a darker trim, and a light edge along the top.
	 * Material slots: 0 the panel (the lane's paint), 1 the trim, 2 the highlight. Built once per game; Outer owns it.
	 */
	UStaticMesh* BuildBlockMesh(UObject* Outer);
}

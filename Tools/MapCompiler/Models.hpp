/*
 * File:        Models.hpp
 * Author:      emd22
 * Created:     07/11/2026
 * Description: Reads the static glTF/.rxmodel models of a prototype into world space triangles for the bake. Models
 *              stay engine objects; the compiler only uses them as shadow casters and bounce surfaces.
 */

#pragma once

#include "MapSource.hpp"

#include <Asset/Loader/Object/ModelGeometry.hpp>
#include <Math/Vec2.hpp>

namespace fx::mapc {

struct ModelVertex
{
	DVec3 Position = DVec3(0.0);
	DVec3 Normal = DVec3(0.0);
	FVec2 Texcoord = FVec2(0.0f, 0.0f);
};

struct ModelSurface
{
	uint32 SourceIndex = 0;
	/// Index into ModelBuilder::Materials
	uint32 MaterialIndex = 0;

	Array<ModelVertex> Vertices;
	Array<int32> Indices;
};

struct ModelStats
{
	uint32 ModelCount = 0;
	uint32 BakedCount = 0;
	uint32 FailedCount = 0;
	uint32 SkippedMeshCount = 0;
	uint32 BlendMeshCount = 0;
	uint32 Triangles = 0;
	uint32 ProbeBoxes = 0;
};

class ModelBuilder
{
public:
	/// Reads every model that is baked or keeps probes out, and adds the probe boxes to `source`
	void Build(MapSource& source);

	uint32 GetMaterialCount() const { return mMaterialCount; }

	/// Finds the material of a global material index
	const loader::StaticModelMaterial& GetMaterial(uint32 index) const;

public:
	Array<ModelSurface> Surfaces;

	ModelStats Stats;

	/// One message per model that could not be read
	SizedArray<String> Errors;

private:
	SizedArray<loader::StaticModel> mModels;
	/// First global material index of each model
	Array<uint32> mMaterialBases;
	uint32 mMaterialCount = 0;
};

} // namespace fx::mapc

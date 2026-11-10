#include "Models.hpp"

#include <Renderer/ProbePlacement.hpp>

namespace fx::mapc {

void ModelBuilder::Build(MapSource& source)
{
	mModels.InitSize(source.Models.Size);
	Errors.InitCapacity(source.Models.Size);

	SizedArray<ProbePlacementBoxes::Plane> no_planes;

	for (uint32 model_index = 0; model_index < source.Models.Size; model_index++) {
		const SourceModel& source_model = source.Models[model_index];
		loader::StaticModel& model = mModels[model_index];

		mMaterialBases.Emplace(mMaterialCount);

		if (!source_model.bBaked && !source_model.bProbeBox) {
			continue;
		}

		Stats.ModelCount++;

		String error;

		if (!loader::ReadStaticModel(source_model.MeshPath, model, error)) {
			Stats.FailedCount++;
			Errors.Emplace(String::Fmt("model '{}': {}", source_model.Name, error));
			continue;
		}

		mMaterialCount += model.Materials.Size;
		Stats.SkippedMeshCount += model.SkippedMeshCount;

		if (source_model.bBaked) {
			Stats.BakedCount++;
		}

		const float32* matrix = source_model.Transform.RawData;

		for (loader::StaticModelMesh& mesh : model.Meshes) {
			const loader::StaticModelMaterial& material = model.Materials[mesh.Material];

			if (material.bBlend) {
				Stats.BlendMeshCount++;
				continue;
			}

			if (source_model.bProbeBox && mesh.Positions.Size >= 3) {
				const FVec3 first(mesh.Positions[0], mesh.Positions[1], mesh.Positions[2]);
				BBox bounds(first, first);

				for (size_t i = 3; i + 2 < mesh.Positions.Size; i += 3) {
					const FVec3 point(mesh.Positions[i], mesh.Positions[i + 1], mesh.Positions[i + 2]);

					bounds.Min = FVec3::Min(bounds.Min, point);
					bounds.Max = FVec3::Max(bounds.Max, point);
				}

				if (ProbePlacement::AddBox(source.ProbeBoxes, bounds, source_model.Transform, no_planes)) {
					Stats.ProbeBoxes++;
				}
			}

			if (!source_model.bBaked) {
				continue;
			}

			SizedArray<float32> no_tangents;
			loader::TransformModelGeometry(mesh.Positions, mesh.Normals, no_tangents, mesh.Indices, matrix);

			const uint32 vertex_count = static_cast<uint32>(mesh.Positions.Size / 3);
			const bool has_uvs = mesh.Uvs.Size == static_cast<size_t>(vertex_count) * 2;

			ModelSurface& surface = Surfaces.Emplace();
			surface.SourceIndex = model_index;
			surface.MaterialIndex = mMaterialBases[model_index] + mesh.Material;
			surface.Vertices.SetPageSize(vertex_count + 1);
			surface.Indices.SetPageSize(static_cast<uint32>(mesh.Indices.Size) + 1);

			for (uint32 vertex = 0; vertex < vertex_count; vertex++) {
				ModelVertex& out = surface.Vertices.Emplace();
				out.Position = DVec3(mesh.Positions[vertex * 3], mesh.Positions[vertex * 3 + 1],
									 mesh.Positions[vertex * 3 + 2]);
				out.Normal = DVec3(mesh.Normals[vertex * 3], mesh.Normals[vertex * 3 + 1],
								   mesh.Normals[vertex * 3 + 2]);

				if (has_uvs) {
					out.Texcoord = FVec2(mesh.Uvs[vertex * 2], mesh.Uvs[vertex * 2 + 1]);
				}
			}

			for (const uint32 index : mesh.Indices) {
				surface.Indices.Emplace(static_cast<int32>(index));
			}

			Stats.Triangles += static_cast<uint32>(mesh.Indices.Size / 3);
		}

		// The geometry now lives in the surfaces, only the materials are needed from here on
		model.Meshes = DynArray<loader::StaticModelMesh, GrowthFunctions::Double> {};
	}
}

const loader::StaticModelMaterial& ModelBuilder::GetMaterial(uint32 index) const
{
	uint32 model_index = 0;

	while (model_index + 1 < mMaterialBases.Size && mMaterialBases[model_index + 1] <= index) {
		model_index++;
	}

	return mModels[model_index].Materials[index - mMaterialBases[model_index]];
}

} // namespace fx::mapc

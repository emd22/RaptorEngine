#include "Blockout.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Material/Material.hpp>
#include <Material/MaterialManager.hpp>
#include <Renderer/PipelineNames.hpp>
#include <World.hpp>

namespace fx {

Blockout::Blockout() {}

void Blockout::Create(World* world)
{
	BlockoutObjects.Create(64);

	pWorld = world;

	mBlockoutMaterialID = gMaterialManager->NewMaterial("TestMat", renderer::ePipelineName::Geometry, false);
	Material* test_material = gMaterialManager->GetMaterial(mBlockoutMaterialID);

	AssetTicket diffuse = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_UNorm,
												   "Data/Demo/Textures/white_grid.png", eImageCreateFlags::None);

	test_material->Attach(Material::eResourceType::Diffuse, diffuse);
	test_material->Finalize();
}

void Blockout::Load(const String& path)
{
	ConfigFile info {};
	info.Load(path.CStr());


	ConfigEntry* blocks_entry = info.GetEntry(HashStr32("all"));

	for (ConfigEntry& entry : blocks_entry->Members) {
		Vec3f position = entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);

		String blockout_id = String::Fmt("Blockout_{}", entry.Name.Get());

		const PagedArray<ConfigValue>& scales = entry.GetMember(HashStr32("scale"))->GetArrayData();

		LogInfo("Creating block id {}", blockout_id);

		if (scales.Size() < 6) {
			LogInfo("Not enough scales! {}", scales.Size());
			continue;
		}


		Ref<MeshGen::GeneratedMesh> cube_mesh = MeshGen::MakeCube({
			.Left = { .Scale = scales[0].Get<float32>() },
			.Right = { .Scale = scales[1].Get<float32>() },
			.Top = { .Scale = scales[2].Get<float32>() },
			.Bottom = { .Scale = scales[3].Get<float32>() },
			.Front = { .Scale = scales[4].Get<float32>() },
			.Back = { .Scale = scales[5].Get<float32>() },
		});


		Object* existing_blockout = gObjectManager->FindObject(HashStr32(blockout_id.CStr()));

		// If the blockout block already exists, remove and recreate it
		if (existing_blockout != nullptr) {
			pWorld->Detach(existing_blockout->ID);
			gObjectManager->DestroyObject(existing_blockout->ID);
		}

		Object* object = gObjectManager->NewObject(blockout_id.Str());
		object->pMesh = cube_mesh->AsDefaultMesh();
		object->MoveBy(position);
		object->mMaterialID = mBlockoutMaterialID;

		AssetTicket ticket(static_cast<void*>(object));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);
	}
}


Blockout::~Blockout() {}


} // namespace fx

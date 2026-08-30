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
												   "Data/Demo/Textures/gray_check.png", eImageCreateFlags::None);

	test_material->Attach(Material::eResourceType::Diffuse, diffuse);
	test_material->Finalize();
}

static void RemoveBlockoutFromWorld(World* world)
{
	SizedArray<Object*> objects = gObjectManager->CollectWithTags(eObjectTag::Blockout);

	for (Object* object : objects) {
		world->Detach(object->ID);
		gObjectManager->DestroyObject(object->ID);
	}
}


void Blockout::Load(const String& path)
{
	ConfigFile info {};
	info.Load(path.CStr());

	ConfigEntry* blocks_entry = info.GetEntry(HashStr32("all"));

	// Remove the current blockout from the world
	RemoveBlockoutFromWorld(pWorld);

	for (ConfigEntry& entry : blocks_entry->Members) {
		Vec3f position = entry.GetMemberValue<Vec3f>(HashStr32("pos"), Vec3f::sZero);

		String blockout_id = String::Fmt("PROTO_{}", entry.Name.Get());

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

			.bAlignUVs = true,
		});


		Object* object = gObjectManager->NewObject(blockout_id.Str(), eObjectTag::Blockout);
		object->pMesh = cube_mesh->AsDefaultMesh();
		object->MoveBy(position);
		object->mMaterialID = mBlockoutMaterialID;
		object->SetShadowCaster(true);

		AssetTicket ticket(static_cast<void*>(object));
		ticket.MarkAndSignalLoaded();

		pWorld->Attach(ticket);
	}
}


Blockout::~Blockout() {}


} // namespace fx

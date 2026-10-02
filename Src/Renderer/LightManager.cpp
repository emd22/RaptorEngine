#include "LightManager.hpp"

#include <Engine.hpp>
#include <Renderer/Light.hpp>
#include <World.hpp>
#include <WorldGrid.hpp>

namespace fx {

const LightID LightID::scNull = LightID(UINT32_MAX);

void LightManager::Create() { mLightList.Init(scMaxLights); }

void LightManager::RegisterLight(LightBase* light)
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 index;
	LightBase** slot = mLightList.NewItem(&index, light);

	if (slot == nullptr) {
		LogError(LC_RENDER, "Cannot register light '{}', the light list is full ({} max)", light->Name.Get(),
				 scMaxLights);
		return;
	}

	light->ID = LightID(index);
	light->OnAttached(gWorld);

	gWorldGrid->AddLight(light);
}

LightBase* LightManager::GetLight(LightID id)
{
	if (id.IsInvalid() || id.IsNull()) {
		return nullptr;
	}

	std::lock_guard<std::mutex> guard(mInUse);

	LightBase** slot = mLightList.GetItem(id.GetID());

	if (slot == nullptr) {
		return nullptr;
	}

	return (*slot);
}

void LightManager::DestroyLight(LightID& id)
{
	std::lock_guard<std::mutex> guard(mInUse);

	if (id.IsInvalid() || id.IsNull()) {
		return;
	}

	// Same sort of thing as ObjectManager
	LightID id_copy = id;

	LightBase** slot = mLightList.GetItem(id.GetID());
	if (slot != nullptr) {
		gWorldGrid->RemoveLight(*slot);

		if (gWorld != nullptr) {
			gWorld->mLightList.InvalidateLight(id);
		}

		(*slot)->ID.Invalidate();
		// Gross
		delete (*slot);
	}

	mLightList.FreeItem(id_copy.GetID());

	id.Invalidate();
}

LightBase* LightManager::FindLight(Hash32 name_hash)
{
	std::lock_guard<std::mutex> guard(mInUse);

	const uint32 capacity = mLightList.Capacity;

	for (uint32 i = 0; i < capacity; i++) {
		if (!mLightList.SlotsInUse.Get(i)) {
			continue;
		}

		LightBase** slot = mLightList.GetItem(i);

		if (slot != nullptr && (*slot)->Name == name_hash) {
			return *slot;
		}
	}

	return static_cast<LightBase*>(nullptr);
}

LightDirectional* LightManager::GetDirectionalLight()
{
	std::lock_guard<std::mutex> guard(mInUse);

	const uint32 capacity = mLightList.Capacity;

	for (uint32 i = 0; i < capacity; i++) {
		if (!mLightList.SlotsInUse.Get(i)) {
			continue;
		}

		LightBase** slot = mLightList.GetItem(i);

		if (slot != nullptr && (*slot)->Type == eLightType::Directional) {
			return static_cast<LightDirectional*>(*slot);
		}
	}

	return static_cast<LightDirectional*>(nullptr);
}

void LightManager::Destroy() { mLightList.Free(); }

} // namespace fx

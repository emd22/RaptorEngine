#include "ObjectScripts.hpp"

#include <Blockout.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Script/ScriptManager.hpp>
#include <World.hpp>
#include <algorithm>

namespace fx {

ObjectScriptManager::Entry* ObjectScriptManager::FindEntry(ObjectID id)
{
	const auto found = std::find_if(mEntries.begin(), mEntries.end(),
									[&](const Entry& entry) { return entry.ID.GetID() == id.GetID(); });

	return (found != mEntries.end()) ? &(*found) : nullptr;
}

const ObjectScriptManager::Entry* ObjectScriptManager::FindEntry(ObjectID id) const
{
	return const_cast<ObjectScriptManager*>(this)->FindEntry(id);
}

void ObjectScriptManager::BindFunctions(Entry& entry)
{
	entry.pFnStart = entry.pScript->GetFunction<void(Object*)>("object_start");
	entry.pFnUpdate = entry.pScript->GetFunction<void(Object*, float32)>("object_update");
	entry.pFnStop = entry.pScript->GetFunction<void(Object*)>("object_stop");
	entry.pFnTriggerEnter = entry.pScript->GetFunction<void(Object*)>("object_trigger_enter");
	entry.pFnTriggerExit = entry.pScript->GetFunction<void(Object*)>("object_trigger_exit");
}

Vec3f ObjectScriptManager::GetCrossingDirection(const Entry& entry, const Vec3f point)
{
	static constexpr float32 scMinCrossingDistance = 1e-4f;

	if (!entry.bHasLastPoint) {
		return Vec3f::sZero;
	}

	const Vec3f movement = point - entry.LastPoint;

	if (movement.Length() < scMinCrossingDistance) {
		return Vec3f::sZero;
	}

	return movement.Normalize();
}

void ObjectScriptManager::LeaveTrigger(Entry& entry, Object* object, const Vec3f direction)
{
	if (!entry.bTriggerInside) {
		return;
	}

	entry.bTriggerInside = false;
	entry.ExitDirection = direction;

	if (entry.pFnTriggerExit != nullptr) {
		entry.pScript->CallFunctionPtr<void(Object*)>(entry.pFnTriggerExit, object);
	}
}

bool ObjectScriptManager::PassesEnterGate(const Entry& entry, Object* object, const Vec3f direction) const
{
	Vec3f local_direction;

	if (!TryGetRequiredEnterDirection(entry.ID, local_direction)) {
		return true;
	}

	if (direction.IsZero()) {
		return false;
	}

	const Vec4f world = object->GetWorldMatrix() * Vec4f(local_direction.X, local_direction.Y, local_direction.Z, 0.0f);

	return direction.Dot(Vec3f(world.X, world.Y, world.Z)) > 0.0f;
}

void ObjectScriptManager::UpdateTrigger(Entry& entry, Object* object, const Vec3f point)
{
	if (!object->IsTrigger() || gWorld->pBlockout == nullptr) {
		entry.bWasInside = false;
		LeaveTrigger(entry, object);
		return;
	}

	const bool inside = gWorld->pBlockout->ContainsPoint(object, point);

	if (inside == entry.bWasInside) {
		return;
	}

	entry.bWasInside = inside;

	const Vec3f direction = GetCrossingDirection(entry, point);

	if (!inside) {
		LeaveTrigger(entry, object, direction);
		return;
	}

	if (!PassesEnterGate(entry, object, direction)) {
		return;
	}

	entry.bTriggerInside = true;
	entry.EnterDirection = direction;

	if (entry.pFnTriggerEnter != nullptr) {
		entry.pScript->CallFunctionPtr<void(Object*)>(entry.pFnTriggerEnter, object);
	}
}

void ObjectScriptManager::FreeEntry(Entry& entry)
{
	if (entry.pScript != nullptr && gScriptManager != nullptr) {
		gScriptManager->FreeScript(entry.pScript);
	}

	entry.pScript = nullptr;
}

bool ObjectScriptManager::Attach(Object* object, String path)
{
	if (object == nullptr) {
		return false;
	}

	if (path.IsEmpty()) {
		Detach(object->ID);
		return true;
	}

	Entry* existing = FindEntry(object->ID);

	if (existing != nullptr) {
		if (existing->Path == path) {
			return !existing->pScript->HasErrors();
		}

		FreeEntry(*existing);
	}

	script::Script* script = gScriptManager->LoadScript(path);

	if (script == nullptr) {
		LogError(LC_SCRIPT, "Could not attach '{}' to object '{}', out of script slots", path, object->Name.Get());

		if (existing != nullptr) {
			mEntries.erase(mEntries.begin() + (existing - mEntries.data()));
		}

		return false;
	}

	if (existing == nullptr) {
		mEntries.push_back(Entry { .ID = object->ID });
		existing = &mEntries.back();
	}

	existing->Path = path;
	existing->pScript = script;
	existing->bStarted = false;

	BindFunctions(*existing);

	return !script->HasErrors();
}

void ObjectScriptManager::Detach(ObjectID id)
{
	Entry* entry = FindEntry(id);

	if (entry == nullptr) {
		return;
	}

	FreeEntry(*entry);
	mEntries.erase(mEntries.begin() + (entry - mEntries.data()));
}

bool ObjectScriptManager::HasScript(ObjectID id) const { return FindEntry(id) != nullptr; }

bool ObjectScriptManager::HasErrors(ObjectID id) const
{
	const Entry* entry = FindEntry(id);

	return entry != nullptr && (entry->pScript == nullptr || entry->pScript->HasErrors());
}

const String& ObjectScriptManager::GetPath(ObjectID id) const
{
	static const String scNoPath = String();

	const Entry* entry = FindEntry(id);

	return (entry != nullptr) ? entry->Path : scNoPath;
}

Vec3f ObjectScriptManager::GetEnterDirection(ObjectID id) const
{
	const Entry* entry = FindEntry(id);

	return (entry != nullptr) ? entry->EnterDirection : Vec3f::sZero;
}

Vec3f ObjectScriptManager::GetExitDirection(ObjectID id) const
{
	const Entry* entry = FindEntry(id);

	return (entry != nullptr) ? entry->ExitDirection : Vec3f::sZero;
}

void ObjectScriptManager::SetRequiredEnterDirection(ObjectID id, const Vec3f local_direction)
{
	static constexpr float32 scMinDirectionLength = 1e-4f;

	const bool has_direction = local_direction.Length() > scMinDirectionLength;

	for (size_t i = 0; i < mEnterGates.size(); ++i) {
		if (mEnterGates[i].ID.GetID() != id.GetID()) {
			continue;
		}

		if (has_direction) {
			mEnterGates[i].LocalDirection = local_direction.Normalize();
		}
		else {
			mEnterGates.erase(mEnterGates.begin() + i);
		}

		return;
	}

	if (has_direction) {
		mEnterGates.push_back(EnterGate { .ID = id, .LocalDirection = local_direction.Normalize() });
	}
}

bool ObjectScriptManager::TryGetRequiredEnterDirection(ObjectID id, Vec3f& out_local_direction) const
{
	for (const EnterGate& gate : mEnterGates) {
		if (gate.ID.GetID() == id.GetID()) {
			out_local_direction = gate.LocalDirection;
			return true;
		}
	}

	return false;
}

void ObjectScriptManager::ClearRequiredEnterDirection(ObjectID id) { SetRequiredEnterDirection(id, Vec3f::sZero); }

void ObjectScriptManager::Update(float32 delta_time, const Vec3f trigger_point)
{
	for (size_t i = 0; i < mEntries.size(); ++i) {
		Entry& entry = mEntries[i];

		if (entry.pScript == nullptr || entry.pScript->HasErrors()) {
			continue;
		}

		Object* object = gObjectManager->GetObject(entry.ID);

		if (object == nullptr) {
			continue;
		}

		if (!entry.bStarted) {
			entry.bStarted = true;

			if (entry.pFnStart != nullptr) {
				entry.pScript->CallFunctionPtr<void(Object*)>(entry.pFnStart, object);
			}
		}

		UpdateTrigger(entry, object, trigger_point);

		entry.LastPoint = trigger_point;
		entry.bHasLastPoint = true;

		if (entry.pFnUpdate != nullptr) {
			entry.pScript->CallFunctionPtr<void(Object*, float32)>(entry.pFnUpdate, object, delta_time);
		}
	}
}

void ObjectScriptManager::Stop()
{
	for (size_t i = 0; i < mEntries.size(); ++i) {
		Entry& entry = mEntries[i];

		if (!entry.bStarted) {
			continue;
		}

		entry.bStarted = false;
		entry.bHasLastPoint = false;
		entry.bWasInside = false;

		Object* object = gObjectManager->GetObject(entry.ID);

		if (object == nullptr) {
			entry.bTriggerInside = false;
			continue;
		}

		LeaveTrigger(entry, object);

		if (entry.pFnStop != nullptr && !entry.pScript->HasErrors()) {
			entry.pScript->CallFunctionPtr<void(Object*)>(entry.pFnStop, object);
		}
	}
}

void ObjectScriptManager::OnScriptsReloaded()
{
	for (Entry& entry : mEntries) {
		entry.bStarted = false;
		entry.bWasInside = false;
		entry.bTriggerInside = false;
		entry.bHasLastPoint = false;

		if (entry.pScript != nullptr) {
			BindFunctions(entry);
		}
	}
}

void ObjectScriptManager::Clear()
{
	for (Entry& entry : mEntries) {
		FreeEntry(entry);
	}

	mEntries.clear();
	mEnterGates.clear();
}

} // namespace fx

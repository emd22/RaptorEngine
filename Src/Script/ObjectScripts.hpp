#pragma once

#include "Script.hpp"

#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <vector>

namespace fx {

class Object;

class ObjectScriptManager
{
public:
	using FnStart = script::ScriptFunctionType<void(Object*)>;
	using FnUpdate = script::ScriptFunctionType<void(Object*, float32)>;
	using FnStop = script::ScriptFunctionType<void(Object*)>;
	using FnTrigger = script::ScriptFunctionType<void(Object*)>;

	struct Entry
	{
		ObjectID ID;
		String Path;
		script::Script* pScript = nullptr;
		FnStart pFnStart = nullptr;
		FnUpdate pFnUpdate = nullptr;
		FnStop pFnStop = nullptr;
		FnTrigger pFnTriggerEnter = nullptr;
		FnTrigger pFnTriggerExit = nullptr;
		Vec3f LastPoint = Vec3f::sZero;
		Vec3f EnterDirection = Vec3f::sZero;
		Vec3f ExitDirection = Vec3f::sZero;
		bool bHasLastPoint = false;
		bool bStarted = false;
		bool bWasInside = false;
		bool bTriggerInside = false;
	};

	struct EnterGate
	{
		ObjectID ID;
		Vec3f LocalDirection = Vec3f::sZero;
	};

public:
	ObjectScriptManager() = default;
	ObjectScriptManager(const ObjectScriptManager&) = delete;
	ObjectScriptManager& operator=(const ObjectScriptManager&) = delete;

	bool Attach(Object* object, String path);
	void Detach(ObjectID id);

	bool HasScript(ObjectID id) const;
	bool HasErrors(ObjectID id) const;
	const String& GetPath(ObjectID id) const;

	Vec3f GetEnterDirection(ObjectID id) const;
	Vec3f GetExitDirection(ObjectID id) const;

	void SetRequiredEnterDirection(ObjectID id, const Vec3f local_direction);
	bool TryGetRequiredEnterDirection(ObjectID id, Vec3f& out_local_direction) const;
	void ClearRequiredEnterDirection(ObjectID id);

	void Update(float32 delta_time, const Vec3f trigger_point);
	void Stop();

	void OnScriptsReloaded();

	void Clear();

	~ObjectScriptManager() { Clear(); }

private:
	Entry* FindEntry(ObjectID id);
	const Entry* FindEntry(ObjectID id) const;

	static void BindFunctions(Entry& entry);
	void UpdateTrigger(Entry& entry, Object* object, const Vec3f point);
	bool PassesEnterGate(const Entry& entry, Object* object, const Vec3f direction) const;
	static Vec3f GetCrossingDirection(const Entry& entry, const Vec3f point);
	static void LeaveTrigger(Entry& entry, Object* object, const Vec3f direction = Vec3f::sZero);
	static void FreeEntry(Entry& entry);

private:
	std::vector<Entry> mEntries;
	std::vector<EnterGate> mEnterGates;
};

} // namespace fx

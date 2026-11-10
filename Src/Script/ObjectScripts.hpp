#pragma once

#include "Script.hpp"

#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/FVec3.hpp>
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
		FVec3 LastPoint = FVec3::sZero;
		FVec3 EnterDirection = FVec3::sZero;
		FVec3 ExitDirection = FVec3::sZero;
		bool bHasLastPoint = false;
		bool bStarted = false;
		bool bWasInside = false;
		bool bTriggerInside = false;
	};

	struct EnterGate
	{
		ObjectID ID;
		FVec3 LocalDirection = FVec3::sZero;
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

	FVec3 GetEnterDirection(ObjectID id) const;
	FVec3 GetExitDirection(ObjectID id) const;

	void SetRequiredEnterDirection(ObjectID id, const FVec3 local_direction);
	bool TryGetRequiredEnterDirection(ObjectID id, FVec3& out_local_direction) const;
	void ClearRequiredEnterDirection(ObjectID id);

	void Update(float32 delta_time, const FVec3 trigger_point);
	void Stop();

	void OnScriptsReloaded();

	void Clear();

	~ObjectScriptManager() { Clear(); }

private:
	Entry* FindEntry(ObjectID id);
	const Entry* FindEntry(ObjectID id) const;

	static void BindFunctions(Entry& entry);
	void UpdateTrigger(Entry& entry, Object* object, const FVec3 point);
	bool PassesEnterGate(const Entry& entry, Object* object, const FVec3 direction) const;
	static FVec3 GetCrossingDirection(const Entry& entry, const FVec3 point);
	static void LeaveTrigger(Entry& entry, Object* object, const FVec3 direction = FVec3::sZero);
	static void FreeEntry(Entry& entry);

private:
	std::vector<Entry> mEntries;
	std::vector<EnterGate> mEnterGates;
};

} // namespace fx

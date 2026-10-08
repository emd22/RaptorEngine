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
		bool bStarted = false;
		bool bTriggerInside = false;
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

	void Update(float32 delta_time, const Vec3f trigger_point);
	void Stop();

	void OnScriptsReloaded();

	void Clear();

	~ObjectScriptManager() { Clear(); }

private:
	Entry* FindEntry(ObjectID id);
	const Entry* FindEntry(ObjectID id) const;

	static void BindFunctions(Entry& entry);
	static void UpdateTrigger(Entry& entry, Object* object, const Vec3f point);
	static void LeaveTrigger(Entry& entry, Object* object);
	static void FreeEntry(Entry& entry);

private:
	std::vector<Entry> mEntries;
};

} // namespace fx

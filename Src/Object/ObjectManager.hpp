#pragma once

#include "Object.hpp"
#include "ObjectID.hpp"

#include <Core/Bitset.hpp>
#include <Core/FreeArray.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <vector>

namespace fx {

struct alignas(16) ObjectGpuEntry
{
	float ModelMatrix[16];
};

class Mat4f;

// using ObjectId = uint32;


class LoadedObjectRange
{
public:
	class Iterator
	{
	public:
		Iterator(FreeArray<Object>::Iterator current, FreeArray<Object>::Iterator end) : mCurrent(current), mEnd(end)
		{
			SkipLoading();
		}

		Object& operator*() const { return *mCurrent; }
		Object* operator->() const { return &(*mCurrent); }

		Iterator& operator++()
		{
			++mCurrent;
			SkipLoading();
			return *this;
		}

		bool operator==(const Iterator& other) const { return mCurrent == other.mCurrent; }
		bool operator!=(const Iterator& other) const { return mCurrent != other.mCurrent; }

	private:
		void SkipLoading()
		{
			while (mCurrent != mEnd && mCurrent->IsLoading()) {
				++mCurrent;
			}
		}

	private:
		FreeArray<Object>::Iterator mCurrent;
		FreeArray<Object>::Iterator mEnd;
	};

public:
	explicit LoadedObjectRange(FreeArray<Object>& objects) : mObjects(objects) {}

	Iterator begin() { return Iterator(mObjects.begin(), mObjects.end()); }
	Iterator end() { return Iterator(mObjects.end(), mObjects.end()); }

private:
	FreeArray<Object>& mObjects;
};


class ObjectManager
{
public:
	static constexpr uint32 scMaxObjects = 1024;
	static constexpr uint32 scBoundSize = scMaxObjects * sizeof(ObjectGpuEntry);

public:
	void Create();

	ObjectID NewObjectID(const String& name, eObjectTag tags = eObjectTag::None);
	Object* NewObject(const String& name, MaterialID material, eObjectTag tags = eObjectTag::None,
					  bool is_loading = false);

	Object* GetObject(ObjectID id);
	void DestroyObject(ObjectID& id);

	Object* FindObject(const Hash32 name_hash);

	void Submit(const ObjectID& id, ObjectGpuEntry& entry);
	void Submit(const ObjectID& id, const Mat4f& model_matrix);
	void ReleaseAllObjects();

	/**
	 * @brief Returns a list of all objects currently logged in the object manager.
	 */
	SizedArray<Object*> CollectObjects();

	SizedArray<Object*> CollectWithTags(eObjectTag tags);

	void PrintActive(uint32 limit = 20);

	uint32 GetOffsetObjectIndex(uint32 object_id) const;
	uint32 GetBaseOffset() const;
	uint32 GetPageSize() const;

	/**
	 * @brief Finds an object slot with `num_instances` free slots following.
	 *
	 * @note This does not update the object's object id or set the reserved instances counter, use
	 * Object::ReserveInstances() for that!
	 *
	 * @param object_id An object id of the current object if it needs to be moved.
	 * @returns The object id for the first slot of the block.
	 */
	ObjectID ReserveInstances(const ObjectID& object_id, uint32 num_instances);

	LoadedObjectRange GetCache() { return LoadedObjectRange(mObjectList); }

	void Destroy();

	~ObjectManager() { Destroy(); }

private:
	ObjectGpuEntry* GetBufferAtFrame(uint32 object_id);

public:
	renderer::RawGpuBuffer mObjectGpuBuffer {};
	std::mutex mInUse;

private:
	FreeArray<Object> mObjectList;
};

void DestroyObjectTree(ObjectID root_id);
void CollectObjectTree(Object* root, std::vector<Object*>& out_parts);

} // namespace fx

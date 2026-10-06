#pragma once

#include "Object.hpp"
#include "ObjectID.hpp"

#include <raptor_ffi.h>

#include <Core/Bitset.hpp>
#include <vector>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>

namespace fx {

struct alignas(16) ObjectGpuEntry
{
	float ModelMatrix[16];
};

class Mat4f;

// using ObjectId = uint32;


class ObjectManager
{
public:
	static constexpr uint32 scMaxObjects = 1024;
	static constexpr uint32 scBoundSize = scMaxObjects * sizeof(ObjectGpuEntry);

public:
	void Create();

	Object* NewObject(const std::string& name, MaterialID material, eObjectTag tags = eObjectTag::None);

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
	 * @brief The objects that are in use, to loop over. The list of them is taken when this is called.
	 */
	class Range
	{
	public:
		class Iterator
		{
		public:
			Iterator(Object* base, const uint32* at) : mpBase(base), mpAt(at) {}

			Object& operator*() const { return mpBase[*mpAt]; }
			Iterator& operator++()
			{
				++mpAt;
				return *this;
			}
			bool operator!=(const Iterator& other) const { return mpAt != other.mpAt; }

		private:
			Object* mpBase;
			const uint32* mpAt;
		};

		Range(Object* base, std::vector<uint32>&& ids) : mpBase(base), mIds(std::move(ids)) {}

		Iterator begin() { return Iterator(mpBase, mIds.data()); }
		Iterator end() { return Iterator(mpBase, mIds.data() + mIds.size()); }

		size_t Size() const { return mIds.size(); }

	private:
		Object* mpBase;
		std::vector<uint32> mIds;
	};

	Range GetCache();

	const RxObjectStore* GetStore() const { return mpStore; }

	void Destroy();

	~ObjectManager()
	{
		Destroy();
		rx_object_store_free(mpStore);
	}

private:
	ObjectGpuEntry* GetBufferAtFrame(uint32 object_id);

	Object* Slot(uint32 id) { return reinterpret_cast<Object*>(mpStorage) + id; }

	std::vector<uint32> UsedIds() const;

public:
	// renderer::DescriptorPool mDescriptorPool {};

public:
	renderer::RawGpuBuffer mObjectGpuBuffer {};
	// Bitset mObjectSlotsInUse;

	// renderer::DescriptorSet mObjectBufferDS {};
	// VkDescriptorSetLayout DsLayoutObjectBuffer = nullptr;


	std::mutex mInUse;

private:
	RxObjectStore* mpStore = rx_object_store_new(scMaxObjects);

	/// Where the objects are, one slot for each id in the store
	uint8* mpStorage = nullptr;
};

} // namespace fx

#pragma once

#include "Assert.hpp"
#include "Hash.hpp"
#include "String.hpp"
#include "Types.hpp"

#include <cstdlib>
#include <new>
#include <string>
#include <utility>

namespace fx {

inline constexpr Hash64 HashMix64(uint64 value)
{
	value ^= value >> 30;
	value *= 0xBF58476D1CE4E5B9ULL;
	value ^= value >> 27;
	value *= 0x94D049BB133111EBULL;
	value ^= value >> 31;

	return value;
}

template <typename TKeyType>
struct HashMapHasher
{
	static Hash64 Hash(const TKeyType& key)
		requires std::has_unique_object_representations_v<TKeyType>
	{
		if constexpr (std::is_integral_v<TKeyType> || std::is_enum_v<TKeyType>) {
			return HashMix64(static_cast<uint64>(key));
		}
		else if constexpr (std::is_pointer_v<TKeyType>) {
			return HashMix64(reinterpret_cast<uintptr_t>(key));
		}
		else {
			return HashObj64(key);
		}
	}
};

template <>
struct HashMapHasher<String>
{
	static Hash64 Hash(const String& key) { return HashStr64(key.CStr()); }
};

template <>
struct HashMapHasher<std::string>
{
	static Hash64 Hash(const std::string& key) { return HashStr64(key.c_str()); }
};

template <typename THasher, typename TKeyType>
concept C_IsHashMapHasher = requires(const TKeyType& key) {
	{ THasher::Hash(key) } -> std::convertible_to<Hash64>;
};

template <typename TKeyType, typename TValueType, typename THasher = HashMapHasher<TKeyType>>
	requires C_IsHashMapHasher<THasher, TKeyType>
class HashMap
{
	static constexpr uint32 scInitialCapacity = 16;
	static constexpr uint32 scNotFound = UINT32_MAX;

public:
	struct Entry
	{
		Hash64 Hash;
		TKeyType Key;
		TValueType Value;
	};

	static_assert(alignof(Entry) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "HashMap entries must not be over-aligned");

public:
	HashMap() = default;
	HashMap(uint32 capacity) { Init(capacity); }

	HashMap(const HashMap& other) = delete;
	HashMap(HashMap&& other) { (*this) = std::move(other); }

	void Init(uint32 capacity)
	{
		if (pData != nullptr) {
			Free();
		}

		InternalAllocate(RoundCapacity(capacity));
	}

	FX_FORCE_INLINE bool IsInited() const { return pData != nullptr; }

	FX_FORCE_INLINE bool IsEmpty() const { return Size == 0; }
	FX_FORCE_INLINE bool IsNotEmpty() const { return !IsEmpty(); }

	TValueType& Insert(const TKeyType& key, const TValueType& value) { return Emplace(key, value); }
	TValueType& Insert(const TKeyType& key, TValueType&& value) { return Emplace(key, std::move(value)); }

	template <typename... TArgs>
	TValueType& Emplace(const TKeyType& key, TArgs&&... args)
	{
		const Hash64 hash = HashKey(key);

		uint32 index = FindIndex(key, hash);
		if (index != scNotFound) {
			TValueType new_value(std::forward<TArgs>(args)...);

			TValueType* value = &pData[index].Value;

			value->~TValueType();
			new (value) TValueType(std::move(new_value));

			return *value;
		}

		TKeyType new_key(key);
		TValueType new_value(std::forward<TArgs>(args)...);

		GrowIfNeeded();

		Entry* entry = &pData[FindFreeIndex(pData, Capacity, hash)];

		new (&entry->Key) TKeyType(std::move(new_key));
		new (&entry->Value) TValueType(std::move(new_value));
		entry->Hash = hash;

		++Size;

		return entry->Value;
	}

	TValueType* Find(const TKeyType& key)
	{
		uint32 index = FindIndex(key, HashKey(key));
		if (index == scNotFound) {
			return nullptr;
		}

		return &pData[index].Value;
	}

	const TValueType* Find(const TKeyType& key) const
	{
		uint32 index = FindIndex(key, HashKey(key));
		if (index == scNotFound) {
			return nullptr;
		}

		return &pData[index].Value;
	}

	FX_FORCE_INLINE bool Contains(const TKeyType& key) const { return FindIndex(key, HashKey(key)) != scNotFound; }

	TValueType& operator[](const TKeyType& key)
	{
		TValueType* value = Find(key);
		if (value != nullptr) {
			return *value;
		}

		return Emplace(key);
	}

	bool Remove(const TKeyType& key)
	{
		uint32 index = FindIndex(key, HashKey(key));
		if (index == scNotFound) {
			return false;
		}

		DestroyEntry(&pData[index]);
		--Size;

		const uint32 mask = Capacity - 1;

		uint32 hole = index;
		uint32 next = (index + 1) & mask;

		while (pData[next].Hash != HashNull64) {
			const uint32 ideal = static_cast<uint32>(pData[next].Hash) & mask;

			if (((next - ideal) & mask) >= ((next - hole) & mask)) {
				MoveEntry(&pData[hole], &pData[next]);
				hole = next;
			}

			next = (next + 1) & mask;
		}

		return true;
	}

	void Reserve(uint32 element_count)
	{
		uint32 capacity = RoundCapacity(element_count + (element_count / 3) + 1);
		if (capacity > Capacity) {
			Rehash(capacity);
		}
	}

	void Clear()
	{
		for (uint32 i = 0; i < Capacity; i++) {
			if (pData[i].Hash != HashNull64) {
				DestroyEntry(&pData[i]);
			}
		}

		Size = 0;
	}

	void Free()
	{
		if (pData == nullptr) {
			return;
		}

		Clear();

		std::free(reinterpret_cast<void*>(pData));

		pData = nullptr;
		Capacity = 0;
	}

	HashMap& operator=(const HashMap& other) = delete;

	HashMap& operator=(HashMap&& other) noexcept
	{
		if (this == &other) {
			return *this;
		}

		Free();

		pData = other.pData;
		Size = other.Size;
		Capacity = other.Capacity;

		other.pData = nullptr;
		other.Size = 0;
		other.Capacity = 0;

		return *this;
	}

	~HashMap() { Free(); }

public:
	template <typename TEntryType>
	class IteratorBase
	{
	public:
		IteratorBase(TEntryType* pData, uint32 index, uint32 capacity) : pData(pData), Index(index), Capacity(capacity)
		{
			SkipToNextUsed();
		}

		TEntryType& operator*() const { return pData[Index]; }
		TEntryType* operator->() const { return &pData[Index]; }

		IteratorBase& operator++()
		{
			++Index;
			SkipToNextUsed();
			return *this;
		}

		IteratorBase operator++(int)
		{
			IteratorBase tmp = *this;
			++(*this);
			return tmp;
		}

		bool operator==(const IteratorBase& other) const { return Index == other.Index; }
		bool operator!=(const IteratorBase& other) const { return !(*this == other); }

	private:
		void SkipToNextUsed()
		{
			while (Index < Capacity && pData[Index].Hash == HashNull64) {
				++Index;
			}
		}

		TEntryType* pData;
		uint32 Index;
		uint32 Capacity;
	};

	using Iterator = IteratorBase<Entry>;
	using ConstIterator = IteratorBase<const Entry>;

	Iterator begin() { return Iterator(pData, 0, Capacity); }
	Iterator end() { return Iterator(pData, Capacity, Capacity); }

	ConstIterator begin() const { return ConstIterator(pData, 0, Capacity); }
	ConstIterator end() const { return ConstIterator(pData, Capacity, Capacity); }

	ConstIterator cbegin() const { return ConstIterator(pData, 0, Capacity); }
	ConstIterator cend() const { return ConstIterator(pData, Capacity, Capacity); }

private:
	static Hash64 HashKey(const TKeyType& key)
	{
		Hash64 hash = THasher::Hash(key);
		if (hash == HashNull64) {
			hash = 0;
		}

		return hash;
	}

	static uint32 RoundCapacity(uint32 capacity)
	{
		uint32 rounded = scInitialCapacity;
		while (rounded < capacity) {
			rounded *= 2;
		}

		return rounded;
	}

	static uint32 FindFreeIndex(const Entry* entries, uint32 capacity, Hash64 hash)
	{
		const uint32 mask = capacity - 1;

		uint32 index = static_cast<uint32>(hash) & mask;
		while (entries[index].Hash != HashNull64) {
			index = (index + 1) & mask;
		}

		return index;
	}

	static void DestroyEntry(Entry* entry)
	{
		entry->Key.~TKeyType();
		entry->Value.~TValueType();
		entry->Hash = HashNull64;
	}

	static void MoveEntry(Entry* dst, Entry* src)
	{
		new (&dst->Key) TKeyType(std::move(src->Key));
		new (&dst->Value) TValueType(std::move(src->Value));
		dst->Hash = src->Hash;

		DestroyEntry(src);
	}

	uint32 FindIndex(const TKeyType& key, Hash64 hash) const
	{
		if (Size == 0) {
			return scNotFound;
		}

		const uint32 mask = Capacity - 1;

		uint32 index = static_cast<uint32>(hash) & mask;
		while (pData[index].Hash != HashNull64) {
			if (pData[index].Hash == hash && pData[index].Key == key) {
				return index;
			}

			index = (index + 1) & mask;
		}

		return scNotFound;
	}

	void GrowIfNeeded()
	{
		if (pData == nullptr) {
			InternalAllocate(scInitialCapacity);
			return;
		}

		if ((Size + 1) * 4 > Capacity * 3) {
			Rehash(Capacity * 2);
		}
	}

	void Rehash(uint32 capacity)
	{
		Entry* old_data = pData;
		const uint32 old_capacity = Capacity;

		InternalAllocate(capacity);

		for (uint32 i = 0; i < old_capacity; i++) {
			Entry* entry = &old_data[i];
			if (entry->Hash == HashNull64) {
				continue;
			}

			MoveEntry(&pData[FindFreeIndex(pData, Capacity, entry->Hash)], entry);
		}

		std::free(reinterpret_cast<void*>(old_data));
	}

	void InternalAllocate(uint32 capacity)
	{
		pData = reinterpret_cast<Entry*>(std::malloc(sizeof(Entry) * capacity));
		AssertMsg(pData != nullptr, "HashMap: out of memory");

		for (uint32 i = 0; i < capacity; i++) {
			pData[i].Hash = HashNull64;
		}

		Capacity = capacity;
	}

public:
	Entry* pData = nullptr;
	uint32 Size = 0;
	uint32 Capacity = 0;
};

} // namespace fx

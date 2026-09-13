#pragma once

#include "Assert.hpp"
#include "Log.hpp"
#include "Types.hpp"

#include <Math/MathUtil.hpp>

namespace fx {


template <typename T>
class UndoStack
{
public:
	struct Iterator
	{
		Iterator(T* ptr, size_t index) : mpPtr(ptr), mIndex(index) {}

		Iterator& operator++()
		{
			mIndex++;
			return *this;
		}

		Iterator& operator++(int value)
		{
			Iterator before = *this;
			mIndex++;
			return before;
		}

		T& operator*() const { return mpPtr[mIndex]; }

		bool operator==(const Iterator& b) const { return mpPtr == b.mpPtr && mIndex == b.mIndex; }


	private:
		T* mpPtr;
		size_t mIndex;
	};

public:
	UndoStack() = default;
	UndoStack(uint32 num_objects) { InitCapacity(num_objects); }

	UndoStack(const UndoStack& other) = delete;
	UndoStack(UndoStack&& other) { (*this) = std::move(other); }

	UndoStack& operator=(const UndoStack& other) = delete;
	UndoStack& operator=(UndoStack&& other)
	{
		mpData = other.mpData;
		mCapacity = other.mCapacity;
		mSize = other.mSize;
		mPushIndex = other.mPushIndex;
		mPopIndex = other.mPopIndex;
		mRedoCount = other.mRedoCount;

		other.mCapacity = 0;
		other.mRedoCount = 0;
		other.mpData = nullptr;

		return *this;
	}


	Iterator begin() const { return Iterator(mpData, 0); }
	Iterator end() const { return Iterator(mpData, mSize); }

	bool IsInited() const { return mpData != nullptr; }

	/**
	 * @brief Returns the number of items enqueued
	 */
	uint32 GetSize() const { return mSize; }

	void InitCapacity(uint32 num_objects)
	{
		Assert(num_objects > 0);

		if (mpData != nullptr) {
			// Recreate queue
			Destroy();
		}

		mCapacity = num_objects;
		mPushIndex = 0;
		mPopIndex = 0;
		mSize = 0;

		const uint32 buffer_size = sizeof(T) * num_objects;
		mpData = reinterpret_cast<T*>(std::malloc(buffer_size));

		if (mpData == nullptr) {
			LogError(LC_CORE, "Could not allocate memory for queue of size {}", buffer_size);
			mCapacity = 0;
		}
	}

	T& Push(const T& value)
	{
		// We are out of space, pop the front value off.
		if (mSize >= mCapacity) {
			PopFront();
		}

		InvalidateRedos();

		if (mPushIndex >= mCapacity) {
			mPushIndex = 0;
		}

		++mSize;

		T* ptr = mpData + (mPushIndex++);
		new (ptr) T(value);

		return *ptr;
	}

	T& Push(T&& value)
	{
		// We are out of space, pop the front value off.
		if (mSize >= mCapacity) {
			PopFront();
		}

		InvalidateRedos();

		if (mPushIndex >= mCapacity) {
			mPushIndex = 0;
		}

		++mSize;

		T* ptr = mpData + (mPushIndex++);
		new (ptr) T(std::move(value));

		return *ptr;
	}

	template <typename... TArgs>
	T& Emplace(TArgs&&... args)
	{
		// We are out of space, pop the front value off.
		if (mSize >= mCapacity) {
			PopFront();
		}

		InvalidateRedos();

		if (mPushIndex >= mCapacity) {
			mPushIndex = 0;
		}

		++mSize;

		T* ptr = mpData + (mPushIndex++);
		new (ptr) T(std::forward<TArgs>(args)...);

		return *ptr;
	}

	T PopFrontValue()
	{
		Assert(mSize > 0);


		--mSize;

		T* ptr = mpData + (mPopIndex++);
		if (mPopIndex >= mCapacity) {
			mPopIndex = 0;
		}

		T value = std::move(*ptr);
		ptr->~T();

		return value;
	}

	T PopBackValue()
	{
		Assert(mSize > 0);

		--mSize;

		// Wrap around if we hit zero.
		if (mPushIndex == 0) {
			mPushIndex = mCapacity - 1;
		}
		else {
			--mPushIndex;
		}

		T* ptr = mpData + mPushIndex;

		T value = std::move(*ptr);
		ptr->~T();

		return value;
	}

	T& First() const
	{
		Assert(mSize > 0);
		T* ptr = mpData + (mPopIndex);

		return *ptr;
	}

	T* Last()
	{
		if (mSize <= 0) {
			return nullptr;
		}

		uint32 index = (mPushIndex == 0) ? (mCapacity - 1) : (mPushIndex - 1);
		T* ptr = mpData + index;

		return ptr;
	}


	void PopFront()
	{
		if (mSize == 0) {
			LogError(LC_CORE, "Cannot pop from empty queue");
			return;
		}


		--mSize;

		T* ptr = mpData + (mPopIndex++);
		if (mPopIndex >= mCapacity) {
			mPopIndex = 0;
		}

		if constexpr (!std::is_trivially_destructible_v<T>) {
			ptr->~T();
		}
	}

	bool DoUndo()
	{
		if (mSize == 0) {
			return false;
		}

		--mSize;

		// Wrap around if we hit zero.
		if (mPushIndex == 0) {
			mPushIndex = mCapacity - 1;
		}
		else {
			--mPushIndex;
		}

		++mRedoCount;

		return true;
	}

	T* DoRedo()
	{
		if (mRedoCount <= 0) {
			return nullptr;
		}

		--mRedoCount;

		T* ptr = mpData + (mPushIndex);

		++mPushIndex;
		if (mPushIndex >= mCapacity) {
			mPushIndex = 0;
		}

		++mSize;

		return ptr;
	}


	void InvalidateRedos()
	{
		for (uint32 i = 0; i < mRedoCount; i++) {
			T* ptr = mpData + ((mPushIndex + i) % mCapacity);

			if constexpr (!std::is_trivially_destructible_v<T>) {
				ptr->~T();
			}
		}

		mRedoCount = 0;
	}

	bool IsEmpty() const { return mSize == 0; }


	void Destroy()
	{
		if (!mpData) {
			return;
		}

		// Destroy all items remaining - Pop decrements mSize, so while loop is needed
		while (!IsEmpty()) {
			PopFront();
		}

		std::free(static_cast<void*>(mpData));
		mpData = nullptr;
		mCapacity = 0;
	}

	~UndoStack() = default;


private:
	T* mpData = nullptr;

	uint32 mSize = 0;
	uint32 mCapacity = 0;

	uint32 mPushIndex = 0;

	// The number of valid redo-able items.
	uint32 mRedoCount = 0;

	uint32 mPopIndex = 0;
};


} // namespace fx

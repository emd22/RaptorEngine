#pragma once

#include "Assert.hpp"
#include "Log.hpp"
#include "Types.hpp"

#include <cstdlib>
#include <new>
#include <utility>

namespace fx {

/**
 * @brief A fixed capacity ring of undoable items. Undone items are kept alive past the top of the stack until they
 * are either redone or invalidated by a new push.
 */
template <typename T>
class UndoStack
{
public:
	UndoStack() = default;
	explicit UndoStack(uint32 capacity) { InitCapacity(capacity); }

	UndoStack(const UndoStack& other) = delete;
	UndoStack(UndoStack&& other) { (*this) = std::move(other); }

	UndoStack& operator=(const UndoStack& other) = delete;
	UndoStack& operator=(UndoStack&& other)
	{
		if (this == &other) {
			return *this;
		}

		Destroy();

		mpData = std::exchange(other.mpData, nullptr);
		mCapacity = std::exchange(other.mCapacity, 0);
		mSize = std::exchange(other.mSize, 0);
		mPushIndex = std::exchange(other.mPushIndex, 0);
		mPopIndex = std::exchange(other.mPopIndex, 0);
		mRedoCount = std::exchange(other.mRedoCount, 0);

		return *this;
	}

	~UndoStack() { Destroy(); }

	/// The number of items that can be undone
	uint32 GetSize() const { return mSize; }
	uint32 GetCapacity() const { return mCapacity; }
	bool IsEmpty() const { return mSize == 0; }

	void InitCapacity(uint32 capacity)
	{
		Assert(capacity > 0);

		Destroy();

		mpData = static_cast<T*>(std::malloc(sizeof(T) * capacity));

		if (mpData == nullptr) {
			LogError(LC_CORE, "Could not allocate memory for undo stack of {} items", capacity);
			return;
		}

		mCapacity = capacity;
	}

	/// Pushes an item to the top of the stack, dropping the oldest item if the stack is full. Anything that could be
	/// redone is invalidated.
	template <typename TValue>
	T& Push(TValue&& value)
	{
		Assert(mCapacity > 0);

		InvalidateRedos();

		if (mSize >= mCapacity) {
			PopFront();
		}

		T* ptr = new (mpData + mPushIndex) T(std::forward<TValue>(value));

		mPushIndex = Next(mPushIndex);
		++mSize;

		return *ptr;
	}

	/// The oldest item
	T& First() const
	{
		Assert(mSize > 0);
		return mpData[mPopIndex];
	}

	/// The newest item, or null if there is nothing to undo
	T* Last() const
	{
		if (mSize == 0) {
			return nullptr;
		}

		return mpData + Previous(mPushIndex);
	}

	/// Drops the oldest item
	void PopFront()
	{
		if (mSize == 0) {
			LogError(LC_CORE, "Cannot pop from an empty undo stack");
			return;
		}

		mpData[mPopIndex].~T();

		mPopIndex = Next(mPopIndex);
		--mSize;
	}

	/// Steps back over the newest item and returns it so that it can be reverted, or null if there is nothing to undo.
	/// The item stays alive until it is redone or invalidated.
	T* Undo()
	{
		if (mSize == 0) {
			return nullptr;
		}

		mPushIndex = Previous(mPushIndex);
		--mSize;
		++mRedoCount;

		return mpData + mPushIndex;
	}

	/// Steps forward over the last undone item and returns it so that it can be reapplied, or null if there is nothing
	/// to redo.
	T* Redo()
	{
		if (mRedoCount == 0) {
			return nullptr;
		}

		T* ptr = mpData + mPushIndex;

		mPushIndex = Next(mPushIndex);
		++mSize;
		--mRedoCount;

		return ptr;
	}

	void InvalidateRedos()
	{
		for (uint32 i = 0, index = mPushIndex; i < mRedoCount; i++, index = Next(index)) {
			mpData[index].~T();
		}

		mRedoCount = 0;
	}

	void Clear()
	{
		InvalidateRedos();

		while (!IsEmpty()) {
			PopFront();
		}

		mPopIndex = 0;
		mPushIndex = 0;
	}

	void Destroy()
	{
		if (mpData == nullptr) {
			return;
		}

		Clear();

		std::free(static_cast<void*>(mpData));
		mpData = nullptr;
		mCapacity = 0;
	}

private:
	uint32 Next(uint32 index) const { return (index + 1 >= mCapacity) ? 0 : (index + 1); }
	uint32 Previous(uint32 index) const { return (index == 0) ? (mCapacity - 1) : (index - 1); }

private:
	T* mpData = nullptr;

	uint32 mSize = 0;
	uint32 mCapacity = 0;

	/// Where the next item is pushed, which is one past the newest item
	uint32 mPushIndex = 0;
	/// The oldest item
	uint32 mPopIndex = 0;

	/// The number of undone items past `mPushIndex` that can still be redone
	uint32 mRedoCount = 0;
};


} // namespace fx

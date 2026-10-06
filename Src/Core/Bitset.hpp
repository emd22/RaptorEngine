#pragma once

#include "Assert.hpp"
#include "BitUtil.hpp"
#include "Log.hpp"
#include "SizedArray.hpp"
#include "Types.hpp"

#include <raptor_ffi.h>

namespace fx {

/**
 * @brief A set of flags, one per slot. The bits are kept by Rust.
 */
class Bitset
{
public:
	static constexpr uint32 scNoFreeBits = UINT32_MAX;

	static constexpr uint32 scBitsPerInt = 64;

public:
	Bitset() = default;
	explicit Bitset(uint32 max_bits) { InitZero(max_bits); }

	Bitset(const Bitset& other) : mpSlots((other.mpSlots != nullptr) ? rx_slots_clone(other.mpSlots) : nullptr) {}

	Bitset(Bitset&& other) noexcept : mpSlots(other.mpSlots) { other.mpSlots = nullptr; }

	Bitset& operator=(const Bitset& other)
	{
		if (this != &other) {
			Release();
			mpSlots = (other.mpSlots != nullptr) ? rx_slots_clone(other.mpSlots) : nullptr;
		}

		return *this;
	}

	Bitset& operator=(Bitset&& other) noexcept
	{
		if (this != &other) {
			Release();
			mpSlots = other.mpSlots;
			other.mpSlots = nullptr;
		}

		return *this;
	}

	void InitZero(uint32 max_bits)
	{
		Release();
		mpSlots = rx_slots_new(max_bits, false);
	}

	void InitOne(uint32 max_bits)
	{
		Release();
		mpSlots = rx_slots_new(max_bits, true);
	}

	/**
	 * @brief Finds the next zero bit in the bitset
	 * @return An index to the bit, or `Bitset::scNoFreeBits` if there are none remaining.
	 */
	uint32 FindNextFreeBit(uint32 start_index = 0) const
	{
		return (mpSlots != nullptr) ? rx_slots_find_next_free(mpSlots, start_index) : scNoFreeBits;
	}

	uint32 FindNextSetBit(uint32 start_index = 0) const
	{
		return (mpSlots != nullptr) ? rx_slots_find_next_set(mpSlots, start_index) : scNoFreeBits;
	}

	/**
	 * @brief Finds the first run of `group_size` zero bits, or `scNoFreeBits`.
	 */
	uint32 FindNextFreeBitGroup(uint32 group_size) const
	{
		return (mpSlots != nullptr) ? rx_slots_find_free_group(mpSlots, group_size) : scNoFreeBits;
	}

	/**
	 * @brief Makes room for the slot `current` to be followed by `instances` more, moving it to a free run if the slots
	 * after it are taken.
	 * @returns The first slot of the run, or `scNoFreeBits` if there is no room.
	 */
	uint32 ReserveInstances(uint32 current, uint32 instances, bool& out_moved)
	{
		return rx_slots_reserve_instances(mpSlots, current, instances, &out_moved);
	}

	void Set(uint32 index)
	{
		if (mpSlots != nullptr) {
			rx_slots_set(mpSlots, index);
		}
	}

	bool Get(uint32 index) const { return mpSlots != nullptr && rx_slots_get(mpSlots, index) != 0; }

	void Unset(uint32 index)
	{
		if (mpSlots != nullptr) {
			rx_slots_unset(mpSlots, index);
		}
	}

	uint64 GetBitCapacity() const { return (mpSlots != nullptr) ? rx_slots_capacity(mpSlots) : 0; }

	bool IsInited() const { return mpSlots != nullptr; }

	/**
	 * @brief Sets all bits to zero.
	 */
	void ClearAll()
	{
		if (mpSlots != nullptr) {
			rx_slots_clear_all(mpSlots);
		}
	}

	void Print() const;

	~Bitset() { Release(); }

private:
	void Release()
	{
		rx_slots_free(mpSlots);
		mpSlots = nullptr;
	}

private:
	RxSlotSet* mpSlots = nullptr;
};


} // namespace fx

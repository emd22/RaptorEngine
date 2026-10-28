#pragma once

#include <Core/Types.hpp>
#include <functional>
#include <string>
#include <vector>

namespace fx {

struct TimerID
{
	uint32 Slot = UINT32_MAX;
	uint32 Generation = 0;

	bool IsValid() const { return Slot != UINT32_MAX; }

	int64 Pack() const
	{
		if (!IsValid()) {
			return 0;
		}

		return static_cast<int64>((static_cast<uint64>(Generation) << 32) | Slot);
	}

	static TimerID Unpack(int64 packed)
	{
		const uint64 bits = static_cast<uint64>(packed);
		return TimerID { .Slot = static_cast<uint32>(bits), .Generation = static_cast<uint32>(bits >> 32) };
	}
};

class TimerQueue
{
public:
	using Callback = std::function<void(TimerID)>;

	static constexpr uint32 scMaxCatchUp = 4;
	static constexpr float64 scMinRepeatInterval = 1e-3;

	struct Desc
	{
		float64 Interval = 0.0;
		int64 Fires = 1;
		const void* pOwner = nullptr;
		std::string Label;
	};

	struct Info
	{
		TimerID ID;
		float64 Remaining = 0.0;
		int64 Fires = 1;
		const void* pOwner = nullptr;
		std::string Label;
	};

public:
	TimerID Add(Desc desc, Callback callback);

	void Cancel(TimerID id);
	void CancelOwnedBy(const void* owner);
	void CancelAll();

	bool IsActive(TimerID id) const;
	float64 GetRemaining(TimerID id) const;
	float64 GetTime() const { return mClock; }
	std::vector<Info> List() const;

	void Update(float64 delta_time);

private:
	struct Timer
	{
		Callback Fn;
		std::string Label;
		const void* pOwner = nullptr;
		float64 Due = 0.0;
		float64 Interval = 0.0;
		int64 Fires = 1;
		uint64 Sequence = 0;
		uint64 DispatchTick = 0;
		uint32 DispatchCount = 0;
		uint32 Generation = 1;
		bool bActive = false;
	};

	static constexpr uint32 scNoSlot = UINT32_MAX;

	bool IsCurrent(TimerID id) const;
	uint32 FindNextDue(uint64 sequence_limit) const;
	void Release(uint32 slot);

private:
	std::vector<Timer> mTimers;
	std::vector<uint32> mFreeSlots;
	float64 mClock = 0.0;
	uint64 mNextSequence = 0;
	uint64 mTick = 0;
};

} // namespace fx

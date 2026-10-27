#include "TimerQueue.hpp"

#include <algorithm>

namespace fx {

TimerID TimerQueue::Add(Desc desc, Callback callback)
{
	uint32 slot = scNoSlot;

	if (mFreeSlots.empty()) {
		slot = static_cast<uint32>(mTimers.size());
		mTimers.emplace_back();
	}
	else {
		slot = mFreeSlots.back();
		mFreeSlots.pop_back();
	}

	Timer& timer = mTimers[slot];

	timer.Fn = std::move(callback);
	timer.Label = std::move(desc.Label);
	timer.pOwner = desc.pOwner;
	timer.Fires = desc.Fires;
	timer.Interval = std::max(desc.Interval, 0.0);

	if (timer.Fires != 1) {
		timer.Interval = std::max(timer.Interval, scMinRepeatInterval);
	}

	timer.Due = mClock + timer.Interval;
	timer.Sequence = mNextSequence++;
	timer.DispatchTick = 0;
	timer.DispatchCount = 0;
	timer.bActive = true;

	return TimerID { .Slot = slot, .Generation = timer.Generation };
}

void TimerQueue::Cancel(TimerID id)
{
	if (IsCurrent(id)) {
		Release(id.Slot);
	}
}

void TimerQueue::CancelOwnedBy(const void* owner)
{
	if (owner == nullptr) {
		return;
	}

	for (uint32 slot = 0; slot < mTimers.size(); ++slot) {
		if (mTimers[slot].bActive && mTimers[slot].pOwner == owner) {
			Release(slot);
		}
	}
}

void TimerQueue::CancelAll()
{
	for (uint32 slot = 0; slot < mTimers.size(); ++slot) {
		if (mTimers[slot].bActive) {
			Release(slot);
		}
	}
}

bool TimerQueue::IsActive(TimerID id) const { return IsCurrent(id); }

float64 TimerQueue::GetRemaining(TimerID id) const
{
	if (!IsCurrent(id)) {
		return 0.0;
	}

	return std::max(mTimers[id.Slot].Due - mClock, 0.0);
}

std::vector<TimerQueue::Info> TimerQueue::List() const
{
	std::vector<Info> infos;

	for (uint32 slot = 0; slot < mTimers.size(); ++slot) {
		const Timer& timer = mTimers[slot];

		if (!timer.bActive) {
			continue;
		}

		infos.push_back(Info {
			.ID = TimerID { .Slot = slot, .Generation = timer.Generation },
			.Remaining = std::max(timer.Due - mClock, 0.0),
			.Fires = timer.Fires,
			.pOwner = timer.pOwner,
			.Label = timer.Label,
		});
	}

	std::sort(infos.begin(), infos.end(), [](const Info& a, const Info& b) { return a.Remaining < b.Remaining; });

	return infos;
}

void TimerQueue::Update(float64 delta_time)
{
	mClock += delta_time;
	++mTick;

	// Timers added while dispatching wait for the next update
	const uint64 sequence_limit = mNextSequence;

	for (uint32 slot = FindNextDue(sequence_limit); slot != scNoSlot; slot = FindNextDue(sequence_limit)) {
		Timer& timer = mTimers[slot];

		if (timer.DispatchTick != mTick) {
			timer.DispatchTick = mTick;
			timer.DispatchCount = 0;
		}

		if (timer.DispatchCount >= scMaxCatchUp) {
			timer.Due = mClock + timer.Interval;
			continue;
		}

		++timer.DispatchCount;

		const TimerID id { .Slot = slot, .Generation = timer.Generation };
		const Callback callback = timer.Fn;

		if (timer.Fires == 1) {
			Release(slot);
		}
		else {
			if (timer.Fires > 0) {
				--timer.Fires;
			}

			timer.Due += timer.Interval;
		}

		if (callback) {
			callback(id);
		}
	}
}

bool TimerQueue::IsCurrent(TimerID id) const
{
	return id.Slot < mTimers.size() && mTimers[id.Slot].bActive && mTimers[id.Slot].Generation == id.Generation;
}

uint32 TimerQueue::FindNextDue(uint64 sequence_limit) const
{
	uint32 best = scNoSlot;

	for (uint32 slot = 0; slot < mTimers.size(); ++slot) {
		const Timer& timer = mTimers[slot];

		if (!timer.bActive || timer.Sequence >= sequence_limit || timer.Due > mClock) {
			continue;
		}

		if (best == scNoSlot) {
			best = slot;
			continue;
		}

		const Timer& current = mTimers[best];

		if (timer.Due < current.Due || (timer.Due == current.Due && timer.Sequence < current.Sequence)) {
			best = slot;
		}
	}

	return best;
}

void TimerQueue::Release(uint32 slot)
{
	Timer& timer = mTimers[slot];

	timer.Fn = nullptr;
	timer.Label.clear();
	timer.pOwner = nullptr;
	timer.bActive = false;
	++timer.Generation;

	mFreeSlots.push_back(slot);
}

} // namespace fx

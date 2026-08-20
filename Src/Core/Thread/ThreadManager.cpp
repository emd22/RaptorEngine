
#include "ThreadManager.hpp"

#include <Renderer/Limits.hpp>

#define TO_THREAD_ID(index_) ThreadID((index_) + 1)
#define FROM_THREAD_ID(id_)	 ((id_).GetID() - 1)

namespace fx {

const ThreadID ThreadID::scMainThread = ThreadID(0);
const ThreadID ThreadID::scInvalid = ThreadID(UINT32_MAX);

ThreadManager::ThreadManager()
{
	mMainTID = SysThread::GetCurrentTID();
	mThreadCache.Init(Limits::MaxConcurrentThreads);
}

ThreadID ThreadManager::NewThread(const String& name, ThreadFunc thread_func)
{
	SysThread* th = nullptr;
	uint32 th_index = 0;

	{
		// Lock only for item alloc/find
		std::lock_guard<std::mutex> guard(mMutex);
		th = mThreadCache.NewItem(&th_index);
	}

	if (th == nullptr) {
		LogError("Could not create new thread '{}' (out of space)", name);
		return ThreadID::scInvalid;
	}

	th->ID = TO_THREAD_ID(th_index);
	th->Name = name;
	th->Create(thread_func);

	return th->ID;
}

void ThreadManager::Join(ThreadID id)
{
	std::lock_guard<std::mutex> guard(mMutex);
	SysThread* th = mThreadCache.GetItem(FROM_THREAD_ID(id));

	if (th == nullptr) {
		LogError("Could not find valid thread (ID={})", id);
		return;
	}

	th->Join();

	mThreadCache.FreeItem(FROM_THREAD_ID(id));
}

void ThreadManager::Detach(ThreadID id)
{
	std::lock_guard<std::mutex> guard(mMutex);

	SysThread* th = mThreadCache.GetItem(FROM_THREAD_ID(id));
	if (th == nullptr) {
		LogError("Could not find valid thread (ID={})", id);
		return;
	}

	th->Detach();

	mThreadCache.FreeItem(FROM_THREAD_ID(id));
}

bool ThreadManager::IsMainThread() const { return SysThread::AreTIDsEqual(SysThread::GetCurrentTID(), mMainTID); }

ThreadManager::~ThreadManager()
{
	std::lock_guard<std::mutex> guard(mMutex);

	// Find a list of threads that are still active

	SizedArray<SysThread*> still_running_list(Limits::MaxConcurrentThreads);

	uint32 index = 0;
	while (true) {
		index = mThreadCache.SlotsInUse.FindNextSetBit(index);
		if (index == Bitset::scNoFreeBits) {
			break;
		}

		SysThread* th = mThreadCache.GetItem(index);
		if (th == nullptr) {
			++index;
			continue;
		}

		if (th->bIsRunning) {
			still_running_list.Insert(th);
		}

		++index;
	}

	// Log any that still exist

	for (const SysThread* st : still_running_list) {
		LogError("Thread '{}' (ID={}) is still running on thread manager shutdown!", st->Name, st->ID);
	}
}


} // namespace fx

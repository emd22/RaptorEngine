#pragma once

#include "SysThread.hpp"
#include "ThreadID.hpp"

#include <Core/FreeArray.hpp>


namespace fx {


class ThreadManager
{
public:
	ThreadManager();

	ThreadID NewThread(const String& name, ThreadFunc thread_func);

	void Join(ThreadID id);
	void Detach(ThreadID id);

	bool IsMainThread() const;

	~ThreadManager();

private:
	FreeArray<SysThread> mThreadCache;
	std::mutex mMutex;

	SysThreadInternalType mMainTID;
};


} // namespace fx

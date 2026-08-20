#include "SysThread.hpp"

#include <Core/Assert.hpp>
#include <utility>

namespace fx {

#ifdef FX_THREADS_PTHREAD

/////////////////////////////////////
// PThread Implementation
/////////////////////////////////////


SysThreadImpl_PThread::SysThreadImpl_PThread() = default;


SysThreadImpl_PThread::SysThreadImpl_PThread(SysThreadImpl_PThread&& other) noexcept
{
	InternalThread = std::exchange(other.InternalThread, nullptr);
	pEntryFunction = std::move(other.pEntryFunction);
	bIsRunning = std::exchange(other.bIsRunning, false);
}

SysThreadImpl_PThread& SysThreadImpl_PThread::operator=(SysThreadImpl_PThread&& other) noexcept
{
	if (this == &other) {
		return *this;
	}

	if (bIsRunning) {
		pthread_detach(InternalThread);
	}

	InternalThread = other.InternalThread;
	bIsRunning = other.bIsRunning;

	other.InternalThread = nullptr;
	other.bIsRunning = false;

	pEntryFunction = other.pEntryFunction;
	other.pEntryFunction = nullptr;

	return *this;
}

void SysThreadImpl_PThread::Create(ThreadFunc thread_func)
{
	AssertMsg(bIsRunning == false, "Cannot start a thread that is already running");

	pEntryFunction = std::move(thread_func);

	const int result = pthread_create(&InternalThread, nullptr, &SysThreadImpl_PThread::InternalEntrypoint, this);
	AssertMsg(result == 0, "Internal thread create failed");

	bIsRunning = true;
}

void* SysThreadImpl_PThread::InternalEntrypoint(void* arg)
{
	SysThreadImpl_PThread* impl = static_cast<SysThreadImpl_PThread*>(arg);

	if (impl->pEntryFunction != nullptr) {
		impl->pEntryFunction();
	}

	return 0;
}

void SysThreadImpl_PThread::Join()
{
	if (!bIsRunning) {
		return;
	}

	pthread_join(InternalThread, nullptr);
	bIsRunning = false;
}

void SysThreadImpl_PThread::Detach()
{
	if (!bIsRunning) {
		return;
	}

	pthread_detach(InternalThread);
	bIsRunning = false;
}


#endif // FX_THREADS_PTHREAD

#ifdef FX_THREADS_WINDOWS

/////////////////////////////////////
// Windows Definition
/////////////////////////////////////

SysThreadImpl_Windows::SysThreadImpl_Windows() = default;

SysThreadImpl_Windows::~SysThreadImpl_Windows()
{
	if (InternalThread != nullptr) {
		CloseHandle(InternalThread);
	}
}

SysThreadImpl_Windows::SysThreadImpl_Windows(SysThreadImpl_Windows&& other) noexcept
{
	InternalThread = std::exchange(other.InternalThread, nullptr);
	InternalID = std::exchange(other.InternalID, 0);
	pEntryFunction = std::move(other.pEntryFunction);
	bIsRunning = std::exchange(other.bIsRunning, false);
}

SysThreadImpl_Windows& SysThreadImpl_Windows::operator=(SysThreadImpl_Windows&& other) noexcept
{
	if (this == &other) {
		return *this;
	}

	if (bIsRunning) {
		pthread_detach(InternalThread);
	}

	InternalThread = other.InternalThread;
	InternalID = other.InternalID;
	bIsRunning = other.bIsRunning;

	other.InternalThread = nullptr;
	other.InternalID = 0;
	other.bIsRunning = false;

	pEntryFunction = other.pEntryFunction;
	other.pEntryFunction = nullptr;

	return *this;
}

void SysThreadImpl_Windows::Create(ThreadFunc func)
{
	assert(!bIsRunning && "SysThreadImpl_Windows::Create called on an already-running thread");

	pEntryFunction = func;
	InternalThread = CreateThread(nullptr, 0, &SysThreadImpl_Windows::InternalEntrypoint, this, 0, &InternalID);

	assert(InternalThread != nullptr && "CreateThread failed");

	bIsRunning = true;
}

DWORD WINAPI SysThreadImpl_Windows::InternalEntrypoint(LPVOID arg)
{
	SysThreadImpl_Windows* impl = static_cast<SysThreadImpl_Windows*>(arg);
	if (impl->pEntryFunction != nullptr) {
		impl->pEntryFunction();
	}

	return 0;
}

void SysThreadImpl_Windows::Join()
{
	if (!bIsRunning) {
		return;
	}

	WaitForSingleObject(InternalThread, INFINITE);
	CloseHandle(InternalThread);
	InternalThread = nullptr;
	bIsRunning = false;
}

void SysThreadImpl_Windows::Detach()
{
	if (!bIsRunning) {
		return;
	}

	CloseHandle(InternalThread);
	InternalThread = nullptr;
	bIsRunning = false;
}

#endif // FX_THREADS_WINDOWS


/////////////////////////////////////
// Common SysThread functions
/////////////////////////////////////

SysThreadInternalType SysThread::GetCurrentTID()
{
#if defined(FX_THREADS_PTHREAD)
	return pthread_self();
#elif defined(FX_THREADS_WINDOWS)
	return GetCurrentThread();
#endif
}

bool SysThread::AreTIDsEqual(SysThreadInternalType a, SysThreadInternalType b)
{
#if defined(FX_THREADS_PTHREAD)
	return pthread_equal(a, b) != 0;
#elif defined(FX_THREADS_WINDOWS)
	return CompareObjectHandles(a, b);
#endif
}

SysThread::~SysThread()
{
	if (!IsRunning()) {
		return;
	}

	Panic("SysThread", "Thread was not joined or detached before being destroyed");
}


} // namespace fx

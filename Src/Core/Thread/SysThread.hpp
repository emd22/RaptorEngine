#pragma once

#include "ThreadID.hpp"

#include <Core/Defines.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <functional>

namespace fx {

#if defined(FX_PLATFORM_MACOS) || defined(FX_PLATFORM_LINUX)
#include <pthread.h>
#define FX_THREADS_PTHREAD 1
#elif defined(FX_PLATFORM_WINDOWS)
#define FX_THREADS_WINDOWS 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#error "Unsupported platform"
#endif


using ThreadFunc = std::function<void()>;

class SysThreadImplBase
{
public:
	virtual ~SysThreadImplBase() = default;

	virtual void Create(ThreadFunc func) = 0;
	virtual void Join() = 0;
	virtual void Detach() = 0;

	virtual uint32 GetInternalThreadID() const = 0;

	bool IsRunning() const { return bIsRunning; };


public:
	/// The created ID for the thread.
	ThreadID ID = 0;

	ThreadFunc pEntryFunction = nullptr;
	bool bIsRunning = false;
};


#ifdef FX_THREADS_PTHREAD
class SysThreadImpl_PThread : public SysThreadImplBase
{
public:
	SysThreadImpl_PThread();

	SysThreadImpl_PThread(const SysThreadImpl_PThread& other) = delete;
	SysThreadImpl_PThread& operator=(const SysThreadImpl_PThread& other) = delete;

	SysThreadImpl_PThread(SysThreadImpl_PThread&& other) noexcept;
	SysThreadImpl_PThread& operator=(SysThreadImpl_PThread&& other) noexcept;

	virtual void Create(ThreadFunc func) override;
	virtual void Join() override;
	virtual void Detach() override;
	virtual uint32 GetInternalThreadID() const override { return 0; }

	pthread_t GetInternalThread() const { return InternalThread; }

protected:
	static void* InternalEntrypoint(void* InArg);

private:
	pthread_t InternalThread = nullptr;
};

using SysThreadImplType = SysThreadImpl_PThread;
using SysThreadInternalType = pthread_t;

#endif


#ifdef FX_THREADS_WINDOWS
class SysThreadImpl_Windows : public SysThreadImplBase
{
public:
	SysThreadImpl_Windows();
	virtual ~SysThreadImpl_Windows();

	SysThreadImpl_Windows(const SysThreadImpl_Windows& other) = delete;
	SysThreadImpl_Windows& operator=(const SysThreadImpl_Windows& other) = delete;

	SysThreadImpl_Windows(SysThreadImpl_Windows&& other) noexcept;
	SysThreadImpl_Windows& operator=(SysThreadImpl_Windows&& other) noexcept;

	virtual void Create(ThreadFunc func) override;
	virtual void Join() override;
	virtual void Detach() override;

	HANDLE GetInternalThread() const { return InternalThread; }
	virtual uint32 GetInternalThreadID() const override { return InternalID; }

protected:
	static DWORD WINAPI InternalEntrypoint(LPVOID arg);

private:
	DWORD InternalID = 0;
	HANDLE InternalThread = nullptr;
};

using SysThreadImplType = SysThreadImpl_Windows;
using SysThreadInternalType = HANDLE;

#endif


class SysThread : public SysThreadImplType
{
public:
	SysThread() = default;
	explicit SysThread(ThreadFunc func) { Create(func); }

	SysThread(const SysThread& other) = delete;
	SysThread& operator=(const SysThread& other) = delete;

	SysThread(SysThread&& other) = default;
	SysThread& operator=(SysThread&& other) = default;

	FX_FORCE_INLINE const String& GetName() const { return Name; }

	static SysThreadInternalType GetCurrentTID();
	static bool AreTIDsEqual(SysThreadInternalType a, SysThreadInternalType b);

	~SysThread();

public:
	String Name;
};


} // namespace fx

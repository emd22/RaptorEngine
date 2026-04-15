#pragma once

#include "Defines.hpp"

#ifdef FX_PLATFORM_MACOS
#include <pthread.h>
#endif

class Thread
{
public:
    void Create();
    static void Join(Thread& thread);
    void Destroy();

    ~Thread() { Destroy(); }
#ifdef FX_PLATFORM_MACOS
    operator pthread_t() const { return mThread; }
    operator pthread_t*() { return &mThread; }

#endif

private:
#ifdef FX_PLATFORM_MACOS
    pthread_t mThread;
#endif
};

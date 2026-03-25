#include "FxPanic.hpp"

#include <Asset/AxManager.hpp>
#include <FxEngine.hpp>


void FxTerminate()
{
    AxManager::GetInstance().Shutdown();

    FX_BREAKPOINT;

    std::terminate();
}

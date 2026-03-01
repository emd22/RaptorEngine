
#define VMA_DEBUG_LOG(...) FxLogWarning(__VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/FxConfigFile.hpp>
#include <Asset/FxDataPack.hpp>
#include <FxEngine.hpp>
#include <Math/FxMathConsts.hpp>
#include <Math/FxMathUtil.hpp>
#include <Renderer/RxGlobals.hpp>

FX_SET_MODULE_NAME("Main")

int main()
{
    FxMemPool::GetGlobalPool().Create(100, FxUnitMebibyte);
    {
        FoxtrotGame game {};
    }


    FxEngineGlobalsDestroy();
    RxGlobals::Destroy();
}

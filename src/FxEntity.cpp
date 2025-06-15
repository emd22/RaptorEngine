#include "FxEntity.hpp"

FxEntityManager& FxEntityManager::GetGlobalManager()
{
    static FxEntityManager global_manager;
    return global_manager;
}

void FxEntityManager::Create(uint32 entities_per_page)
{
    GetGlobalManager().mEntityPool.Create(entities_per_page);
}

FxEntity* FxEntityManager::NewEntity()
{
    FxEntityManager& global_manager = GetGlobalManager();

    if (!global_manager.mEntityPool.IsInited()) {
        global_manager.Create();
    }

    return global_manager.mEntityPool.Insert();
}

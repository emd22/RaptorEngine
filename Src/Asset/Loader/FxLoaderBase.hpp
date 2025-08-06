#pragma once

#include <string>

#include "../FxAssetBase.hpp"

template <typename T>
class FxRef;

class FxLoaderBase
{
public:
    enum class Status
    {
        None,
        Success,
        Error,
    };

    FxLoaderBase()
    {
    }

    virtual Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) = 0;
    virtual Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) = 0;

    virtual void Destroy(FxRef<FxAssetBase>& asset) = 0;

    virtual ~FxLoaderBase()
    {
    }

    virtual void CreateGpuResource(FxRef<FxAssetBase>& asset) = 0;
protected:
    friend class FxAssetManager;
};

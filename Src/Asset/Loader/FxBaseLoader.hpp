#pragma once

#include <string>

#include "../FxBaseAsset.hpp"

template <typename T>
class FxRef;

class FxBaseLoader
{
public:
    enum class Status
    {
        None,
        Success,
        Error,
    };

    FxBaseLoader()
    {
    }

    virtual Status LoadFromFile(FxRef<FxBaseAsset> asset, const std::string& path) = 0;
    virtual Status LoadFromMemory(FxRef<FxBaseAsset> asset, const uint8* data, uint32 size) = 0;

    virtual void Destroy(FxRef<FxBaseAsset>& asset) = 0;

    virtual ~FxBaseLoader()
    {
    }

    virtual void CreateGpuResource(FxRef<FxBaseAsset>& asset) = 0;
protected:
    friend class FxAssetManager;
};

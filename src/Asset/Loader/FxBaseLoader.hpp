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

    virtual Status LoadFromFile(FxRef<FxBaseAsset>& asset, const std::string& path) = 0;
    virtual void Destroy(FxRef<FxBaseAsset>& asset) = 0;

    virtual ~FxBaseLoader()
    {
    }

    virtual void CreateGpuResource(FxRef<FxBaseAsset>& asset) = 0;
protected:
    friend class FxAssetManager;
};

#pragma once

#include <string>

#include "../FxBaseAsset.hpp"

// template <typename T>
// class FxRef;

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

    virtual Status LoadFromFile(std::shared_ptr<FxBaseAsset>& asset, const std::string& path) = 0;
    virtual void Destroy(std::shared_ptr<FxBaseAsset>& asset) = 0;

    virtual ~FxBaseLoader()
    {
    }

    virtual void CreateGpuResource(std::shared_ptr<FxBaseAsset>& asset) = 0;
protected:
    friend class FxAssetManager;
};

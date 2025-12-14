#pragma once

#include "../AxBase.hpp"

#include <string>

template <typename T>
class FxRef;

class AxLoaderBase
{
public:
    enum class Status
    {
        eNone,
        eSuccess,
        eError,
    };

    AxLoaderBase() {}

    virtual Status LoadFromFile(FxRef<AxBase> asset, const std::string& path) = 0;
    virtual Status LoadFromMemory(FxRef<AxBase> asset, const uint8* data, uint32 size) = 0;

    virtual void Destroy(FxRef<AxBase>& asset) = 0;

    virtual ~AxLoaderBase() {}

    virtual void CreateGpuResource(FxRef<AxBase>& asset) = 0;

protected:
    friend class AxManager;
};

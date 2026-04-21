#pragma once

#include "../AxBase.hpp"

#include <string>

namespace fx {

template <typename T>
class Ref;

class AxLoaderBase
{
public:
    enum class eStatus
    {
        None,
        Success,
        Error,
    };

    AxLoaderBase() {}

    virtual eStatus LoadFromFile(TSRef<AxBase> asset, const std::string& path) = 0;
    virtual eStatus LoadFromMemory(TSRef<AxBase> asset, const uint8* data, uint32 size) = 0;

    virtual void Destroy(TSRef<AxBase>& asset) = 0;

    virtual ~AxLoaderBase() {}

    virtual void CreateGpuResource(TSRef<AxBase>& asset) = 0;

protected:
    friend class AxManager;
};

} // namespace fx

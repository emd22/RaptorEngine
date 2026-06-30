#pragma once

#include "LoaderBase.hpp"

#include <Core/String.hpp>

namespace fx {

class ObjectID;

namespace loader {
class ObjectLoaderBase : public LoaderBase
{
public:
    eLoaderType GetLoaderType() const override { return eLoaderType::ObjectLoader; };

public:
    enum class eStatus
    {
        None,
        Success,
        Error,
    };

    ObjectLoaderBase() = default;

    virtual eLoaderStatus Load(const ObjectID& id, const String& path) = 0;
    virtual eLoaderStatus Load(const ObjectID& id, const uint8* data, uint32 size) = 0;

    virtual void CreateGpuResource(const ObjectID& id) = 0;

    virtual void Destroy() = 0;
    virtual ~ObjectLoaderBase() = default;

protected:
    friend class AxManager;
};


template <typename TLoader>
concept C_IsObjectLoader = std::is_base_of_v<ObjectLoaderBase, TLoader>;

} // namespace loader

} // namespace fx

#pragma once

#include <concepts>

namespace fx {

namespace loader {

enum class eLoaderStatus
{
    None,
    Success,
    Error,
};

enum class eLoaderType
{
    BaseLoader,
    ObjectLoader,
    ImageLoader,
};

class LoaderBase
{
public:
    virtual eLoaderType GetLoaderType() const { return eLoaderType::BaseLoader; };

public:
    LoaderBase() = default;

    virtual ~LoaderBase() = default;
};


template <typename TLoader>
concept C_IsLoader = std::is_base_of_v<LoaderBase, TLoader>;

} // namespace loader
} // namespace fx

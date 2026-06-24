#pragma once

#include <concepts>

namespace fx {

namespace loader {

#define FX_DEFINE_LOADER_TYPE(loader_type_) static const eLoaderType scLoaderType = loader_type_

enum class eLoaderType
{
    BaseLoader,
    ObjectLoader,
    ImageLoader,
};

class LoaderBase
{
public:
    FX_DEFINE_LOADER_TYPE(eLoaderType::BaseLoader);
};


template <typename TLoader>
concept C_IsLoader = std::is_base_of_v<LoaderBase, TLoader>;

} // namespace loader
} // namespace fx

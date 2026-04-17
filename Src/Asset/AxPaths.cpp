#include "AxPaths.hpp"

#include <Core/FilesystemIO.hpp>

// TODO: Find asset path based on current dir
const char* AssetPath(AxPathQuery query)
{
    switch (query) {
    case AxPathQuery::Shaders:
        return FX_BASE_DIR "/Shaders/";
    case AxPathQuery::DataPacks:
        return FX_BASE_DIR "/build/";
    }
}

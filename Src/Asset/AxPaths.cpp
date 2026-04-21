#include "AxPaths.hpp"

#include <Core/FilesystemIO.hpp>

// TODO: Find asset path based on current dir
const char* AssetPath(eAxPathQuery query)
{
    switch (query) {
    case eAxPathQuery::Shaders:
        return FX_BASE_DIR "/Shaders/";
    case eAxPathQuery::DataPacks:
        return FX_BASE_DIR "/build/";
    }
}

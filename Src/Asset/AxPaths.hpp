#pragma once

enum class eAxPathQuery
{
    Shaders,
    DataPacks,
};


const char* AssetPath(eAxPathQuery query);

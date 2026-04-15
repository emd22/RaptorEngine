#pragma once

enum class AxPathQuery
{
    eShaders,
    eDataPacks,
};


const char* AssetPath(AxPathQuery query);

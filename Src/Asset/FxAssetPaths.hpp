#pragma once

enum class FxAssetPathQuery
{
    eShaders,
    eDataPacks,
};


const char* FxAssetPath(FxAssetPathQuery query);

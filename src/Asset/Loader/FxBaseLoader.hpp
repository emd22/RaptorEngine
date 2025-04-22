#pragma once

#include <string>

#include "../FxBaseAsset.hpp"

class FxBaseLoader
{
public:
    enum class Status
    {
        Success,
        Error,
    };

    FxBaseLoader()
    {
    }

    virtual Status LoadFromFile(FxBaseAsset *asset, std::string path) = 0;
    virtual void Destroy(FxBaseAsset *asset) = 0;

    virtual ~FxBaseLoader()
    {
    }
};

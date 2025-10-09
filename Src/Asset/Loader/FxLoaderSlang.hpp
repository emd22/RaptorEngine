#pragma once

#include "FxLoaderBase.hpp"

#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>

class FxLoaderSlang : public FxLoaderBase
{
public:
    using Status = FxLoaderBase::Status;

    FxLoaderSlang() = default;

    Status LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<FxAssetBase>& asset) override;

    ~FxLoaderSlang() override {}

protected:
    void CreateGpuResource(FxRef<FxAssetBase>& asset) override;

private:
};

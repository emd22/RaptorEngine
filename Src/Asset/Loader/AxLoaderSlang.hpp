#pragma once

#include "AxLoaderBase.hpp"

#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>

class AxLoaderSlang : public AxLoaderBase
{
public:
    using Status = AxLoaderBase::Status;

    AxLoaderSlang() = default;

    Status LoadFromFile(FxRef<AxBase> asset, const std::string& path) override;
    Status LoadFromMemory(FxRef<AxBase> asset, const uint8* data, uint32 size) override;

    void Destroy(FxRef<AxBase>& asset) override;

    ~AxLoaderSlang() override {}

protected:
    void CreateGpuResource(FxRef<AxBase>& asset) override;

private:
};

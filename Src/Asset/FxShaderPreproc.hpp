#pragma once

#include <Core/FxSlice.hpp>
#include <Core/FxTypes.hpp>
#include <Renderer/Backend/RxShader.hpp>

namespace FxShaderPreproc {
struct Result
{
public:
    Result() = default;
    FxSlice<char>& GetShaderBounds() { return ProgramData[static_cast<uint32>(CurrentType)]; }
    void SetShaderBounds(const FxSlice<char>& bounds) { GetShaderBounds() = bounds; }
    void SetCurrentShader(RxShaderType type) { CurrentType = type; }

public:
    std::array<FxSlice<char>, RxShaderUtil::scNumShaderTypes> ProgramData = { { FxSlice<char>(nullptr),
                                                                                FxSlice<char>(nullptr) } };
    RxShaderType CurrentType = RxShaderType::eVertex;
};

Result Process(const FxSlice<char>& data);
}; // namespace FxShaderPreproc

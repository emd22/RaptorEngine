#pragma once

#include <Core/FxSlice.hpp>
#include <Core/FxTypes.hpp>
#include <Renderer/Backend/RxShader.hpp>
#include <vector>


namespace FxShaderPreproc {

enum EntryType : uint16
{
    eStructuredBuffer,
    eUniformBuffer,
    eSampler2D,
};

struct Entry
{
    Entry() = delete;
    Entry(EntryType type, uint8 set, uint8 binding) : Type(type), Set(set), Binding(binding) {}

    EntryType Type;
    uint8 Set;
    uint8 Binding;
};


struct Result
{
public:
    Result() = default;
    FxSlice<char>& GetShaderBounds() { return ProgramData[static_cast<uint32>(CurrentType)]; }
    FxSlice<char>& GetShaderBounds(RxShaderType type) { return ProgramData[static_cast<uint32>(type)]; }
    void SetShaderBounds(const FxSlice<char>& bounds) { GetShaderBounds() = bounds; }
    void SetCurrentShader(RxShaderType type) { CurrentType = type; }

public:
    std::array<FxSlice<char>, RxShaderUtil::scNumShaderTypes> ProgramData = { {
        FxSlice<char>(nullptr),
        FxSlice<char>(nullptr),
    } };

    RxShaderType CurrentType = RxShaderType::eVertex;

    std::vector<Entry> Reflection;
};


/**
 * @brief Processes a shader file and retrieves each program and associated reflection data.
 */
Result Process(const FxSlice<char>& data);


}; // namespace FxShaderPreproc

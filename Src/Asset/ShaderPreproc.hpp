#pragma once

#include <Core/DynArray.hpp>
#include <Core/Slice.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/Shader.hpp>
#include <array>
#include <vector>

namespace fx {

namespace ShaderPreproc {


using DataBuffer = DynArray<char, GrowthFunctions::InPages>;

enum eEntryType : uint16
{
    StructuredBuffer,
    UniformBuffer,
    Sampler2D,
};

struct Entry
{
    Entry() = delete;
    Entry(eEntryType type, uint8 set, uint8 binding) : Type(type), Set(set), Binding(binding) {}

    eEntryType Type;
    uint8 Set;
    uint8 Binding;
};


struct Result
{
public:
    Result() = default;

    DataBuffer& GetBuffer() { return ProgramData[static_cast<uint32>(CurrentType)]; }
    DataBuffer& GetBuffer(renderer::eShaderType type) { return ProgramData[static_cast<uint32>(type)]; }

    void InsertString(const std::string& str)
    {
        DataBuffer& buffer = GetBuffer();
        for (const char ch : str) {
            buffer.Insert(ch);
        }
    }

    void SetCurrentShader(renderer::eShaderType type)
    {
        bBroadcastToAllPrograms = false;
        CurrentType = type;
    }

public:
    std::array<DataBuffer, renderer::ShaderUtil::scNumShaderTypes> ProgramData;

    renderer::eShaderType CurrentType = renderer::eShaderType::Vertex;

    // Before a program definition, broadcast to all
    bool bBroadcastToAllPrograms = true;

    std::vector<Entry> Reflection;
};


/**
 * @brief Processes a shader file and retrieves each program and associated reflection data.
 */
Result Process(const Slice<char>& data, const SizedArray<ShaderMacro>& macros);
void DebugSaveToDisk(const char* name, const Result& result);

}; // namespace ShaderPreproc

} // namespace fx

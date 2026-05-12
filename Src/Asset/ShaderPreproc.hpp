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

using ReflectionList = std::vector<ShaderReflectionEntry>;


struct Result
{
public:
    Result() = default;

    DataBuffer& GetBuffer() { return ProgramData[static_cast<uint32>(CurrentType)]; }
    DataBuffer& GetBuffer(eShaderType type) { return ProgramData[static_cast<uint32>(type)]; }

    ReflectionList& GetReflection() { return ReflectionData[static_cast<uint32>(CurrentType)]; }
    ReflectionList& GetReflection(eShaderType type) { return ReflectionData[static_cast<uint32>(type)]; }

    void InsertString(const std::string& str)
    {
        DataBuffer& buffer = GetBuffer();
        for (const char ch : str) {
            buffer.Insert(ch);
        }
    }

    void SetCurrentShader(eShaderType type)
    {
        bBroadcastToAllPrograms = false;
        CurrentType = type;
    }


public:
    std::array<DataBuffer, renderer::ShaderUtil::scNumShaderTypes> ProgramData;
    std::array<ReflectionList, renderer::ShaderUtil::scNumShaderTypes> ReflectionData;

    eShaderType CurrentType = eShaderType::Vertex;

    // Before a program definition, broadcast to all
    bool bBroadcastToAllPrograms = true;

    /// Reflection data read in by the preprocessor.
};


/**
 * @brief Processes a shader file and retrieves each program and associated reflection data.
 */
Result Process(const Slice<char>& data, const SizedArray<ShaderMacro>& macros);
void DebugSaveToDisk(const char* name, const Result& result);

}; // namespace ShaderPreproc

} // namespace fx

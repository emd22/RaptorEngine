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

enum eReflectionEntryType : uint16
{
    StructuredBuffer,
    UniformBuffer,
    Texture,
};

struct ReflectionEntry
{
    ReflectionEntry() = delete;
    ReflectionEntry(eReflectionEntryType type, uint8 set, uint8 binding) : Type(type), Set(set), Binding(binding) {}

    uint32 AsUInt() const
    {
        const uint32 value = (static_cast<uint32>(Type) << 16) | (static_cast<uint32>(Set) << 8) |
                             static_cast<uint32>(Binding);
        return value;
    }

    static ReflectionEntry FromUInt(uint32 value)
    {
        ReflectionEntry entry(static_cast<eReflectionEntryType>(static_cast<uint16>(value >> 16)),
                              static_cast<uint8>(value >> 8), static_cast<uint8>(value));

        return entry;
    }

public:
    eReflectionEntryType Type;
    uint8 Set;
    uint8 Binding;
};

using ReflectionList = std::vector<ReflectionEntry>;


struct Result
{
public:
    Result() = default;

    DataBuffer& GetBuffer() { return ProgramData[static_cast<uint32>(CurrentType)]; }
    DataBuffer& GetBuffer(eShaderType type) { return ProgramData[static_cast<uint32>(type)]; }

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

    eShaderType CurrentType = eShaderType::Vertex;

    // Before a program definition, broadcast to all
    bool bBroadcastToAllPrograms = true;

    /// Reflection data read in by the preprocessor.
    ReflectionList Reflection;
};


/**
 * @brief Processes a shader file and retrieves each program and associated reflection data.
 */
Result Process(const Slice<char>& data, const SizedArray<ShaderMacro>& macros);
void DebugSaveToDisk(const char* name, const Result& result);

}; // namespace ShaderPreproc

} // namespace fx

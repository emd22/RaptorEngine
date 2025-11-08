#pragma once

#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>

enum class RxShaderType
{
    eUnknown,
    eVertex,
    eFragment,
};

constexpr FX_FORCE_INLINE VkShaderStageFlagBits RxShaderTypeToVkShaderStage(RxShaderType type)
{
    switch (type) {
    case RxShaderType::eUnknown:
        FxLogError("Shader stage is RxShaderType::eUnknown!");
        [[fallthrough]];
    case RxShaderType::eVertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case RxShaderType::eFragment:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    return VK_SHADER_STAGE_VERTEX_BIT;
}

class RxShader
{
public:
    RxShader() = default;
    RxShader(nullptr_t np) : ShaderModule(nullptr), Type(RxShaderType::eUnknown) {}

    RxShader(const char* path, RxShaderType type, const FxSizedArray<FxShaderMacro>& macros) : Type(type)
    {
        Load(path, type, macros);
    }

    void Load(const char* path, RxShaderType type, const FxSizedArray<FxShaderMacro>& macros);

    FX_FORCE_INLINE bool operator==(nullptr_t np) const { return ShaderModule == nullptr; }

    FX_FORCE_INLINE VkShaderStageFlagBits GetStageBit() const
    {
        switch (Type) {
        case RxShaderType::eUnknown:
            FxLogError("Shader stage is RxShaderType::Unknown!");
            [[fallthrough]];
        case RxShaderType::eVertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case RxShaderType::eFragment:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        return VK_SHADER_STAGE_VERTEX_BIT;
    }

    void Destroy();

    ~RxShader() { Destroy(); }

private:
    void CreateShaderModule(uint32 file_size, uint32* shader_data);

public:
    VkShaderModule ShaderModule = nullptr;
    RxShaderType Type = RxShaderType::eUnknown;
};

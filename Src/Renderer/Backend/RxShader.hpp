#pragma once

#include "ShaderList.hpp"

#include <Core/FxTypes.hpp>

class RxShader
{
public:
    RxShader() = default;
    RxShader(nullptr_t np) : ShaderModule(nullptr), Type(RxShaderType::Unknown) {}

    RxShader(const char* path, RxShaderType type) : Type(type) { Load(path, type); }

    void Load(const char* path, RxShaderType type);

    FX_FORCE_INLINE bool operator==(nullptr_t np) const { return ShaderModule == nullptr; }

    FX_FORCE_INLINE VkShaderStageFlagBits GetStageBit() const
    {
        switch (Type) {
        case RxShaderType::Unknown:
            FxLogError("Shader stage is RxShaderType::Unknown!");
            [[fallthrough]];
        case RxShaderType::Vertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case RxShaderType::Fragment:
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
    RxShaderType Type = RxShaderType::Unknown;
};

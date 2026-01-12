#pragma once

#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxRef.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>

enum class RxShaderType
{
    eUnknown,
    eVertex,
    eFragment,
};

class RxShaderUtil
{
public:
    /**
     * @brief Get the underlying Vulkan shader stage bit for an RxShaderType.
     */
    static constexpr VkShaderStageFlagBits ToUnderlyingType(RxShaderType type)
    {
        switch (type) {
        case RxShaderType::eUnknown:
            FxLogError("Shader stage is RxShaderType::eUnknown!");
            break;

        case RxShaderType::eVertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case RxShaderType::eFragment:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        return VK_SHADER_STAGE_VERTEX_BIT;
    }
};


class RxShaderProgram
{
public:
    RxShaderProgram() = default;
    RxShaderProgram(nullptr_t np) : pShader(nullptr), ShaderType(RxShaderType::eUnknown) {}

    RxShaderProgram(RxShaderProgram&& other)
    {
        pShader = other.pShader;
        ShaderType = other.ShaderType;

        other.pShader = nullptr;
        other.ShaderType = RxShaderType::eUnknown;
    }

    void Destroy();
    ~RxShaderProgram() { Destroy(); }

    FX_FORCE_INLINE bool operator==(nullptr_t np) const { return pShader == nullptr; }

private:
public:
    VkShaderModule pShader = nullptr;
    RxShaderType ShaderType = RxShaderType::eUnknown;
};

class RxShader
{
public:
    static FxHash64 GenerateShaderId(RxShaderType type, const FxSizedArray<FxShaderMacro>& macros);

    RxShader() = delete;
    RxShader(const char* path) { Load(path); }

    FxRef<RxShaderProgram> GetProgram(RxShaderType shader_type, const FxSizedArray<FxShaderMacro>& macros);

    void Load(const char* path);

private:
    void CreateShaderModule(uint32 file_size, uint32* shader_data, VkShaderModule& shader_module);

    /**
     * @brief Fetches all compiled shader permutations from the datapack if the pack exists.
     */
    bool PreloadCompiledPrograms(const std::string& pack_path);

    void RecompileShader(const std::string& source_path, const std::string& output_path,
                         const FxSizedArray<FxShaderMacro>& macros);

    const std::string GetSourcePath() const;
    const std::string GetProgramPath() const;

private:
    std::string Name = "Unknown";

    FxDataPack mDataPack;
};

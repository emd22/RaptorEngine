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

class RxShader;

namespace RxShaderUtil {
static constexpr uint32 scNumShaderTypes = static_cast<uint32>(RxShaderType::eFragment);

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
}; // namespace RxShaderUtil


class RxShaderProgram
{
public:
    RxShaderProgram() = default;
    RxShaderProgram(nullptr_t np) : InternalShader(nullptr), ShaderType(RxShaderType::eUnknown) {}

    RxShaderProgram(RxShaderProgram&& other)
    {
        InternalShader = other.InternalShader;
        ShaderType = other.ShaderType;
        pShader = other.pShader;

        other.InternalShader = nullptr;
        other.ShaderType = RxShaderType::eUnknown;
        other.pShader = nullptr;
    }

    void Destroy();

    ~RxShaderProgram() { Destroy(); }

    FX_FORCE_INLINE bool operator==(nullptr_t np) const { return InternalShader == nullptr; }

    FX_FORCE_INLINE VkShaderModule& Get()
    {
        FxAssert(InternalShader != nullptr);
        return InternalShader;
    }

public:
    VkShaderModule InternalShader = nullptr;
    RxShader* pShader = nullptr;

    RxShaderType ShaderType = RxShaderType::eUnknown;
};

class RxShader
{
    /**
     * @brief Holds a collection of shader programs that have already been loaded from the DataPack or created with the
     * ShaderCompiler.
     *
     * A cached shader can be retrieved from mCachedTypes using the shader type as a key, using the helper function
     * `RetrieveCachedShaderProgram`.
     *
     * Each of these programs should be
     */
    struct ProgramCache
    {
        std::unordered_map<FxHash64, FxRef<RxShaderProgram>> Programs;
    };

public:
    static FxHash64 GenerateShaderId(RxShaderType type, const FxSizedArray<FxShaderMacro>& macros);

    RxShader() = delete;
    RxShader(const char* path) { Load(path); }

    /**
     * @brief Returns a cached program if it has previously been queried or loads the uncached version from disk.
     */
    FxRef<RxShaderProgram> GetProgram(RxShaderType shader_type, const FxSizedArray<FxShaderMacro>& macros);

    /**
     * @brief Loads a shader program from the DataPack or compiles it if it does not exist.
     */
    FxRef<RxShaderProgram> LoadUncachedProgram(RxShaderType shader_type, const FxSizedArray<FxShaderMacro>& macros);

    void Load(const char* path);

    const std::string& GetName() const { return Name; }

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

    /// A list of shader types (that hold shader programs) that have already been retreived from the datapack or
    /// created.
    FxStackArray<ProgramCache, RxShaderUtil::scNumShaderTypes + 1> mCachedTypes;

    FxDataPack mDataPack;
};

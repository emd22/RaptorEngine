#pragma once

#include <Asset/FxShaderCompiler.hpp>
#include <Core/FxRef.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxTypes.hpp>

enum class RxShaderType
{
    eVertex,
    eFragment,
};

class RxShader;


namespace RxShaderUtil {
static constexpr uint32 scNumShaderTypes = static_cast<uint32>(RxShaderType::eFragment) + 1;

/**
 * @brief Get the underlying Vulkan shader stage bit for an RxShaderType.
 */
static constexpr VkShaderStageFlagBits ToUnderlyingType(RxShaderType type)
{
    switch (type) {
    case RxShaderType::eVertex:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case RxShaderType::eFragment:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    return VK_SHADER_STAGE_VERTEX_BIT;
}

static FX_FORCE_INLINE const char* TypeToName(RxShaderType type)
{
    switch (type) {
    case RxShaderType::eVertex:
        return "Vertex";
    case RxShaderType::eFragment:
        return "Fragment";
    }
    return "Unknown";
}
}; // namespace RxShaderUtil


enum class RxShaderDescriptorType : uint16
{
    eStructuredBuffer,
    eUniformBuffer,
    eSampler2D,
};

struct RxShaderDescriptorEntry
{
    RxShaderDescriptorEntry() = default;
    RxShaderDescriptorEntry(RxShaderDescriptorType type, bool use_dynamic_type, FxHash32 name_hash, uint32 set,
                            uint32 binding)
        : Type(type), bUseDynamicType(static_cast<uint16>(use_dynamic_type)), NameHash(name_hash), Set(set),
          Binding(binding)
    {
    }

    RxShaderDescriptorType Type; // uint16
    uint16 bUseDynamicType = 0;
    FxHash32 NameHash = 0;
    uint32 Set = 0;
    uint32 Binding = 0;
};


struct RxShaderOutline
{
public:
    RxShaderOutline() { PushConstantSizes.MarkFull(); }

    FxHash64 GetHash()
    {
        if (mHash == FxHashNull64 || mbOutOfDate) {
            mHash = FxHashData64(FxSlice(DescriptorEntries));
            mbOutOfDate = false;
        }

        return mHash;
    }

    uint32 GetReflectionSize() const;

    void WriteToBuffer(uint32* raw_buffer) const;

    /**
     * @brief Reads the outline from a data buffer
     * @returns The size of the outline
     */
    uint32 ReadFromBuffer(const FxSlice<uint32>& data);

    void AddDescriptor(RxShaderDescriptorType type, bool use_dynamic_type, FxHash32 name_hash, uint32 set,
                       uint32 binding)
    {
        if (!DescriptorEntries.IsInited()) {
            DescriptorEntries.InitCapacity(16);
        }

        mbOutOfDate = true;
        DescriptorEntries.Insert(RxShaderDescriptorEntry(type, use_dynamic_type, name_hash, set, binding));
    }

    bool operator==(RxShaderOutline& other)
    {
        const bool pcs_match = memcmp(PushConstantSizes.pData, other.PushConstantSizes.pData,
                                      other.PushConstantSizes.GetSizeInBytes());

        return pcs_match && (GetHash() == other.GetHash());
    }

public:
    FxSizedArray<RxShaderDescriptorEntry> DescriptorEntries;
    FxStackArray<uint32, RxShaderUtil::scNumShaderTypes> PushConstantSizes = { 0, 0 };

private:
    FxHash64 mHash = FxHashNull64;

    bool mbOutOfDate : 1 = false;
};


class RxShaderProgram
{
public:
    RxShaderProgram() = default;
    RxShaderProgram(nullptr_t np) : InternalShader(nullptr), ShaderType(RxShaderType::eVertex) {}

    RxShaderProgram(RxShaderProgram&& other)
    {
        InternalShader = other.InternalShader;
        ShaderType = other.ShaderType;
        pShader = other.pShader;

        other.InternalShader = nullptr;
        other.ShaderType = RxShaderType::eVertex;
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

    RxShaderOutline Outline;

    RxShaderType ShaderType = RxShaderType::eVertex;
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
    RxShader(const char* path)
    {
        mCachedTypes.MarkFull();
        Load(path);
    }

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
    void CreateShaderModule(RxShaderProgram& program, uint32 file_size, uint32* shader_data,
                            VkShaderModule& shader_module);

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
    FxStackArray<ProgramCache, RxShaderUtil::scNumShaderTypes> mCachedTypes;

    FxDataPack mDataPack;
};

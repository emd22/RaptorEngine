#pragma once

#include <Asset/ShaderCompiler.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <unordered_map>


namespace fx::renderer {

enum class RxShaderType : uint16
{
    eVertex,
    eFragment,
};

class RxShader;
class RxCommandBuffer;
class RxPipeline;

using RxShaderId = Hash64;

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


enum class RxShaderOutlineEntryType : uint16
{
    eStructuredBuffer,
    eUniformBuffer,
    eSampler2D,
};

class RxShaderOutlineUtil
{
public:
    static const char* GetTypeName(RxShaderOutlineEntryType type)
    {
        switch (type) {
        case RxShaderOutlineEntryType::eStructuredBuffer:
            return "Structured Buffer";
        case RxShaderOutlineEntryType::eUniformBuffer:
            return "Uniform Buffer";
        case RxShaderOutlineEntryType::eSampler2D:
            return "Sampler 2D";
        }
        return "Unknown";
    }
};

struct RxShaderOutlineEntry
{
    RxShaderOutlineEntry() = default;
    RxShaderOutlineEntry(RxShaderOutlineEntryType type, RxShaderType shader_type, bool use_dynamic_type,
                         Hash32 name_hash, uint32 set, uint32 binding)
        : Type(type), ShaderType(shader_type), bUseDynamicType(static_cast<uint16>(use_dynamic_type)),
          NameHash(name_hash), Set(set), Binding(binding)
    {
    }

    RxShaderOutlineEntryType Type; // uint16
    RxShaderType ShaderType;
    uint16 bUseDynamicType = 0;
    Hash32 NameHash = 0;
    uint32 Set = 0;
    uint32 Binding = 0;
};


struct RxShaderDescriptorId
{
    uint32 Set = 0;
    /// Hash of the outline entries and the shader type
    Hash32 Hash = HashNull32;

    bool bContainsDynamicEntry = false;
};


struct RxShaderOutline
{
public:
    /// The maximum number of sets that can be bound in shader
    static constexpr uint32 scNumSets = 6;
    static constexpr uint32 scMaxEntriesPerBucket = 10;

    using DescEntry = RxShaderOutlineEntry;
    using DescType = RxShaderOutlineEntryType;

    using EntryList = SizedArray<DescEntry>;

public:
    RxShaderOutline()
    {
        PushConstantSizes.MarkFull();
        SetBuckets.MarkFull();
    }

    void Print() const;
    uint32 GetReflectionSize() const;

    void WriteToBuffer(uint32* raw_buffer) const;

    /**
     * @brief Reads the outline from a data buffer
     * @returns The size of the outline
     */
    uint32 ReadFromBuffer(const Slice<uint32>& data);

    void AddEntry(RxShaderOutlineEntryType type, RxShaderType shader_type, bool use_dynamic_type, Hash32 name_hash,
                  uint32 set, uint32 binding)
    {
        SizedArray<DescEntry>& bucket = SetBuckets[set];

        if (!bucket.IsInited()) {
            bucket.InitCapacity(scMaxEntriesPerBucket);
        }

        bucket.Insert(RxShaderOutlineEntry(type, shader_type, use_dynamic_type, name_hash, set, binding));

        ++DescriptorEntryCount;
    }

public:
    StackArray<EntryList, scNumSets> SetBuckets;

    // SizedArray<RxShaderDescriptorEntry> DescriptorEntries;
    StackArray<uint32, RxShaderUtil::scNumShaderTypes> PushConstantSizes = { 0, 0 };

    /// The number of descriptor entries stored.
    uint32 DescriptorEntryCount = 0;

private:
};

struct RxShaderBindOptions
{
    bool bUseOffset = false;
    uint32 BufferOffset = 0;
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
        ShaderOutline = other.ShaderOutline;
        Descriptors.CloneFrom(other.Descriptors);

        other.InternalShader = nullptr;
        other.pShader = nullptr;
    }

    void BuildDescriptors();

    void Bind(const RxCommandBuffer& cmd, const RxPipeline& pipeline, const RxShaderBindOptions& bind_options);

    void Destroy();

    ~RxShaderProgram() { Destroy(); }

    FX_FORCE_INLINE bool operator==(nullptr_t np) const { return InternalShader == nullptr; }

    FX_FORCE_INLINE VkShaderModule& Get()
    {
        Assert(InternalShader != nullptr);
        return InternalShader;
    }

public:
    VkShaderModule InternalShader = nullptr;
    RxShader* pShader = nullptr;

    Ref<RxShaderOutline> ShaderOutline { nullptr };

    SizedArray<RxShaderDescriptorId> Descriptors;

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
        std::unordered_map<RxShaderId, Ref<RxShaderProgram>, Hash64Stl> Programs;
    };

public:
    static RxShaderId GenerateShaderId(RxShaderType type, const SizedArray<ShaderMacro>& macros);

    RxShader() = delete;
    RxShader(const char* path)
    {
        mCachedTypes.MarkFull();
        Load(path);
    }

    /**
     * @brief Returns a cached program if it has previously been queried or loads the uncached version from disk.
     */
    Ref<RxShaderProgram> GetProgram(RxShaderType shader_type, const SizedArray<ShaderMacro>& macros);

    /**
     * @brief Loads a shader program from the DataPack or compiles it if it does not exist.
     */
    Ref<RxShaderProgram> LoadUncachedProgram(RxShaderType shader_type, const SizedArray<ShaderMacro>& macros);

    void Load(const char* path);

    const String& GetName() const { return Name; }

private:
    void CreateShaderModule(RxShaderProgram& program, uint32 file_size, uint32* shader_data,
                            VkShaderModule& shader_module);

    /**
     * @brief Fetches all compiled shader permutations from the datapack if the pack exists.
     */
    bool PreloadCompiledPrograms(const char* pack_path);

    void RecompileShader(const String& source_path, const String& output_path, const SizedArray<ShaderMacro>& macros);

    const String GetSourcePath() const;
    const String GetProgramPath() const;

private:
    String Name = "Unknown";

    /// A list of shader types (that hold shader programs) that have already been retreived from the datapack or
    /// created.
    StackArray<ProgramCache, RxShaderUtil::scNumShaderTypes> mCachedTypes;

    DataPack mDataPack;
};

} // namespace fx::renderer

#pragma once

#include "ShaderReflection.hpp"
#include "ShaderType.hpp"

#include <Asset/ShaderCompiler.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Core/HashMap.hpp>


namespace fx {


struct ProgramData
{
	SizedArray<ShaderReflectionEntry> Reflection;
	Slice<uint8> pProgramData;

	FX_FORCE_INLINE bool HasData() const { return pProgramData.Size > 0; }
	FX_FORCE_INLINE bool IsValid() const { return pProgramData.pData != nullptr && HasData(); }
};


namespace renderer {

class Shader;
class CommandBuffer;
class Pipeline;

static_assert(sizeof(ShaderReflectionEntry) == sizeof(RxReflectionEntry));

/**
 * @brief A reference to a compiled shader program. The program lives in a record owned by Rust, copies of this
 * share it, and the shader module is destroyed with the last copy. A default constructed program is null.
 */
class ShaderProgram
{
public:
	ShaderProgram() = default;
	ShaderProgram(nullptr_t np) {}

	ShaderProgram(const ShaderProgram& other);
	ShaderProgram(ShaderProgram&& other) noexcept;

	ShaderProgram& operator=(const ShaderProgram& other);
	ShaderProgram& operator=(ShaderProgram&& other) noexcept;

	/**
	 * @brief Creates the program from SPIR-V and its reflection data.
	 *
	 * @param shader The shader the program was loaded from, for reporting.
	 * @return A null program if the shader module could not be created.
	 */
	static ShaderProgram Create(eShaderType type, Shader* shader, const uint32* code, uint32 word_count,
								const SizedArray<ShaderReflectionEntry>& reflection);

	FX_FORCE_INLINE bool IsValid() const { return mpRecord != nullptr; }
	FX_FORCE_INLINE bool operator==(nullptr_t np) const { return mpRecord == nullptr; }
	FX_FORCE_INLINE bool operator!=(nullptr_t np) const { return mpRecord != nullptr; }

	FX_FORCE_INLINE VkShaderModule Get() const
	{
		Assert(mpRecord != nullptr);
		return reinterpret_cast<VkShaderModule>(mpRecord->module);
	}

	FX_FORCE_INLINE eShaderType GetType() const
	{
		return (mpRecord != nullptr) ? static_cast<eShaderType>(mpRecord->shader_type) : eShaderType::Vertex;
	}

	FX_FORCE_INLINE Shader* GetShader() const
	{
		return (mpRecord != nullptr) ? static_cast<Shader*>(mpRecord->user) : nullptr;
	}

	/// Bit N is set if the program declares a stage input at location N. All bits are set if unknown.
	FX_FORCE_INLINE uint32 GetInputLocationMask() const
	{
		return (mpRecord != nullptr) ? mpRecord->input_location_mask : ~0U;
	}

	FX_FORCE_INLINE const ShaderReflectionEntry* GetReflectionData() const
	{
		return (mpRecord != nullptr) ? reinterpret_cast<const ShaderReflectionEntry*>(mpRecord->reflection) : nullptr;
	}

	FX_FORCE_INLINE uint32 GetReflectionCount() const
	{
		return (mpRecord != nullptr) ? static_cast<uint32>(mpRecord->reflection_count) : 0;
	}

	~ShaderProgram();

private:
	void Release();

private:
	RxShaderProgram* mpRecord = nullptr;
};

class Shader
{
	/**
	 * @brief Holds a collection of shader programs that have already been loaded from the DataPack or created with
	 * the ShaderCompiler.
	 *
	 * A cached shader can be retrieved from mCachedTypes using the shader type as a key, using the helper function
	 * `RetrieveCachedShaderProgram`.
	 *
	 * Each of these programs should be
	 */
	struct ProgramCache
	{
		HashMap<ShaderId, ShaderProgram> Programs;
	};

public:
	static ShaderId GenerateShaderId(eShaderType type, const SizedArray<ShaderMacro>& macros);

	/**
	 * @brief Folds a macro list into `hash` by its contents
	 */
	static Hash64 HashMacros(const SizedArray<ShaderMacro>& macros, Hash64 hash = FX_HASH64_FNV1A_INIT);

	Shader() = delete;
	Shader(const char* path)
	{
		mCachedTypes.MarkFull();
		Load(path);
	}

	/**
	 * @brief Returns a cached program if it has previously been queried or loads the uncached version from disk.
	 */
	ShaderProgram GetProgram(eShaderType shader_type, const SizedArray<ShaderMacro>& macros);

	/**
	 * @brief Loads a shader program from the DataPack or compiles it if it does not exist.
	 */
	ShaderProgram LoadUncachedProgram(eShaderType shader_type, const SizedArray<ShaderMacro>& macros);

	void Load(const char* path);

	const String& GetName() const { return Name; }

private:
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
	StackArray<ProgramCache, ShaderUtil::scNumShaderTypes> mCachedTypes;

	DataPack mDataPack;
};

} // namespace renderer

} // namespace fx

#pragma once

#include "ShaderReflection.hpp"
#include "ShaderType.hpp"

#include <Asset/ShaderCompiler.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <unordered_map>


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

struct ShaderDescriptorId
{
	uint32 Set = 0;
	/// Hash of the outline entries and the shader type
	Hash32 Hash = HashNull32;

	bool bContainsDynamicEntry = false;
};


struct ShaderBindOptions
{
	bool bUseOffset = false;
	uint32 BufferOffset = 0;
};


class ShaderProgram
{
public:
	ShaderProgram() = default;
	ShaderProgram(nullptr_t np) : InternalShader(nullptr), ShaderType(eShaderType::Vertex) {}

	ShaderProgram(ShaderProgram&& other)
	{
		InternalShader = other.InternalShader;
		ShaderType = other.ShaderType;
		pShader = other.pShader;
		Reflection = std::move(other.Reflection);

		other.InternalShader = nullptr;
		other.pShader = nullptr;
	}

	void BuildDescriptor();

	void Bind(const CommandBuffer& cmd, const Pipeline& pipeline, const ShaderBindOptions& bind_options);

	FX_FORCE_INLINE bool operator==(nullptr_t np) const { return InternalShader == nullptr; }

	FX_FORCE_INLINE VkShaderModule& Get()
	{
		Assert(InternalShader != nullptr);
		return InternalShader;
	}

	void PrintReflection();

	void Destroy();
	~ShaderProgram() { Destroy(); }

public:
	VkShaderModule InternalShader = nullptr;
	Shader* pShader = nullptr;

	SizedArray<ShaderReflectionEntry> Reflection;
	eShaderType ShaderType = eShaderType::Vertex;
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
		std::unordered_map<ShaderId, Ref<ShaderProgram>, Hash64Stl> Programs;
	};

public:
	static ShaderId GenerateShaderId(eShaderType type, const SizedArray<ShaderMacro>& macros);

	Shader() = delete;
	Shader(const char* path)
	{
		mCachedTypes.MarkFull();
		Load(path);
	}

	/**
	 * @brief Returns a cached program if it has previously been queried or loads the uncached version from disk.
	 */
	Ref<ShaderProgram> GetProgram(eShaderType shader_type, const SizedArray<ShaderMacro>& macros);

	/**
	 * @brief Loads a shader program from the DataPack or compiles it if it does not exist.
	 */
	Ref<ShaderProgram> LoadUncachedProgram(eShaderType shader_type, const SizedArray<ShaderMacro>& macros);

	void Load(const char* path);

	const String& GetName() const { return Name; }

private:
	void CreateShaderModule(ShaderProgram& program, uint32 file_size, uint32* shader_data,
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
	StackArray<ProgramCache, ShaderUtil::scNumShaderTypes> mCachedTypes;

	DataPack mDataPack;
};

} // namespace renderer

} // namespace fx

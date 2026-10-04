#include "Shader.hpp"

#include "DescriptorCache.hpp"
#include "Engine.hpp"

#include <Asset/AxPaths.hpp>
#include <Asset/ShaderCompiler.hpp>
#include <Asset/ShaderPreproc.hpp>
#include <Core/File.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/RefUtil.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>


#define DEBUG_FORCE_OUT_OF_DATE 1

namespace fx::renderer {

/////////////////////////////////////
// Shader Functions
/////////////////////////////////////

static_assert(sizeof(ShaderMacro) == sizeof(RxShaderMacroRef));

Hash64 Shader::HashMacros(const SizedArray<ShaderMacro>& macros, Hash64 hash)
{
	return rx_shader_hash_macros(reinterpret_cast<const RxShaderMacroRef*>(macros.pData), macros.Size, hash);
}

ShaderId Shader::GenerateShaderId(eShaderType type, const SizedArray<ShaderMacro>& macros)
{
	return rx_shader_id(static_cast<uint32>(type), reinterpret_cast<const RxShaderMacroRef*>(macros.pData),
						macros.Size);
}

bool Shader::PreloadCompiledPrograms(const char* pack_path)
{
	bool did_read = mDataPack.ReadFromFile(pack_path);

	if (!did_read) {
		return false;
	}

	mDataPack.ReadAllEntries();

	return true;
}

void Shader::Load(const char* shader_name)
{
	Name = shader_name;

	String program_path = GetProgramPath();
	PreloadCompiledPrograms(program_path.CStr());
}


const String Shader::GetSourcePath() const { return String(AssetPath(eAxPathQuery::Shaders)) + Name + ".hlsl"; }
const String Shader::GetProgramPath() const
{
	String compiled_folder = String(AssetPath(eAxPathQuery::Shaders)) + "Spirv/";
	return (compiled_folder + Name + ".spack");
}


ShaderProgram Shader::LoadUncachedProgram(eShaderType shader_type, const SizedArray<ShaderMacro>& macros)
{
	String source_path = GetSourcePath();
	const char* c_source_path = source_path.CStr();

	String program_path = GetProgramPath();


	if (!mDataPack.IsOpen() || mDataPack.Entries.IsEmpty()) {
		mDataPack.ReadFromFile(program_path.CStr());
	}

	// Check if the shader is out of date
	bool is_out_of_date = ShaderCompiler::IsOutOfDate(c_source_path);
#ifdef DEBUG_FORCE_OUT_OF_DATE
	is_out_of_date = true;
#endif

	if (is_out_of_date) {
		// LogWarning(LC_SHADER, "Shader {} is out of date! ({} macros)", c_source_path, macros.Size);

		RecompileShader(source_path, program_path, macros);

		// If the shader failed to compile, it will not be written back to the datapack. We can continue using the out
		// of date shader.
	}

	// Generate an ID based on the shader type and macros. This is used for querying for the program in the DataPack.
	Hash64 program_id = Shader::GenerateShaderId(shader_type, macros);
	LogDebug(LC_SHADER, "Getting program from {} (Id={})", Name, program_id);

	// Check for the program in the DataPack
	// DataPackEntry* dp_entry = mDataPack.QuerySection(program_id);
	ProgramData program_data = ShaderCompiler::GetProgramData(program_id, mDataPack);

	// If there is no compiled version of the program in the DataPack, compile it and save it to disk.
	if (!program_data.IsValid()) {
		return ShaderProgram {};
		// LogWarning(LC_SHADER, "Shader was not found in datapack, recompiling...");
		// RecompileShader(GetSourcePath(), program_path, macros);

		// program_data = ShaderCompiler::GetProgramData(program_id, mDataPack);

		// if (!program_data.IsValid()) {
		// 	LogError(LC_SHADER, "Pack entry does not exist after compilation! (Id={})", program_id);
		// 	return program;
		// }
	}

	// If there is data available, create the program.
	if (program_data.HasData()) {
		Assert(program_data.pProgramData.Size == MathUtil::AlignValue<4>(program_data.pProgramData.Size));
		Assert(program_data.pProgramData.Size > 0);

		return ShaderProgram::Create(shader_type, this, reinterpret_cast<uint32*>(program_data.pProgramData.pData),
									 program_data.pProgramData.Size / sizeof(uint32), program_data.Reflection);
	}

	// Previously, there used to be logic here to force load from the data pack. Since data pack loading is now handled
	// by the shader compiler, we should not need to handle this case.
	LogError(LC_SHADER, "Shader: No data available");

	return ShaderProgram {};
}


ShaderProgram Shader::GetProgram(eShaderType shader_type, const SizedArray<ShaderMacro>& macros)
{
	ProgramCache& cached_type = mCachedTypes[static_cast<uint32>(shader_type)];

	const Hash64 macro_hash = HashMacros(macros, FX_HASH64_FNV1A_INIT);
	ShaderProgram* cached_program = cached_type.Programs.Find(macro_hash);

	if (cached_program != nullptr) {
		return *cached_program;
	}

	// Program has not been cached, load it from the data pack or compile it, and save it back to the cache.
	ShaderProgram program = LoadUncachedProgram(shader_type, macros);
	cached_type.Programs.Insert(macro_hash, program);

	return program;
}

void Shader::RecompileShader(const String& source_path, const String& compiled_path,
							 const SizedArray<ShaderMacro>& macros)
{
	// Compile to the shader pack
	ShaderCompiler::eResult compile_result = ShaderCompiler::Compile(source_path.CStr(), mDataPack, macros);

	if (compile_result == ShaderCompiler::eResult::Success) {
		mDataPack.WriteToFile(compiled_path.CStr());
	}
	else if (compile_result == ShaderCompiler::eResult::Failed) {
		Panic("RecompileShader", "Error compiling shaders");
	}
}


ShaderProgram::ShaderProgram(const ShaderProgram& other) : mpRecord(other.mpRecord)
{
	if (mpRecord != nullptr) {
		rx_shader_program_retain(mpRecord);
	}
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept : mpRecord(other.mpRecord) { other.mpRecord = nullptr; }

ShaderProgram& ShaderProgram::operator=(const ShaderProgram& other)
{
	if (mpRecord == other.mpRecord) {
		return *this;
	}

	Release();

	mpRecord = other.mpRecord;

	if (mpRecord != nullptr) {
		rx_shader_program_retain(mpRecord);
	}

	return *this;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept
{
	if (this == &other) {
		return *this;
	}

	Release();

	mpRecord = other.mpRecord;
	other.mpRecord = nullptr;

	return *this;
}

ShaderProgram ShaderProgram::Create(eShaderType type, Shader* shader, const uint32* code, uint32 word_count,
									const SizedArray<ShaderReflectionEntry>& reflection)
{
	int32 status = 0;

	ShaderProgram program;

	program.mpRecord = rx_shader_program_new(gGraphics->GetDevice()->GetRustDevice(), code, word_count,
											 reinterpret_cast<const RxReflectionEntry*>(reflection.pData),
											 reflection.Size, static_cast<uint32>(type), shader, &status);

	if (program.mpRecord == nullptr) {
		LogError(LC_SHADER, "Could not create Vulkan shader module: {}", Util::ResultToStr(static_cast<VkResult>(status)));
	}

	return program;
}

void ShaderProgram::Release()
{
	if (mpRecord == nullptr) {
		return;
	}

	GraphicsBackend* graphics = gGraphics;

	rx_shader_program_release(mpRecord, (graphics != nullptr) ? graphics->GetDevice()->GetRustDevice() : nullptr);
	mpRecord = nullptr;
}

ShaderProgram::~ShaderProgram() { Release(); }

} // namespace fx::renderer

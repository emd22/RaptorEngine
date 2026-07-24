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
#include <Renderer/RenderBackend.hpp>


#define DEBUG_FORCE_OUT_OF_DATE 1

namespace fx::renderer {

/////////////////////////////////////
// Shader Functions
/////////////////////////////////////

ShaderId Shader::GenerateShaderId(eShaderType type, const SizedArray<ShaderMacro>& macros)
{
	Hash64 hash = FX_HASH64_FNV1A_INIT;

	constexpr Hash64 cPrefixHashVS = HashStr64("VERTSHADER");
	constexpr Hash64 cPrefixHashFS = HashStr64("FRAGSHADER");
	constexpr Hash64 cPrefixHashCS = HashStr64("COMPUTESHADER");

	if (type == eShaderType::Vertex) {
		hash = cPrefixHashVS;
	}
	else if (type == eShaderType::Pixel) {
		hash = cPrefixHashFS;
	}
	else if (type == eShaderType::Compute) {
		hash = cPrefixHashCS;
	}

	return HashData64(Slice<ShaderMacro>(macros), hash);
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


Ref<ShaderProgram> Shader::LoadUncachedProgram(eShaderType shader_type, const SizedArray<ShaderMacro>& macros)
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

	// Create the program
	Ref<ShaderProgram> program = MakeRef<ShaderProgram>();
	program->ShaderType = shader_type;
	program->pShader = this;

	// Check for the program in the DataPack
	// DataPackEntry* dp_entry = mDataPack.QuerySection(program_id);
	ProgramData program_data = ShaderCompiler::GetProgramData(program_id, mDataPack);

	// If there is no compiled version of the program in the DataPack, compile it and save it to disk.
	if (!program_data.IsValid()) {
		return Ref<ShaderProgram> { nullptr };
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

		CreateShaderModule(*program, program_data.pProgramData.Size,
						   reinterpret_cast<uint32*>(program_data.pProgramData.pData), program->InternalShader);

		// Now that the shader is officially loaded, move over the reflection data.
		program->Reflection = std::move(program_data.Reflection);
		program->PrintReflection();

		return program;
	}

	// Previously, there used to be logic here to force load from the data pack. Since data pack loading is now handled
	// by the shader compiler, we should not need to handle this case.
	LogError(LC_SHADER, "Shader: No data available");

	return program;
}


Ref<ShaderProgram> Shader::GetProgram(eShaderType shader_type, const SizedArray<ShaderMacro>& macros)
{
	ProgramCache& cached_type = mCachedTypes[static_cast<uint32>(shader_type)];

	const Hash64 macro_hash = HashData64(MakeSlice(macros.pData, macros.GetSizeInBytes()));
	auto program_it = cached_type.Programs.find(macro_hash);

	if (program_it != cached_type.Programs.end()) {
		return program_it->second;
	}

	// Program has not been cached, load it from the data pack or compile it, and save it back to the cache.
	Ref<ShaderProgram> program = LoadUncachedProgram(shader_type, macros);
	cached_type.Programs[macro_hash] = program;

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
}


void ShaderProgram::BuildDescriptor()
{
	DsLayoutBuilder layout_builder {};

	uint32 binding_index = 0;

	bool has_dynamic_offsets = false;

	for (const ShaderReflectionEntry& refl_entry : Reflection) {
		if (ShaderReflectionUtil::RequiresOffset(refl_entry.Type)) {
			has_dynamic_offsets = true;
		}

		layout_builder.AddBinding(binding_index++, ShaderReflectionUtil::TypeToVkDescriptorType(refl_entry.Type),
								  ShaderType);
	}

	if (!layout_builder.HasBindings()) {
		return;
	}

	layout_builder.Build();

	// Descriptor.Create(gRenderer->pDeferredRenderer->DescriptorPool, layout_builder.Build(), has_dynamic_offsets);
}


void ShaderProgram::Bind(const CommandBuffer& cmd, const Pipeline& pipeline, const ShaderBindOptions& bind_options)
{
	// for (const ShaderDescriptorId& ds_id : Descriptors) {
	//     Ref<DescriptorSet> ds = gDescriptorCache->Request(ds_id);
	//     if (bind_options.bUseOffset && ds->HasDynamicOffsets()) {
	//         ds->BindWithOffset(ds_id.Set, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline, bind_options.BufferOffset);
	//         continue;
	//     }

	//     ds->Bind(ds_id.Set, cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	// }
}

void ShaderProgram::PrintReflection()
{
	LogInfo(LC_SHADER, "Shader '{}' ({}) reflection:", pShader->GetName(), ShaderUtil::TypeToName(ShaderType));

	for (const ShaderReflectionEntry& entry : Reflection) {
		LogInfo(LC_SHADER, "\tType: {}, Set={}, Binding={}", ShaderReflectionUtil::GetName(entry.Type), entry.Set,
				entry.Binding);
	}

	LogInfo(LC_SHADER, "");
}

void ShaderProgram::Destroy()
{
	if (InternalShader == nullptr) {
		return;
	}

	GpuDevice* device = gRenderer->GetDevice();
	vkDestroyShaderModule(device->Device, InternalShader, nullptr);
}


void Shader::CreateShaderModule(ShaderProgram& program, uint32 file_size, uint32* raw_data,
								VkShaderModule& shader_module)
{
	const VkShaderModuleCreateInfo create_info {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = file_size,
		.pCode = raw_data,
	};

	GpuDevice* device = gRenderer->GetDevice();

	const VkResult status = vkCreateShaderModule(device->Device, &create_info, nullptr, &shader_module);

	if (status != VK_SUCCESS) {
		LogError(LC_SHADER, "Could not create Vulkan shader module: {}", Util::ResultToStr(status));
		return;
	}
}

} // namespace fx::renderer

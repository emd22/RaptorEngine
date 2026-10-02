#include "ShaderPreproc.hpp"

#include <Core/File.hpp>
#include <Core/Log.hpp>
#include <Util/RustInterop.hpp>

// Rust definitions
#include <raptor_ffi.h>

#include <string>
#include <vector>

namespace fx {

namespace ShaderPreproc {

static constexpr uint32 scDataPageSize = 512;

Result Process(const Slice<char>& data, const SizedArray<ShaderMacro>& macros)
{
	std::vector<RxShaderMacro> rust_macros;
	rust_macros.reserve(macros.Size);

	for (const ShaderMacro& macro : macros) {
		if (macro.pcName != nullptr) {
			rust_macros.push_back(RxShaderMacro { .name = macro.pcName, .value = macro.pcValue });
		}
	}

	const RxLogSink log = { .user = nullptr, .log = RustInterop::Log };

	RxPreprocResult* processed = rx_preproc_process(reinterpret_cast<const uint8*>(data.pData), data.Size,
													rust_macros.data(), rust_macros.size(), &log);

	Result result {};

	if (processed == nullptr) {
		LogError(LC_SHADER, "Preproc: The preprocessor failed");
		return result;
	}

	static constexpr eShaderType scStages[] = { eShaderType::Vertex, eShaderType::Pixel, eShaderType::Compute };

	for (uint32 stage = 0; stage < std::size(scStages); stage++) {
		size_t length = 0;
		const uint8* program = rx_preproc_program(processed, stage, &length);

		DataBuffer& buffer = result.GetBuffer(scStages[stage]);
		buffer.SetPageSize(scDataPageSize);

		for (size_t index = 0; index < length; index++) {
			buffer.Insert(static_cast<char>(program[index]));
		}

		size_t count = 0;
		const RxReflectionEntry* entries = rx_preproc_reflection(processed, stage, &count);

		for (size_t index = 0; index < count; index++) {
			result.GetReflection(scStages[stage])
				.emplace_back(static_cast<eShaderReflectionType>(entries[index].type), entries[index].set,
							  entries[index].binding);
		}
	}

	rx_preproc_free(processed);

	return result;
}

static void SaveProgramToDisk(const char* name, eShaderType shader_type, const Result& result)
{
	const DataBuffer& buffer = result.ProgramData[static_cast<uint32>(shader_type)];

	if (buffer.Size > 0) {
		std::string sname = std::string(name) + "_" + ShaderUtil::TypeToName(shader_type) + ".hlsl";
		File file(sname.c_str(), File::eModType::Write, File::eDataType::Binary);
		file.Write(Slice<char>(buffer.pData, buffer.Size));
		file.Close();
	}
}

void DebugSaveToDisk(const char* name, const Result& result)
{
	SaveProgramToDisk(name, eShaderType::Vertex, result);
	SaveProgramToDisk(name, eShaderType::Pixel, result);
	SaveProgramToDisk(name, eShaderType::Compute, result);
}

}; // namespace ShaderPreproc

} // namespace fx

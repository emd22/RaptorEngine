#include "ShaderCompiler.hpp"

#include <Core/Defines.hpp>
#include <Renderer/Backend/Shader.hpp>

// #include <ThirdParty/Slang/slang-com-ptr.h>
// #include <ThirdParty/Slang/slang.h>

#include "ShaderPreproc.hpp"

// DXC
#ifndef FX_PLATFORM_WINDOWS
#include <dxc/WinAdapter.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <atlbase.h>
#include <windows.h>
#endif

#include "ShaderPreproc.hpp"

#include <dxc/dxcapi.h>

#include <Asset/DataPack.hpp>
#include <Core/BasicDb.hpp>
#include <Core/ByteBuffer.hpp>
#include <Core/File.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/StackArray.hpp>
#include <Renderer/Backend/Shader.hpp>

// #define FX_SHADER_NO_REFLECTION 1


namespace fx {


static BasicDb sShaderCompileDb;

using CompileResult = ShaderCompiler::eResult;

static bool IsShaderUpToDate(renderer::ShaderId entry_id, const char* path)
{
	BasicDbEntry* entry = sShaderCompileDb.FindEntry(entry_id);
	if (!entry) {
		LogWarning(LC_SHADER, "Cannot find entry for shader!");
		return false;
	}

	uint64 latest_modification_time = FilesystemIO::FileGetLastModified(path);

	if (std::stoull(entry->Value) < latest_modification_time) {
		return false;
	}

	return true;
}


bool ShaderCompiler::IsOutOfDate(const char* path)
{
	if (!sShaderCompileDb.IsOpen()) {
		sShaderCompileDb.Open(FX_BASE_DIR "/Shaders/LastUpdated.fxd");
	}

	renderer::ShaderId compile_entry_id = HashStr64(path);

	return !IsShaderUpToDate(compile_entry_id, path);
}

bool ShaderCompiler::CompileIfOutOfDate(const char* path, DataPack& pack, const SizedArray<ShaderMacro>& macros)
{
	bool out_of_date = IsOutOfDate(path);

	if (out_of_date) {
		Compile(path, pack, macros);
	}

	return out_of_date;
}

#define SHADER_VERSION L"6_7"

static const wchar_t* ShaderTypeToDxName(eShaderType type)
{
	switch (type) {
	case eShaderType::Vertex:
		return L"vs_" SHADER_VERSION;
	case eShaderType::Pixel:
		return L"ps_" SHADER_VERSION;
	case eShaderType::Compute:
		return L"cs_" SHADER_VERSION;
	default:;
	}
	return L"";
}

struct CompileState
{
	const char* pcPath;
	DataPack& Pack;
	const SizedArray<ShaderMacro>& pcMacros;
	ShaderPreproc::Result& Preproc;
	CComPtr<IDxcUtils> pUtils;
	CComPtr<IDxcCompiler3> pCompiler;
	CComPtr<IDxcIncludeHandler> pIncludeHandler;
};


ProgramData ShaderCompiler::GetProgramData(const Hash64 program_id, DataPack& pack)
{
	DataPackEntry* entry = pack.GetEntry(program_id, true);

	ProgramData program = { .Reflection = {}, .pProgramData = nullptr };

	// Entry was not found, return null data
	if (entry == nullptr) {
		LogWarning(LC_SHADER, "Could not find shader entry");
		return program;
	}

	const SizedArray<uint8>& entry_data = entry->Data;

	ByteBuffer bb = ByteBuffer::FromData(entry_data.pData, entry_data.Size);

#ifndef FX_SHADER_NO_REFLECTION
	uint32 num_reflected_entries = bb.Get<uint32>();
	program.Reflection.InitCapacity(num_reflected_entries);

	for (uint32 i = 0; i < num_reflected_entries; i++) {
		uint32 entry_data_int = bb.Get<uint32>();
		program.Reflection.Insert(ShaderReflectionEntry::FromUInt(entry_data_int));
	}

#endif

	// Get the SPIRV data
	program.pProgramData = Slice<uint8>(bb.GetPtr<uint8>(), bb.GetRemainingSize());

	return program;
}


static ByteBuffer BuildReflectionHeader(const CompileState& state, eShaderType shader_type)
{
	ByteBuffer bb;

	std::vector<ShaderReflectionEntry>& refl = state.Preproc.GetReflection(shader_type);

	// Each reflection entry contains a Type(uint16), Set(uint8) and Binding(uint8).
	constexpr uint32 entry_size = sizeof(uint32);

	// The header contains the number of reflected entries.
	constexpr uint32 header_size = sizeof(uint32);

	// The final size composed of the header and all entries.
	uint32 final_size = header_size + (entry_size * refl.size());


	bb.Create(final_size);

	// Output the header
	bb.Insert<uint32>(static_cast<uint32>(refl.size()));

	// Output all entries
	for (const ShaderReflectionEntry& entry : refl) {
		bb.Insert<uint32>(entry.AsUInt());
	}

	return bb;
}


static CompileResult CompileProgram(const CompileState& state, eShaderType shader_type)
{
	constexpr uint32 cCodePage = DXC_CP_UTF8;


	// Convert path to a wide char string
	wchar_t wpath[128];
	mbstowcs(wpath, state.pcPath, 128);

	const SizedArray<LPCWSTR> compile_args = {
		wpath,
		// Entrypoint
		L"-E",
		L"main",

		// Target profile
		L"-T",
		ShaderTypeToDxName(shader_type),

		// Output format
		L"-spirv",
	};

	CComPtr<IDxcBlobEncoding> source_blob;
	auto& shader_raw_data = state.Preproc.GetBuffer(shader_type);
	state.pUtils->CreateBlob(shader_raw_data.pData, shader_raw_data.Size, cCodePage, &source_blob);

	DxcBuffer buffer {};
	buffer.Encoding = cCodePage;
	buffer.Ptr = source_blob->GetBufferPointer();
	buffer.Size = source_blob->GetBufferSize();

	HRESULT hresult;
	CComPtr<IDxcResult> result { nullptr };
	hresult = state.pCompiler->Compile(&buffer, compile_args.pData, static_cast<uint32>(compile_args.Size),
									   state.pIncludeHandler, IID_PPV_ARGS(&result));

	if (SUCCEEDED(hresult)) {
		result->GetStatus(&hresult);
	}

	if (FAILED(hresult) && result) {
		CComPtr<IDxcBlobEncoding> error_blob;
		hresult = result->GetErrorBuffer(&error_blob);

		if (SUCCEEDED(hresult) && error_blob) {
			LogError(LC_SHADER, "Failed to compile shader '{}'!", state.pcPath);
			LogError(LC_SHADER, "Err: {}", reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
			return CompileResult::Failed;
		}
	}

	CComPtr<IDxcBlob> spirv_bin;
	result->GetResult(&spirv_bin);

	const renderer::ShaderId shader_id = renderer::Shader::GenerateShaderId(shader_type, state.pcMacros);

	LogInfo(LC_SHADER, "Writing shader '{}' (Id={:x}) to data pack!", state.pcPath, shader_id);

	// Build the final buffer and write to the data pack
	{
#ifndef FX_SHADER_NO_REFLECTION
		ByteBuffer reflection_header = BuildReflectionHeader(state, shader_type);
#else
		ByteBuffer reflection_header;
#endif
		ByteBuffer final_buffer(reflection_header.GetSize() + spirv_bin->GetBufferSize());

		LogWarning(LC_SHADER, "Reflection header: {:p}, {}", reflection_header.pData, reflection_header.GetSize());
		// Write the reflection header
		final_buffer.InsertRaw(reflection_header.pData, reflection_header.GetSize());
		// Write the SPIRV data
		final_buffer.InsertRaw(reinterpret_cast<uint8*>(spirv_bin->GetBufferPointer()), spirv_bin->GetBufferSize());

		// Add the final buffer to the data pack
		state.Pack.AddEntry(shader_id,
							MakeSlice<uint8>(reinterpret_cast<uint8*>(final_buffer.pData), final_buffer.GetSize()));
	}

	return CompileResult::Success;
}

#define SOURCE_EXISTS(_shader_type) (preproc.GetBuffer(_shader_type).Size > 0)
#define COMPILE_DEFAULT(_shader_type)                                                                                  \
	CompileProgram(CompileState(path, pack, macros, preproc, utils, compiler, include_handler), _shader_type)

#define TRY_COMPILE_PROGRAM(_shader_type)                                                                              \
	if (SOURCE_EXISTS(_shader_type) && (COMPILE_DEFAULT(_shader_type) != CompileResult::Success)) {                    \
		return CompileResult::Failed;                                                                                  \
	}


ShaderCompiler::eResult ShaderCompiler::Compile(const char* path, DataPack& pack, const SizedArray<ShaderMacro>& macros,
												bool do_db_flush)
{
	LogInfo(LC_SHADER, "Compiling shader {} with {} macros", path, macros.Size);

	File file(path, File::eModType::Read, File::eDataType::Binary);
	Slice<char> file_data = file.Read<char>();

	ShaderPreproc::Result preproc = ShaderPreproc::Process(file_data, macros);

	CComPtr<IDxcUtils> utils;
	CComPtr<IDxcCompiler3> compiler;

	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

	CComPtr<IDxcIncludeHandler> include_handler;
	utils->CreateDefaultIncludeHandler(&include_handler);

	TRY_COMPILE_PROGRAM(eShaderType::Vertex);
	TRY_COMPILE_PROGRAM(eShaderType::Pixel);
	TRY_COMPILE_PROGRAM(eShaderType::Compute);

	return CompileResult::Success;
}

void ShaderCompiler::Destroy() { sShaderCompileDb.Close(); }

} // namespace fx

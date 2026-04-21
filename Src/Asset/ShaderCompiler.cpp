#include "ShaderCompiler.hpp"

#include <Core/Defines.hpp>

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
#include <dxc/dxcapi.h>

#include <Asset/DataPack.hpp>
#include <Core/BasicDb.hpp>
#include <Core/File.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/StackArray.hpp>
#include <Renderer/Backend/Shader.hpp>

namespace fx {


static BasicDb sShaderCompileDb;

using CompileResult = ShaderCompiler::eResult;

static bool IsShaderUpToDate(renderer::ShaderId entry_id, const char* path)
{
    BasicDbEntry* entry = sShaderCompileDb.FindEntry(entry_id);
    if (!entry) {
        LogWarning("Cannot find entry for shader!");
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

static const wchar_t* ShaderTypeToDxName(renderer::eShaderType type)
{
    switch (type) {
    case renderer::eShaderType::Vertex:
        return L"vs_" SHADER_VERSION;
    case renderer::eShaderType::Pixel:
        return L"ps_" SHADER_VERSION;
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


static CompileResult CompileProgram(const CompileState& state, renderer::eShaderType shader_type)
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

    // printf("CODE:\n%.*s\n", shader_raw_data.Size, shader_raw_data.pData);

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
            LogError("Failed to compile shader '{}'!", state.pcPath);
            LogError("Err: {}", reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
            return CompileResult::Failed;
        }
    }

    CComPtr<IDxcBlob> spirv_bin;
    result->GetResult(&spirv_bin);

    const renderer::ShaderId shader_id = renderer::Shader::GenerateShaderId(shader_type, state.pcMacros);

    LogInfo("IS 4 byte aligned? {:s}", !(spirv_bin->GetBufferSize() % 4));

    LogInfo("Writing shader '{}' (Id={:x}) to data pack!", state.pcPath, shader_id);

    state.Pack.AddEntry(shader_id, MakeSlice<uint8>(reinterpret_cast<uint8*>(spirv_bin->GetBufferPointer()),
                                                    spirv_bin->GetBufferSize()));


    return CompileResult::Success;
}

#define SOURCE_EXISTS(_shader_type) (preproc.GetBuffer(_shader_type).Size > 0)
#define COMPILE_DEFAULT(_shader_type)                                                                                  \
    CompileProgram(CompileState(path, pack, macros, preproc, utils, compiler, include_handler), _shader_type)

#define TRY_COMPILE_PROGRAM(_shader_type)                                                                              \
    if (SOURCE_EXISTS(_shader_type) && COMPILE_DEFAULT(_shader_type) != CompileResult::Success) {                      \
        return CompileResult::Failed;                                                                                  \
    }


ShaderCompiler::eResult ShaderCompiler::Compile(const char* path, DataPack& pack, const SizedArray<ShaderMacro>& macros,
                                                bool do_db_flush)
{
    LogInfo("Compiling shader {} with {} macros", path, macros.Size);

    File file(path, File::eModType::Read, File::eDataType::Binary);
    Slice<char> file_data = file.Read<char>();

    ShaderPreproc::Result preproc = ShaderPreproc::Process(file_data, macros);

    CComPtr<IDxcUtils> utils;
    CComPtr<IDxcCompiler3> compiler;

    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

    CComPtr<IDxcIncludeHandler> include_handler;
    utils->CreateDefaultIncludeHandler(&include_handler);

    TRY_COMPILE_PROGRAM(renderer::eShaderType::Vertex);
    TRY_COMPILE_PROGRAM(renderer::eShaderType::Pixel);

    return CompileResult::Success;
}

void ShaderCompiler::Destroy() { sShaderCompileDb.Close(); }

} // namespace fx

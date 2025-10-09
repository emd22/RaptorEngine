#include "FxLoaderSlang.hpp"

#include <ThirdParty/Slang/slang.h>

#include <Asset/FxAssetShader.hpp>
#include <Core/FxFile.hpp>

using Status = FxLoaderSlang::Status;

// static EShLanguage RxShaderTypeToShLanguage(RxShaderType type)
// {
//     switch (type) {
//     case RxShaderType::Fragment:
//         return EShLangFragment;
//     case RxShaderType::Vertex:
//         return EShLangVertex;
//     default:
//         FxLogError("Unknown shader type!");
//         return EShLangVertex;
//     }
// }

Status FxLoaderSlang::LoadFromFile(FxRef<FxAssetBase> asset, const std::string& path)
{
    FxRef<FxAssetShader> shader(asset);

    FxFile shader_file(path.c_str(), "rb");

    // glslang::TShader glsl_shader(RxShaderTypeToShLanguage(shader->ShaderType));

    // FxSlice<char> file_data = shader_file.Read();
    // glsl_shader.setStrings(&file_data.Ptr, 1);

    // if (!glsl_shader.parse(GetResources(), 450, false, EShMsgDefault)) {
    // }


    // glsl_shader.addSourceText(file_data.Ptr, file_data.Size);

    shader_file.Close();

    return Status::Success;
}

Status FxLoaderSlang::LoadFromMemory(FxRef<FxAssetBase> asset, const uint8* data, uint32 size)
{
    return Status::Success;
}

void FxLoaderSlang::CreateGpuResource(FxRef<FxAssetBase>& asset) {}

void FxLoaderSlang::Destroy(FxRef<FxAssetBase>& asset) {}

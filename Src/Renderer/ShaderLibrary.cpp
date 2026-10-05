#include "ShaderLibrary.hpp"

#include <Core/Log.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Util/RustInterop.hpp>

namespace fx::renderer {

FX_SET_MODULE_NAME("ShaderLibrary")

ShaderLibrary::ShaderLibrary(const char* directory) { mpLibrary = rx_shader_library_new(directory); }

ShaderLibrary::~ShaderLibrary()
{
	GraphicsBackend* graphics = gGraphics;

	const bool can_free = (graphics != nullptr) && (graphics->GetDevice()->GetRustDevice() != nullptr);

	rx_shader_library_free(mpLibrary, can_free ? graphics->GetDevice()->GetRustDevice() : nullptr);
	mpLibrary = nullptr;
}

Hash64 ShaderLibrary::HashMacros(const SizedArray<ShaderMacro>& macros)
{
	return rx_shader_hash_macros(reinterpret_cast<const RxShaderMacroRef*>(macros.pData), macros.Size,
								 FX_HASH64_FNV1A_INIT);
}

ShaderProgram ShaderLibrary::GetProgram(eShaderName shader, eShaderType type, const SizedArray<ShaderMacro>& macros)
{
	const RxLogSink log = { .user = nullptr, .log = RustInterop::Log };

	int32 status = RX_SHADER_LOAD_OK;

	RxShaderProgram* program = rx_shader_library_get_program(
		mpLibrary, gGraphics->GetDevice()->GetRustDevice(), ShaderNameUtil::GetName(shader),
		static_cast<uint32>(type), reinterpret_cast<const RxShaderMacroRef*>(macros.pData), macros.Size, &log,
		&status);

	if (status != RX_SHADER_LOAD_OK) {
		Panic("RecompileShader", "Error compiling shaders");
	}

	return ShaderProgram::Adopt(program);
}

} // namespace fx::renderer

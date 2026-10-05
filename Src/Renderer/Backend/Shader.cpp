#include "Shader.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

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

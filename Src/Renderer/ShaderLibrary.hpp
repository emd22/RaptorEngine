#pragma once

#include "Backend/Shader.hpp"
#include "ShaderNames.hpp"

#include <Core/Hash.hpp>
#include <Core/SizedArray.hpp>

namespace fx::renderer {

/**
 * @brief Finds the compiled programs of the shaders, compiling them from their source when they are asked for. The
 * compiling, and the programs the library makes, are in Rust.
 */
class ShaderLibrary
{
public:
	/// `directory` holds the shaders and ends with a slash
	explicit ShaderLibrary(const char* directory);

	ShaderLibrary(const ShaderLibrary&) = delete;
	ShaderLibrary& operator=(const ShaderLibrary&) = delete;

	/**
	 * @brief Gets the program of one stage of a shader for a set of macros. The shader is compiled the first time a
	 * set of macros is asked for. The program is null if the shader has no such stage.
	 */
	ShaderProgram GetProgram(eShaderName shader, eShaderType type, const SizedArray<ShaderMacro>& macros);

	/**
	 * @brief Folds a macro list into a hash by its contents
	 */
	static Hash64 HashMacros(const SizedArray<ShaderMacro>& macros);

	RxShaderLibrary* GetRust() const { return mpLibrary; }

	~ShaderLibrary();

private:
	RxShaderLibrary* mpLibrary = nullptr;
};

} // namespace fx::renderer

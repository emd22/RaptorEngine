#pragma once

#include "ShaderReflection.hpp"
#include "ShaderType.hpp"

#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Renderer/Backend/Descriptors.hpp>

namespace fx {

/// A macro that a shader is compiled with. The name and value are not copied, so they have to be string literals.
struct ShaderMacro
{
	const char* pcName;
	const char* pcValue;
};

static_assert(sizeof(ShaderMacro) == sizeof(RxShaderMacroRef));

namespace renderer {

static_assert(sizeof(ShaderReflectionEntry) == sizeof(RxReflectionEntry));

/**
 * @brief A reference to a compiled shader program. The program lives in a record owned by Rust, copies of this
 * share it, and the shader module is destroyed with the last copy. A default constructed program is null.
 */
class ShaderProgram
{
public:
	ShaderProgram() = default;
	ShaderProgram(nullptr_t np) {}

	ShaderProgram(const ShaderProgram& other);
	ShaderProgram(ShaderProgram&& other) noexcept;

	ShaderProgram& operator=(const ShaderProgram& other);
	ShaderProgram& operator=(ShaderProgram&& other) noexcept;

	/**
	 * @brief Takes over the reference to a program that the shader library gave out.
	 */
	static ShaderProgram Adopt(RxShaderProgram* record)
	{
		ShaderProgram program;
		program.mpRecord = record;
		return program;
	}

	FX_FORCE_INLINE bool IsValid() const { return mpRecord != nullptr; }
	FX_FORCE_INLINE const RxShaderProgram* GetRecord() const { return mpRecord; }
	FX_FORCE_INLINE bool operator==(nullptr_t np) const { return mpRecord == nullptr; }
	FX_FORCE_INLINE bool operator!=(nullptr_t np) const { return mpRecord != nullptr; }

	FX_FORCE_INLINE VkShaderModule Get() const
	{
		Assert(mpRecord != nullptr);
		return reinterpret_cast<VkShaderModule>(mpRecord->module);
	}

	FX_FORCE_INLINE eShaderType GetType() const
	{
		return (mpRecord != nullptr) ? static_cast<eShaderType>(mpRecord->shader_type) : eShaderType::Vertex;
	}

	/// The name of the shader the program is from
	FX_FORCE_INLINE const char* GetName() const { return (mpRecord != nullptr) ? mpRecord->name : "Unknown"; }

	/// Bit N is set if the program declares a stage input at location N. All bits are set if unknown.
	FX_FORCE_INLINE uint32 GetInputLocationMask() const
	{
		return (mpRecord != nullptr) ? mpRecord->input_location_mask : ~0U;
	}

	FX_FORCE_INLINE const ShaderReflectionEntry* GetReflectionData() const
	{
		return (mpRecord != nullptr) ? reinterpret_cast<const ShaderReflectionEntry*>(mpRecord->reflection) : nullptr;
	}

	FX_FORCE_INLINE uint32 GetReflectionCount() const
	{
		return (mpRecord != nullptr) ? static_cast<uint32>(mpRecord->reflection_count) : 0;
	}

	~ShaderProgram();

private:
	void Release();

private:
	RxShaderProgram* mpRecord = nullptr;
};

} // namespace renderer

} // namespace fx

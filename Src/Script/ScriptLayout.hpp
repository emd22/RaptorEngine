#pragma once

#include <strata/strata_types.h>

#include <Core/Slice.hpp>
#include <Core/Types.hpp>
#include <cstddef>
#include <type_traits>

namespace fx::script {

class Script;

struct HostField
{
	const char* pcName = nullptr;
	const char* pcScriptName = nullptr;

	uint32 Offset = 0;
	uint32 Size = 0;

	StrataTypeKind Kind = STRATA_TYPE_INVALID;
};

template <typename T>
consteval StrataTypeKind HostFieldKind()
{
	using TElement = std::remove_cv_t<std::remove_all_extents_t<T>>;

	if constexpr (std::is_enum_v<TElement>) {
		return HostFieldKind<std::underlying_type_t<TElement>>();
	}
	else if constexpr (std::is_same_v<TElement, bool>) {
		return STRATA_TYPE_BOOL;
	}
	else if constexpr (std::is_same_v<TElement, int8>) {
		return STRATA_TYPE_I8;
	}
	else if constexpr (std::is_same_v<TElement, uint8>) {
		return STRATA_TYPE_U8;
	}
	else if constexpr (std::is_same_v<TElement, int16>) {
		return STRATA_TYPE_I16;
	}
	else if constexpr (std::is_same_v<TElement, uint16>) {
		return STRATA_TYPE_U16;
	}
	else if constexpr (std::is_same_v<TElement, int32>) {
		return STRATA_TYPE_I32;
	}
	else if constexpr (std::is_same_v<TElement, uint32>) {
		return STRATA_TYPE_U32;
	}
	else if constexpr (std::is_same_v<TElement, int64>) {
		return STRATA_TYPE_I64;
	}
	else if constexpr (std::is_same_v<TElement, uint64>) {
		return STRATA_TYPE_U64;
	}
	else if constexpr (std::is_same_v<TElement, float32>) {
		return STRATA_TYPE_F32;
	}
	else if constexpr (std::is_same_v<TElement, float64>) {
		return STRATA_TYPE_F64;
	}
	else {
		static_assert(sizeof(TElement) == 0,
					  "Unsupported host field type. Make sure you are listing nested struct members individually!");
	}
}

/** `script_name` is the path for the field in the script struct (e.g. "Spread.Hip"). */
#define FX_SCRIPT_FIELD(TStruct, TMember, script_name)                                                                 \
	::fx::script::HostField                                                                                            \
	{                                                                                                                  \
		#TMember, script_name, static_cast<::fx::uint32>(offsetof(TStruct, TMember)),                                  \
			static_cast<::fx::uint32>(sizeof(TStruct::TMember)),                                                       \
			::fx::script::HostFieldKind<decltype(TStruct::TMember)>()                                                  \
	}

/**
 * @brief Checks that a script struct has the same memory layout as a host struct passed to it by pointer.
 *
 * Some notes:
 * The struct in the script must be marked with `@component` (or used by one) to gather the structs metadata.
 * Nested script structs are flattened into one chunk.
 *
 * Note that this only works for JIT scripts, so you bet your ass I will be back here in a few months.
 */
bool ValidateScriptStruct(const Script& script, const char* script_struct, uint32 host_size,
						  Slice<const HostField> host_fields);

template <typename THost, size_t TCount>
bool ValidateScriptStruct(const Script& script, const char* script_struct, const HostField (&host_fields)[TCount])
{
	return ValidateScriptStruct(script, script_struct, static_cast<uint32>(sizeof(THost)),
								Slice<const HostField>(host_fields, TCount));
}

} // namespace fx::script

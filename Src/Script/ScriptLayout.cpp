#include "ScriptLayout.hpp"

#include "Script.hpp"

#include <Core/Log.hpp>
#include <cstdio>
#include <cstring>

namespace fx::script {

namespace {

struct LayoutWalk
{
	const StrataTypes* pTypes = nullptr;
	Slice<const HostField> HostFields = nullptr;
	uint32 NextHostField = 0;

	const char* pcScriptPath = nullptr;
	const char* pcStructName = nullptr;

	/** Dotted path of the struct being walked, e.g. "Spread." */
	char Prefix[512] {};
	uint32 PrefixLength = 0;
};

const char* KindName(StrataTypeKind kind)
{
	switch (kind) {
	case STRATA_TYPE_BOOL:
		return "bool";
	case STRATA_TYPE_I8:
		return "i8";
	case STRATA_TYPE_U8:
		return "u8";
	case STRATA_TYPE_I16:
		return "i16";
	case STRATA_TYPE_U16:
		return "u16";
	case STRATA_TYPE_I32:
		return "i32";
	case STRATA_TYPE_U32:
		return "u32";
	case STRATA_TYPE_I64:
		return "i64";
	case STRATA_TYPE_U64:
		return "u64";
	case STRATA_TYPE_F32:
		return "f32";
	case STRATA_TYPE_F64:
		return "f64";
	case STRATA_TYPE_FLOAT2:
		return "float2";
	case STRATA_TYPE_FLOAT3:
		return "float3";
	case STRATA_TYPE_FLOAT4:
		return "float4";
	case STRATA_TYPE_STRUCT:
		return "struct";
	case STRATA_TYPE_ENUM:
		return "enum";
	case STRATA_TYPE_HANDLE:
		return "handle";
	default:
		return "invalid";
	}
}

StrataTypeKind ResolveKind(const StrataTypes* types, const StrataTypesField& field)
{
	if (field.kind != STRATA_TYPE_ENUM) {
		return field.kind;
	}

	StrataTypesEnum enum_info;
	if (!strataTypesGetEnum(types, field.typeIndex, &enum_info)) {
		return STRATA_TYPE_INVALID;
	}

	return enum_info.underlyingKind;
}

bool WalkStruct(LayoutWalk& walk, const StrataTypesStruct& script_struct, uint32 base_offset)
{
	for (uint32 i = 0; i < script_struct.fieldCount; i++) {
		StrataTypesField field;
		if (!strataTypesGetField(walk.pTypes, &script_struct, i, &field)) {
			LogError(LC_SCRIPT, "Could not read field {} of script struct '{}' ({})", i, script_struct.name,
					 walk.pcScriptPath);
			return false;
		}

		const uint32 offset = base_offset + field.offset;

		if (field.kind == STRATA_TYPE_STRUCT) {
			StrataTypesStruct nested;
			if (!strataTypesGetStruct(walk.pTypes, field.typeIndex, &nested)) {
				LogError(LC_SCRIPT, "Could not read script struct '{}' ({})", field.typeName, walk.pcScriptPath);
				return false;
			}

			uint32 element_count = field.arrayLength;
			if (element_count == 0) {
				element_count = 1;
			}

			const uint32 prefix_length = walk.PrefixLength;

			for (uint32 element = 0; element < element_count; element++) {
				const uint32 available = sizeof(walk.Prefix) - prefix_length;
				int written = 0;

				if (field.arrayLength == 0) {
					written = snprintf(walk.Prefix + prefix_length, available, "%s.", field.name);
				}
				else {
					written = snprintf(walk.Prefix + prefix_length, available, "%s[%u].", field.name, element);
				}


				// Ensure that we don't go over our prefix buffer size
				if (written < 0 || static_cast<uint32>(written) >= available) {
					LogError(LC_SCRIPT, "Script struct '{}' ({}): field path too long at {}{}", walk.pcStructName,
							 walk.pcScriptPath, walk.Prefix, field.name);
					return false;
				}

				walk.PrefixLength = prefix_length + static_cast<uint32>(written);

				const bool matches = WalkStruct(walk, nested, offset + element * nested.size);

				walk.PrefixLength = prefix_length;
				walk.Prefix[prefix_length] = '\0';

				if (!matches) {
					return false;
				}
			}

			continue;
		}

		char script_name[sizeof(walk.Prefix) + 64];
		snprintf(script_name, sizeof(script_name), "%s%s", walk.Prefix, field.name);

		if (walk.NextHostField >= walk.HostFields.Size) {
			LogError(LC_SCRIPT, "Script struct '{}' ({}): field {} at offset {} has no host counterpart",
					 walk.pcStructName, walk.pcScriptPath, script_name, offset);
			return false;
		}

		const HostField& host = walk.HostFields.pData[walk.NextHostField];
		const StrataTypeKind kind = ResolveKind(walk.pTypes, field);

		if (host.Offset != offset || host.Size != field.size || host.Kind != kind ||
			strcmp(host.pcScriptName, script_name) != 0) {
			LogError(LC_SCRIPT,
					 "Script struct '{}' ({}): field {} (offset {}, size {}, {}) does not match host field {} "
					 "(expects {}, offset {}, size {}, {})",
					 walk.pcStructName, walk.pcScriptPath, script_name, offset, field.size, KindName(kind), host.pcName,
					 host.pcScriptName, host.Offset, host.Size, KindName(host.Kind));
			return false;
		}

		++walk.NextHostField;
	}

	return true;
}

} // namespace

bool ValidateScriptStruct(const Script& script, const char* script_struct, uint32 host_size,
						  Slice<const HostField> host_fields)
{
	const char* script_path = script.GetPath().CStr();
	const Slice<const uint8> metadata = script.GetTypeMetadata();

	StrataTypes types;
	if (metadata.pData == nullptr || !strataTypesOpen(&types, metadata.pData, metadata.Size)) {
		LogError(LC_SCRIPT, "Script '{}' has no type metadata. Ensure the struct '{}' is marked as @component",
				 script_path, script_struct);
		return false;
	}

	StrataTypesStruct found;
	bool is_found = false;

	for (uint32 i = 0; i < strataTypesStructCount(&types); i++) {
		if (strataTypesGetStruct(&types, i, &found) && !strcmp(found.name, script_struct)) {
			is_found = true;
			break;
		}
	}

	if (!is_found) {
		LogError(LC_SCRIPT, "Script '{}' has no metadata for struct '{}'; mark it as @component", script_path,
				 script_struct);
		return false;
	}

	if (found.size != host_size) {
		LogError(LC_SCRIPT, "Script struct '{}' ({}) is {} bytes but the host struct is {} bytes", script_struct,
				 script_path, found.size, host_size);
		return false;
	}

	LayoutWalk walk {
		.pTypes = &types,
		.HostFields = host_fields,
		.pcScriptPath = script_path,
		.pcStructName = script_struct,
	};

	if (!WalkStruct(walk, found, 0)) {
		return false;
	}

	if (walk.NextHostField != host_fields.Size) {
		const HostField& host = host_fields.pData[walk.NextHostField];
		LogError(LC_SCRIPT, "Script struct '{}' ({}): host field {} (expects {}) has no script counterpart",
				 script_struct, script_path, host.pcName, host.pcScriptName);
		return false;
	}

	return true;
}

} // namespace fx::script

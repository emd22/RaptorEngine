/*
 * File:        RustInterop.hpp
 * Author:      emd22
 * Created:     02/10/2026
 * Description: Functions for interop with Rust code
 */

#pragma once

#include <raptor_ffi.h>

#include <Core/Log.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <cstdio>
#include <string>
#include <string_view>

namespace fx {

namespace RustInterop {

inline void Log(void*, int32 level, int32 category, const char* message, size_t length)
{
	const std::string_view text(message, length);
	const eLogCategory log_category = static_cast<eLogCategory>(category);

	switch (level) {
	case RX_LOG_PRINT:
		puts(std::string(text).c_str());
		break;
	case RX_LOG_INFO:
		LogInfo(log_category, "{}", text);
		break;
	case RX_LOG_WARNING:
		LogWarning(log_category, "{}", text);
		break;
	case RX_LOG_DEBUG:
		LogDebug(log_category, "{}", text);
		break;
	default:
		LogError(log_category, "{}", text);
		break;
	}
}

inline void ReadVec3s(SizedArray<Vec3f>& out, const float* data, size_t count)
{
	if (count == 0) {
		return;
	}

	out.InitCapacity(count);

	for (size_t i = 0; i < count; i++) {
		out.Insert(Vec3f(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]));
	}
}

inline void ReadVec2s(SizedArray<Vec2f>& out, const float* data, size_t count)
{
	if (count == 0) {
		return;
	}

	out.InitCapacity(count);

	for (size_t i = 0; i < count; i++) {
		out.Insert(Vec2f(data[i * 2], data[i * 2 + 1]));
	}
}

inline void ReadIndices(SizedArray<uint32>& out, const uint32* data, size_t count)
{
	if (count == 0) {
		return;
	}

	out.InitCapacity(count);

	for (size_t i = 0; i < count; i++) {
		out.Insert(data[i]);
	}
}

inline void ReadMesh(const RxMesh* mesh, SizedArray<Vec3f>& positions, SizedArray<Vec3f>& normals,
					 SizedArray<Vec3f>& tangents, SizedArray<Vec2f>& texcoords, SizedArray<uint32>& indices)
{
	size_t count = 0;

	const float* position_data = rx_mesh_positions(mesh, &count);
	ReadVec3s(positions, position_data, count);

	const float* normal_data = rx_mesh_normals(mesh, &count);
	ReadVec3s(normals, normal_data, count);

	const float* tangent_data = rx_mesh_tangents(mesh, &count);
	ReadVec3s(tangents, tangent_data, count);

	const float* texcoord_data = rx_mesh_texcoords(mesh, &count);
	ReadVec2s(texcoords, texcoord_data, count);

	const uint32* index_data = rx_mesh_indices(mesh, &count);
	ReadIndices(indices, index_data, count);
}

} // namespace RustInterop

} // namespace fx

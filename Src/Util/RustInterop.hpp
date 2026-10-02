/*
 * File:        RustInterop.hpp
 * Author:      emd22
 * Created:     02/10/2026
 * Description: Functions for interop with Rust code
 */

#pragma once

#include <raptor_ffi.h>

#include <Core/Log.hpp>
#include <Core/Types.hpp>
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
	default:
		LogError(log_category, "{}", text);
		break;
	}
}

} // namespace RustInterop

} // namespace fx

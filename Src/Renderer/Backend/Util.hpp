
#pragma once

#include <vulkan/vulkan.h>

#include <Core/Defines.hpp>
#include <raptor_ffi.h>

// #include <Core/Types.hpp>

namespace fx::renderer {

#define VkTry(func_result, message)                                                                                    \
	{                                                                                                                  \
		const VkResult result__ = (func_result);                                                                       \
		if (result__ != VK_SUCCESS) {                                                                                  \
			ModulePanicVulkan((message), result__);                                                                    \
		}                                                                                                              \
	}

class Util
{
public:
	static const char* ResultToStr(VkResult result)
	{
		thread_local char buffer[64];

		rx_vk_result_name(result, buffer, sizeof(buffer));

		return buffer;
	}

	static void SetDebugLabel_(const char* name, VkObjectType object_type, unsigned long long obj);

	template <typename T>
	static void SetDebugLabel(const char* name, VkObjectType object_type, T obj)
	{
#ifdef FX_BUILD_DEBUG
		SetDebugLabel_(name, object_type, reinterpret_cast<unsigned long long>(obj));
#endif
	}
};

} // namespace fx::renderer

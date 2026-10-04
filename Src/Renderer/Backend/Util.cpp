#include "Util.hpp"

#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::renderer {

void Util::SetDebugLabel_(const char* name, VkObjectType object_type, unsigned long long obj_handle)
{
	rx_gpu_set_object_name(gGraphics->GetDevice()->GetRustDevice(), object_type, static_cast<uint64>(obj_handle), name);
}

} // namespace fx::renderer

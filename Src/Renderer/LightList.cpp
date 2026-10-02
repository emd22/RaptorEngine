#include "LightList.hpp"

namespace fx::renderer {

void LightList::AddLight(const LightID id)
{
	if (id.IsNull() || id.IsInvalid() || Contains(id)) {
		return;
	}

	mLights.Insert(id);
}

void LightList::InvalidateLight(const LightID id)
{
	for (LightID& found_id : mLights) {
		if (found_id == id) {
			found_id.Invalidate();
			break;
		}
	}
}

bool LightList::Contains(const LightID id) const
{
	for (const LightID& found_id : mLights) {
		if (!found_id.IsInvalid() && found_id == id) {
			return true;
		}
	}

	return false;
}

void LightList::Clear() { mLights.Clear(); }

uint32 LightList::GetItemCount() const { return static_cast<uint32>(mLights.Size); }

} // namespace fx::renderer

#pragma once

#include <Core/DynArray.hpp>
#include <Core/Types.hpp>
#include <Renderer/LightID.hpp>

namespace fx::renderer {

class LightList
{
public:
	LightList() = default;

	/**
	 * @brief Adds a light to the list unless it is already in it, as a light that reaches several tiles is found once
	 * for each of them.
	 */
	void AddLight(const LightID id);

	/**
	 * @brief Invalidate a light in the list. Use when a light is going to be destroyed or will be invalid before the
	 * next lightlist rebuild.
	 */
	void InvalidateLight(const LightID id);

	bool Contains(const LightID id) const;

	void Clear();

	uint32 GetItemCount() const;

	/**
	 * @brief The lights to render this frame. Entries of destroyed lights are invalid and need skipping.
	 */
	const DynArray<LightID>& GetLights() const { return mLights; }

private:
	DynArray<LightID> mLights;
};

} // namespace fx::renderer

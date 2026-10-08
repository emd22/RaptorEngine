#pragma once

#include "LightID.hpp"

#include <Core/FreeArray.hpp>
#include <Core/Hash.hpp>
#include <Core/Ref.hpp>
#include <Core/String.hpp>
#include <Renderer/Limits.hpp>
#include <mutex>

namespace fx {

class LightBase;
class LightDirectional;

class LightManager
{
public:
	static constexpr uint32 scMaxLights = Limits::MaxActiveLights;

public:
	void Create();

	template <typename TLightType, typename... TArgs>
	TLightType* NewLight(const String& name, TArgs&&... args)
	{
		TLightType* light = new TLightType(std::forward<TArgs>(args)...);
		light->Name = name;

		RegisterLight(light);

		return light;
	}

	/**
	 * @brief Retrieves a light by its ID
	 */
	LightBase* GetLight(LightID id);
	void RemoveLight(LightID& id);

	void Clear();

	/**
	 * @brief Finds an attached light by its name, returns a null ref if there is no match
	 */
	LightBase* FindLight(Hash32 name_hash);

	LightDirectional* GetDirectionalLight();

	const FreeArray<LightBase*>& GetCache() const { return mLightList; }
	FreeArray<LightBase*>& GetCache() { return mLightList; }

	void Destroy();

	~LightManager() { Destroy(); }

private:
	/// Gives `light` a slot and a LightID, and runs its OnAttached hook
	void RegisterLight(LightBase* light);

	/**
	 * Internal function to destroy and dispose of a light.
	 */
	void DestroyLight(LightBase* light);

private:
	FreeArray<LightBase*> mLightList;

	std::mutex mInUse;
};

} // namespace fx

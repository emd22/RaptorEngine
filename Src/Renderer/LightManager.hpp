#pragma once

#include "LightID.hpp"

#include <Core/FreeArray.hpp>
#include <Core/Hash.hpp>
#include <Core/Ref.hpp>
#include <Renderer/Limits.hpp>
#include <mutex>
#include <string>

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
	TLightType* NewLight(const std::string& name, TArgs&&... args)
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
	void DestroyLight(LightID& id);

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

private:
	FreeArray<LightBase*> mLightList;

	std::mutex mInUse;
};

} // namespace fx

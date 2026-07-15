#pragma once

#include "LoaderBase.hpp"

#include <Asset/AssetTicket.hpp>
#include <Core/String.hpp>
#include <Object/Object.hpp>

namespace fx {


namespace loader {
class ObjectLoaderBase : public LoaderBase
{
public:
	eLoaderType GetLoaderType() const override { return eLoaderType::ObjectLoader; };

public:
	ObjectLoaderBase() = default;

	virtual eLoaderStatus Load(AssetTicket& ticket, const String& path) = 0;
	virtual eLoaderStatus Load(AssetTicket& ticket, const uint8* data, uint32 size) = 0;

	virtual void CreateGpuResource(AssetTicket& ticket) = 0;

	virtual void Destroy() = 0;
	virtual ~ObjectLoaderBase() = default;

protected:
	friend class AxManager;
};


template <typename TLoader>
concept C_IsObjectLoader = std::is_base_of_v<ObjectLoaderBase, TLoader>;

} // namespace loader

} // namespace fx

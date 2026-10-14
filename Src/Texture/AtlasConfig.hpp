#pragma once

#include <Asset/ConfigFile.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>

namespace fx {

struct AtlasRegion
{
	float32 ScaleU = 1.0f;
	float32 ScaleV = 1.0f;
	float32 OffsetU = 0.0f;
	float32 OffsetV = 0.0f;
};

class AtlasConfig
{
public:
	bool Load(const char* directory, const char* config_path, const char* default_image);

	bool GetRegion(const char* group_name, AtlasRegion& out_region) const;

	const String& GetImagePath() const { return mImagePath; }

private:
	ConfigFile mConfig;
	String mConfigPath;
	String mImagePath;

	const ConfigEntry* mpGroups = nullptr;
	float32 mTileSize = 0.0f;
	float32 mAtlasSize[2] = { 0.0f, 0.0f };
};

} // namespace fx

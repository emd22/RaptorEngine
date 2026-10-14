#include "AtlasConfig.hpp"

#include <Core/Log.hpp>

namespace fx {

FX_SET_MODULE_NAME("AtlasConfig")

static bool ReadIntPair(const ConfigEntry* entry, float32* out)
{
	if (entry == nullptr || !entry->bIsArray || entry->GetArrayData().Size() < 2) {
		return false;
	}

	out[0] = entry->GetArrayData()[0].Get<float32>();
	out[1] = entry->GetArrayData()[1].Get<float32>();
	return true;
}

bool AtlasConfig::Load(const char* directory, const char* config_path, const char* default_image)
{
	mConfig.Load(config_path);
	mConfigPath = config_path;

	mImagePath = directory;

	const ConfigEntry* image_entry = mConfig.GetEntry(HashStr32("image"));

	if (image_entry != nullptr) {
		mImagePath += image_entry->GetValue<const char*>();
	}
	else {
		mImagePath += default_image;
	}

	mTileSize = mConfig.GetEntryValue<float32>(HashStr32("tile_size"), 0.0f);
	mpGroups = mConfig.GetEntry(HashStr32("groups"));

	if (!ReadIntPair(mConfig.GetEntry(HashStr32("atlas_size")), mAtlasSize) || mTileSize <= 0.0f ||
		mpGroups == nullptr) {
		LogError(LC_ASSET, "Could not read the atlas layout from {}", config_path);
		mpGroups = nullptr;
		return false;
	}

	return true;
}

bool AtlasConfig::GetRegion(const char* group_name, AtlasRegion& out_region) const
{
	if (mpGroups == nullptr) {
		return false;
	}

	const ConfigEntry* group = mpGroups->GetMember(HashStr32(group_name));

	float32 tile[2];
	float32 tiles[2];

	if (group == nullptr || !ReadIntPair(group->GetMember(HashStr32("tile")), tile) ||
		!ReadIntPair(group->GetMember(HashStr32("tiles")), tiles)) {
		LogError(LC_ASSET, "Atlas {} has no group '{}'", mConfigPath, group_name);
		return false;
	}

	out_region.ScaleU = (tiles[0] * mTileSize) / mAtlasSize[0];
	out_region.ScaleV = (tiles[1] * mTileSize) / mAtlasSize[1];
	out_region.OffsetU = (tile[0] * mTileSize) / mAtlasSize[0];
	out_region.OffsetV = (tile[1] * mTileSize) / mAtlasSize[1];

	return true;
}

} // namespace fx

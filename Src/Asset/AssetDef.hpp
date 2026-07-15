#pragma once

namespace fx {

enum class eAssetType
{
	None,
	Binary,
	Object,
	Image,
};

enum class eAssetLoadOp
{
	None,

	/// Reads a file source into an asset given a path.
	ReadAndUpload,

	/// Reads unprocessed file data using a loader and uploads to an asset.
	ProcessAndUpload,

	/// Directly uploads the data into an asset.
	DirectUpload,
};

constexpr const char* AssetTypeToString(eAssetType type)
{
	switch (type) {
	case eAssetType::None:
		return "None";
	case eAssetType::Binary:
		return "Binary";
	case eAssetType::Object:
		return "Object";
	case eAssetType::Image:
		return "Image";
	default:;
	}
	return "None";
}


} // namespace fx

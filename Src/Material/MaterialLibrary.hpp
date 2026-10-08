#pragma once

#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>

namespace fx {

struct MaterialLibraryID
{
public:
	static const MaterialLibraryID scNull;

public:
	explicit constexpr MaterialLibraryID(int32 material_id) : ID(material_id) {}

	FX_FORCE_INLINE constexpr bool IsValid() const { return ID >= 0; }

	constexpr bool operator==(const MaterialLibraryID other) const { return ID == other.ID; }
	constexpr bool operator!=(const MaterialLibraryID other) const { return ID != other.ID; }

public:
	int32 ID = -1;
};

inline constexpr MaterialLibraryID MaterialLibraryID::scNull { -1 };

class MaterialLibrary
{
public:
	struct Entry
	{
		Name EntryName;
		MaterialID Material = MaterialID::scNull;
		String DiffusePath;
	};

public:
	bool Load(const std::string& list_path, const std::string& texture_root);

	FX_FORCE_INLINE uint32 GetCount() const { return static_cast<uint32>(mEntries.Size()); }

	const String& GetName(const MaterialLibraryID id) const;
	const String& GetDiffusePath(const MaterialLibraryID id) const;

	MaterialID GetMaterial(const MaterialLibraryID id) const;
	MaterialLibraryID FindID(const MaterialID material) const;
	MaterialLibraryID FindIDByName(const char* name) const;

private:
	PagedArray<Entry> mEntries;
};

} // namespace fx

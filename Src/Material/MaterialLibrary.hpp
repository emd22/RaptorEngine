#pragma once

#include <Core/PagedArray.hpp>
#include <string>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>

namespace fx {

class MaterialLibrary
{
public:
	struct Entry
	{
		String Name;
		MaterialID Material = MaterialID::scNull;
		std::string DiffusePath;
	};

public:
	bool Load(const std::string& list_path, const std::string& texture_root);

	FX_FORCE_INLINE uint32 GetCount() const { return static_cast<uint32>(mEntries.Size()); }

	const String& GetName(uint32 id) const;
	const std::string& GetDiffusePath(uint32 id) const;
	MaterialID GetMaterial(int32 id) const;
	int32 FindID(const MaterialID& material) const;
	int32 FindIDByName(const char* name) const;

private:
	PagedArray<Entry> mEntries;
};

} // namespace fx

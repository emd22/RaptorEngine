#pragma once

#include <raptor_ffi.h>

#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>

#include <memory>
#include <string>

namespace fx {

class MaterialLibrary
{
public:
	MaterialLibrary();

	bool Load(const std::string& list_path, const std::string& texture_root);

	uint32 GetCount() const { return rx_material_library_count(mpLibrary.get()); }

	const char* GetName(uint32 id) const { return rx_material_library_name(mpLibrary.get(), id); }
	MaterialID GetMaterial(int32 id) const { return MaterialID(rx_material_library_material(mpLibrary.get(), id)); }
	int32 FindID(const MaterialID& material) const { return rx_material_library_find(mpLibrary.get(), material.GetID()); }

private:
	struct Deleter
	{
		void operator()(RxMaterialLibrary* library) const { rx_material_library_free(library); }
	};

	std::unique_ptr<RxMaterialLibrary, Deleter> mpLibrary;
};

} // namespace fx

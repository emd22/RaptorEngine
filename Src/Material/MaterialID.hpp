#pragma once

#include <Core/Types.hpp>
#include <Material/SpecialMaterial.hpp>
#include <format>

namespace fx {

struct MaterialID
{
	using IDType = uint32;

	static const MaterialID scNull;

public:
	MaterialID() = default;
	MaterialID(IDType id) : ID(id) {}
	explicit MaterialID(eSpecialMaterial special) : ID(static_cast<IDType>(special)) {}
	MaterialID(const MaterialID& other) : ID(other.ID) {}

	MaterialID& operator=(uint32 value) = delete;
	MaterialID& operator=(const MaterialID& other)
	{
		ID = other.ID;
		return *this;
	}

	FX_FORCE_INLINE bool operator==(const MaterialID& other) const { return ID == other.ID; }

	FX_FORCE_INLINE IDType GetID() const { return ID; }
	FX_FORCE_INLINE bool IsNull() const { return ID == 0; }

	FX_FORCE_INLINE eSpecialMaterial GetSpecial() const
	{
		if (ID < static_cast<IDType>(eSpecialMaterial::Count)) {
			return static_cast<eSpecialMaterial>(ID);
		}

		return eSpecialMaterial::None;
	}

public:
	IDType ID = 0;
};


} // namespace fx

template <>
struct std::formatter<fx::MaterialID>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::MaterialID& id, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "MaterialID({})", id.GetID());
	}
};

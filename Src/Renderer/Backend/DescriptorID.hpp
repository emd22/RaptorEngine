#pragma once

#include <Core/Hash.hpp>

namespace fx::renderer {

/**
 * @brief Identifier for a descriptor set layout.
 */
struct DsLayoutID
{
	DsLayoutID() = delete;
	explicit DsLayoutID(Hash32 value) : ID(value) {}

	FX_FORCE_INLINE bool IsValid() const { return ID != HashNull32; }
	FX_FORCE_INLINE Hash32 GetID() const { return ID; }

	~DsLayoutID() = default;

public:
	Hash32 ID = HashNull32;
};


/**
 * @brief Identifier for a descriptor set.
 */
struct DescriptorID
{
	DescriptorID() = delete;
	explicit DescriptorID(Hash32 value) : ID(value) {}

	FX_FORCE_INLINE bool IsValid() const { return ID != HashNull32; }
	FX_FORCE_INLINE Hash32 GetID() const { return ID; }

	~DescriptorID() = default;

public:
	Hash32 ID = HashNull32;
};


} // namespace fx::renderer


template <>
struct std::formatter<fx::renderer::DsLayoutID>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::renderer::DsLayoutID& id, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "DsLayoutID({})", id.ID);
	}
};

template <>
struct std::formatter<fx::renderer::DescriptorID>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::renderer::DescriptorID& id, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "DescriptorID({})", id.ID);
	}
};

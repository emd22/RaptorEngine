#pragma once

#include <Core/Types.hpp>
#include <format>

namespace fx {

/*
 *         Texture ID Structure
 * --------------------------------------
 * [I][   P   ][          LI            ]
 *
 * I  ( 1 bit ) ->  Is the ID invalid (deleted or otherwise)
 * P  ( 7 bits) ->  Reserved
 * LI (24 bits) ->  Local index
 */


struct TextureID
{
	using IDType = uint32;

	static const TextureID Null;

	static const IDType scInvalidBit = (1U << 31);
	static const IDType scIDMask = 0x00FFFFFFU;

public:
	TextureID() = default;
	TextureID(IDType id) : ID(id) {}
	TextureID(const TextureID& other) : ID(other.ID) {}

	TextureID& operator=(IDType value) = delete;
	TextureID& operator=(const TextureID& other)
	{
		ID = other.ID;
		return *this;
	}

	IDType operator()() const { return ID; }

	bool operator==(const TextureID& other) const { return ID == (other.ID & scIDMask); }
	bool operator<(const TextureID& other) const { return ID < other.ID; }

	FX_FORCE_INLINE IDType GetID() const { return (ID & scIDMask); }
	FX_FORCE_INLINE bool IsNull() const { return ID == UINT32_MAX; }

	FX_FORCE_INLINE bool IsInvalid() const { return (ID & scInvalidBit) != 0; }
	FX_FORCE_INLINE void Invalidate() { ID |= scInvalidBit; };

public:
	IDType ID = UINT32_MAX;
};

} // namespace fx

namespace std {
template <>
struct hash<fx::TextureID>
{
	std::size_t operator()(const fx::TextureID& id) const noexcept { return id.ID; }
};
} // namespace std


template <>
struct std::formatter<fx::TextureID>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::TextureID& id, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "TextureID({})", id.GetID());
	}
};

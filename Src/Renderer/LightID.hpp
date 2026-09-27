#pragma once

#include <Core/Types.hpp>
#include <format>

namespace fx {


struct LightID
{
	using IDType = uint32;

	static const LightID scNull;

	static constexpr IDType scInvalidBit = (1U << 31);

public:
	LightID() = default;
	LightID(IDType id) : ID(id) {}
	LightID(const LightID& other) : ID(other.ID) {}

	LightID& operator=(uint32 value) = delete;
	LightID& operator=(const LightID& other)
	{
		ID = other.ID;
		return *this;
	}

	IDType operator()() const { return ID; }

	bool operator==(const LightID& other) const { return GetID() == other.GetID(); }
	bool operator<(const LightID& other) const { return ID < other.ID; }

	FX_FORCE_INLINE IDType GetID() const { return (ID & ~scInvalidBit); }
	FX_FORCE_INLINE bool IsNull() const { return ID == UINT32_MAX; }

	FX_FORCE_INLINE bool IsInvalid() const { return (ID & scInvalidBit) != 0; }
	FX_FORCE_INLINE void Invalidate() { ID |= scInvalidBit; };

public:
	IDType ID = UINT32_MAX;
};

} // namespace fx

namespace std {
template <>
struct hash<fx::LightID>
{
	std::size_t operator()(const fx::LightID& id) const noexcept { return id.ID; }
};
} // namespace std


template <>
struct std::formatter<fx::LightID>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::LightID& id, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "LightID({})", id.GetID());
	}
};

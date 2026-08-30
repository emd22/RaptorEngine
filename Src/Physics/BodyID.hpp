#pragma once

#include <Core/Types.hpp>
#include <format>

namespace fx::physics {

struct BodyID
{
	using IDType = uint32;

	static const BodyID scNull;

	static const IDType scInvalidBit = (1U << 31);
	static const IDType scIDMask = 0x7FFFFFFF;

public:
	BodyID() = default;
	BodyID(IDType id) : ID(id) {}
	BodyID(const BodyID& other) : ID(other.ID) {}

	BodyID& operator=(uint32 value) = delete;
	BodyID& operator=(const BodyID& other)
	{
		ID = other.ID;
		return *this;
	}

	IDType operator()() const { return ID; }

	bool operator==(const BodyID& other) const { return ID == (other.ID & scIDMask); }
	bool operator<(const BodyID& other) const { return ID < other.ID; }

	FX_FORCE_INLINE IDType GetID() const { return (ID & scIDMask); }
	FX_FORCE_INLINE bool IsNull() const { return ID == UINT32_MAX; }

	FX_FORCE_INLINE bool IsInvalid() const { return (ID & scInvalidBit) != 0; }
	FX_FORCE_INLINE void Invalidate() { ID |= scInvalidBit; };

public:
	IDType ID = UINT32_MAX;
};

} // namespace fx::physics

namespace std {
template <>
struct hash<fx::physics::BodyID>
{
	std::size_t operator()(const fx::physics::BodyID& id) const noexcept { return id.ID; }
};
} // namespace std


template <>
struct std::formatter<fx::physics::BodyID>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::physics::BodyID& id, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "ObjectID({})", id.GetID());
	}
};

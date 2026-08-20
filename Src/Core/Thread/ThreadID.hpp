#pragma once

#include <Core/Types.hpp>
#include <format>

namespace fx {

struct ThreadID
{
	using IDType = uint32;

	static const ThreadID scMainThread;
	static const ThreadID scInvalid;

public:
	ThreadID() = default;
	ThreadID(IDType id) : ID(id) {}
	ThreadID(const ThreadID& other) : ID(other.ID) {}

	ThreadID& operator=(uint32 value) = delete;
	ThreadID& operator=(const ThreadID& other)
	{
		ID = other.ID;
		return *this;
	}

	IDType operator()() const { return ID; }

	FX_FORCE_INLINE bool operator==(const ThreadID& other) const { return (ID == other.ID); }
	FX_FORCE_INLINE bool operator<(const ThreadID& other) const { return (ID < other.ID); }

	FX_FORCE_INLINE IDType GetID() const { return ID; }
	FX_FORCE_INLINE bool IsMainThread() const { return ID == scMainThread; }
	FX_FORCE_INLINE bool IsInvalid() const { return ID == scInvalid; }

public:
	IDType ID = UINT32_MAX;
};

} // namespace fx


template <>
struct std::formatter<fx::ThreadID>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const fx::ThreadID& id, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "ThreadID({})", id.GetID());
	}
};

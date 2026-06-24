#pragma once

#include <Core/Types.hpp>
#include <format>

namespace fx {

/*
 *             ID Structure
 * --------------------------------------
 * [I][   P   ][          LI            ]
 *
 * I  ( 1 bit )   ->  Is the ID invalid
 * P  ( 7 bits)  ->  Page number
 * LI (24 bits) ->  Local index
 */


struct ObjectID
{
    using IDType = uint32;

    static const ObjectID Null;

    static const IDType scInvalidBit = (1U << 31);
    static const IDType scIDMask = 0x0000FFFF;

public:
    ObjectID() = default;
    ObjectID(IDType id) : ID(id) {}
    ObjectID(const ObjectID& other) : ID(other.ID) {}

    ObjectID& operator=(uint32 value) = delete;
    ObjectID& operator=(const ObjectID& other)
    {
        ID = other.ID;
        return *this;
    }

    FX_FORCE_INLINE IDType GetID() const { return (ID & scIDMask); }
    FX_FORCE_INLINE bool IsNull() const { return ID == UINT32_MAX; }

    FX_FORCE_INLINE bool IsInvalid() const { return (ID & scInvalidBit) != 0; }
    FX_FORCE_INLINE void Invalidate() { ID |= scInvalidBit; };

    FX_FORCE_INLINE IDType GetPageNumber() const { return ((ID & ~(scInvalidBit)) >> 24); }

public:
    IDType ID = UINT32_MAX;
};

} // namespace fx

template <>
struct std::formatter<fx::ObjectID>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::ObjectID& id, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "ObjectID({})", id.GetID());
    }
};

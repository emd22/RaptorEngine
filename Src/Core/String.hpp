#pragma once

#include <Core/Types.hpp>
#include <format>
#include <string>

namespace fx {

class String
{
    static constexpr uint32 scStackAllocSize = 32;

public:
    static String NoCopy(char* ptr, uint32 length);

    template <typename... TTypes>
    static String Fmt(const char* fmt, TTypes&&... args)
    {
        return String(std::vformat(fmt, std::make_format_args(args...)));
    }

    String() = default;
    String(uint32 allocation_size);
    String(const char* str, uint32 length);
    String(const std::string& str);
    String(const char* str);
    String(const String& other) { (*this) = other; }

    FX_FORCE_INLINE bool IsHeapAllocated() const { return (mpHeapStr != nullptr); }

    FX_FORCE_INLINE uint32 GetLength() const { return Length; }

    void Clear();

    const char* CStr() const
    {
        if (IsHeapAllocated()) {
            return mpHeapStr;
        }

        return mpStackStr;
    }

    std::string Str() const { return std::string(CStr(), Length); }

    String& operator=(const char* str);
    String& operator=(const String& other);

    bool operator==(const String& other) const;

    String operator+(const String& other) const;
    String operator+(const char* other) const;

    const char operator[](size_t index) const;
    char& operator[](size_t index);

    ~String();

private:
    FX_FORCE_INLINE char* GetInternalPtr()
    {
        if (IsHeapAllocated()) {
            return mpHeapStr;
        }

        return mpStackStr;
    }

    FX_FORCE_INLINE const char* GetInternalPtr() const
    {
        if (IsHeapAllocated()) {
            return mpHeapStr;
        }

        return mpStackStr;
    }

public:
    uint32 Length = 0;

private:
    char mpStackStr[scStackAllocSize];
    char* mpHeapStr = nullptr;
};

} // namespace fx


template <>
struct std::formatter<fx::String>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::String& str, std::format_context& ctx) const
    {
        return std::format_to(ctx.out(), "{}", str.CStr());
    }
};

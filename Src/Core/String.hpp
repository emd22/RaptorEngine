#pragma once

#include <Core/Types.hpp>
#include <format>
#include <string>

namespace fx {

class String
{
    static constexpr uint32 scStackAllocSize = 16;

public:
    static constexpr uint32 scNotFound = UINT32_MAX;

public:
    static String NoCopy(char* ptr, uint32 length);

    template <typename... TTypes>
    static String Fmt(const char* fmt, TTypes&&... args)
    {
        return String(std::vformat(fmt, std::make_format_args(args...)));
    }

    String() = default;
    explicit String(uint32 allocation_size);
    String(const char* str, uint32 length);
    String(const std::string& str);
    String(const char* str);
    String(const String& other) { (*this) = other; }

    FX_FORCE_INLINE bool IsHeapAllocated() const { return (mpHeapStr != nullptr); }

    FX_FORCE_INLINE uint32 GetLength() const { return Length; }

    uint32 FindFirst(char ch) const;
    uint32 FindLast(char ch) const;
    uint32 FindNext(uint32 start, char ch) const;

    uint32 FindNext(uint32 start, const String& match) const;

    /**
     * @brief Replace all occurances of `to_replace` with
     */
    String ReplaceAll(const char* to_replace, char replacement);

    void Clear();

    String& ShortenTo(uint32 new_length);

    const char* CStr() const
    {
        if (IsHeapAllocated()) {
            return mpHeapStr;
        }

        return mpStackStr;
    }

    /**
     * @brief Creates a copy of this string from `start` to `end`.
     * @returns The copy of the string
     */
    String SubStrAbs(uint32 start, uint32 end) const;
    String SubStr(uint32 start, uint32 length) const;

    std::string Str() const { return std::string(CStr(), Length); }

    String& operator=(const char* str);
    String& operator=(const String& other);

    bool operator==(const String& other) const;

    String operator+(const String& other) const;
    String operator+(const char* other) const;

    String& operator+=(const String& other);

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

class ConstString
{
public:
    ConstString() = default;
    ConstString(const char* ptr, uint32 length);

    const char* CStr() const { return pStr; }

public:
    uint32 Length = 0;
    const char* pStr = nullptr;
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

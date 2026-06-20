#include "String.hpp"

#include <Core/Assert.hpp>
#include <Core/Log.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Engine.hpp>

namespace fx {

String String::NoCopy(char* ptr, uint32 length)
{
    String result;
    result.mpHeapStr = ptr;
    result.Length = length;
    return result;
}

String::String(const char* str, uint32 length)
{
    Length = length;

    char* dst = mpStackStr;

    // If the size cannot fit into the stack string, allocate a buffer for it
    if (length >= scStackAllocSize) {
        mpHeapStr = gEnginePool->Alloc<char>(length + 1);
        dst = mpHeapStr;
    }

    memcpy(dst, str, length);
    dst[length] = 0;
}

String::String(const char* str) : String(str, strlen(str)) {}

String::String(const std::string& str) : String(str.c_str(), str.length()) {}

String::String(uint32 allocation_size)
{
    Length = allocation_size;

    // If the size cannot fit into the stack string, allocate a buffer for it
    if (Length >= scStackAllocSize) {
        mpHeapStr = gEnginePool->Alloc<char>(Length + 1);
    }
}

String::String(String&& other) { (*this) = other; }


String String::SubStrAbs(uint32 start, uint32 end) const
{
    Assert(start <= Length && end <= Length);
    return String(GetInternalPtr() + start, (end - start));
}

String String::SubStr(uint32 start, uint32 length) const
{
    Assert(start <= Length && start + length <= Length);
    return String(GetInternalPtr() + start, length);
}

bool String::operator==(const String& other) const
{
    const char* a = GetInternalPtr();
    const char* b = other.GetInternalPtr();

    if (a == nullptr || b == nullptr) {
        return false;
    }

    return (!std::strcmp(a, b));
}

String String::operator+(const String& other) const
{
    // +1 for null terminator
    const uint32 result_len = Length + other.Length;
    String result(result_len);

    char* dst = result.GetInternalPtr();
    memcpy(dst, CStr(), Length);
    memcpy(dst + Length, other.CStr(), other.Length);
    dst[result_len] = '\0';

    return result;
}

String String::operator+(const char* other) const
{
    const uint32 other_len = strlen(other);

    // +1 for null terminator
    const uint32 result_len = Length + other_len;
    String result(result_len);

    char* dst = result.GetInternalPtr();
    memcpy(dst, CStr(), Length);
    memcpy(dst + Length, other, other_len);
    dst[result_len] = '\0';

    return result;
}

String& String::operator+=(const String& other)
{
    const uint32 existing_length = Length;
    const uint32 final_length = existing_length + other.Length;

    char* dst = GetInternalPtr();

    // Allocate heap string if there is not enough space remaining
    if (final_length >= scStackAllocSize) {
        if (mpHeapStr == nullptr) {
            mpHeapStr = gEnginePool->Alloc<char>(final_length + 1);

            // Copy the existing stack string to the newly allocated buffer
            memcpy(mpHeapStr, dst, existing_length);
        }
        else {
            mpHeapStr = gEnginePool->Realloc(mpHeapStr, final_length + 1);
        }

        dst = mpHeapStr;
    }

    memcpy(dst + existing_length, other.CStr(), other.Length);

    Length = final_length;
    dst[final_length] = '\0';

    return *this;
}


String& String::operator=(const char* str)
{
    const uint32 new_len = strlen(str) + 1;

    char* dst = mpStackStr;

    if (new_len >= scStackAllocSize && new_len > Length) {
        if (mpHeapStr == nullptr) {
            mpHeapStr = gEnginePool->Alloc<char>(new_len);
        }
        else {
            mpHeapStr = gEnginePool->Realloc(mpHeapStr, new_len);
        }
        dst = mpHeapStr;
    }

    memcpy(dst, str, new_len - 1);

    Length = new_len - 1;
    dst[Length] = 0;

    return *this;
}

String& String::operator=(const String& other)
{
    // Delete the current string if it exists
    Clear();

    if (other.IsHeapAllocated()) {
        mpHeapStr = gEnginePool->Alloc<char>(other.Length + 1);
        Length = other.Length;
        memcpy(mpHeapStr, other.mpHeapStr, Length);
        mpHeapStr[Length] = '\0';
    }
    else {
        Length = other.Length;
        memcpy(mpStackStr, other.mpStackStr, Length);
        mpStackStr[Length] = '\0';
    }


    return *this;
}

String& String::operator=(String&& other)
{
    // Delete the current string if it exists
    Clear();

    if (other.IsHeapAllocated()) {
        mpHeapStr = other.mpHeapStr;
        Length = other.Length;

        // Set the other string to null to avoid freeing
        other.mpHeapStr = nullptr;
        other.Length = 0;
    }
    else {
        Length = other.Length;
        memcpy(mpStackStr, other.mpStackStr, Length);

        // Not just transferring ptrs here, so we need to add the null terminator
        mpStackStr[Length] = '\0';

        other.Length = 0;
    }


    return *this;
}

uint32 String::FindFirst(char ch) const { return FindNext(0, ch); }
uint32 String::FindLast(char ch) const { return FindPrev(0, ch); }

uint32 String::FindPrev(uint32 skip, char ch) const
{
    const uint32 real_length = Length - 1;

    if (skip > real_length || (real_length - skip) == 0) {
        return scNotFound;
    }

    const char* pstr = GetInternalPtr();
    char cur = 0;


    for (uint32 i = real_length - skip; i > 0 && (cur = pstr[i]); i--) {
        if (cur == ch) {
            return i;
        }
    }

    return scNotFound;
}

uint32 String::FindNext(uint32 start, char ch) const
{
    if (start >= Length || (Length - start) == 0) {
        return scNotFound;
    }

    const char* pstr = GetInternalPtr();
    char cur = 0;

    for (uint32 i = start; (cur = pstr[i]) != 0; i++) {
        if (cur == ch) {
            return i;
        }
    }

    return scNotFound;
}

uint32 String::FindNext(uint32 start, const String& match) const
{
    // If there is only one character, use the char specific function
    if (match.Length == 1) {
        return FindNext(start, match[0]);
    }

    // Checks
    if (start >= Length || (Length - start) < match.Length) {
        return scNotFound;
    }


    const char* pstr = GetInternalPtr();
    char cur = 0;

    char first = match[0];

    for (uint32 i = start; (cur = pstr[i]) != 0; i++) {
        if (cur == first) {
            bool found = true;


            // Check for the rest of the string
            for (uint32 match_index = 1; match_index < match.Length; match_index++) {
                if (pstr[i + match_index] != match[match_index]) {
                    found = false;
                    break;
                }
            }

            if (found) {
                return i;
            }
        }
    }

    return scNotFound;
}

bool String::StartsWith(const char ch) const
{
    if (Length < 1) {
        return false;
    }

    return (GetInternalPtr()[0] == ch);
}

bool String::EndsWith(const char ch) const
{
    if (Length < 1) {
        return false;
    }

    return (GetInternalPtr()[Length - 1] == ch);
}


String String::ReplaceAll(const char* to_replace, char replacement)
{
    String copy = *this;

    char* original_ptr = GetInternalPtr();
    const char* replace_ptr = to_replace;

    uint32 count = 0;

    for (uint32 i = 0; i < Length; i++) {
        char ch = original_ptr[i];
        char replace_ch = 0;

        while ((replace_ch = *(replace_ptr++))) {
            if (ch == replace_ch) {
                copy[i] = replacement;
                ++count;
            }
        }

        replace_ptr = to_replace;
    }

    return copy;
}

void String::Clear()
{
    if (mpHeapStr && Length > scStackAllocSize) {
        gEnginePool->Free<char>(mpHeapStr);
    }

    mpHeapStr = nullptr;
    Length = 0;
}

String& String::ShortenTo(uint32 new_length)
{
    if (new_length >= Length) {
        return *this;
    }

    if (mpHeapStr) {
        // The string is now able to fit into the stack allocated buffer, copy it to there and free the stack string.
        if (new_length < scStackAllocSize) {
            memcpy(mpStackStr, mpHeapStr, Length);
            gEnginePool->Free<char>(mpHeapStr);
            mpHeapStr = nullptr;
        }
        else {
            mpHeapStr = gEnginePool->Realloc(mpHeapStr, new_length + 1);
        }
    }

    Length = new_length;
    GetInternalPtr()[Length] = '\0';

    return *this;
}


const char String::operator[](size_t index) const { return CStr()[index]; }
char& String::operator[](size_t index) { return GetInternalPtr()[index]; }

String::~String() { Clear(); }


/////////////////////////////////////
// Const String functions
/////////////////////////////////////


ConstString::ConstString(const char* ptr, uint32 length) : pStr(ptr), Length(length) {}

} // namespace fx

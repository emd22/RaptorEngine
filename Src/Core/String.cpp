#include "String.hpp"

#include <Core/Log.hpp>
#include <Core/MemPool/MemPool.hpp>
#include <Core/Panic.hpp>
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


String String::SubStr(uint32 start, uint32 end) const
{
    Assert(start <= Length && end <= Length);
    return String(GetInternalPtr() + start, (end - start));
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


String& String::operator=(const char* str)
{
    const uint32 new_len = strlen(str);

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

    memcpy(dst, str, new_len);


    Length = new_len;
    dst[Length] = 0;

    return *this;
}

String& String::operator=(const String& other)
{
    (*this) = other.CStr();

    return *this;
}

void String::Clear()
{
    if (mpHeapStr && Length > scStackAllocSize) {
        gEnginePool->Free<char>(mpHeapStr);
    }

    mpHeapStr = nullptr;
    Length = 0;
}


const char String::operator[](size_t index) const { return CStr()[index]; }
char& String::operator[](size_t index) { return GetInternalPtr()[index]; }

String::~String() { Clear(); }

} // namespace fx

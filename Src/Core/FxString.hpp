#pragma once

#include <Core/FxTypes.hpp>
#include <cstring>
#include <string>

// TODO: support wide characters

class FxString
{
public:
    FxString(char* ptr, uint64 length) : Data(ptr), Length(length) {}

    FxString(const std::string& other) { *this = other; }

    ~FxString()
    {
        if (mAllocated) {
            free(Data);
        }
    }

    FxString(char* ptr) : Data(ptr) { std::strlen(ptr); }

    bool Empty() const { return (Data == nullptr || Length == 0); }


    FxString& operator=(const std::string& other)
    {
        Data = (char*)malloc(other.size());
        memcpy(Data, other.data(), other.size());
        mAllocated = true;
    }

public:
    char* Data = nullptr;
    uint64 Length = 0;

private:
    bool mAllocated = false;
};

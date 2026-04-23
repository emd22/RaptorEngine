#pragma once

#include <Core/Hash.hpp>
#include <Core/String.hpp>

namespace fx {

class Name
{
public:
    Name() = default;
    Name(const char* name) : mName(name), mHash(HashStr32(name)) {}
    Name(const String& name) : mName(name), mHash(HashStr32(name.CStr())) {}

    const String& Get() const { return mName; }
    Hash32 GetHash() const { return mHash; }

    void Set(const String& name)
    {
        mName = name;
        mHash = HashStr32(name.CStr());
    }

    void Set(const char* name)
    {
        mName = name;
        mHash = HashStr32(name);
    }

    Name& operator=(const char* name)
    {
        Set(name);
        return *this;
    }

    Name& operator=(const String& name)
    {
        Set(name);
        return *this;
    }

    bool operator==(Hash32 hash) const { return mHash == hash; }

    void Clear()
    {
        mName.Clear();
        mHash = HashNull32;
    }

private:
    String mName = "";
    Hash32 mHash = HashNull32;
};

} // namespace fx

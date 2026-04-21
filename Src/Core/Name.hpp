#pragma once

#include <Core/Hash.hpp>

namespace fx {

class Name
{
public:
    Name() = default;
    Name(const char* name) : mName(name), mHash(HashStr64(name)) {}
    Name(const std::string& name) : mName(name), mHash(HashStr64(name.c_str())) {}

    const std::string& Get() const { return mName; }
    Hash64 GetHash() const { return mHash; }

    void Set(const std::string& name)
    {
        mName = name;
        mHash = HashStr64(name.c_str());
    }

    void Set(const char* name)
    {
        mName = name;
        mHash = HashStr64(name);
    }

    Name& operator=(const char* name)
    {
        Set(name);
        return *this;
    }

    Name& operator=(const std::string& name)
    {
        Set(name);
        return *this;
    }

    bool operator==(Hash64 hash) const { return mHash == hash; }

    void Clear()
    {
        mName.clear();
        mHash = HashNull64;
    }

private:
    std::string mName = "";
    Hash64 mHash = HashNull64;
};

} // namespace fx

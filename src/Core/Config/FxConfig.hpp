#pragma once

#include <Core/Types.hpp>

#include <vector>
#include <cstdio>

class FxConfig
{
public:
    FxConfig() = default;

    void Load(const char *path);

    template <typename Type>
    Type GetValue(uint32 NameHash);

    template <typename Type>
    Type GetValue(const char *Name);

protected:
    void ParseConfig();

private:
    struct Entry
    {
        uint32 NameHash;

        union {
            int32 IntValue;
            float32 FloatValue;
            char *StrValue;
        };

        enum ValueType {
            Int,
            Float,
            String,
        } Type;
    };

    std::vector<Entry> mEntries;

    FILE *mFile = nullptr;
};

extern FxConfig FxGlobalConfig;

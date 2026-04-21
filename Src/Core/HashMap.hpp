#pragma once

#include "Hash.hpp"

#include <cstdlib>

template <typename TKeyType, typename TValueType>
class HashMap
{
    static constexpr uint32 scInitialCapacity = 16;

    using KeyHashType = Hash64;

    struct Entry
    {
        KeyHashType Key;
        TValueType Value;
    };

public:
    HashMap() { mpEntries = std::malloc(sizeof(Entry) * scInitialCapacity); }

    void Set(TKeyType key) {}


private:
    Entry* mpEntries = nullptr;
};

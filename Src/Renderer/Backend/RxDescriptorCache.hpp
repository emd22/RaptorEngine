#pragma once

#include "RxDescriptors.hpp"

#include <Core/FxRef.hpp>
#include <Core/FxStackArray.hpp>
#include <Core/FxTypes.hpp>
#include <unordered_map>

struct RxShaderDescriptorEntry;

class RxDescriptorCache
{
    static constexpr uint32 scMaxSets = 6;
    static constexpr uint32 scMaxBindings = 6;
    static constexpr uint32 scMaxEntries = 4;

public:
    struct Entry
    {
        RxDescriptorSet Set;
        FxHash32 Hash = FxHashNull32;
    };

    struct Binding
    {
        FxSizedArray<Entry> Entries;

        void AddEntry(const RxShaderDescriptorEntry& entry);
    };

public:
    RxDescriptorCache() { mSections.MarkFull(); };

    FxRef<RxDescriptorSet> Request(const RxShaderDescriptorEntry& entry);

    ~RxDescriptorCache() = default;

public:
    //
    FxStackArray<FxSizedArray<Binding>, scMaxSets> mSections;
};

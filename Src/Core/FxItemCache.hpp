#pragma once

#include <Core/FxBitset.hpp>
#include <Core/FxHash.hpp>
#include <Core/FxSizedArray.hpp>
#include <map>

template <typename TItemType>
struct FxItemCacheSection
{
    FxBitset ItemsInUse;
    FxSizedArray<TItemType> Items;

public:
    void Create(uint32 max_items)
    {
        Items.Create(max_items);
        ItemsInUse.InitZero(max_items);
    }

    TItemType* RequestNewItem()
    {
        int index = ItemsInUse.FindNextFreeBit();

        if (index == FxBitset::scNoFreeBits) {
            return nullptr;
        }

        ItemsInUse.Set(index);

        return &Items[index];
    }
};

template <typename TItemType, typename TCacheSectionType>
    requires std::is_base_of_v<FxItemCacheSection<TItemType>, TCacheSectionType>
class FxItemCache
{
public:
    FxItemCache() = default;

    void Create(uint32 max_items_per_section) { mItemsPerSection = max_items_per_section; }


    bool IsItemInCache(FxHash key)
    {
        auto section_it = mCache.find(key);

        return (section_it == mCache.end());
    }

    TItemType* RequestItemFromHash(FxHash key)
    {
        auto section_it = mCache.find(key);

        if (section_it == mCache.end()) {
            CreateCacheSection(key, mItemsPerSection);
            auto section_it = mCache.find(key);

            FxAssert(section_it != mCache.end());
        }

        FxItemCacheSection<TItemType>& section = section_it->second;

        return section.RequestNewItem();
    }

    void ReleaseItemFromHash(FxHash hash, TItemType* item)
    {
        auto section_it = mCache.find(hash);

        if (section_it == mCache.end()) {
            FxLogError("Could not find cache section to free from!");
            return;
        }

        FxItemCacheSection<TItemType>& section = section_it->second;

        auto item_index = section.Items.GetItemIndex(item);

        // If the item was found, clear the bit
        if (item_index != FxSizedArray<TItemType>::scItemNotFound) {
            section.ItemsInUse.Unset(item_index);
        }
    }

    void Destroy();

    virtual ~FxItemCache() { Destroy(); }

protected:
    TCacheSectionType& CreateCacheSection(FxHash hash, uint32 number_of_items)
    {
        TCacheSectionType cache_section {};

        cache_section.Items.InitCapacity(number_of_items);
        cache_section.ItemsInUse.InitZero(number_of_items);

        auto iter = mCache.insert({ hash, std::move(cache_section) });

        return iter.first->second;
    }

public:
    std::unordered_map<FxHash, TCacheSectionType> mCache;

protected:
    uint32 mItemsPerSection = 0;
};

template <typename TItemType, typename TCacheSectionType>
struct FxItemCacheHandle
{
    using CacheType = FxItemCache<TItemType, TCacheSectionType>;

public:
    FxItemCacheHandle(CacheType* cache, TItemType* item, FxHash hash) : Item(item), Hash(hash), mCache(cache) {}
    FxItemCacheHandle(const FxItemCacheHandle& other) = delete;
    FxItemCacheHandle(FxItemCacheHandle&& other)
    {
        Item = other.Item;
        Hash = other.Hash;

        mCache = other.mCache;

        other.Item = nullptr;
    }

    ~FxItemCacheHandle()
    {
        if (Item == nullptr) {
            return;
        }

        mCache->ReleaseItemFromHash(Hash, Item);
    }

    TItemType* operator->() { return Item; }
    TItemType& operator*() { return *Item; }

public:
    TItemType* Item = nullptr;
    FxHash Hash;

private:
    CacheType* mCache;
};

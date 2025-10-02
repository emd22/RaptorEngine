#pragma once

#include <cstdlib>

#define FX_PAGED_ARRAY_ALLOC(type_, size_) reinterpret_cast<type_*>(std::malloc(size_))
#define FX_PAGED_ARRAY_FREE(type_, ptr_)   std::free(reinterpret_cast<void*>(ptr_))

#include <Core/FxPagedArrayImpl.hpp>

#undef FX_PAGED_ARRAY_ALLOC
#undef FX_PAGED_ARRAY_FREE

#if 0
#include "../FxPanic.hpp"
#include "../FxTypes.hpp"

#ifdef FX_PAGED_ARRAY_USE_MALLOC
#include <cstdlib>

#define FX_PAGED_ARRAY_ALLOC(type_, n_bytes_) reinterpret_cast<type_>(std::malloc(n_bytes_))
#define FX_PAGED_ARRAY_FREE(type_, ptr_)      std::free(reinterpret_cast<void*>(ptr_));

#else

#include <Core/MemPool/FxMemPool.hpp>

#define FX_PAGED_ARRAY_ALLOC(type_, n_bytes_) reinterpret_cast<type_>(std::malloc(n_bytes_))
#define FX_PAGED_ARRAY_FREE(type_, ptr_)      std::free(reinterpret_cast<void*>(ptr_));

#endif

template <typename ElementType>
class FxMPPagedArray
{
public:
    struct Page
    {
        ElementType* Data;

        /** The number of elements in use in `Data` */
        uint32 Size;

        Page* Next;
        Page* Prev;
    };

public:
    struct Iterator
    {
        // Iterator()
        //     : mCurrentPage(CurrentPage), mCurrentIndex(0)
        // {
        // }

        Iterator(Page* current_page, uint32 index) : mCurrentPage(current_page), mCurrentIndex(index) {}

        Iterator& operator++()
        {
            ++mCurrentIndex;

            if (mCurrentIndex > mCurrentPage->Size) {
                mCurrentPage = mCurrentPage->Next;
                mCurrentIndex = 0;
            }

            return *this;
        }

        Iterator operator++(int)
        {
            Iterator iterator = *this;
            ++*this;

            return iterator;
        }

        Iterator& operator--()
        {
            if (mCurrentIndex == 0) {
                mCurrentPage = mCurrentPage->Prev;
                mCurrentIndex = mCurrentPage->Size;
            }

            --mCurrentIndex;

            return *this;
        }

        ElementType& operator*() { return mCurrentPage->Data[mCurrentIndex]; }

        bool operator!=(const Iterator& other)
        {
            return mCurrentPage && (mCurrentPage != other.mCurrentPage || mCurrentIndex != other.mCurrentIndex);
        }

        Page* mCurrentPage = nullptr;
        uint32 mCurrentIndex = 0;
    };

    FxMPPagedArray() = default;

    FxMPPagedArray(uint32 page_node_capacity) { Create(page_node_capacity); }

    FxMPPagedArray& operator=(const FxMPPagedArray& other)
    {
        FirstPage = other.FirstPage;
        CurrentPage = other.CurrentPage;

        PageNodeCapacity = other.PageNodeCapacity;
        CurrentPageIndex = other.CurrentPageIndex;

        TrackedSize = other.TrackedSize;

        return *this;
    }

    FxMPPagedArray& operator=(FxMPPagedArray&& other)
    {
        FirstPage = other.FirstPage;
        CurrentPage = other.CurrentPage;

        PageNodeCapacity = other.PageNodeCapacity;
        CurrentPageIndex = other.CurrentPageIndex;

        TrackedSize = other.TrackedSize;

        other.FirstPage = nullptr;
        other.CurrentPage = nullptr;

        other.PageNodeCapacity = 0;
        other.CurrentPageIndex = 0;

        other.TrackedSize = 0;

        return *this;
    }

    Iterator begin() const { return Iterator(FirstPage, 0); }

    Iterator end() const { return Iterator(CurrentPage, CurrentPage->Size); }

    size_t GetCalculatedSize() const
    {
        size_t size = 0;
        Page* page = FirstPage;

        while (page != nullptr) {
            size += page->Size;
            page = page->Next;
        }

        assert(size == TrackedSize);

        return size;
    }

    inline size_t Size() const
    {
        // return GetCalculatedSize();
        return TrackedSize;
    }

    void Create(uint32 page_node_capacity = 32)
    {
        // We are already allocated, free the previous pages
        // TODO: replace this with some reallocs!
        if (FirstPage || CurrentPage) {
            Destroy();
        }

        PageNodeCapacity = page_node_capacity;

        FirstPage = AllocateNewPage(nullptr, nullptr);
        CurrentPage = FirstPage;
    }

    bool IsInited() const { return (FirstPage != nullptr && CurrentPage != nullptr); }

    ElementType* Insert()
    {
        // Create a new item and initialize it
        ElementType* element = &CurrentPage->Data[CurrentPage->Size];
        new (element) ElementType;

        // Move to the next index
        ++CurrentPage->Size;

        ++TrackedSize;

        // There are no free slots left in the page, allocate a new page
        if (CurrentPage->Size >= PageNodeCapacity) {
            // Since the size will be N + 1, decrement by one
            --CurrentPage->Size;

            Page* new_page = AllocateNewPage(CurrentPage, nullptr);

            CurrentPage->Next = new_page;
            CurrentPage = new_page;

            FxLogDebug("Allocating new page for FxMPPagedArray");
        }

        return element;
    }

    void Insert(const ElementType& element)
    {
        ElementType* new_element = &CurrentPage->Data[CurrentPage->Size];
        new (new_element) ElementType(element);

        ++CurrentPage->Size;

        ++TrackedSize;

        // There are no free slots left in the page, allocate a new page
        if (CurrentPage->Size >= PageNodeCapacity) {
            // Since the size will be N + 1, decrement by one
            --CurrentPage->Size;

            Page* new_page = AllocateNewPage(CurrentPage, nullptr);

            CurrentPage->Next = new_page;
            CurrentPage = new_page;

            FxLogDebug("Allocating new page for FxMPPagedArray");
        }
    }

    Page* FindPageForElement(ElementType* value)
    {
        Page* current_page = CurrentPage;

        while (current_page != nullptr) {
            const void* data_start_ptr = reinterpret_cast<char*>(current_page->Data);
            const void* data_end_ptr = data_start_ptr + (sizeof(ElementType) * PageNodeCapacity);

            const char* value_u8 = reinterpret_cast<char*>(value);

            // If the value is within the bounds of this pages' pointers, then we know it is in this page.
            if (value_u8 >= data_start_ptr && value_u8 <= data_end_ptr) {
                return current_page;
            }
        }
        return nullptr;
    }

    ElementType& GetLast() { return CurrentPage->Data[CurrentPage->Size - 1]; }

    ElementType* RemoveLast()
    {
        // If there are no pages remaining, return null
        if (CurrentPage == nullptr) {
            return nullptr;
        }

        SizeCheck(CurrentPage->Size);

        ElementType* element = &GetLast();

        // If there are no items left in the page and there is another page before this one, switch
        // to that page.
        if (CurrentPage->Size == 0 && CurrentPage->Prev) {
            Page* page_to_remove = CurrentPage;

            CurrentPage = page_to_remove->Prev;
            CurrentPage->Next = nullptr;

            std::free(page_to_remove->Data);
            std::free(page_to_remove);

            --CurrentPageIndex;

            return element;
        }

        if (CurrentPage->Size == 0 && CurrentPage->Prev == nullptr) {
            return nullptr;
        }

        --CurrentPage->Size;

        --TrackedSize;

        return element;
    }

    void Clear()
    {
        // Free all pages that are not the first page
        while (CurrentPage->Prev != nullptr) {
            Page* this_page = CurrentPage;
            CurrentPage = CurrentPage->Prev;

            std::free(this_page->Data);
            std::free(this_page);

            CurrentPage->Next = nullptr;

            --CurrentPageIndex;
        }

        FirstPage->Next = nullptr;
        FirstPage->Size = 0;

        TrackedSize = 0;
    }

    inline bool IsEmpty() const
    {
        if (FirstPage == nullptr || CurrentPage == nullptr) {
            return true;
        }

        if (CurrentPage == FirstPage && CurrentPage->Size == 0) {
            return true;
        }

        return false;
    }


    ElementType& operator[](size_t index) { return Get(index); }

    ElementType& Get(size_t index)
    {
        Page* page = nullptr;
        const uint32 dest_page_index = (index / PageNodeCapacity);
        uint32 start_index = 0;

        // If the page index is closer to the end, start from the last page
        if ((CurrentPageIndex - dest_page_index) < dest_page_index) {
            page = CurrentPage;
            start_index = CurrentPageIndex;

            while (start_index != dest_page_index) {
                page = page->Prev;
                --start_index;
            }
        }
        // Start from the first page
        else {
            page = FirstPage;

            while (start_index != dest_page_index) {
                page = page->Next;
                ++start_index;
            }
        }

        const uint32 item_index = index - (dest_page_index * PageNodeCapacity);

        return page->Data[item_index];
    }


    void Destroy()
    {
        if (FirstPage == nullptr || DoNotDestroy) {
            return;
        }

        Page* current_page = FirstPage;

        while (current_page != nullptr) {
            Page* next_page = current_page->Next;

            if (current_page->Data) {
                std::free(static_cast<void*>(current_page->Data));
            }

            std::free(static_cast<void*>(current_page));

            current_page = next_page;
        }

        TrackedSize = 0;

        FirstPage = nullptr;
        CurrentPage = nullptr;
    }

    ~FxMPPagedArray()
    {
        if (FirstPage == nullptr) {
            return;
        }

        Destroy();
    }

private:
    Page* AllocateNewPage(Page* prev, Page* next)
    {
        // Allocate and initialize the page object
        void* allocated_page = std::malloc(sizeof(Page));
        if (allocated_page == nullptr) {
            FxPanic("FxPagedArray", "Memory error allocating page");
            return nullptr; // for msvc
        }

        Page* page = static_cast<Page*>(allocated_page);

        page->Size = 0;
        page->Next = next;
        page->Prev = prev;

        // Allocate the buffer of nodes in the page
        void* allocated_nodes = std::malloc(sizeof(ElementType) * PageNodeCapacity);

        if (allocated_nodes == nullptr) {
            FxPanic("FxPagedArray", "Memory error allocating page data");
            return nullptr; // for msvc
        }

        page->Data = static_cast<ElementType*>(allocated_nodes);

        ++CurrentPageIndex;

        return page;
    }

    inline void SizeCheck(uint32 size) const
    {
        if (size > PageNodeCapacity) {
            FxPanic("FxPagedArray", "The current size of a page is greater than the allocated page node capacity!");
        }
    }

public:
    uint32 PageNodeCapacity = 0;

    Page* FirstPage = nullptr;
    Page* CurrentPage = nullptr;
    int32 CurrentPageIndex = 0;

    bool DoNotDestroy = false;

    uint32 TrackedSize = 0;
};
#endif

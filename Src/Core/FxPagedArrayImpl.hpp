#pragma once

#include <Core/FxTypes.hpp>

#include <Core/FxPanic.hpp>

#ifndef FX_PAGED_ARRAY_ALLOC
#warning "FX_PAGED_ARRAY_ALLOC is not defined!"
#define FX_PAGED_ARRAY_ALLOC(type_, size_) (0)
#endif

#ifndef FX_PAGED_ARRAY_FREE
#warning "FX_PAGED_ARRAY_FREE is not defined!"
#define FX_PAGED_ARRAY_FREE(type_, ptr_)
#endif

template <typename TElementType>
class FxPagedArray
{
public:
    struct Page
    {
        TElementType* Data = nullptr;

        /** The number of elements in use in `Data` */
        uint32 Size;

        Page* Next;
        Page* Prev;

    public:
        void Destroy()
        {
            if (!Data) {
                return;
            }

            // Call the destructor for each item in the page
            if constexpr (std::is_destructible_v<TElementType>) {
                for (uint32 i = 0; i < Size; i++) {
                    Data[i].~TElementType();
                }
            }

            // Since we have already called the destructors on each object, we don't want to call the
            // destructor on the ptr of the object. Free the page without calling the destructor
            FX_PAGED_ARRAY_FREE(void, (reinterpret_cast<void*>(Data)));
            Data = nullptr;
        }
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

        TElementType& operator*() { return mCurrentPage->Data[mCurrentIndex]; }

        bool operator!=(const Iterator& other)
        {
            return mCurrentPage && (mCurrentPage != other.mCurrentPage || mCurrentIndex != other.mCurrentIndex);
        }

        Page* mCurrentPage = nullptr;
        uint32 mCurrentIndex = 0;
    };

    FxPagedArray() = default;

    FxPagedArray(const FxPagedArray& other) = delete;
    FxPagedArray(uint32 page_node_capacity) { Create(page_node_capacity); }

    FxPagedArray(FxPagedArray&& other)
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

        // return *this;
    }

    FxPagedArray& operator=(const FxPagedArray& other)
    {
        FirstPage = other.FirstPage;
        CurrentPage = other.CurrentPage;

        PageNodeCapacity = other.PageNodeCapacity;
        CurrentPageIndex = other.CurrentPageIndex;

        TrackedSize = other.TrackedSize;

        return *this;
    }

    // FxPagedArray& operator=(FxPagedArray&& other)
    // {
    //     FirstPage = other.FirstPage;
    //     CurrentPage = other.CurrentPage;

    //     PageNodeCapacity = other.PageNodeCapacity;
    //     CurrentPageIndex = other.CurrentPageIndex;

    //     TrackedSize = other.TrackedSize;

    //     other.FirstPage = nullptr;
    //     other.CurrentPage = nullptr;

    //     other.PageNodeCapacity = 0;
    //     other.CurrentPageIndex = 0;

    //     other.TrackedSize = 0;

    //     return *this;
    // }

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

        FxAssert(size == TrackedSize);

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

    TElementType* Insert()
    {
        // Create a new item and initialize it
        TElementType* element = &CurrentPage->Data[CurrentPage->Size];

        // if constexpr (std::is_copy_constructible_v<TElementType>) {
        ::new (element) TElementType;
        // }

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

            FxLogDebug("Allocating new page for FxPagedArray");
        }

        return element;
    }

    TElementType& Insert(const TElementType& element)
    {
        TElementType* new_element = &CurrentPage->Data[CurrentPage->Size];

        if constexpr (std::is_copy_constructible_v<TElementType>) {
            ::new (new_element) TElementType(element);
        }
        else {
            memcpy(new_element, &element, sizeof(TElementType));
        }

        ++CurrentPage->Size;

        ++TrackedSize;

        // There are no free slots left in the page, allocate a new page
        if (CurrentPage->Size >= PageNodeCapacity) {
            // Since the size will be N + 1, decrement by one
            --CurrentPage->Size;

            Page* new_page = AllocateNewPage(CurrentPage, nullptr);

            CurrentPage->Next = new_page;
            CurrentPage = new_page;

            FxLogDebug("Allocating new page for FxPagedArray");
        }

        return *new_element;
    }

    TElementType& Insert(TElementType&& element)
    {
        TElementType* new_element = &CurrentPage->Data[CurrentPage->Size];

        if constexpr (std::is_move_constructible_v<TElementType>) {
            ::new (new_element) TElementType(std::move(element));
        }
        else {
            memcpy(new_element, &element, sizeof(TElementType));
        }

        ++CurrentPage->Size;

        ++TrackedSize;

        // There are no free slots left in the page, allocate a new page
        if (CurrentPage->Size >= PageNodeCapacity) {
            // Since the size will be N + 1, decrement by one
            --CurrentPage->Size;

            Page* new_page = AllocateNewPage(CurrentPage, nullptr);

            CurrentPage->Next = new_page;
            CurrentPage = new_page;

            FxLogDebug("Allocating new page for FxPagedArray");
        }

        return *new_element;
    }

    Page* FindPageForElement(TElementType* value)
    {
        Page* current_page = CurrentPage;

        while (current_page != nullptr) {
            const void* data_start_ptr = reinterpret_cast<char*>(current_page->Data);
            const void* data_end_ptr = data_start_ptr + (sizeof(TElementType) * PageNodeCapacity);

            const char* value_u8 = reinterpret_cast<char*>(value);

            // If the value is within the bounds of this pages' pointers, then we know it is in this page.
            if (value_u8 >= data_start_ptr && value_u8 <= data_end_ptr) {
                return current_page;
            }
        }
        return nullptr;
    }

    TElementType& GetLast() { return CurrentPage->Data[CurrentPage->Size - 1]; }

    TElementType* RemoveLast()
    {
        // If there are no pages remaining, return null
        if (CurrentPage == nullptr) {
            return nullptr;
        }

        SizeCheck(CurrentPage->Size);

        TElementType* element = &GetLast();

        // If there are no items left in the page and there is another page before this one, switch
        // to that page.
        if (CurrentPage->Size == 0 && CurrentPage->Prev) {
            Page* page_to_remove = CurrentPage;

            CurrentPage = page_to_remove->Prev;
            CurrentPage->Next = nullptr;

            if (page_to_remove != nullptr) {
                page_to_remove->Destroy();
            }

            FX_PAGED_ARRAY_FREE(Page, page_to_remove);

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
        Destroy();
        Create(PageNodeCapacity);
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


    TElementType& operator[](size_t index) { return Get(index); }

    TElementType& Get(size_t index)
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

            if (current_page != nullptr) {
                current_page->Destroy();
            }

            FX_PAGED_ARRAY_FREE(Page, current_page);

            current_page = next_page;
        }

        TrackedSize = 0;

        FirstPage = nullptr;
        CurrentPage = nullptr;
    }

    ~FxPagedArray()
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
        void* allocated_page = FX_PAGED_ARRAY_ALLOC(Page, sizeof(Page));
        if (allocated_page == nullptr) {
            FxPanic("FxPagedArray", "Memory error allocating page");
            return nullptr; // for msvc
        }

        Page* page = static_cast<Page*>(allocated_page);

        page->Size = 0;
        page->Next = next;
        page->Prev = prev;

        // Allocate the buffer of nodes in the page
        void* allocated_nodes = FX_PAGED_ARRAY_ALLOC(TElementType, (sizeof(TElementType) * PageNodeCapacity));

        if (allocated_nodes == nullptr) {
            FxPanic("FxPagedArray", "Memory error allocating page data");
            return nullptr; // for msvc
        }

        page->Data = static_cast<TElementType*>(allocated_nodes);

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

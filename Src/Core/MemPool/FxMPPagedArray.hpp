#pragma once

#include "../Types.hpp"
#include "../FxPanic.hpp"

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
    FxMPPagedArray() = default;

    FxMPPagedArray(uint32 page_node_capacity)
    {
        Create(page_node_capacity);
    }

    void Create(uint32 page_node_capacity = 32)
    {
        PageNodeCapacity = page_node_capacity;

        FirstPage = AllocateNewPage(nullptr, nullptr);
        CurrentPage = FirstPage;
    }

    bool IsInited() const
    {
        return (FirstPage != nullptr && CurrentPage != nullptr);
    }

    ElementType* Insert()
    {

        // There are no elements left in the page, allocate a new page.
        if (CurrentPage->Size >= PageNodeCapacity) {
            Page* new_page = AllocateNewPage(CurrentPage, nullptr);

            CurrentPage->Next = new_page;
            CurrentPage = new_page;

            Log::Info("Allocating new page", 0);
        }

        ElementType* element = &CurrentPage->Data[CurrentPage->Size++];

        new (element) ElementType;

        return element;
    }

    Page* FindPageForElement(ElementType* value)
    {
        Page* current_page = CurrentPage;

        while (current_page != nullptr) {
            const void* data_start_ptr = current_page->Data;
            const void* data_end_ptr = data_start_ptr + (sizeof(ElementType) * PageNodeCapacity);

            // If the value is within the bounds of this pages' pointers, then we know it is in this page.
            if (value >= data_start_ptr && value <= data_end_ptr) {
                return current_page;
            }
        }
        return nullptr;
    }

    ElementType& GetLast()
    {
        return CurrentPage->Data[CurrentPage->Size - 1];
    }

    ElementType* RemoveLast()
    {
        if (CurrentPage == nullptr) {
            return nullptr;
        }

        SizeCheck(CurrentPage->Size);

        ElementType* element = &GetLast();

        if (CurrentPage->Size == 0 && CurrentPage->Prev) {
            Page* current_page = CurrentPage;

            CurrentPage = current_page->Prev;

            std::free(current_page->Data);
            std::free(current_page);

            return element;
        }

        if (CurrentPage->Size == 0 && CurrentPage->Prev == nullptr) {
            return nullptr;
        }

        CurrentPage->Size--;
        return element;
    }

    bool IsEmpty() const
    {
        if (FirstPage == nullptr || CurrentPage == nullptr) {
            return true;
        }

        if (CurrentPage == FirstPage && CurrentPage->Size == 0) {
            return true;
        }

        return false;
    }

    void Destroy()
    {
        if (FirstPage == nullptr) {
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

        FirstPage = nullptr;
        CurrentPage = nullptr;
    }

    ~FxMPPagedArray()
    {
        Destroy();
    }

private:
    Page* AllocateNewPage(Page* prev, Page* next)
    {
        // Allocate and initialize the page object
        void* allocated_page = std::malloc(sizeof(Page));
        if (allocated_page == nullptr) {
            FxPanic("FxPagedArray", "Memory error allocating page", 0);
            return nullptr; // for msvc
        }

        Page* page = static_cast<Page*>(allocated_page);

        page->Size = 0;
        page->Next = next;
        page->Prev = prev;

        // Allocate the buffer of nodes in the page
        void* allocated_nodes = std::malloc(sizeof(ElementType) * PageNodeCapacity);

        if (allocated_nodes == nullptr) {
            FxPanic("FxPagedArray", "Memory error allocating page data", 0);
            return nullptr; // for msvc
        }

        page->Data = static_cast<ElementType*>(allocated_nodes);


        return page;
    }

    inline void SizeCheck(uint32 size) const
    {
        if (size > PageNodeCapacity) {
            FxPanic("FxPagedArray", "The current size of a page is greater than the allocated page node capacity!", 0);
        }
    }

public:
    uint32 PageNodeCapacity = 0;

    Page* FirstPage = nullptr;
    Page* CurrentPage = nullptr;
};

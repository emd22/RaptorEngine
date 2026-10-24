#pragma once

#include <Core/Allocator.hpp>
#include <Core/Assert.hpp>
#include <Core/Types.hpp>
#include <Engine.hpp>

#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

namespace fx {

// The size that a paged array is initialized to if any modifying functions are called before `.Create()`.
#define FX_PAGED_ARRAY_AUTO_INIT_SIZE 32

/**
 * @brief A dynamic array of objects stored in fixed-size pages. Elements never move once inserted, so pointers and
 * references to them stay valid until the element is removed or the array is cleared.
 *
 * Pages from `pFirstPage` up to `pCurrentPage` are full, `pCurrentPage` holds at least one element unless the array
 * is empty, and `pCurrentPage->pNext` is either null or a single empty spare page kept to avoid reallocating when
 * insertions and removals alternate across a page boundary.
 *
 * @tparam TElementType The type of the object that will be stored.
 * @tparam TAllocator The allocator pages are allocated from.
 */
template <typename TElementType, typename TAllocator = StdAllocator>
	requires C_IsAllocator<TAllocator>
class PagedArray
{
public:
	struct Page
	{
		TElementType* pData = nullptr;

		/** The number of elements in use in `pData` */
		uint32 Size = 0;

		Page* pNext = nullptr;
		Page* pPrev = nullptr;
	};

	template <bool TIsConst>
	struct IteratorBase
	{
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = TElementType;
		using difference_type = std::ptrdiff_t;
		using pointer = std::conditional_t<TIsConst, const TElementType*, TElementType*>;
		using reference = std::conditional_t<TIsConst, const TElementType&, TElementType&>;
		using PagePtr = std::conditional_t<TIsConst, const Page*, Page*>;

		IteratorBase() = default;
		IteratorBase(PagePtr current_page, uint32 index) : mCurrentPage(current_page), mCurrentIndex(index) {}

		operator IteratorBase<true>() const
			requires(!TIsConst)
		{
			return IteratorBase<true>(mCurrentPage, mCurrentIndex);
		}

		IteratorBase& operator++()
		{
			++mCurrentIndex;

			if (mCurrentIndex >= mCurrentPage->Size && mCurrentPage->pNext && mCurrentPage->pNext->Size > 0) {
				mCurrentPage = mCurrentPage->pNext;
				mCurrentIndex = 0;
			}

			return *this;
		}

		IteratorBase operator++(int)
		{
			IteratorBase iterator = *this;
			++*this;

			return iterator;
		}

		IteratorBase& operator--()
		{
			if (mCurrentIndex == 0) {
				mCurrentPage = mCurrentPage->pPrev;
				mCurrentIndex = mCurrentPage->Size;
			}

			--mCurrentIndex;

			return *this;
		}

		IteratorBase operator--(int)
		{
			IteratorBase iterator = *this;
			--*this;

			return iterator;
		}

		reference operator*() const { return mCurrentPage->pData[mCurrentIndex]; }
		pointer operator->() const { return &mCurrentPage->pData[mCurrentIndex]; }

		bool operator==(const IteratorBase& other) const
		{
			return mCurrentPage == other.mCurrentPage && mCurrentIndex == other.mCurrentIndex;
		}

		bool operator!=(const IteratorBase& other) const { return !(*this == other); }

		PagePtr mCurrentPage = nullptr;
		uint32 mCurrentIndex = 0;
	};

	using Iterator = IteratorBase<false>;
	using ConstIterator = IteratorBase<true>;

public:
	PagedArray() = default;

	/**
	 * @brief Creates a new paged array with a page capacity of `page_node_capacity`.
	 */
	explicit PagedArray(uint32 page_node_capacity) { Create(page_node_capacity); }

	PagedArray(const PagedArray& other)
	{
		CopyFrom(other);
	}

	PagedArray(PagedArray&& other) noexcept { TakeFrom(other); }

	PagedArray& operator=(const PagedArray& other)
	{
		if (this != &other) {
			Destroy();
			CopyFrom(other);
		}

		return *this;
	}

	PagedArray& operator=(PagedArray&& other) noexcept
	{
		if (this != &other) {
			Destroy();
			TakeFrom(other);
		}

		return *this;
	}

	Iterator begin() { return Iterator(pFirstPage, 0); }
	Iterator end() { return Iterator(pCurrentPage, pCurrentPage ? pCurrentPage->Size : 0); }

	ConstIterator begin() const { return ConstIterator(pFirstPage, 0); }
	ConstIterator end() const { return ConstIterator(pCurrentPage, pCurrentPage ? pCurrentPage->Size : 0); }

	size_t GetCalculatedSize() const
	{
		size_t size = 0;

		for (const Page* page = pFirstPage; page != nullptr; page = page->pNext) {
			size += page->Size;
		}

		Assert(size == TrackedSize);

		return size;
	}

	inline size_t Size() const { return TrackedSize; }

	void Create(uint32 page_node_capacity = FX_PAGED_ARRAY_AUTO_INIT_SIZE)
	{
		AssertMsg(page_node_capacity > 0, "PagedArray page capacity must be greater than zero");

		Destroy();

		PageNodeCapacity = page_node_capacity;

		pFirstPage = AllocatePage(nullptr);
		pCurrentPage = pFirstPage;
		CurrentPageIndex = 0;
	}

	bool IsInited() const { return (pFirstPage != nullptr && pCurrentPage != nullptr); }

	template <typename... TArgs>
	TElementType& Emplace(TArgs&&... args)
	{
		static_assert(std::is_constructible_v<TElementType, TArgs...>,
					  "PagedArray element cannot be constructed from these arguments");

		InitializeIfNeeded();

		if (pCurrentPage->Size >= PageNodeCapacity) {
			AdvancePage();
		}

		TElementType* element = &pCurrentPage->pData[pCurrentPage->Size];
		::new (element) TElementType(std::forward<TArgs>(args)...);

		++pCurrentPage->Size;
		++TrackedSize;

		return *element;
	}

	TElementType* Insert() { return &Emplace(); }

	TElementType& Insert(const TElementType& element) { return Emplace(element); }

	TElementType& Insert(TElementType&& element) { return Emplace(std::move(element)); }

	Page* FindPageForElement(const TElementType* value) const
	{
		const std::less<const TElementType*> less;

		for (Page* page = pFirstPage; page != nullptr; page = page->pNext) {
			if (!less(value, page->pData) && less(value, page->pData + page->Size)) {
				return page;
			}
		}

		return nullptr;
	}

	TElementType& GetLast()
	{
		AssertMsg(!IsEmpty(), "GetLast called on an empty PagedArray");

		return pCurrentPage->pData[pCurrentPage->Size - 1];
	}

	const TElementType& GetLast() const
	{
		AssertMsg(!IsEmpty(), "GetLast called on an empty PagedArray");

		return pCurrentPage->pData[pCurrentPage->Size - 1];
	}

	void RemoveLast()
	{
		if (IsEmpty()) {
			return;
		}

		--pCurrentPage->Size;
		pCurrentPage->pData[pCurrentPage->Size].~TElementType();

		--TrackedSize;

		if (pCurrentPage->Size == 0 && pCurrentPage->pPrev) {
			if (pCurrentPage->pNext) {
				FreePage(pCurrentPage->pNext);
				pCurrentPage->pNext = nullptr;
			}

			pCurrentPage = pCurrentPage->pPrev;
			--CurrentPageIndex;
		}
	}

	void Clear()
	{
		if (!IsInited()) {
			return;
		}

		DestroyElements();
		FreePagesAfter(pFirstPage);

		pCurrentPage = pFirstPage;
		CurrentPageIndex = 0;
	}

	inline bool IsEmpty() const { return TrackedSize == 0; }

	TElementType& operator[](size_t index) { return Get(index); }
	const TElementType& operator[](size_t index) const { return Get(index); }

	TElementType& Get(size_t index) { return const_cast<TElementType&>(std::as_const(*this).Get(index)); }

	const TElementType& Get(size_t index) const
	{
		AssertMsg(index < TrackedSize, "PagedArray access out of range");

		const size_t page_index = index / PageNodeCapacity;
		const uint32 item_index = static_cast<uint32>(index % PageNodeCapacity);

		const Page* page = nullptr;

		if (page_index * 2 >= CurrentPageIndex) {
			page = pCurrentPage;

			for (size_t i = CurrentPageIndex; i > page_index; i--) {
				page = page->pPrev;
			}
		}
		else {
			page = pFirstPage;

			for (size_t i = 0; i < page_index; i++) {
				page = page->pNext;
			}
		}

		return page->pData[item_index];
	}

	void Destroy()
	{
		if (pFirstPage != nullptr) {
			DestroyElements();
			FreePagesAfter(pFirstPage);
			FreePage(pFirstPage);
		}

		pFirstPage = nullptr;
		pCurrentPage = nullptr;
		CurrentPageIndex = 0;
		TrackedSize = 0;
	}

	void PrintDebugInfo() const
	{
		LogInfo(LC_MEMORY, "");
		LogInfo(LC_MEMORY, "=== Paged Array Debug Info ===");
		LogInfo(LC_MEMORY, "PageNodeCapacity={}", PageNodeCapacity);
		LogInfo(LC_MEMORY, "pFirstPage={:p}", reinterpret_cast<void*>(pFirstPage));
		LogInfo(LC_MEMORY, "pCurrentPage={:p}", reinterpret_cast<void*>(pCurrentPage));
		if (pCurrentPage) {
			LogInfo(LC_MEMORY, "CurrentPageSize={}", pCurrentPage->Size);
		}
		LogInfo(LC_MEMORY, "CurrentPageIndex={}", CurrentPageIndex);
		LogInfo(LC_MEMORY, "TrackedSize={}", TrackedSize);
		LogInfo(LC_MEMORY, "");
	}

	~PagedArray() { Destroy(); }

private:
	Page* AllocatePage(Page* prev)
	{
		static_assert(alignof(TElementType) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__,
					  "PagedArray does not support over-aligned element types");

		constexpr size_t header_size = (sizeof(Page) + alignof(TElementType) - 1) / alignof(TElementType) *
									   alignof(TElementType);

		void* memory = TAllocator::AllocRaw(header_size + sizeof(TElementType) * PageNodeCapacity);

		if (memory == nullptr) {
			Panic("PagedArray", "Memory error allocating page");
		}

		Page* page = ::new (memory) Page;
		page->pData = reinterpret_cast<TElementType*>(static_cast<uint8*>(memory) + header_size);
		page->pPrev = prev;

		return page;
	}

	static void FreePage(Page* page) { TAllocator::FreeRaw(page); }

	void FreePagesAfter(Page* page)
	{
		Page* next_page = page->pNext;
		page->pNext = nullptr;

		while (next_page != nullptr) {
			Page* following_page = next_page->pNext;
			FreePage(next_page);
			next_page = following_page;
		}
	}

	void DestroyElements()
	{
		for (Page* page = pFirstPage; page != nullptr; page = page->pNext) {
			if constexpr (!std::is_trivially_destructible_v<TElementType>) {
				for (uint32 i = 0; i < page->Size; i++) {
					page->pData[i].~TElementType();
				}
			}

			page->Size = 0;
		}

		TrackedSize = 0;
	}

	void AdvancePage()
	{
		if (pCurrentPage->pNext == nullptr) {
			pCurrentPage->pNext = AllocatePage(pCurrentPage);
		}

		pCurrentPage = pCurrentPage->pNext;
		++CurrentPageIndex;
	}

	void CopyFrom(const PagedArray& other)
	{
		static_assert(std::is_copy_constructible_v<TElementType>, "PagedArray element type is not copyable");

		if (!other.IsInited()) {
			PageNodeCapacity = other.PageNodeCapacity;
			return;
		}

		Create(other.PageNodeCapacity);

		for (const TElementType& element : other) {
			Emplace(element);
		}
	}

	void TakeFrom(PagedArray& other)
	{
		PageNodeCapacity = other.PageNodeCapacity;
		pFirstPage = other.pFirstPage;
		pCurrentPage = other.pCurrentPage;
		CurrentPageIndex = other.CurrentPageIndex;
		TrackedSize = other.TrackedSize;

		other.pFirstPage = nullptr;
		other.pCurrentPage = nullptr;
		other.CurrentPageIndex = 0;
		other.TrackedSize = 0;
	}

	/**
	 * @brief Checks if the paged array is initialized. If not, this function automatically initializes it.
	 */
	FX_FORCE_INLINE void InitializeIfNeeded()
	{
		if (IsInited()) {
			return;
		}

#ifdef FX_WARN_PAGED_ARRAY_AUTO_INIT
		LogWarning("PagedArray was not previously initialized, auto initializing!");
#endif

		if (PageNodeCapacity == 0) {
			PageNodeCapacity = FX_PAGED_ARRAY_AUTO_INIT_SIZE;
		}

		Create(PageNodeCapacity);
	}

public:
	/// The number of objects that are allocated in a page.
	uint32 PageNodeCapacity = 0;

	Page* pFirstPage = nullptr;
	Page* pCurrentPage = nullptr;

	/// The index of `pCurrentPage` in the page list.
	uint32 CurrentPageIndex = 0;

	/// The number of inserted objects
	uint32 TrackedSize = 0;
};

} // namespace fx

#pragma once

#include "SizedArray.hpp"
#include "StackArray.hpp"
#include "Types.hpp"

#include <string_view>


namespace fx {

template <typename T>
struct Slice
{
public:
	T* pData = nullptr;
	uint64 Size;

public:
	using Iterator = T*;
	using ConstIterator = T*;

	constexpr Iterator begin() const { return pData; }
	constexpr Iterator end() const { return pData + Size; }

	constexpr Slice(const SizedArray<T>& sized_arr) : pData(sized_arr.pData), Size(sized_arr.Size) {}
	constexpr Slice(const Slice<T>& other) { (*this) = other; }

	template <typename U>
		requires std::is_convertible_v<U*, T*>
	constexpr Slice(const SizedArray<U>& sized_arr) : pData(sized_arr.pData), Size(sized_arr.Size)
	{
	}

	/**
	 * @brief StringView constructor (from const string literal)
	 */
	template <size_t N>
		requires std::is_same_v<std::remove_cv_t<T>, char>
	constexpr Slice(const char (&arr)[N]) : pData(arr), Size(N - 1)
	{
	}

	template <typename U>
		requires std::is_convertible_v<U*, T*>
	constexpr Slice(const Slice<U>& other)
	{
		(*this) = other;
	}

	template <typename TOther>
	static constexpr Slice<T> FromArray(const SizedArray<TOther>& arr)
	{
		Slice<T> slice { .pData = static_cast<T*>(arr.pData), .Size = arr.Size };
		return slice;
	}

	static constexpr Slice<T> FromArray(const SizedArray<T>& arr)
	{
		Slice<T> slice { .pData = arr.pData, .Size = arr.Size };
		return slice;
	}

	template <uint32 TSize>
	constexpr Slice(StackArray<T, TSize>& stack_arr) : pData(stack_arr.pData), Size(stack_arr.Size)
	{
	}

	constexpr Slice(T* ptr, uint64 size) : pData(ptr), Size(size) {}
	constexpr Slice(nullptr_t np) : pData(nullptr), Size(0) {}

	constexpr Slice(Slice<T>&& other) { (*this) = std::move(other); }

	template <typename TOtherType>
	constexpr Slice(const Slice<TOtherType>& other)
	{
		pData = static_cast<T*>(other.pData);
		Size = other.Size;
	}


	Slice& operator=(T* value) = delete;

	constexpr Slice& operator=(Slice<T>&& other)
	{
		pData = other.pData;
		Size = other.Size;

		other.pData = nullptr;
		other.Size = 0;

		return *this;
	}

	constexpr Slice& operator=(const Slice<T>& other)
	{
		pData = other.pData;
		Size = other.Size;

		return *this;
	}

	template <typename U>
		requires std::is_convertible_v<U*, T*>
	constexpr Slice& operator=(const Slice<U>& other)
	{
		pData = other.pData;
		Size = other.Size;

		return *this;
	}

	constexpr bool operator==(nullptr_t np) { return pData == nullptr; }

	constexpr operator T*() const { return pData; }

	constexpr T& operator[](size_t index) const
	{
		if (index > Size) {
			Panic("Slice", "Index out of range! ({:d} > {:d})\n", index, Size);
		}

		return pData[index];
	}

	FX_FORCE_INLINE constexpr void SetNull()
	{
		pData = nullptr;
		Size = 0;
	}

	FX_FORCE_INLINE constexpr uint64 GetSizeInBytes() const { return sizeof(T) * Size; }
};

using StringView = Slice<const char>;


FX_FORCE_INLINE constexpr StringView MakeStringView(const char* ptr, uint64 size)
{
	return Slice<const char>(ptr, size);
}

FX_FORCE_INLINE constexpr StringView MakeStringView(const char* ptr)
{
	// Use string_view to get the length (constexpr) as the header is a lot less thicc than string.
	return Slice<const char>(ptr, std::string_view(ptr).length());
}

/** Creates a new Slice object */
template <typename T>
constexpr Slice<T> MakeSlice(T* ptr, uint64 size)
{
	return Slice<T>(ptr, size);
}

} // namespace fx


template <>
struct std::formatter<fx::Slice<fx::uint8>>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	constexpr auto format(const fx::Slice<fx::uint8> slice, std::format_context& ctx) const
	{
		std::string str = "";

		for (fx::uint64 i = 0; i < slice.Size; i++) {
			const fx::uint8 value = slice.pData[i];
			if (i == slice.Size - 1) {
				str += std::format("{:x}", static_cast<unsigned int>(value));
			}
			else {
				str += std::format("{:x}, ", static_cast<unsigned int>(value));
			}
		}

		return std::format_to(ctx.out(), "[ {} ]", str);
	}
};

template <>
struct std::formatter<fx::StringView>
{
	auto parse(format_parse_context& ctx) { return ctx.begin(); }

	constexpr auto format(const fx::StringView sv, std::format_context& ctx) const
	{
		return std::format_to(ctx.out(), "{:.{}}", sv.pData, sv.Size);
	}
};

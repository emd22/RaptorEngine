/*
 * File:        Serialize.hpp
 * Author:      emd22
 * Created:     24/10/2026
 * Description: Routines to serialize structures and data to disk from C++
 */

#pragma once

#include <Core/ByteBuffer.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <concepts>
#include <string>
#include <type_traits>
#include <utility>


namespace fx {


class ConfigEntry;
class ConfigFile;


struct SerializedFieldLayout
{
	enum class eValueType : uint8
	{
		Raw,
		Object,
		Bool,
		Int,
		UInt,
		Float,
		String,
	};

	eValueType ValueType = eValueType::Raw;
	uint32 ElementSize = 0;
	uint32 ElementCount = 0;
	bool bIsArray = false;

	uint32 MinElementCount = 0;
	float64 FillValue = 0.0;
};


struct SerializedField
{
	Name EntryName;
	uint32 TapeOffset = 0;
	uint32 Size = 0;
	SerializedFieldLayout Layout;
};


/**
 * @brief Returns true if the value is serializable
 */
class SerializerIO;


template <typename T>
struct Serializable;


template <typename T>
concept C_IsTapeValue = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;

template <typename T>
concept C_IsSerializableObject = requires(SerializerIO& io, T& value) {
	{ Serializable<T>::Visit(io, value) } -> std::same_as<bool>;
};

template <typename T>
concept C_IsSerializedString = std::is_same_v<T, String> || std::is_same_v<T, std::string>;

template <typename T>
concept C_IsSerializableField = C_IsTapeValue<T> || C_IsSerializableObject<T> || C_IsSerializedString<T>;


template <typename T>
struct SerializedArrayType;

template <typename T>
struct SerializedStringType;

template <typename T>
concept C_IsSerializedArrayType = requires {
	typename SerializedArrayType<T>::ElementType;
	{ SerializedArrayType<T>::scCount } -> std::convertible_to<uint32>;
};

template <typename T>
concept C_IsPartialSerializedArrayType = C_IsSerializedArrayType<T> && requires {
	{ SerializedArrayType<T>::scMinCount } -> std::convertible_to<uint32>;
	{ SerializedArrayType<T>::scFillValue } -> std::convertible_to<float64>;
};


template <typename T>
constexpr SerializedFieldLayout GetSerializedFieldLayout()
{
	using eValueType = SerializedFieldLayout::eValueType;

	if constexpr (C_IsSerializedArrayType<T>) {
		using TElement = typename SerializedArrayType<T>::ElementType;
		constexpr uint32 element_count = SerializedArrayType<T>::scCount;
		constexpr SerializedFieldLayout element = GetSerializedFieldLayout<TElement>();

		static_assert(element.ValueType != eValueType::Raw && !element.bIsArray,
					  "Serialized array types must contain scalar elements");
		static_assert(sizeof(TElement) * element_count <= sizeof(T), "Serialized array type is too small");

		SerializedFieldLayout layout { element.ValueType, element.ElementSize, element_count, true };

		if constexpr (C_IsPartialSerializedArrayType<T>) {
			static_assert(SerializedArrayType<T>::scMinCount <= element_count,
						  "Serialized array type minimum count is larger than its count");

			layout.MinElementCount = SerializedArrayType<T>::scMinCount;
			layout.FillValue = static_cast<float64>(SerializedArrayType<T>::scFillValue);
		}

		return layout;
	}
	else if constexpr (std::is_enum_v<T>) {
		return GetSerializedFieldLayout<std::underlying_type_t<T>>();
	}
	else if constexpr (std::is_same_v<T, bool>) {
		return { eValueType::Bool, sizeof(bool), 1, false };
	}
	else if constexpr (std::is_integral_v<T>) {
		if constexpr (std::is_signed_v<T>) {
			return { eValueType::Int, sizeof(T), 1, false };
		}
		else {
			return { eValueType::UInt, sizeof(T), 1, false };
		}
	}
	else if constexpr (std::is_floating_point_v<T>) {
		return { eValueType::Float, sizeof(T), 1, false };
	}
	else if constexpr (std::is_array_v<T> && std::rank_v<T> == 1) {
		constexpr SerializedFieldLayout element = GetSerializedFieldLayout<std::remove_extent_t<T>>();

		if constexpr (element.ValueType != eValueType::Raw && !element.bIsArray) {
			return { element.ValueType, element.ElementSize, static_cast<uint32>(std::extent_v<T>), true };
		}
		else {
			return {};
		}
	}
	else {
		return {};
	}
}


class SerializerIO
{
	/// The size to grow the tape by.
	static constexpr uint32 scTapeBlockSize = 512;

public:
	enum class eMode : uint8
	{
		Write,
		Read,
		ReadConfig,
	};

public:
	SerializerIO() : SerializerIO(scTapeBlockSize) {}
	explicit SerializerIO(uint32 reserve_size);

	template <typename T>
		requires C_IsSerializableField<T>
	bool Field(const char* name, const T& value)
	{
		AssertMsg(mMode == eMode::Write, "Cannot read into a const field");

		if constexpr (C_IsSerializableObject<T>) {
			return WriteObjectField(name, value);
		}
		else if constexpr (C_IsSerializedString<T>) {
			return WriteString(name, value);
		}
		else {
			WriteField(name, &value, sizeof(T), GetSerializedFieldLayout<T>());
			return true;
		}
	}

	template <typename T>
		requires C_IsSerializableField<T>
	bool Field(const char* name, T& value)
	{
		if constexpr (C_IsSerializableObject<T>) {
			if (mMode == eMode::Write) {
				return WriteObjectField(name, value);
			}

			if (mMode == eMode::ReadConfig) {
				const ConfigEntry* object_entry = FindConfigObject(name);

				if (object_entry == nullptr) {
					return false;
				}

				const ConfigEntry* parent_entry = std::exchange(mpConfigSource, object_entry);
				const bool all_fields_found = Serializable<T>::Visit(*this, value);
				mpConfigSource = parent_entry;

				return all_fields_found;
			}

			return Serializable<T>::Visit(*this, value);
		}
		else if constexpr (C_IsSerializedString<T>) {
			if (mMode == eMode::Write) {
				return WriteString(name, value);
			}

			return ReadString(name, value);
		}
		else {
			return ReadOrWriteValueField(name, value);
		}
	}

	void SetMode(eMode mode)
	{
		mMode = mode;
		mTape.Rewind();
	}

	eMode GetMode() const { return mMode; }

	uint32 GetFieldCount() const { return static_cast<uint32>(mFields.Size()); }
	const ByteBuffer& GetTape() const { return mTape; }

	void Clear();

	void Save(ByteBuffer& out) const;
	void Load(const void* data, uint32 size);

	void ExportTo(ConfigEntry& entry) const;
	void ImportFrom(const ConfigEntry& entry);
	void ImportFrom(const ConfigFile& file);

	void PrintDebug() const;

private:
	template <typename T>
	bool WriteObjectField(const char* name, const T& value)
	{
		SerializedField* record = BeginObjectField(name);
		const uint32 first_child_index = GetFieldCount();

		const bool all_fields_found = Serializable<T>::Visit(*this, value);

		EndObjectField(record, first_child_index);
		return all_fields_found;
	}

	template <typename T>
	bool WriteString(const char* name, const T& value)
	{
		if constexpr (std::is_same_v<T, String>) {
			WriteStringField(name, value.CStr(), value.GetLength());
		}
		else {
			Assert(value.size() <= UINT32_MAX);
			WriteStringField(name, value.data(), static_cast<uint32>(value.size()));
		}

		return true;
	}

	template <typename T>
	bool ReadString(const char* name, T& value)
	{
		const char* data = nullptr;
		uint32 length = 0;
		bool found = false;

		if (mMode == eMode::ReadConfig) {
			found = ReadConfigStringField(name, data, length);
		}
		else {
			found = ReadStringField(data, length);
		}

		if (!found) {
			return false;
		}

		if constexpr (std::is_same_v<T, String>) {
			value = String(data, length);
		}
		else {
			value.assign(data, length);
		}

		return true;
	}

	template <typename T>
	bool ReadOrWriteValueField(const char* name, T& value)
	{
		if (mMode == eMode::Write) {
			WriteField(name, &value, sizeof(T), GetSerializedFieldLayout<std::remove_const_t<T>>());
			return true;
		}

		if (mMode == eMode::ReadConfig) {
			return ReadConfigField(name, &value, GetSerializedFieldLayout<T>());
		}

		return ReadField(&value, sizeof(T));
	}

	SerializedField* BeginObjectField(const char* name);
	void EndObjectField(SerializedField* record, uint32 first_child_index);

	const ConfigEntry* FindConfigMember(const char* name) const;
	const ConfigEntry* FindConfigObject(const char* name) const;

	void WriteField(const char* name, const void* value, uint32 size, const SerializedFieldLayout& layout);
	bool ReadField(void* value, uint32 size);
	bool ReadConfigField(const char* name, void* value, const SerializedFieldLayout& layout) const;

	void WriteStringField(const char* name, const char* data, uint32 length);
	bool ReadStringField(const char*& out_data, uint32& out_length);
	bool ReadConfigStringField(const char* name, const char*& out_data, uint32& out_length) const;

	void ReserveTape(uint32 size);

private:
	/// An allocated block of data for all values to be stored. Note that the names and indices are stored separately.
	ByteBuffer mTape;

	PagedArray<SerializedField> mFields;

	const ConfigEntry* mpConfigSource = nullptr;
	const ConfigFile* mpConfigFile = nullptr;

	eMode mMode = eMode::Write;
};


#define RS_OBJECT_BEGIN(type_)                                                                                         \
	template <>                                                                                                        \
	struct fx::Serializable<type_>                                                                                     \
	{                                                                                                                  \
		static fx::SerializerIO Save(const type_& v)                                                                   \
		{                                                                                                              \
			fx::SerializerIO io;                                                                                       \
			Visit(io, v);                                                                                              \
			return io;                                                                                                 \
		}                                                                                                              \
                                                                                                                       \
		static bool Load(fx::SerializerIO& io, type_& v)                                                               \
		{                                                                                                              \
			io.SetMode(fx::SerializerIO::eMode::Read);                                                                 \
			return Visit(io, v);                                                                                       \
		}                                                                                                              \
                                                                                                                       \
		static void SaveToConfig(const type_& v, fx::ConfigEntry& entry) { Save(v).ExportTo(entry); }                  \
                                                                                                                       \
		static bool LoadFromConfig(const fx::ConfigEntry& entry, type_& v)                                             \
		{                                                                                                              \
			fx::SerializerIO io(0);                                                                                    \
			io.ImportFrom(entry);                                                                                      \
			return Visit(io, v);                                                                                       \
		}                                                                                                              \
                                                                                                                       \
		static bool LoadFromConfig(const fx::ConfigFile& file, type_& v)                                               \
		{                                                                                                              \
			fx::SerializerIO io(0);                                                                                    \
			io.ImportFrom(file);                                                                                       \
			return Visit(io, v);                                                                                       \
		}                                                                                                              \
                                                                                                                       \
		template <typename TValue>                                                                                     \
		static bool Visit(fx::SerializerIO& io, TValue& v)                                                             \
		{                                                                                                              \
			bool all_fields_found = true;

/// Creates an array type (e.g. `vec = [1.0, 2.0, 3.0]`) for a struct
#define RS_ARRAY_TYPE(type_, element_type_, count_)                                                                    \
	template <>                                                                                                        \
	struct fx::SerializedArrayType<type_>                                                                              \
	{                                                                                                                  \
		using ElementType = element_type_;                                                                             \
		static constexpr uint32 scCount = count_;                                                                      \
	};


/// Creates an array type, any missing components are set to `fill_value_`.
#define RS_ARRAY_TYPE_PARTIAL(type_, element_type_, count_, min_count_, fill_value_)                                   \
	template <>                                                                                                        \
	struct fx::SerializedArrayType<type_>                                                                              \
	{                                                                                                                  \
		using ElementType = element_type_;                                                                             \
		static constexpr uint32 scCount = count_;                                                                      \
		static constexpr uint32 scMinCount = min_count_;                                                               \
		static constexpr fx::float64 scFillValue = fill_value_;                                                        \
	};


/// Define a specific member of a struct. Takes the name from the name of the member.
#define RS_FIELD(field_name_) all_fields_found &= io.Field(#field_name_, v.field_name_);

/// Define a specific member of a struct with a custom name.
#define RS_FIELD_NAMED(field_name_, label_) all_fields_found &= io.Field(label_, v.field_name_);

#define RS_OBJECT_END()                                                                                                \
	return all_fields_found;                                                                                           \
	}                                                                                                                  \
	}                                                                                                                  \
	;


} // namespace fx

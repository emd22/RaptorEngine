#pragma once

#include <Core/ByteBuffer.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <type_traits>


namespace fx {


struct SerializedField
{
	Name EntryName;
	uint32 TapeOffset = 0;
	uint32 Size = 0;
};


template <typename T>
concept C_IsTapeValue = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;


class SerializerIO
{
	/// The size to grow the tape by.
	static constexpr uint32 scTapeBlockSize = 512;

public:
	enum class eMode : uint8
	{
		Write,
		Read,
	};

public:
	SerializerIO() : SerializerIO(scTapeBlockSize) {}
	explicit SerializerIO(uint32 reserve_size);

	template <typename T>
		requires C_IsTapeValue<T>
	bool Field(const char* name, const T& value)
	{
		AssertMsg(mMode == eMode::Write, "Cannot read into a const field");
		WriteField(name, &value, sizeof(T));
		return true;
	}

	template <typename T>
		requires C_IsTapeValue<T>
	bool Field(const char* name, T& value)
	{
		if (mMode == eMode::Write) {
			WriteField(name, &value, sizeof(T));
			return true;
		}

		return ReadField(&value, sizeof(T));
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

	void PrintDebug() const;

private:
	void WriteField(const char* name, const void* value, uint32 size);
	bool ReadField(void* value, uint32 size);

	void ReserveTape(uint32 size);

private:
	/// An allocated block of data for all values to be stored. Note that the names and indices are stored separately.
	ByteBuffer mTape;

	PagedArray<SerializedField> mFields;

	eMode mMode = eMode::Write;
};


template <typename T>
struct Serializable;


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
		template <typename TValue>                                                                                     \
		static bool Visit(fx::SerializerIO& io, TValue& v)                                                             \
		{                                                                                                              \
			bool all_fields_found = true;

#define RS_FIELD(field_name_)				all_fields_found &= io.Field(#field_name_, v.field_name_);
#define RS_FIELD_NAMED(field_name_, label_) all_fields_found &= io.Field(label_, v.field_name_);

#define RS_OBJECT_END()                                                                                                \
	return all_fields_found;                                                                                           \
	}                                                                                                                  \
	}                                                                                                                  \
	;


struct TestStruct
{
	float32 TestFloat = 10.0f;
	uint32 TestInt = 20u;
};


} // namespace fx


RS_OBJECT_BEGIN(fx::TestStruct)
RS_FIELD(TestFloat)
RS_FIELD(TestInt)
RS_OBJECT_END()

#pragma once

#include <stdio.h>
#include <stdlib.h>

#include <Core/Allocator.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Slice.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Engine.hpp>
#include <Math/MathUtil.hpp>
#include <filesystem>

namespace fx {

class File
{
private:
	static constexpr const char* spcErrCannotWriteUnopened = "Cannot write to file as it has not been opened!";
	static constexpr const char* spcErrCannotReadUnopened = "Cannot read from file as it has not been opened!";

public:
	enum class eDataType
	{
		Text = 0,
		Binary,
	};

	enum class eModType
	{
		Read = 0,
		Write,
	};

public:
	File() {}
	File(const String& path, eModType mt, eDataType dt);

	static std::filesystem::file_time_type GetLastModifiedTime(const std::string& path);

	void Open(const String& path, eModType mt, eDataType dt);
	uint64 GetFileSize();

	uint64 Read2(void* buffer, uint64 element_size);

	template <typename TType>
	uint64 Read2(const Slice<TType>& out_buffer)
	{
		if (!pFileHandle) {
			return 0;
		}

		return fread(out_buffer.pData, sizeof(TType), out_buffer.Size, pFileHandle);
	}

	/**
	 * @brief An updated, safer read function. Reads an entire file into a returned array.
	 */
	template <typename TDataType>
	SizedArray<TDataType> Read2()
	{
		if (!pFileHandle) {
			return SizedArray<TDataType>::CreateEmpty();
		}

		const uint64 buffer_size = MathUtil::AlignValue<sizeof(TDataType)>(GetFileSize());

		SizedArray<TDataType> buffer;
		buffer.InitCapacity(buffer_size / sizeof(TDataType));

		const uint64 items_read = Read2(buffer.pData, sizeof(TDataType));
		buffer.Size = items_read;

		return buffer;
	}


	size_t WriteRaw(const void* data, uint64 size);

	void Write(const char* str);
	void Write(const std::string& str) { Write(str.c_str()); }
	void Write(const String& str) { Write(str.CStr()); }

	template <typename... TTypes>
	void WriteMulti(TTypes... values)
	{
		(Write(values), ...);
	}

	template <typename TType>
		requires std::is_integral_v<TType>
	void Write(TType value)
	{
		if (!pFileHandle) {
			LogWarning(LC_CORE, spcErrCannotWriteUnopened);
			return;
		}

		if (mDataType == eDataType::Binary) {
			fwrite(&value, sizeof(value), 1, pFileHandle);
		}
		else {
			std::string str_value = std::to_string(value);
			fputs(str_value.c_str(), pFileHandle);
		}
	}

	template <typename TType>
	size_t WriteSlice(const Slice<TType> data)
	{
		return WriteRaw(reinterpret_cast<const void*>(data.pData), data.GetSizeInBytes()) / sizeof(TType);
	}

	template <typename TType>
	void Write(TType* data, uint64 size)
	{
		WriteRaw(reinterpret_cast<const void*>(data), size);
	}

	bool IsFileOpen() const { return (pFileHandle != nullptr); }

	void Write(char ch);
	void Flush();

	void SeekTo(uint64 index);
	void SeekBy(uint32 by);
	void SeekToEnd();
	uint64 GetPosition() const;

	void Close();

	~File() { Close(); }

public:
	FILE* pFileHandle = nullptr;

private:
	eModType mModType = eModType::Read;
	eDataType mDataType = eDataType::Text;

	uint64 mFileSize = 0;
	bool mbSizeOutOfDate : 1 = true;
};

} // namespace fx

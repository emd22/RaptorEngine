#include "RxModelFormat.hpp"

#include <Core/File.hpp>
#include <Core/Log.hpp>
#include <cstdio>
#include <cstring>

namespace fx {

namespace loader {

namespace rx {

uint32 GetFormatSize(uint8 format)
{
	switch (format) {
	case VF_F32x1:
	case VF_F16x2:
	case VF_UNorm8x4:
	case VF_SNorm8x4:
	case VF_UInt8x4:
	case VF_UNorm16x2:
		return 4;
	case VF_F32x2:
	case VF_F16x4:
	case VF_UNorm16x4:
	case VF_SNorm16x4:
	case VF_UInt16x4:
		return 8;
	case VF_F32x3:
		return 12;
	case VF_F32x4:
		return 16;
	default:
		return 0;
	}
}

uint32 GetFormatComponents(uint8 format)
{
	switch (format) {
	case VF_F32x1:
		return 1;
	case VF_F32x2:
	case VF_F16x2:
	case VF_UNorm16x2:
		return 2;
	case VF_F32x3:
		return 3;
	case VF_F32x4:
	case VF_F16x4:
	case VF_UNorm8x4:
	case VF_SNorm8x4:
	case VF_UInt8x4:
	case VF_UNorm16x4:
	case VF_SNorm16x4:
	case VF_UInt16x4:
		return 4;
	default:
		return 0;
	}
}

} // namespace rx

bool RxBinaryFile::Open(const String& path, const char* magic)
{
	Close();

	mPath = path;


	File filep(path.CStr(), File::eModType::Read, File::eDataType::Binary);

	if (!filep.IsFileOpen()) {
		LogError(LC_ASSET, "Could not open '{}'", path);
		return false;
	}

	const uint64 disk_size = filep.GetFileSize();

	if (disk_size < static_cast<uint64>(sizeof(rx::FileHeader))) {
		LogError(LC_ASSET, "'{}' is too small to be a {} file", path, magic);
		filep.Close();
		return false;
	}

	mData = filep.Read2<uint8>();

	filep.Close();

	if (mData.Size != static_cast<size_t>(disk_size)) {
		LogError(LC_ASSET, "Could not read '{}'", path);
		Close();
		return false;
	}

	const rx::FileHeader& header = GetHeader();

	if (memcmp(header.Magic, magic, 4) != 0) {
		LogError(LC_ASSET, "'{}' is not a {} file", path, magic);
		Close();
		return false;
	}

	if (header.VersionMajor != rx::scVersionMajor) {
		LogError(LC_ASSET, "'{}' is version {}.{}, only major version {} is supported", path, header.VersionMajor,
				 header.VersionMinor, rx::scVersionMajor);
		Close();
		return false;
	}

	if (header.FileSize > static_cast<uint64>(disk_size)) {
		LogError(LC_ASSET, "'{}' is truncated ({} of {} bytes)", path, disk_size, header.FileSize);
		Close();
		return false;
	}

	const uint64 table_end = sizeof(rx::FileHeader) + static_cast<uint64>(header.ChunkCount) * sizeof(rx::ChunkEntry);

	if (table_end > header.FileSize) {
		LogError(LC_ASSET, "'{}' has a chunk table that runs past the end of the file", path);
		Close();
		return false;
	}

	const rx::ChunkEntry* chunks = reinterpret_cast<const rx::ChunkEntry*>(mData.pData + sizeof(rx::FileHeader));

	for (uint32 i = 0; i < header.ChunkCount; i++) {
		const rx::ChunkEntry& chunk = chunks[i];

		const bool in_bounds = chunk.Offset >= table_end && chunk.Offset <= header.FileSize &&
							   chunk.Size <= header.FileSize - chunk.Offset;

		if (!in_bounds || (chunk.Offset % 16) != 0) {
			LogError(LC_ASSET, "'{}' chunk '{:.4}' has an invalid offset or runs past the end of the file", path,
					 std::string_view(chunk.Id, 4));
			Close();
			return false;
		}

		if (chunk.Size != static_cast<uint64>(chunk.Count) * chunk.Stride) {
			LogError(LC_ASSET, "'{}' chunk '{:.4}' has a size that is not count x stride", path,
					 std::string_view(chunk.Id, 4));
			Close();
			return false;
		}
	}

	if (const rx::ChunkEntry* strings = FindChunk("STRS")) {
		mpStrings = reinterpret_cast<const char*>(GetChunkData(*strings));
		mStringsSize = strings->Size;

		if (mStringsSize > 0 && (mpStrings[0] != '\0' || mpStrings[mStringsSize - 1] != '\0')) {
			LogError(LC_ASSET, "'{}' has a malformed string table", path);
			Close();
			return false;
		}
	}

	return true;
}

void RxBinaryFile::Close()
{
	mData.Free();

	mpStrings = nullptr;
	mStringsSize = 0;
}

const rx::ChunkEntry* RxBinaryFile::FindChunk(const char* id) const
{
	if (!IsOpen()) {
		return nullptr;
	}

	const rx::ChunkEntry* chunks = reinterpret_cast<const rx::ChunkEntry*>(mData.pData + sizeof(rx::FileHeader));

	for (uint32 i = 0; i < GetHeader().ChunkCount; i++) {
		if (memcmp(chunks[i].Id, id, 4) == 0) {
			return &chunks[i];
		}
	}

	return nullptr;
}

const void* RxBinaryFile::GetChunkRaw(const char* id, uint32 stride, uint32& out_count, bool required) const
{
	out_count = 0;

	const rx::ChunkEntry* chunk = FindChunk(id);

	if (chunk == nullptr) {
		if (required) {
			LogError(LC_ASSET, "'{}' is missing its '{}' chunk", mPath, id);
		}
		return nullptr;
	}

	if (chunk->Stride != stride) {
		LogError(LC_ASSET, "'{}' chunk '{}' has stride {}, expected {}", mPath, id, chunk->Stride, stride);
		return nullptr;
	}

	out_count = chunk->Count;

	return GetChunkData(*chunk);
}

const char* RxBinaryFile::GetString(uint32 offset) const
{
	if (mpStrings == nullptr || offset >= mStringsSize) {
		return "";
	}

	return mpStrings + offset;
}

} // namespace loader

} // namespace fx

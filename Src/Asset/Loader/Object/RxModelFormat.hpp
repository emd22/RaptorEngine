#pragma once

#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <string>

namespace fx {

namespace loader {

namespace rx {

constexpr uint16 scVersionMajor = 1;
constexpr uint32 scNoJoint = 0xFFFFFFFFu;
constexpr uint32 scMaxStreams = 16;

static_assert(true);
#pragma pack(push, 1)

struct FileHeader
{
	char Magic[4];
	uint16 VersionMajor;
	uint16 VersionMinor;
	uint32 ChunkCount;
	uint32 Flags;
	uint64 FileSize;
	uint64 ContentHash;
};

struct ChunkEntry
{
	char Id[4];
	uint16 ChunkVersion;
	uint16 Flags;
	uint64 Offset;
	uint64 Size;
	uint32 Count;
	uint32 Stride;
};

struct MeshHeader
{
	uint32 VertexCount;
	uint32 IndexCount;
	uint32 IndexSize;
	uint32 SubmeshCount;
	uint32 LodCount;
	uint32 AttributeCount;
	uint32 StreamCount;
	uint32 Flags;
	uint64 SkeletonSignature;
	float32 AabbMin[3];
	float32 AabbMax[3];
	float32 Sphere[4];
};

struct VertexAttribute
{
	uint8 Semantic;
	uint8 SemanticIndex;
	uint8 Format;
	uint8 Stream;
	uint16 Offset;
	uint16 Reserved0;
	uint32 Name;
	uint32 Reserved1;
};

struct Submesh
{
	uint32 Name;
	uint32 Material;
	uint32 FirstIndex;
	uint32 IndexCount;
	uint32 BaseVertex;
	uint32 VertexCount;
	uint32 Lod;
	uint32 Flags;
	float32 AabbMin[3];
	float32 AabbMax[3];
	uint32 ParentJoint;
	uint32 Reserved;
};

struct SkeletonHeader
{
	uint32 JointCount;
	uint32 Flags;
	uint64 Signature;
};

struct Joint
{
	uint32 Name;
	int32 Parent;
	uint32 Flags;
	uint32 Reserved;
};

struct Transform
{
	float32 Rotation[4];
	float32 Translation[3];
	float32 Pad0;
	float32 Scale[3];
	float32 Pad1;
};

struct Matrix4
{
	float32 M[4][4];
};

struct AnimHeader
{
	float32 SampleRate;
	uint32 FrameCount;
	float32 Duration;
	uint32 TrackCount;
	uint64 SkeletonSignature;
	uint32 Flags;
	uint32 Reserved0;
	float32 AabbMin[3];
	float32 AabbMax[3];
	uint32 Reserved1[2];
};

struct Track
{
	uint32 TargetIndex;
	uint32 TargetName;
	uint32 KeyCount;
	uint32 DataOffset;
	uint8 Channel;
	uint8 Format;
	uint16 Flags;
	uint32 Reserved;
};

#pragma pack(pop)

static_assert(sizeof(FileHeader) == 32);
static_assert(sizeof(ChunkEntry) == 32);
static_assert(sizeof(MeshHeader) == 80);
static_assert(sizeof(VertexAttribute) == 16);
static_assert(sizeof(Submesh) == 64);
static_assert(sizeof(SkeletonHeader) == 16);
static_assert(sizeof(Joint) == 16);
static_assert(sizeof(Transform) == 48);
static_assert(sizeof(Matrix4) == 64);
static_assert(sizeof(AnimHeader) == 64);
static_assert(sizeof(Track) == 24);

enum eVertexSemantic : uint8
{
	VS_Position = 1,
	VS_Normal = 2,
	VS_Tangent = 3,
	VS_TexCoord = 4,
	VS_Color = 5,
	VS_Joints = 6,
	VS_Weights = 7,
	VS_Custom = 255,
};

enum eVertexFormat : uint8
{
	VF_F32x1 = 1,
	VF_F32x2 = 2,
	VF_F32x3 = 3,
	VF_F32x4 = 4,
	VF_F16x2 = 5,
	VF_F16x4 = 6,
	VF_UNorm8x4 = 7,
	VF_SNorm8x4 = 8,
	VF_UInt8x4 = 9,
	VF_UNorm16x2 = 10,
	VF_UNorm16x4 = 11,
	VF_SNorm16x4 = 12,
	VF_UInt16x4 = 13,
};

enum eTrackChannel : uint8
{
	TC_Translation = 0,
	TC_Rotation = 1,
	TC_Scale = 2,
	TC_Curve = 3,
};

constexpr uint8 scTrackFormatF32 = 1;

constexpr uint32 scMeshHasSkin = 1u << 0;
constexpr uint32 scSubmeshSkinned = 1u << 0;
constexpr uint32 scJointDeform = 1u << 0;
constexpr uint32 scAnimAdditive = 1u << 0;
constexpr uint16 scTrackConstant = 1u << 0;

uint32 GetFormatSize(uint8 format);
uint32 GetFormatComponents(uint8 format);

} // namespace rx

class RxBinaryFile
{
public:
	RxBinaryFile() = default;
	RxBinaryFile(const RxBinaryFile&) = delete;
	RxBinaryFile& operator=(const RxBinaryFile&) = delete;

	bool Open(const String& path, const char* magic);
	void Close();

	bool IsOpen() const { return mData.pData != nullptr; }

	const String& GetPath() const { return mPath; }
	const rx::FileHeader& GetHeader() const { return *reinterpret_cast<const rx::FileHeader*>(mData.pData); }

	const rx::ChunkEntry* FindChunk(const char* id) const;
	const uint8* GetChunkData(const rx::ChunkEntry& chunk) const { return mData.pData + chunk.Offset; }

	template <typename TElement>
	const TElement* GetChunk(const char* id, uint32& out_count, bool required = true) const
	{
		return static_cast<const TElement*>(GetChunkRaw(id, sizeof(TElement), out_count, required));
	}

	template <typename TElement>
	const TElement* GetSingle(const char* id) const
	{
		uint32 count = 0;
		const TElement* element = GetChunk<TElement>(id, count);

		return (count >= 1) ? element : nullptr;
	}

	const char* GetString(uint32 offset) const;

private:
	const void* GetChunkRaw(const char* id, uint32 stride, uint32& out_count, bool required) const;

private:
	SizedArray<uint8> mData;
	String mPath;

	const char* mpStrings = nullptr;
	uint64 mStringsSize = 0;
};

} // namespace loader

} // namespace fx

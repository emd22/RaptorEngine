#include "CompiledMap.hpp"

#include <Core/File.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fx {

namespace {

constexpr uint32 scFlagSealed = 1 << 0;

constexpr uint32 scMaxStringLength = 4096;
constexpr uint32 scMaxElementCount = 1 << 28;
constexpr uint32 scMaxLightmapSize = 8192;

bool IsNameBefore(const String& a, const String& b) { return std::strcmp(a.CStr(), b.CStr()) < 0; }

struct Reader
{
public:
	Reader() = delete;
	Reader(File* file_to_read) : mpFile(file_to_read) {}

	template <typename TType>
	void Read(TType* values, size_t count)
	{
		Assert(mpFile != nullptr);

		if (bFailed || count == 0) {
			return;
		}

		const uint64 bytes_read = mpFile->Read2(Slice(values, count));

		if (bytes_read != count) {
			bFailed = true;
		}
	}

	template <typename TType>
	TType Read()
	{
		TType value {};
		Read(&value, 1);
		return value;
	}

	uint32 ReadCount()
	{
		const uint32 count = Read<uint32>();

		if (count > scMaxElementCount) {
			bFailed = true;
			return 0;
		}

		if (bFailed) {
			return 0;
		}

		return count;
	}

	String ReadString()
	{
		Assert(mpFile != nullptr);

		const uint32 length = Read<uint32>();

		if (bFailed || length > scMaxStringLength) {
			bFailed = true;
			return {};
		}

		char buffer[scMaxStringLength];
		Read(buffer, length);

		if (bFailed) {
			return String {};
		}

		return String(buffer, length);
	}

public:
	bool bFailed = false;

private:
	File* mpFile = nullptr;
};

struct Writer
{
public:
	Writer() = delete;
	Writer(File* file_to_write) : mpFile(file_to_write) {}

	template <typename TType>
	void Write(const TType* values, size_t count)
	{
		Assert(mpFile != nullptr);

		if (bFailed || count == 0) {
			return;
		}

		const size_t bytes_written = mpFile->WriteSlice(Slice(values, count));

		if (bytes_written != count) {
			bFailed = true;
		}
	}

	template <typename TType>
	void Write(const TType& value)
	{
		Write(&value, 1);
	}

	template <typename TType>
		requires std::same_as<TType, Vec3f>
	void Write(const Vec3f& value)
	{
		Assert(mpFile != nullptr);

		if (bFailed) {
			return;
		}

		const size_t bytes_written = mpFile->WriteSlice(Slice(value.mData, 3));

		if (bytes_written != 3) {
			bFailed = true;
		}
	}

	void WriteCount(size_t count) { Write(static_cast<uint32>(count)); }

	void WriteString(const String& value)
	{
		WriteCount(value.GetLength());
		Write(value.CStr(), value.GetLength());
	}


public:
	bool bFailed = false;

private:
	File* mpFile;
};

} // namespace

bool CompiledMap::Load(const String& path)
{
	*this = CompiledMap {};

	File file_to_read(path, File::eModType::Read, File::eDataType::Binary);
	if (!file_to_read.IsFileOpen()) {
		return false;
	}

	Reader reader { &file_to_read };

	const uint32 magic = reader.Read<uint32>();
	const uint32 version = reader.Read<uint32>();

	if (reader.bFailed || magic != scMagic || version != scVersion) {
		file_to_read.Close();
		return false;
	}

	SourceHash = reader.Read<uint64>();
	bSealed = (reader.Read<uint32>() & scFlagSealed) != 0;

	Materials.InitSize(reader.ReadCount());

	for (CompiledMapMaterial& material : Materials) {
		material.Name = reader.ReadString();
		material.LibraryIndex = reader.Read<int32>();
	}

	BrushNames.InitSize(reader.ReadCount());

	for (String& name : BrushNames) {
		name = reader.ReadString();
	}

	Surfaces.InitSize(reader.ReadCount());

	for (CompiledMapSurface& surface : Surfaces) {
		surface.MaterialIndex = reader.Read<uint32>();

		const Vec3f bounds_min = reader.Read<Vec3f>();
		const Vec3f bounds_max = reader.Read<Vec3f>();
		//
		// reader.Read(surface.BoundsMin, 3);
		// reader.Read(surface.BoundsMax, 3);

		surface.Bounds = BBox(bounds_min, bounds_max);

		const uint32 vertex_count = reader.ReadCount();
		const uint32 index_count = reader.ReadCount();

		surface.Vertices.InitSize(vertex_count);
		surface.Indices.InitSize(index_count);

		reader.Read(surface.Vertices.pData, vertex_count);
		reader.Read(surface.Indices.pData, index_count);

		const uint32 lightmap_texcoord_count = reader.ReadCount();

		if (lightmap_texcoord_count != 0 && lightmap_texcoord_count != vertex_count * 2) {
			reader.bFailed = true;
		}

		if (reader.bFailed) {
			break;
		}

		surface.LightmapTexcoords.InitSize(lightmap_texcoord_count);
		reader.Read(surface.LightmapTexcoords.pData, lightmap_texcoord_count);

		if (surface.MaterialIndex >= Materials.Size) {
			reader.bFailed = true;
			break;
		}

		for (const uint32 index : surface.Indices) {
			if (index >= vertex_count) {
				reader.bFailed = true;
				break;
			}
		}
	}

	Hulls.InitSize(reader.ReadCount());

	for (CompiledMapHull& hull : Hulls) {
		const uint32 point_count = reader.ReadCount();

		hull.Points.InitSize(static_cast<size_t>(point_count) * 3);
		reader.Read(hull.Points.pData, hull.Points.Size);

		if (reader.bFailed) {
			break;
		}
	}

	Probes.VolumeCount = reader.ReadCount();

	if (Probes.VolumeCount > Limits::MaxProbeVolumes) {
		reader.bFailed = true;
		Probes.VolumeCount = 0;
	}

	reader.Read(Probes.Volumes, Probes.VolumeCount);
	reader.Read(Probes.Ranges, Probes.VolumeCount);

	ProbeSettings.BrushSpacing = reader.Read<float32>();
	ProbeSettings.LevelSpacing = reader.Read<float32>();
	Probes.VoidPointCount = reader.Read<uint32>();

	const uint32 grid_point_count = reader.ReadCount();
	const uint32 probe_count = reader.ReadCount();

	if (grid_point_count > Limits::MaxProbeGridPoints || probe_count > Limits::MaxIrradianceProbes) {
		reader.bFailed = true;
	}
	else {
		reader.Read(Probes.AddGridPoints(grid_point_count), grid_point_count);

		SizedArray<float32> positions;
		positions.InitSize(static_cast<size_t>(probe_count) * 3);
		reader.Read(positions.pData, positions.Size);

		for (uint32 i = 0; i < probe_count; i++) {
			Probes.Positions.Emplace(Vec3f(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]));
		}
	}

	for (uint32 volume = 0; volume < Probes.VolumeCount && !reader.bFailed; volume++) {
		const uint32* dims = Probes.Volumes[volume].DimsAndFirst;
		const uint64 last_point = static_cast<uint64>(dims[3]) + static_cast<uint64>(dims[0]) * dims[1] * dims[2];
		const uint64 last_probe = static_cast<uint64>(Probes.Ranges[volume].FirstProbe) +
								  Probes.Ranges[volume].ProbeCount;

		if (last_point > grid_point_count || last_probe > probe_count) {
			reader.bFailed = true;
		}
	}

	for (const uint16 probe : Probes.Grid) {
		if (probe < Limits::ProbeGridSolid && probe >= probe_count) {
			reader.bFailed = true;
			break;
		}
	}

	Lightmap.Width = reader.Read<uint32>();
	Lightmap.Height = reader.Read<uint32>();
	reader.Read(Lightmap.SunDirection, 3);

	const uint64 lightmap_texel_count = static_cast<uint64>(Lightmap.Width) * Lightmap.Height;

	if (Lightmap.Width > scMaxLightmapSize || Lightmap.Height > scMaxLightmapSize) {
		reader.bFailed = true;
	}
	else {
		Lightmap.SunVisibility.InitSize(lightmap_texel_count);
		reader.Read(Lightmap.SunVisibility.pData, lightmap_texel_count);
	}

	const uint32 probe_visibility_count = reader.ReadCount();

	if (probe_visibility_count != 0 && probe_visibility_count != Probes.GetProbeCount()) {
		reader.bFailed = true;
	}
	else {
		ProbeSunVisibility.InitSize(probe_visibility_count);
		reader.Read(ProbeSunVisibility.pData, probe_visibility_count);
	}

	if (reader.bFailed) {
		*this = CompiledMap {};
		return false;
	}

	std::sort(BrushNames.begin(), BrushNames.end(), IsNameBefore);

	return true;
}

bool CompiledMap::Save(const String& path) const
{
	File file_to_write(path, File::eModType::Write, File::eDataType::Binary);

	if (!file_to_write.IsFileOpen()) {
		return false;
	}

	Writer writer { &file_to_write };

	writer.Write(scMagic);
	writer.Write(scVersion);
	writer.Write(SourceHash);
	writer.Write<uint32>(bSealed ? scFlagSealed : 0);

	writer.WriteCount(Materials.Size);

	for (const CompiledMapMaterial& material : Materials) {
		writer.WriteString(material.Name);
		writer.Write(material.LibraryIndex);
	}

	writer.WriteCount(BrushNames.Size);

	for (const String& name : BrushNames) {
		writer.WriteString(name);
	}

	writer.WriteCount(Surfaces.Size);

	for (const CompiledMapSurface& surface : Surfaces) {
		writer.Write(surface.MaterialIndex);

		writer.Write(surface.Bounds.Min);
		writer.Write(surface.Bounds.Max);

		writer.WriteCount(surface.Vertices.Size);
		writer.WriteCount(surface.Indices.Size);

		writer.Write(surface.Vertices.pData, surface.Vertices.Size);
		writer.Write(surface.Indices.pData, surface.Indices.Size);

		writer.WriteCount(surface.LightmapTexcoords.Size);
		writer.Write(surface.LightmapTexcoords.pData, surface.LightmapTexcoords.Size);
	}

	writer.WriteCount(Hulls.Size);

	for (const CompiledMapHull& hull : Hulls) {
		writer.WriteCount(hull.GetPointCount());
		writer.Write(hull.Points.pData, hull.Points.Size);
	}

	writer.WriteCount(Probes.VolumeCount);
	writer.Write(Probes.Volumes, Probes.VolumeCount);
	writer.Write(Probes.Ranges, Probes.VolumeCount);
	writer.Write(ProbeSettings.BrushSpacing);
	writer.Write(ProbeSettings.LevelSpacing);
	writer.Write(Probes.VoidPointCount);
	writer.WriteCount(Probes.Grid.Size);
	writer.WriteCount(Probes.Positions.Size);
	writer.Write(Probes.Grid.pData, Probes.Grid.Size);

	for (const Vec3f position : Probes.Positions) {
		const float32 values[3] = { position.X, position.Y, position.Z };
		writer.Write(values, 3);
	}

	writer.Write(Lightmap.Width);
	writer.Write(Lightmap.Height);
	writer.Write(Lightmap.SunDirection, 3);
	writer.Write(Lightmap.SunVisibility.pData, Lightmap.SunVisibility.Size);

	writer.WriteCount(ProbeSunVisibility.Size);
	writer.Write(ProbeSunVisibility.pData, ProbeSunVisibility.Size);

	return (writer.bFailed == false);
}

bool CompiledMap::HasBrush(const String& name) const
{
	return std::binary_search(BrushNames.begin(), BrushNames.end(), name, IsNameBefore);
}

uint64 CompiledMap::HashFile(const String& path)
{
	std::FILE* file = std::fopen(path.CStr(), "rb");

	if (file == nullptr) {
		return 0;
	}

	uint64 hash = 0xcbf29ce484222325ull;
	uint8 buffer[4096];
	size_t read_count = 0;

	while ((read_count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
		for (size_t i = 0; i < read_count; i++) {
			hash = (hash ^ buffer[i]) * 0x100000001b3ull;
		}
	}

	std::fclose(file);

	return (hash == 0) ? 1 : hash;
}

static String ReplaceExtension(const String& path, const char* extension)
{
	const uint32 dot = path.FindLast('.');

	uint32 slash = path.FindLast('/');
	const uint32 backslash = path.FindLast('\\');

	if (slash == String::scNotFound || (backslash != String::scNotFound && backslash > slash)) {
		slash = backslash;
	}

	if (dot == String::scNotFound || (slash != String::scNotFound && dot < slash)) {
		return path + extension;
	}

	return path.SubStrAbs(0, dot) + extension;
}

String CompiledMap::GetPathForPrototype(const String& prototype_path)
{
	return ReplaceExtension(prototype_path, scExtension);
}

bool CompiledMap::IsCompiledMapPath(const String& path)
{
	const uint32 extension_length = static_cast<uint32>(std::strlen(scExtension));

	return path.GetLength() >= extension_length &&
		   std::strcmp(path.CStr() + path.GetLength() - extension_length, scExtension) == 0;
}

String CompiledMap::GetPrototypePath(const String& path)
{
	return IsCompiledMapPath(path) ? ReplaceExtension(path, scPrototypeExtension) : path;
}

} // namespace fx

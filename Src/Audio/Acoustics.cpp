#include "Acoustics.hpp"

#include <Audio/AcousticScene.hpp>
#include <Core/Log.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace fx::Acoustics {

FX_SET_MODULE_NAME("Acoustics")

namespace {

struct MaterialPreset
{
	const char* Name;
	float32 Density;
	IPLMaterial Material;
};

constexpr MaterialPreset scMaterialPresets[] = {
	{ "generic", 1000.0f, { { 0.10f, 0.20f, 0.30f }, 0.05f, { 0.100f, 0.050f, 0.030f } } },
	{ "brick", 1900.0f, { { 0.03f, 0.04f, 0.07f }, 0.05f, { 0.015f, 0.015f, 0.015f } } },
	{ "concrete", 2400.0f, { { 0.05f, 0.07f, 0.08f }, 0.05f, { 0.015f, 0.002f, 0.001f } } },
	{ "ceramic", 2300.0f, { { 0.01f, 0.02f, 0.02f }, 0.05f, { 0.060f, 0.044f, 0.011f } } },
	{ "gravel", 1700.0f, { { 0.60f, 0.70f, 0.80f }, 0.05f, { 0.031f, 0.012f, 0.008f } } },
	{ "carpet", 200.0f, { { 0.24f, 0.69f, 0.73f }, 0.05f, { 0.020f, 0.005f, 0.003f } } },
	{ "glass", 2500.0f, { { 0.06f, 0.03f, 0.02f }, 0.05f, { 0.060f, 0.044f, 0.011f } } },
	{ "plaster", 1100.0f, { { 0.12f, 0.06f, 0.04f }, 0.05f, { 0.056f, 0.056f, 0.004f } } },
	{ "wood", 600.0f, { { 0.11f, 0.07f, 0.06f }, 0.05f, { 0.070f, 0.014f, 0.005f } } },
	{ "metal", 7850.0f, { { 0.20f, 0.07f, 0.06f }, 0.05f, { 0.200f, 0.025f, 0.010f } } },
	{ "rock", 2600.0f, { { 0.13f, 0.20f, 0.24f }, 0.05f, { 0.015f, 0.002f, 0.001f } } },
};

struct MaterialAlias
{
	const char* Surface;
	const char* Preset;
};

constexpr MaterialAlias scMaterialAliases[] = {
	{ "tile", "ceramic" },	  { "tiles", "ceramic" }, { "stone", "rock" },	{ "siding", "wood" },
	{ "shingle", "wood" },	  { "dirt", "gravel" },	  { "grass", "gravel" }, { "sand", "gravel" },
	{ "fabric", "carpet" },	  { "cloth", "carpet" },  { "flesh", "carpet" },
};

using Clock = std::chrono::steady_clock;

constexpr uint32 scProbeDataMagic = 0x43415846;

struct ProbeDataHeader
{
	uint32 Magic = scProbeDataMagic;
	uint32 SteamAudioVersion = STEAMAUDIO_VERSION;
	uint64 PayloadSize = 0;
	uint64 PayloadHash = 0;
};

static_assert(sizeof(ProbeDataHeader) == 24);

uint64 HashPayload(const uint8* data, size_t size)
{
	uint64 hash = 0xCBF29CE484222325ull;

	for (size_t i = 0; i < size; i++) {
		hash = (hash ^ data[i]) * 0x100000001B3ull;
	}

	return hash;
}

uint32 GetMajorMinor(uint32 version) { return version >> 8; }

double SecondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

struct ProgressContext
{
	const AcousticProgressFunction* pFunction = nullptr;
	const char* Stage = "";
};

void IPLCALL OnBakeProgress(IPLfloat32 fraction, void* user_data)
{
	const ProgressContext& context = *static_cast<const ProgressContext*>(user_data);

	if (context.pFunction != nullptr && *context.pFunction) {
		(*context.pFunction)(context.Stage, fraction);
	}
}

IPLMatrix4x4 MakeBoxTransform(const Vec3f min, const Vec3f max)
{
	IPLMatrix4x4 transform {};

	transform.elements[0][0] = max.X - min.X;
	transform.elements[1][1] = max.Y - min.Y;
	transform.elements[2][2] = max.Z - min.Z;
	transform.elements[0][3] = (min.X + max.X) * 0.5f;
	transform.elements[1][3] = (min.Y + max.Y) * 0.5f;
	transform.elements[2][3] = (min.Z + max.Z) * 0.5f;
	transform.elements[3][3] = 1.0f;

	return transform;
}

} // namespace

Vec3f ToAudioSpace(const Vec3f value) { return Vec3f(value.X, value.Y, -value.Z); }

AcousticGeometry ToAudioSpace(const AcousticGeometry& geometry)
{
	AcousticGeometry audio_geometry = geometry;

	for (Vec3f& vertex : audio_geometry.Vertices) {
		vertex = ToAudioSpace(vertex);
	}

	for (size_t i = 0; i + 2 < audio_geometry.Indices.size(); i += 3) {
		std::swap(audio_geometry.Indices[i + 1], audio_geometry.Indices[i + 2]);
	}

	return audio_geometry;
}

uint32 GetMaterialPreset(const String& surface)
{
	const char* name = surface.CStr();

	for (const MaterialAlias& alias : scMaterialAliases) {
		if (std::strcmp(alias.Surface, name) == 0) {
			name = alias.Preset;
			break;
		}
	}

	for (uint32 i = 0; i < std::size(scMaterialPresets); i++) {
		if (std::strcmp(scMaterialPresets[i].Name, name) == 0) {
			return i;
		}
	}

	return 0;
}

const char* GetMaterialPresetName(uint32 preset)
{
	if (preset >= std::size(scMaterialPresets)) {
		return scMaterialPresets[0].Name;
	}

	return scMaterialPresets[preset].Name;
}

float32 GetReflectivity(const String& surface)
{
	const IPLMaterial& material = scMaterialPresets[GetMaterialPreset(surface)].Material;

	return 1.0f - (material.absorption[0] + material.absorption[1] + material.absorption[2]) / 3.0f;
}

IPLMaterial GetMaterial(const AcousticSurface& surface)
{
	const MaterialPreset& preset = scMaterialPresets[GetMaterialPreset(surface.Name)];
	IPLMaterial material = preset.Material;

	if (!surface.Density.has_value() || *surface.Density <= 0.0f) {
		return material;
	}

	const float32 mass_ratio = *surface.Density / preset.Density;
	const float32 absorption_scale = 1.0f / std::sqrt(mass_ratio);

	for (int32 band = 0; band < 3; band++) {
		material.absorption[band] = std::clamp(material.absorption[band] * absorption_scale, 0.0f, 1.0f);
		material.transmission[band] = std::clamp(material.transmission[band] / mass_ratio, 0.0f, 1.0f);
	}

	return material;
}

bool UnwrapProbeData(const std::vector<uint8>& data, const uint8*& out_payload, size_t& out_size, String& out_error)
{
	ProbeDataHeader header;

	if (data.size() < sizeof(header)) {
		out_error = "the data is too short";
		return false;
	}

	std::memcpy(&header, data.data(), sizeof(header));

	if (header.Magic != scProbeDataMagic || header.PayloadSize != data.size() - sizeof(header)) {
		out_error = "the data is not baked acoustic probes";
		return false;
	}

	if (GetMajorMinor(header.SteamAudioVersion) != GetMajorMinor(STEAMAUDIO_VERSION)) {
		out_error = String::Fmt("the probes were baked with Steam Audio {}.{}", header.SteamAudioVersion >> 16,
								(header.SteamAudioVersion >> 8) & 0xFF);
		return false;
	}

	const uint8* payload = data.data() + sizeof(header);
	const size_t payload_size = static_cast<size_t>(header.PayloadSize);

	if (HashPayload(payload, payload_size) != header.PayloadHash) {
		out_error = "the data is damaged";
		return false;
	}

	out_payload = payload;
	out_size = payload_size;

	return true;
}

IPLVector3 ToIPL(const Vec3f value) { return IPLVector3 { value.X, value.Y, value.Z }; }

void IPLCALL OnSteamAudioLog(IPLLogLevel level, const char* message)
{
	if (level == IPL_LOGLEVEL_ERROR) {
		LogError(LC_AUDIO, "Steam Audio: {}", message);
	}
	else if (level == IPL_LOGLEVEL_WARNING) {
		LogWarning(LC_AUDIO, "Steam Audio: {}", message);
	}
}

bool CreateScene(IPLContext context, const AcousticGeometry& audio_geometry, IPLScene& out_scene,
				 IPLStaticMesh& out_mesh)
{
	out_scene = nullptr;
	out_mesh = nullptr;

	IPLSceneSettings scene_settings {};
	scene_settings.type = IPL_SCENETYPE_DEFAULT;

	if (iplSceneCreate(context, &scene_settings, &out_scene) != IPL_STATUS_SUCCESS) {
		LogError(LC_AUDIO, "Could not create the acoustic scene");
		return false;
	}

	const uint32 triangle_count = audio_geometry.GetTriangleCount();

	if (triangle_count > 0) {
		std::vector<IPLVector3> vertices;
		vertices.reserve(audio_geometry.Vertices.size());

		for (const Vec3f vertex : audio_geometry.Vertices) {
			vertices.push_back(ToIPL(vertex));
		}

		std::vector<IPLTriangle> triangles(triangle_count);
		std::vector<IPLint32> material_indices(triangle_count);

		for (uint32 i = 0; i < triangle_count; i++) {
			for (uint32 corner = 0; corner < 3; corner++) {
				triangles[i].indices[corner] = static_cast<IPLint32>(audio_geometry.Indices[i * 3 + corner]);
			}

			material_indices[i] = static_cast<IPLint32>(audio_geometry.TriangleSurfaces[i]);
		}

		std::vector<IPLMaterial> materials;

		for (const AcousticSurface& surface : audio_geometry.Surfaces) {
			materials.push_back(GetMaterial(surface));
		}

		if (materials.empty()) {
			materials.push_back(scMaterialPresets[0].Material);
		}

		IPLStaticMeshSettings mesh_settings {};
		mesh_settings.numVertices = static_cast<IPLint32>(vertices.size());
		mesh_settings.numTriangles = static_cast<IPLint32>(triangle_count);
		mesh_settings.numMaterials = static_cast<IPLint32>(materials.size());
		mesh_settings.vertices = vertices.data();
		mesh_settings.triangles = triangles.data();
		mesh_settings.materialIndices = material_indices.data();
		mesh_settings.materials = materials.data();

		if (iplStaticMeshCreate(out_scene, &mesh_settings, &out_mesh) != IPL_STATUS_SUCCESS) {
			LogError(LC_AUDIO, "Could not create the acoustic mesh ({} triangles)", triangle_count);
			iplSceneRelease(&out_scene);
			return false;
		}

		iplStaticMeshAdd(out_mesh, out_scene);
	}

	iplSceneCommit(out_scene);

	return true;
}

bool Bake(const AcousticGeometry& geometry, const AcousticBakeSettings& settings, AcousticBakeResult& out_result,
		  const AcousticProgressFunction& progress)
{
	out_result = AcousticBakeResult {};

	if (geometry.GetTriangleCount() == 0) {
		out_result.Error = "there is no geometry to bake";
		return false;
	}

	int32 threads = settings.Threads;

	if (threads <= 0) {
		threads = std::max(static_cast<int32>(std::thread::hardware_concurrency()), 1);
	}

	IPLContextSettings context_settings {};
	context_settings.version = STEAMAUDIO_VERSION;
	context_settings.logCallback = OnSteamAudioLog;

	IPLContext context = nullptr;

	if (iplContextCreate(&context_settings, &context) != IPL_STATUS_SUCCESS) {
		out_result.Error = "Steam Audio could not start";
		return false;
	}

	IPLScene scene = nullptr;
	IPLStaticMesh mesh = nullptr;
	IPLProbeArray probe_array = nullptr;
	IPLProbeBatch probe_batch = nullptr;
	IPLSerializedObject serialized = nullptr;

	const auto release = [&]
	{
		if (serialized != nullptr) {
			iplSerializedObjectRelease(&serialized);
		}

		if (probe_batch != nullptr) {
			iplProbeBatchRelease(&probe_batch);
		}

		if (probe_array != nullptr) {
			iplProbeArrayRelease(&probe_array);
		}

		if (mesh != nullptr) {
			iplStaticMeshRelease(&mesh);
		}

		if (scene != nullptr) {
			iplSceneRelease(&scene);
		}

		iplContextRelease(&context);
	};

	if (!CreateScene(context, ToAudioSpace(geometry), scene, mesh)) {
		out_result.Error = "the acoustic scene could not be built";
		release();
		return false;
	}

	const Vec3f corner_a = ToAudioSpace(settings.BoundsMin);
	const Vec3f corner_b = ToAudioSpace(settings.BoundsMax);

	IPLProbeGenerationParams generation {};
	generation.type = IPL_PROBEGENERATIONTYPE_UNIFORMFLOOR;
	generation.spacing = settings.ProbeSpacing;
	generation.height = settings.ProbeHeight;
	generation.transform = MakeBoxTransform(Vec3f::Min(corner_a, corner_b), Vec3f::Max(corner_a, corner_b));

	iplProbeArrayCreate(context, &probe_array);
	iplProbeArrayGenerateProbes(probe_array, scene, &generation);

	const int32 probe_count = iplProbeArrayGetNumProbes(probe_array);

	if (probe_count <= 0) {
		out_result.Error = "no probes were placed, there is no floor inside the bounds";
		release();
		return false;
	}

	out_result.ProbeCount = static_cast<uint32>(probe_count);

	for (int32 i = 0; i < probe_count; i++) {
		const IPLSphere probe = iplProbeArrayGetProbe(probe_array, i);
		const Vec3f centre = ToAudioSpace(Vec3f(probe.center.x, probe.center.y, probe.center.z));

		if (i == 0) {
			out_result.ProbeMin = centre;
			out_result.ProbeMax = centre;
		}

		out_result.ProbeMin = Vec3f::Min(out_result.ProbeMin, centre);
		out_result.ProbeMax = Vec3f::Max(out_result.ProbeMax, centre);
	}

	iplProbeBatchCreate(context, &probe_batch);
	iplProbeBatchAddProbeArray(probe_batch, probe_array);
	iplProbeBatchCommit(probe_batch);

	ProgressContext progress_context { &progress, "reverb" };

	IPLReflectionsBakeParams reflection_params {};
	reflection_params.scene = scene;
	reflection_params.probeBatch = probe_batch;
	reflection_params.sceneType = IPL_SCENETYPE_DEFAULT;
	reflection_params.identifier = scReverbIdentifier;
	reflection_params.bakeFlags = IPL_REFLECTIONSBAKEFLAGS_BAKEPARAMETRIC;
	reflection_params.numRays = settings.ReverbRays;
	reflection_params.numDiffuseSamples = 32;
	reflection_params.numBounces = settings.ReverbBounces;
	reflection_params.simulatedDuration = settings.ReverbDuration;
	reflection_params.savedDuration = settings.ReverbDuration;
	reflection_params.order = scProbeOrder;
	reflection_params.numThreads = threads;
	reflection_params.rayBatchSize = 1;
	reflection_params.irradianceMinDistance = 1.0f;
	reflection_params.bakeBatchSize = 1;

	const auto reflection_start = Clock::now();
	iplReflectionsBakerBake(context, &reflection_params, OnBakeProgress, &progress_context);
	out_result.ReflectionSeconds = SecondsSince(reflection_start);

	if (settings.bPathing) {
		progress_context.Stage = "pathing";

		IPLPathBakeParams path_params {};
		path_params.scene = scene;
		path_params.probeBatch = probe_batch;
		path_params.identifier = scPathingIdentifier;
		path_params.numSamples = settings.PathingSamples;
		path_params.radius = settings.PathingRadius;
		path_params.threshold = settings.PathingThreshold;
		path_params.visRange = settings.PathingVisRange;
		path_params.pathRange = settings.PathingRange;
		path_params.numThreads = threads;

		const auto pathing_start = Clock::now();
		iplPathBakerBake(context, &path_params, OnBakeProgress, &progress_context);
		out_result.PathingSeconds = SecondsSince(pathing_start);
		out_result.bHasPathing = true;
	}

	IPLSerializedObjectSettings serialized_settings {};

	if (iplSerializedObjectCreate(context, &serialized_settings, &serialized) != IPL_STATUS_SUCCESS) {
		out_result.Error = "the probes could not be serialized";
		release();
		return false;
	}

	iplProbeBatchSave(probe_batch, serialized);

	const IPLbyte* data = iplSerializedObjectGetData(serialized);
	const IPLsize size = iplSerializedObjectGetSize(serialized);

	ProbeDataHeader header;
	header.PayloadSize = size;
	header.PayloadHash = HashPayload(data, size);

	out_result.Data.resize(sizeof(header) + size);
	std::memcpy(out_result.Data.data(), &header, sizeof(header));
	std::memcpy(out_result.Data.data() + sizeof(header), data, size);

	release();

	return true;
}

} // namespace fx::Acoustics

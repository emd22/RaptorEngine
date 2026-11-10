#include "AudioSystem.hpp"

#include <miniaudio.h>

#include <Asset/ConfigFile.hpp>
#include <Audio/AudioMeter.hpp>
#include <CVar.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Core/Random.hpp>
#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace fx {

FX_SET_MODULE_NAME("AudioSystem")

namespace {

constexpr uint32 scRootBusCount = static_cast<uint32>(eSoundBus::Count);

constexpr const char* scRootBusNames[scRootBusCount] = {
	"sfx",
	"music",
	"ui",
	"voice",
};

constexpr const char* scBusVolumeCVarNames[scRootBusCount] = {
	"s_volume_sfx",
	"s_volume_music",
	"s_volume_ui",
	"s_volume_voice",
};

constexpr uint32 scLimiterLookahead = 64;
constexpr uint32 scMaxNodeChannels = 8;

// Stereo-linked peak limiter on the master mix. The gain is computed from the incoming samples and applied to
// samples delayed by the lookahead, so it is already down when a peak comes out.
struct MasterLimiter
{
	ma_node_base Node;
	uint32 Channels = 0;
	float32 SampleRate = 48000.0f;
	float32 AttackCoefficient = 0.0f;
	float32 ReleaseCoefficient = 0.0f;
	float32 Gain = 1.0f;
	float32 HeldGain = 1.0f;
	uint32 HoldFrames = 0;
	uint32 DelayIndex = 0;
	float32 Delay[scLimiterLookahead * scMaxNodeChannels] {};
	bool bNodeInitialized = false;

	std::atomic<float32> BlockMinGain = 1.0f;
	AudioMeter Meter;
};

// Sits after each bus's sound group: meters what comes in, then applies the mix gain and low-pass. The gain ramps
// across the block so mix changes made once per game frame do not step.
struct BusNode
{
	ma_node_base Node;
	uint32 Channels = 0;
	float32 SampleRate = 48000.0f;
	bool bNodeInitialized = false;

	std::atomic<float32> TargetGain = 1.0f;
	std::atomic<float32> Lowpass = AudioSystem::scLowpassOpen;

	float32 Gain = 1.0f;
	float32 FilterState[2][scMaxNodeChannels] {};
	AudioMeter Meter;
};

void ProcessBusNode(ma_node* node, const float** frames_in, ma_uint32* frame_count_in, float** frames_out,
					ma_uint32* frame_count_out)
{
	constexpr float32 scLowpassBypass = 18000.0f;

	BusNode& bus = *static_cast<BusNode*>(node);

	const ma_uint32 frame_count = std::min(frame_count_in[0], *frame_count_out);
	const uint32 channels = bus.Channels;
	const float* input = frames_in[0];
	float* output = frames_out[0];

	bus.Meter.Process(input, frame_count, channels, bus.SampleRate);

	const float32 target_gain = bus.TargetGain.load(std::memory_order_relaxed);
	const float32 lowpass = bus.Lowpass.load(std::memory_order_relaxed);
	const bool filter = lowpass < scLowpassBypass;
	const float32 coefficient = 1.0f - std::exp(-FX_2PI * lowpass / bus.SampleRate);
	const float32 gain_step = (target_gain - bus.Gain) / static_cast<float32>(std::max<ma_uint32>(frame_count, 1));

	float32 gain = bus.Gain;

	for (ma_uint32 i = 0; i < frame_count; i++) {
		gain += gain_step;

		for (uint32 channel = 0; channel < channels; channel++) {
			const size_t sample_index = static_cast<size_t>(i) * channels + channel;
			float32 sample = input[sample_index];

			float32& first = bus.FilterState[0][channel];
			float32& second = bus.FilterState[1][channel];

			// Two one-pole stages, 12 dB per octave. Bypassed stages follow the input so turning one on does not click
			if (filter) {
				first += coefficient * (sample - first);
				second += coefficient * (first - second);
				sample = second;
			}
			else {
				first = sample;
				second = sample;
			}

			output[sample_index] = sample * gain;
		}
	}

	bus.Gain = target_gain;

	*frame_count_in = frame_count;
	*frame_count_out = frame_count;
}

ma_node_vtable scBusNodeVtable = {
	ProcessBusNode, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING,
};

void ProcessLimiterNode(ma_node* node, const float** frames_in, ma_uint32* frame_count_in, float** frames_out,
						ma_uint32* frame_count_out)
{
	constexpr float32 scLimiterThreshold = 0.891f;

	MasterLimiter& limiter = *static_cast<MasterLimiter*>(node);

	const ma_uint32 frame_count = std::min(frame_count_in[0], *frame_count_out);
	const uint32 channels = limiter.Channels;

	float32 min_gain = 1.0f;

	for (ma_uint32 i = 0; i < frame_count; i++) {
		const float* in = frames_in[0] + static_cast<size_t>(i) * channels;
		float* out = frames_out[0] + static_cast<size_t>(i) * channels;

		float32 peak = 0.0f;

		for (uint32 channel = 0; channel < channels; channel++) {
			peak = std::max(peak, std::abs(in[channel]));
		}

		float32 target = 1.0f;

		if (peak > scLimiterThreshold) {
			target = scLimiterThreshold / peak;
		}

		if (target <= limiter.HeldGain) {
			limiter.HeldGain = target;
			limiter.HoldFrames = scLimiterLookahead;
		}
		else if (limiter.HoldFrames > 0) {
			--limiter.HoldFrames;
		}
		else {
			limiter.HeldGain = target;
		}

		float32 coefficient = limiter.ReleaseCoefficient;

		if (limiter.HeldGain < limiter.Gain) {
			coefficient = limiter.AttackCoefficient;
		}

		limiter.Gain = limiter.HeldGain + (limiter.Gain - limiter.HeldGain) * coefficient;
		min_gain = std::min(min_gain, limiter.Gain);

		float32* delayed = limiter.Delay + static_cast<size_t>(limiter.DelayIndex) * channels;

		for (uint32 channel = 0; channel < channels; channel++) {
			out[channel] = std::clamp(delayed[channel] * limiter.Gain, -1.0f, 1.0f);
			delayed[channel] = in[channel];
		}

		limiter.DelayIndex = (limiter.DelayIndex + 1) % scLimiterLookahead;
	}

	limiter.BlockMinGain.store(min_gain, std::memory_order_relaxed);
	limiter.Meter.Process(frames_out[0], frame_count, channels, limiter.SampleRate);

	*frame_count_in = frame_count;
	*frame_count_out = frame_count;
}

ma_node_vtable scLimiterNodeVtable = {
	ProcessLimiterNode, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING,
};

float32 RandomInRange(const SoundRange& range) { return range.Min + (range.Max - range.Min) * FastRandUnit(); }

ma_result ApplyClip(ma_sound* sound, const SoundClip& clip)
{
	ma_uint32 sample_rate = 0;

	ma_result result = ma_sound_get_data_format(sound, nullptr, nullptr, &sample_rate, nullptr, 0);

	if (result != MA_SUCCESS) {
		return result;
	}

	const ma_uint64 begin = static_cast<ma_uint64>(static_cast<float64>(clip.Start) * sample_rate);
	const ma_uint64 end = static_cast<ma_uint64>(static_cast<float64>(clip.End) * sample_rate);

	return ma_data_source_set_range_in_pcm_frames(ma_sound_get_data_source(sound), begin, end);
}

template <typename T>
T Value(ConfigFile& config, const char* name, T fallback)
{
	const ConfigEntry* entry = config.GetEntry(HashStr32(name));

	if (entry == nullptr) {
		return fallback;
	}

	return entry->GetValue<T>();
}

SoundRange RangeValue(ConfigFile& config, const char* name, SoundRange fallback)
{
	const ConfigEntry* entry = config.GetEntry(HashStr32(name));

	if (entry == nullptr) {
		return fallback;
	}

	SoundRange range;

	if (entry->bIsArray && entry->ArrayData.IsInited() && entry->ArrayData.Size() >= 2) {
		range.Min = entry->ArrayData[0].Get<float32>();
		range.Max = entry->ArrayData[1].Get<float32>();
	}
	else if (!entry->bIsArray) {
		range.Min = entry->GetValue<float32>();
		range.Max = range.Min;
	}
	else {
		return fallback;
	}

	return range;
}

eSoundAttenuation AttenuationFromName(const char* name)
{
	if (strcmp(name, "none") == 0) {
		return eSoundAttenuation::None;
	}

	if (strcmp(name, "linear") == 0) {
		return eSoundAttenuation::Linear;
	}

	if (strcmp(name, "exponential") == 0) {
		return eSoundAttenuation::Exponential;
	}

	return eSoundAttenuation::Inverse;
}

ma_attenuation_model ToAttenuationModel(eSoundAttenuation attenuation)
{
	switch (attenuation) {
	case eSoundAttenuation::None:
		return ma_attenuation_model_none;
	case eSoundAttenuation::Linear:
		return ma_attenuation_model_linear;
	case eSoundAttenuation::Exponential:
		return ma_attenuation_model_exponential;
	case eSoundAttenuation::Inverse:
		break;
	}

	return ma_attenuation_model_inverse;
}

} // namespace

using Acoustics::ToAudioSpace;

struct AudioSystem::Backend
{
	ma_engine Engine;
	MasterLimiter Limiter;
	ma_sound_group Buses[scMaxBuses];
	bool bBusInitialized[scMaxBuses] {};
	BusNode BusNodes[scMaxBuses];
	ma_sound Voices[scMaxVoices];

	SizedArray<SizedArray<ma_sound*>> Prototypes;
	SizedArray<uint32> LastVariants;
	SizedArray<bool> WarnedAboutFailedLoads;
};

bool AudioSystem::Create(const AudioCreateInfo& info)
{
	constexpr uint32 scOutputChannels = 2;
	constexpr uint32 scNoDeviceSampleRate = 48000;
	constexpr float32 scLimiterReleaseSeconds = 0.1f;

	if (mpBackend != nullptr) {
		return true;
	}

	mpBackend = new Backend;

	ma_engine_config config = ma_engine_config_init();
	config.listenerCount = 1;
	config.periodSizeInFrames = SpatialAudio::scFrameSize;
	config.sampleRate = info.SampleRate;
	config.channels = info.Channels;

	// The spatial voices are stereo, so mix in stereo and let the device convert to its own layout
	if (config.channels == 0) {
		config.channels = scOutputChannels;
	}

	if (info.bNoDevice) {
		config.noDevice = MA_TRUE;

		if (config.sampleRate == 0) {
			config.sampleRate = scNoDeviceSampleRate;
		}
	}

	const ma_result result = ma_engine_init(&config, &mpBackend->Engine);

	if (result != MA_SUCCESS) {
		LogError(LC_AUDIO, "Could not start the audio engine ({}), sound is disabled", ma_result_description(result));

		delete mpBackend;
		mpBackend = nullptr;

		return false;
	}

	MasterLimiter& limiter = mpBackend->Limiter;
	const ma_uint32 limiter_channels = ma_engine_get_channels(&mpBackend->Engine);
	const float32 sample_rate = static_cast<float32>(ma_engine_get_sample_rate(&mpBackend->Engine));

	limiter.Channels = std::min<uint32>(limiter_channels, scMaxNodeChannels);
	limiter.SampleRate = sample_rate;
	limiter.AttackCoefficient = std::exp(-5.0f / static_cast<float32>(scLimiterLookahead));
	limiter.ReleaseCoefficient = std::exp(-1.0f / (scLimiterReleaseSeconds * sample_rate));

	ma_node_config limiter_config = ma_node_config_init();
	limiter_config.vtable = &scLimiterNodeVtable;
	limiter_config.pInputChannels = &limiter_channels;
	limiter_config.pOutputChannels = &limiter_channels;

	if (limiter_channels <= scMaxNodeChannels && ma_node_init(ma_engine_get_node_graph(&mpBackend->Engine),
															  &limiter_config, nullptr, &limiter.Node) == MA_SUCCESS) {
		limiter.bNodeInitialized = true;
		ma_node_attach_output_bus(&limiter.Node, 0, ma_engine_get_endpoint(&mpBackend->Engine), 0);
	}
	else {
		LogWarning(LC_AUDIO, "Could not create the master limiter, the mix can clip");
	}

	for (uint32 i = 0; i < scRootBusCount; i++) {
		mBuses[i] = MixBus {};
		mBuses[i].Name = scRootBusNames[i];
		mBuses[i].NameHash = HashStr32(scRootBusNames[i]);
		mBuses[i].Lowpass = scLowpassOpen;

		CreateBusNodes(i);
	}

	mBusCount = scRootBusCount;

	mpVolumeCVar = gCVars->Set("s_volume", 1.0f);
	mpMuteUnfocusedCVar = gCVars->Set("s_mute_unfocused", 1);
	mpSpatialCVar = gCVars->Set("s_spatial", 1);
	mpHrtfCVar = gCVars->Set("s_hrtf", 1);
	mpReverbCVar = gCVars->Set("s_reverb", 1);
	mpReverbVolumeCVar = gCVars->Set("s_reverb_volume", 0.8f);
	mpReverbRaysCVar = gCVars->Set("s_reverb_rays", SpatialAudio::scDefaultReverbRays);
	mpReverbBouncesCVar = gCVars->Set("s_reverb_bounces", SpatialAudio::scDefaultReverbBounces);

	for (uint32 i = 0; i < scRootBusCount; i++) {
		mpBusVolumeCVars[i] = gCVars->Set(scBusVolumeCVarNames[i], 1.0f);
	}

	ma_device* device = ma_engine_get_device(&mpBackend->Engine);

	if (device != nullptr) {
		LogInfo(LC_AUDIO, "Audio device '{}', {} Hz, {} channels", device->playback.name, GetSampleRate(),
				GetChannelCount());
	}
	else {
		LogInfo(LC_AUDIO, "Audio engine running without a device, {} Hz, {} channels", GetSampleRate(),
				GetChannelCount());
	}

	if (info.bSteamAudio) {
		mSpatial.Create(&mpBackend->Engine, scMaxVoices,
						reinterpret_cast<ma_node_base*>(&mpBackend->Buses[static_cast<uint32>(eSoundBus::Sfx)]));
	}

	LoadSounds();

	return true;
}

void AudioSystem::Destroy()
{
	if (mpBackend == nullptr) {
		return;
	}

	StopAll();
	UnloadDefs();

	mSpatial.Destroy();

	DestroyMixBuses();

	for (uint32 i = scRootBusCount; i > 0; i--) {
		DestroyBusNodes(i - 1);
	}

	mBusCount = 0;

	if (mpBackend->Limiter.bNodeInitialized) {
		ma_node_uninit(&mpBackend->Limiter.Node, nullptr);
	}

	ma_engine_uninit(&mpBackend->Engine);

	delete mpBackend;
	mpBackend = nullptr;

	mDefs.Clear();
}

void AudioSystem::LoadSounds()
{
	if (mpBackend == nullptr) {
		return;
	}

	StopAll();
	UnloadDefs();
	mDefs.Clear();

	// Sound defs name their bus, so the mix comes first
	LoadMix();

	const String directory = FilesystemIO::ResolvePath(scSoundDirectory);

	if (!FilesystemIO::FileExists(directory)) {
		LogWarning(LC_AUDIO, "Sound directory '{}' does not exist", directory);
		return;
	}

	PagedArray<std::string> files = FilesystemIO::DirListIfHasExtension(directory.CStr(), ".conf", false);

	for (const std::string& file : files) {
		const std::string name = file.substr(0, file.size() - strlen(".conf"));

		SoundDef def;

		if (LoadDef(String::Fmt("{}/{}", scSoundDirectory, file.c_str()).CStr(), String(name.c_str()), def)) {
			mDefs.Insert(std::move(def));
		}
	}

	mpBackend->Prototypes.InitSize(mDefs.Size);
	mpBackend->LastVariants.InitSize(mDefs.Size, UINT32_MAX);
	mpBackend->WarnedAboutFailedLoads.InitSize(mDefs.Size, false);

	constexpr ma_uint32 scPrototypeFlags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC |
										   MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;

	for (uint32 i = 0; i < mDefs.Size; i++) {
		const SoundDef& def = mDefs[i];

		if (def.bStream) {
			continue;
		}

		mpBackend->Prototypes[i].InitCapacity(def.Files.Size);

		for (const String& path : def.Files) {
			ma_sound* prototype = new ma_sound;

			const ma_result result = ma_sound_init_from_file(&mpBackend->Engine, path.CStr(), scPrototypeFlags, nullptr,
															 nullptr, prototype);

			if (result != MA_SUCCESS) {
				LogError(LC_AUDIO, "Could not load sound file '{}' ({})", path, ma_result_description(result));

				delete prototype;
				prototype = nullptr;
			}

			mpBackend->Prototypes[i].Insert(prototype);
		}
	}

	LogInfo(LC_AUDIO, "Loaded {} sounds", mDefs.Size);
}

bool AudioSystem::LoadDef(const char* path, const String& name, SoundDef& out_def)
{
	ConfigFile config;
	config.Load(path);

	if (config.HasErrors()) {
		LogError(LC_AUDIO, "Could not parse sound '{}'", path);
		return false;
	}

	out_def.Name = name;
	out_def.NameHash = HashStr32(name.CStr());

	DynArray<const char*, GrowthFunctions::Double> file_names;

	const ConfigEntry* files = config.GetEntry(HashStr32("files"));

	if (files != nullptr && files->bIsArray && files->ArrayData.IsInited()) {
		for (uint32 i = 0; i < files->ArrayData.Size(); i++) {
			file_names.Insert(files->ArrayData[i].Get<const char*>());
		}
	}

	const ConfigEntry* file = config.GetEntry(HashStr32("file"));

	if (file != nullptr && !file->bIsArray) {
		file_names.Insert(file->GetValue<const char*>());
	}

	for (const char* file_name : file_names) {
		const String relative_path = String::Fmt("{}/{}", scSoundDirectory, file_name);
		const String resolved_path = FilesystemIO::ResolvePath(relative_path);

		if (!FilesystemIO::FileExists(resolved_path)) {
			LogError(LC_AUDIO, "Sound '{}' names a file that does not exist ('{}')", path, relative_path);
			continue;
		}

		out_def.Files.Insert(resolved_path);
	}

	if (out_def.Files.IsEmpty()) {
		LogError(LC_AUDIO, "Sound '{}' needs at least one file in 'file' or 'files'", path);
		return false;
	}

	const ConfigEntry* clips = config.GetEntry(HashStr32("clips"));

	if (clips != nullptr && clips->Members.IsInited()) {
		for (const ConfigEntry& clip : clips->Members) {
			if (!clip.bIsArray || !clip.ArrayData.IsInited() || clip.ArrayData.Size() != 2) {
				LogError(LC_AUDIO, "Sound '{}' has a clip that is not [start, end]", path);
				continue;
			}

			const float32 start = clip.ArrayData[0].Get<float32>();
			const float32 end = clip.ArrayData[1].Get<float32>();

			if (start < 0.0f || end <= start) {
				LogError(LC_AUDIO, "Sound '{}' has an empty or negative clip [{}, {}]", path, start, end);
				continue;
			}

			out_def.Clips.Insert(SoundClip { .Start = start, .End = end });
		}
	}
	else if (clips != nullptr) {
		LogError(LC_AUDIO, "Sound '{}' needs 'clips' written as [[start, end], ...]", path);
	}

	const char* bus_name = Value<const char*>(config, "bus", "sfx");
	out_def.Bus = FindBus(bus_name);

	if (out_def.Bus == UINT32_MAX) {
		LogError(LC_AUDIO, "Sound '{}' names bus '{}', which is not a root bus or in {}", path, bus_name, scMixPath);
		out_def.Bus = static_cast<uint32>(eSoundBus::Sfx);
	}

	out_def.Volume = RangeValue(config, "volume", out_def.Volume);
	out_def.Pitch = RangeValue(config, "pitch", out_def.Pitch);

	out_def.bLoop = Value<bool>(config, "loop", false);
	out_def.bStream = Value<bool>(config, "stream", false);
	out_def.bSpatial = Value<bool>(config, "spatial", true);

	out_def.Attenuation = AttenuationFromName(Value<const char*>(config, "attenuation", "inverse"));
	out_def.MinDistance = std::max(Value<float32>(config, "min_distance", out_def.MinDistance), 0.01f);
	out_def.MaxDistance = std::max(Value<float32>(config, "max_distance", out_def.MaxDistance), out_def.MinDistance);
	out_def.Rolloff = Value<float32>(config, "rolloff", out_def.Rolloff);

	out_def.FadeIn = std::max(Value<float32>(config, "fade_in", 0.0f), 0.0f);
	out_def.FadeOut = std::max(Value<float32>(config, "fade_out", 0.0f), 0.0f);

	out_def.Reverb = std::max(Value<float32>(config, "reverb", 0.0f), 0.0f);

	out_def.MaxInstances = static_cast<uint32>(std::max(Value<int32>(config, "max_instances", 0), 0));

	return true;
}

void AudioSystem::UnloadDefs()
{
	if (mpBackend == nullptr) {
		return;
	}

	for (SizedArray<ma_sound*>& prototypes : mpBackend->Prototypes) {
		for (ma_sound* prototype : prototypes) {
			if (prototype != nullptr) {
				ma_sound_uninit(prototype);
				delete prototype;
			}
		}
	}

	mpBackend->Prototypes.Free();
	mpBackend->LastVariants.Free();
	mpBackend->WarnedAboutFailedLoads.Free();
}

bool AudioSystem::CreateBusNodes(uint32 index)
{
	ma_engine* engine = &mpBackend->Engine;
	ma_sound_group* group = &mpBackend->Buses[index];
	BusNode& bus_node = mpBackend->BusNodes[index];

	if (ma_sound_group_init(engine, 0, nullptr, group) != MA_SUCCESS) {
		LogError(LC_AUDIO, "Could not create bus '{}'", mBuses[index].Name);
		return false;
	}

	mpBackend->bBusInitialized[index] = true;

	const ma_uint32 channels = ma_engine_get_channels(engine);

	bus_node.Channels = channels;
	bus_node.SampleRate = static_cast<float32>(ma_engine_get_sample_rate(engine));
	bus_node.Gain = 1.0f;
	bus_node.TargetGain.store(1.0f);
	bus_node.Lowpass.store(scLowpassOpen);
	bus_node.Meter.PeakState = 0.0f;
	bus_node.Meter.MeanSquareState = 0.0f;
	bus_node.Meter.Peak.store(0.0f);
	bus_node.Meter.MeanSquare.store(0.0f);
	std::memset(bus_node.FilterState, 0, sizeof(bus_node.FilterState));

	ma_node_config node_config = ma_node_config_init();
	node_config.vtable = &scBusNodeVtable;
	node_config.pInputChannels = &channels;
	node_config.pOutputChannels = &channels;

	ma_node* output = ma_engine_get_endpoint(engine);

	if (mBuses[index].Parent != UINT32_MAX) {
		output = &mpBackend->Buses[mBuses[index].Parent];
	}
	else if (mpBackend->Limiter.bNodeInitialized) {
		output = &mpBackend->Limiter.Node;
	}

	if (channels <= scMaxNodeChannels &&
		ma_node_init(ma_engine_get_node_graph(engine), &node_config, nullptr, &bus_node.Node) == MA_SUCCESS) {
		bus_node.bNodeInitialized = true;

		ma_node_attach_output_bus(group, 0, &bus_node.Node, 0);
		ma_node_attach_output_bus(&bus_node.Node, 0, output, 0);
	}
	else {
		LogWarning(LC_AUDIO, "Could not create the mix node for bus '{}', it plays without mix gain",
				   mBuses[index].Name);
		ma_node_attach_output_bus(group, 0, output, 0);
	}

	return true;
}

void AudioSystem::DestroyBusNodes(uint32 index)
{
	BusNode& bus_node = mpBackend->BusNodes[index];

	if (bus_node.bNodeInitialized) {
		ma_node_uninit(&bus_node.Node, nullptr);
		bus_node.bNodeInitialized = false;
	}

	if (mpBackend->bBusInitialized[index]) {
		ma_sound_group_uninit(&mpBackend->Buses[index]);
		mpBackend->bBusInitialized[index] = false;
	}
}

void AudioSystem::DestroyMixBuses()
{
	// Children come after their parents, so going backwards never leaves a bus feeding into a destroyed one
	for (uint32 i = mBusCount; i > scRootBusCount; i--) {
		DestroyBusNodes(i - 1);
	}

	mBusCount = std::min(mBusCount, scRootBusCount);
}

uint32 AudioSystem::FindBus(const char* name) const
{
	const Hash32 name_hash = HashStr32(name);

	for (uint32 i = 0; i < mBusCount; i++) {
		if (mBuses[i].NameHash == name_hash) {
			return i;
		}
	}

	return UINT32_MAX;
}

uint32 AudioSystem::FindSnapshot(Hash32 name_hash) const
{
	for (uint32 i = 0; i < mSnapshotCount; i++) {
		if (mSnapshots[i].NameHash == name_hash) {
			return i;
		}
	}

	return UINT32_MAX;
}

void AudioSystem::LoadMix()
{
	// Snapshots started by gameplay stay started across a reload
	Hash32 active_names[scMaxSnapshots];
	uint32 active_counts[scMaxSnapshots];
	float32 active_weights[scMaxSnapshots];
	uint32 active_count = 0;

	for (uint32 i = 0; i < mSnapshotCount; i++) {
		if (mSnapshots[i].ActiveCount == 0 && mSnapshots[i].Weight <= 0.0f) {
			continue;
		}

		active_names[active_count] = mSnapshots[i].NameHash;
		active_counts[active_count] = mSnapshots[i].ActiveCount;
		active_weights[active_count] = mSnapshots[i].Weight;
		++active_count;
	}

	DestroyMixBuses();

	mDuckCount = 0;
	mSnapshotCount = 0;

	for (uint32 i = 0; i < scRootBusCount; i++) {
		mBuses[i].VolumeDb = 0.0f;
		mBuses[i].SnapshotDb = 0.0f;
		mBuses[i].DuckDb = 0.0f;
		mBuses[i].Lowpass = scLowpassOpen;
	}

	if (!FilesystemIO::FileExists(FilesystemIO::ResolvePath(scMixPath))) {
		LogInfo(LC_AUDIO, "No mix file at '{}', only the root buses exist", scMixPath);
		return;
	}

	ConfigFile config;
	config.Load(scMixPath);

	if (config.HasErrors()) {
		LogError(LC_AUDIO, "Could not parse the mix '{}'", scMixPath);
		return;
	}

	const ConfigEntry* buses = config.GetEntry(HashStr32("buses"));

	if (buses != nullptr && buses->Members.IsInited()) {
		LoadMixBuses(*buses);
	}

	const ConfigEntry* ducking = config.GetEntry(HashStr32("ducking"));

	if (ducking != nullptr && ducking->Members.IsInited()) {
		LoadMixDucks(*ducking);
	}

	const ConfigEntry* snapshots = config.GetEntry(HashStr32("snapshots"));

	if (snapshots != nullptr && snapshots->Members.IsInited()) {
		LoadMixSnapshots(*snapshots);
	}

	for (uint32 i = 0; i < active_count; i++) {
		const uint32 index = FindSnapshot(active_names[i]);

		if (index != UINT32_MAX) {
			mSnapshots[index].ActiveCount = active_counts[i];
			mSnapshots[index].Weight = active_weights[i];
		}
	}

	LogInfo(LC_AUDIO, "Loaded mix: {} buses, {} ducks, {} snapshots", mBusCount, mDuckCount, mSnapshotCount);
}

void AudioSystem::LoadMixBuses(const ConfigEntry& buses)
{
	for (const ConfigEntry& entry : buses.Members) {
		const String& name = entry.Name.Get();

		if (FindBus(name.CStr()) != UINT32_MAX) {
			LogError(LC_AUDIO, "Mix bus '{}' is defined twice", name);
			continue;
		}

		if (mBusCount >= scMaxBuses) {
			LogError(LC_AUDIO, "The mix has more than {} buses, '{}' and later ones are ignored", scMaxBuses, name);
			return;
		}

		const char* parent_name = entry.GetMemberValue<const char*>(HashStr32("parent"), "sfx");
		uint32 parent = FindBus(parent_name);

		if (parent == UINT32_MAX) {
			LogError(LC_AUDIO, "Mix bus '{}' has parent '{}', which is not a root bus or a bus above it", name,
					 parent_name);
			parent = static_cast<uint32>(eSoundBus::Sfx);
		}

		const uint32 index = mBusCount;

		MixBus& bus = mBuses[index];
		bus = MixBus {};
		bus.Name = name;
		bus.NameHash = HashStr32(name.CStr());
		bus.Parent = parent;
		bus.VolumeDb = entry.GetMemberValue<float32>(HashStr32("volume_db"), 0.0f);
		bus.Lowpass = scLowpassOpen;

		if (!CreateBusNodes(index)) {
			continue;
		}

		++mBusCount;
	}
}

void AudioSystem::LoadMixDucks(const ConfigEntry& ducks)
{
	for (const ConfigEntry& entry : ducks.Members) {
		const String& name = entry.Name.Get();

		if (mDuckCount >= scMaxDucks) {
			LogError(LC_AUDIO, "The mix has more than {} ducks, '{}' and later ones are ignored", scMaxDucks, name);
			return;
		}

		const char* trigger_name = entry.GetMemberValue<const char*>(HashStr32("trigger"), "");
		const char* target_name = entry.GetMemberValue<const char*>(HashStr32("target"), "");

		const uint32 trigger = FindBus(trigger_name);
		const uint32 target = FindBus(target_name);

		if (trigger == UINT32_MAX || target == UINT32_MAX) {
			LogError(LC_AUDIO, "Duck '{}' needs a trigger and target bus ('{}' -> '{}')", name, trigger_name,
					 target_name);
			continue;
		}

		// The trigger meter includes everything under it, so ducking something inside the trigger would duck itself
		bool target_inside_trigger = false;

		for (uint32 bus = target; bus != UINT32_MAX; bus = mBuses[bus].Parent) {
			if (bus == trigger) {
				target_inside_trigger = true;
				break;
			}
		}

		if (target_inside_trigger) {
			LogError(LC_AUDIO, "Duck '{}' targets '{}', which is the trigger '{}' or inside it", name, target_name,
					 trigger_name);
			continue;
		}

		MixDuck& duck = mDucks[mDuckCount++];
		duck = MixDuck {};
		duck.Name = name;
		duck.Trigger = trigger;
		duck.Target = target;
		duck.DepthDb = std::min(entry.GetMemberValue<float32>(HashStr32("depth_db"), duck.DepthDb), 0.0f);
		duck.ThresholdDb = entry.GetMemberValue<float32>(HashStr32("threshold_db"), duck.ThresholdDb);
		duck.Attack = std::max(entry.GetMemberValue<float32>(HashStr32("attack"), duck.Attack), 0.001f);
		duck.Release = std::max(entry.GetMemberValue<float32>(HashStr32("release"), duck.Release), 0.001f);
	}
}

void AudioSystem::LoadMixSnapshots(const ConfigEntry& snapshots)
{
	const Hash32 fade_hash = HashStr32("fade");
	const Hash32 fade_in_hash = HashStr32("fade_in");
	const Hash32 fade_out_hash = HashStr32("fade_out");

	for (const ConfigEntry& entry : snapshots.Members) {
		const String& name = entry.Name.Get();

		if (mSnapshotCount >= scMaxSnapshots) {
			LogError(LC_AUDIO, "The mix has more than {} snapshots, '{}' and later ones are ignored", scMaxSnapshots,
					 name);
			return;
		}

		MixSnapshot& snapshot = mSnapshots[mSnapshotCount++];
		snapshot = MixSnapshot {};
		snapshot.Name = name;
		snapshot.NameHash = HashStr32(name.CStr());

		const float32 fade = std::max(entry.GetMemberValue<float32>(fade_hash, snapshot.FadeIn), 0.0f);

		snapshot.FadeIn = std::max(entry.GetMemberValue<float32>(fade_in_hash, fade), 0.0f);
		snapshot.FadeOut = std::max(entry.GetMemberValue<float32>(fade_out_hash, fade), 0.0f);

		if (!entry.Members.IsInited()) {
			continue;
		}

		for (const ConfigEntry& member : entry.Members) {
			const Hash32 member_hash = member.Name.GetHash();

			if (member_hash == fade_hash || member_hash == fade_in_hash || member_hash == fade_out_hash) {
				continue;
			}

			const uint32 bus = FindBus(member.Name.Get().CStr());

			if (bus == UINT32_MAX) {
				LogError(LC_AUDIO, "Snapshot '{}' sets bus '{}', which is not in the mix", name, member.Name.Get());
				continue;
			}

			MixSnapshotBus& settings = snapshot.Buses[bus];
			settings.bUsed = true;

			// Either `bus = -6` or `bus = { volume_db = -6; lowpass = 800 }`
			if (member.Members.IsInited()) {
				settings.VolumeDb = member.GetMemberValue<float32>(HashStr32("volume_db"), 0.0f);
				settings.Lowpass = std::max(member.GetMemberValue<float32>(HashStr32("lowpass"), 0.0f), 0.0f);
			}
			else {
				settings.VolumeDb = member.GetValue<float32>();
			}
		}
	}
}

void AudioSystem::StartSnapshot(const char* name)
{
	const uint32 index = FindSnapshot(HashStr32(name));

	if (index == UINT32_MAX) {
		LogWarning(LC_AUDIO, "No mix snapshot named '{}'", name);
		return;
	}

	++mSnapshots[index].ActiveCount;
}

void AudioSystem::StopSnapshot(const char* name)
{
	const uint32 index = FindSnapshot(HashStr32(name));

	if (index == UINT32_MAX || mSnapshots[index].ActiveCount == 0) {
		return;
	}

	--mSnapshots[index].ActiveCount;
}

bool AudioSystem::IsSnapshotActive(const char* name) const
{
	const uint32 index = FindSnapshot(HashStr32(name));

	return index != UINT32_MAX && mSnapshots[index].ActiveCount > 0;
}

uint32 AudioSystem::FindDefIndex(Hash32 name_hash) const
{
	for (uint32 i = 0; i < mDefs.Size; i++) {
		if (mDefs[i].NameHash == name_hash) {
			return i;
		}
	}

	return UINT32_MAX;
}

const SoundDef* AudioSystem::FindDef(Hash32 name_hash) const
{
	const uint32 index = FindDefIndex(name_hash);

	if (index == UINT32_MAX) {
		return nullptr;
	}

	return &mDefs[index];
}

SoundID AudioSystem::Play(const char* name, const FVec3 position) { return Play(HashStr32(name), position); }

SoundID AudioSystem::Play(Hash32 name_hash, const FVec3 position) { return StartVoice(name_hash, position, true); }

SoundID AudioSystem::PlayDelayed(Hash32 name_hash, const FVec3 position, float32 delay)
{
	return StartVoice(name_hash, position, true, delay);
}

SoundID AudioSystem::Play2D(const char* name) { return Play2D(HashStr32(name)); }

Hash32 AudioSystem::FindSurfaceSound(const char* event, const String& surface) const
{
	if (!surface.IsEmpty()) {
		const Hash32 surface_sound = HashStr32(String::Fmt("{}_{}", event, surface.CStr()).CStr());

		if (FindDefIndex(surface_sound) != UINT32_MAX) {
			return surface_sound;
		}
	}

	const Hash32 default_sound = HashStr32(String::Fmt("{}_default", event).CStr());

	if (FindDefIndex(default_sound) != UINT32_MAX) {
		return default_sound;
	}

	return HashStr32(event);
}

SoundID AudioSystem::PlayForSurface(const char* event, const String& surface, const FVec3 position)
{
	if (mpBackend == nullptr) {
		return {};
	}

	return Play(FindSurfaceSound(event, surface), position);
}

SoundID AudioSystem::PlayForSurface2D(const char* event, const String& surface)
{
	if (mpBackend == nullptr) {
		return {};
	}

	return Play2D(FindSurfaceSound(event, surface));
}

SoundID AudioSystem::Play2D(Hash32 name_hash) { return StartVoice(name_hash, FVec3::sZero, false); }

uint32 AudioSystem::AcquireVoice(uint32 def_index)
{
	const SoundDef& def = mDefs[def_index];

	if (def.MaxInstances > 0) {
		uint32 instance_count = 0;
		uint32 oldest_slot = UINT32_MAX;

		for (uint32 i = 0; i < scMaxVoices; i++) {
			const Voice& voice = mVoices[i];

			if (!voice.bActive || voice.bStopping || voice.DefIndex != def_index) {
				continue;
			}

			++instance_count;

			if (oldest_slot == UINT32_MAX || voice.StartOrder < mVoices[oldest_slot].StartOrder) {
				oldest_slot = i;
			}
		}

		if (instance_count >= def.MaxInstances) {
			FadeOutVoice(oldest_slot, scDeclickSeconds);
		}
	}

	for (uint32 i = 0; i < scMaxVoices; i++) {
		if (!mVoices[i].bActive) {
			return i;
		}
	}

	// Every slot is taken: cut a voice that is already fading out, else the oldest one-shot
	uint32 steal_slot = UINT32_MAX;

	for (uint32 i = 0; i < scMaxVoices; i++) {
		const Voice& voice = mVoices[i];

		if (mDefs[voice.DefIndex].bLoop && !voice.bStopping) {
			continue;
		}

		if (steal_slot == UINT32_MAX) {
			steal_slot = i;
			continue;
		}

		const Voice& steal = mVoices[steal_slot];

		if (voice.bStopping != steal.bStopping) {
			if (voice.bStopping) {
				steal_slot = i;
			}

			continue;
		}

		if (voice.StartOrder < steal.StartOrder) {
			steal_slot = i;
		}
	}

	if (steal_slot != UINT32_MAX) {
		ReleaseVoice(steal_slot);
		++mStolenVoiceCount;
	}

	return steal_slot;
}

SoundID AudioSystem::StartVoice(Hash32 name_hash, const FVec3 position, bool spatial, float32 delay)
{
	if (mpBackend == nullptr) {
		return {};
	}

	const uint32 def_index = FindDefIndex(name_hash);

	if (def_index == UINT32_MAX) {
		return {};
	}

	const SoundDef& def = mDefs[def_index];
	const uint32 clip_count = std::max<uint32>(def.Clips.Size, 1);
	const uint32 variant_count = def.Files.Size * clip_count;

	uint32 variant = FastRand32() % variant_count;

	if (variant_count > 1 && variant == mpBackend->LastVariants[def_index]) {
		variant = (variant + 1) % variant_count;
	}

	const uint32 file_index = variant / clip_count;
	const SoundClip* clip = nullptr;

	if (def.Clips.IsNotEmpty()) {
		clip = &def.Clips[variant % clip_count];
	}

	ma_sound* prototype = nullptr;

	if (!def.bStream) {
		prototype = mpBackend->Prototypes[def_index][file_index];

		ma_result load_result = MA_ERROR;

		if (prototype != nullptr) {
			load_result = ma_resource_manager_data_source_result(prototype->pResourceManagerDataSource);
		}

		if (load_result != MA_SUCCESS && load_result != MA_BUSY) {
			if (!mpBackend->WarnedAboutFailedLoads[def_index]) {
				LogError(LC_AUDIO, "Sound '{}' could not load '{}'", def.Name, def.Files[file_index]);
				mpBackend->WarnedAboutFailedLoads[def_index] = true;
			}

			return {};
		}

		if (clip != nullptr &&
			ma_sound_get_data_format(prototype, nullptr, nullptr, nullptr, nullptr, 0) != MA_SUCCESS) {
			return {};
		}
	}

	const uint32 slot = AcquireVoice(def_index);

	if (slot == UINT32_MAX) {
		++mDroppedVoiceCount;

		if (!mbWarnedAboutVoiceOverflow) {
			LogWarning(LC_AUDIO, "Too many looping sounds playing ({}), dropping new ones", scMaxVoices);
			mbWarnedAboutVoiceOverflow = true;
		}

		return {};
	}

	ma_uint32 flags = 0;

	if (!spatial || !def.bSpatial) {
		flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
	}

	if (def.Pitch.Min == 1.0f && def.Pitch.Max == 1.0f) {
		flags |= MA_SOUND_FLAG_NO_PITCH;
	}

	ma_sound* sound = &mpBackend->Voices[slot];
	ma_sound_group* bus = &mpBackend->Buses[static_cast<uint32>(def.Bus)];
	ma_sound_group* attachment = bus;

	SpatialVoice* spatial_voice = nullptr;

	if ((flags & MA_SOUND_FLAG_NO_SPATIALIZATION) == 0 && mSpatial.IsActive() && mpSpatialCVar->IntValue != 0) {
		spatial_voice = mSpatial.CreateVoice(reinterpret_cast<ma_node_base*>(bus));
	}

	if (spatial_voice != nullptr) {
		flags |= MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
		attachment = nullptr;
	}

	ReverbSend* reverb_send = nullptr;

	if (spatial_voice == nullptr && (flags & MA_SOUND_FLAG_NO_SPATIALIZATION) != 0 && def.Reverb > 0.0f &&
		mSpatial.IsActive()) {
		reverb_send = mSpatial.CreateReverbSend(reinterpret_cast<ma_node_base*>(bus), def.Reverb);
	}

	if (reverb_send != nullptr) {
		flags |= MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
		attachment = nullptr;
	}

	ma_result result = MA_SUCCESS;

	if (def.bStream) {
		ma_uint32 stream_flags = flags | MA_SOUND_FLAG_STREAM;

		if (clip == nullptr) {
			stream_flags |= MA_SOUND_FLAG_ASYNC;
		}

		result = ma_sound_init_from_file(&mpBackend->Engine, def.Files[file_index].CStr(), stream_flags, attachment,
										 nullptr, sound);
	}
	else {
		result = ma_sound_init_copy(&mpBackend->Engine, prototype, flags, attachment, sound);
	}

	if (result != MA_SUCCESS) {
		LogError(LC_AUDIO, "Could not start sound '{}' ({})", def.Name, ma_result_description(result));
		mSpatial.DestroyVoice(spatial_voice);
		mSpatial.DestroyReverbSend(reverb_send);
		return {};
	}

	if (clip != nullptr) {
		result = ApplyClip(sound, *clip);

		if (result != MA_SUCCESS) {
			LogError(LC_AUDIO, "Could not set clip [{}, {}] on sound '{}' ({})", clip->Start, clip->End, def.Name,
					 ma_result_description(result));
			ma_sound_uninit(sound);
			mSpatial.DestroyVoice(spatial_voice);
			mSpatial.DestroyReverbSend(reverb_send);
			return {};
		}
	}

	if (spatial_voice != nullptr) {
		ma_node_attach_output_bus(sound, 0, mSpatial.GetVoiceInput(spatial_voice), 0);
	}

	if (reverb_send != nullptr) {
		ma_node_attach_output_bus(sound, 0, mSpatial.GetReverbSendInput(reverb_send), 0);
	}

	mpBackend->LastVariants[def_index] = variant;

	Voice& voice = mVoices[slot];
	voice.DefIndex = def_index;
	voice.StartOrder = mNextStartOrder++;
	voice.Volume = RandomInRange(def.Volume);
	voice.UserVolume = 1.0f;
	voice.Position = position;
	voice.bSpatial = (flags & MA_SOUND_FLAG_NO_SPATIALIZATION) == 0 || spatial_voice != nullptr;
	voice.pSpatial = spatial_voice;
	voice.pReverbSend = reverb_send;
	voice.bActive = true;
	voice.bStopping = false;
	voice.bPendingStart = false;

	++mActiveVoiceCount;

	if (spatial_voice != nullptr) {
		UpdateSpatialVoice(slot);
	}

	ma_sound_set_volume(sound, voice.Volume);
	ma_sound_set_pitch(sound, RandomInRange(def.Pitch));
	ma_sound_set_looping(sound, def.bLoop);

	if ((flags & MA_SOUND_FLAG_NO_SPATIALIZATION) == 0) {
		const FVec3 audio_position = ToAudioSpace(position);

		ma_sound_set_position(sound, audio_position.X, audio_position.Y, audio_position.Z);
		ma_sound_set_attenuation_model(sound, ToAttenuationModel(def.Attenuation));
		ma_sound_set_min_distance(sound, def.MinDistance);
		ma_sound_set_max_distance(sound, def.MaxDistance);
		ma_sound_set_rolloff(sound, def.Rolloff);
		ma_sound_set_doppler_factor(sound, 0.0f);
	}

	if (def.FadeIn > 0.0f) {
		ma_sound_set_fade_in_milliseconds(sound, 0.0f, 1.0f, static_cast<ma_uint64>(def.FadeIn * 1000.0f));
	}

	if (delay > 0.0f) {
		const ma_uint64 delay_frames = static_cast<ma_uint64>(
			delay * static_cast<float32>(ma_engine_get_sample_rate(&mpBackend->Engine)));

		ma_sound_set_start_time_in_pcm_frames(sound,
											  ma_engine_get_time_in_pcm_frames(&mpBackend->Engine) + delay_frames);
	}

	// Spatial voices start in Update, once occlusion (and pathing on baked maps) has been simulated for them
	if (spatial_voice != nullptr) {
		voice.bPendingStart = true;
		voice.RequestFrame = ma_engine_get_time_in_pcm_frames(&mpBackend->Engine);
	}
	else {
		ma_sound_start(sound);
	}

	return SoundID { .Slot = slot, .Generation = voice.Generation };
}

AudioSystem::Voice* AudioSystem::GetVoice(SoundID id)
{
	if (mpBackend == nullptr || id.Slot >= scMaxVoices) {
		return nullptr;
	}

	Voice& voice = mVoices[id.Slot];

	if (!voice.bActive || voice.Generation != id.Generation) {
		return nullptr;
	}

	return &voice;
}

const AudioSystem::Voice* AudioSystem::GetVoice(SoundID id) const
{
	return const_cast<AudioSystem*>(this)->GetVoice(id);
}

void AudioSystem::ReleaseVoice(uint32 slot)
{
	Voice& voice = mVoices[slot];

	if (!voice.bActive) {
		return;
	}

	ma_sound_uninit(&mpBackend->Voices[slot]);

	mSpatial.DestroyVoice(voice.pSpatial);
	voice.pSpatial = nullptr;

	mSpatial.DestroyReverbSend(voice.pReverbSend);
	voice.pReverbSend = nullptr;

	voice.bActive = false;
	voice.bStopping = false;
	voice.bPendingStart = false;
	++voice.Generation;

	--mActiveVoiceCount;
}

void AudioSystem::FadeOutVoice(uint32 slot, float32 seconds)
{
	Voice& voice = mVoices[slot];

	if (voice.bPendingStart) {
		ReleaseVoice(slot);
		return;
	}

	const ma_uint64 fade_frames = static_cast<ma_uint64>(seconds * static_cast<float32>(GetSampleRate()));

	ma_sound_stop_with_fade_in_pcm_frames(&mpBackend->Voices[slot], std::max<ma_uint64>(fade_frames, 1));
	voice.bStopping = true;
}

void AudioSystem::StartReadyVoices()
{
	constexpr float32 scMaxStartDelaySeconds = 0.05f;

	const ma_uint64 now = ma_engine_get_time_in_pcm_frames(&mpBackend->Engine);
	const ma_uint64 max_wait = static_cast<ma_uint64>(scMaxStartDelaySeconds * static_cast<float32>(GetSampleRate()));

	for (uint32 i = 0; i < scMaxVoices; i++) {
		Voice& voice = mVoices[i];

		if (!voice.bActive || !voice.bPendingStart) {
			continue;
		}

		if (!mSpatial.IsVoiceReady(voice.pSpatial) && now - voice.RequestFrame < max_wait) {
			continue;
		}

		mSpatial.SetVoiceStarted(voice.pSpatial);
		ma_sound_start(&mpBackend->Voices[i]);
		voice.bPendingStart = false;
	}
}

void AudioSystem::SetPosition(SoundID id, const FVec3 position)
{
	Voice* voice = GetVoice(id);

	if (voice == nullptr) {
		return;
	}

	voice->Position = position;

	if (voice->pSpatial != nullptr) {
		UpdateSpatialVoice(id.Slot);
		return;
	}

	const FVec3 audio_position = ToAudioSpace(position);

	ma_sound_set_position(&mpBackend->Voices[id.Slot], audio_position.X, audio_position.Y, audio_position.Z);
}

void AudioSystem::SetVolume(SoundID id, float32 volume)
{
	Voice* voice = GetVoice(id);

	if (voice == nullptr) {
		return;
	}

	voice->UserVolume = std::max(volume, 0.0f);

	ma_sound_set_volume(&mpBackend->Voices[id.Slot], voice->Volume * voice->UserVolume);
}

void AudioSystem::Stop(SoundID id)
{
	Voice* voice = GetVoice(id);

	if (voice == nullptr || voice->bStopping) {
		return;
	}

	const SoundDef& def = mDefs[voice->DefIndex];

	FadeOutVoice(id.Slot, std::max(def.FadeOut, scDeclickSeconds));
}

void AudioSystem::Kill(SoundID id)
{
	if (GetVoice(id) == nullptr) {
		return;
	}

	ReleaseVoice(id.Slot);
}

bool AudioSystem::IsPlaying(SoundID id) const { return GetVoice(id) != nullptr; }

void AudioSystem::StopAll()
{
	if (mpBackend == nullptr) {
		return;
	}

	for (uint32 i = 0; i < scMaxVoices; i++) {
		ReleaseVoice(i);
	}
}

void AudioSystem::SetListener(const FVec3 position, const FVec3 forward, const FVec3 up)
{
	if (mpBackend == nullptr) {
		return;
	}

	mListenerPosition = position;

	const FVec3 audio_position = ToAudioSpace(position);
	const FVec3 audio_forward = ToAudioSpace(forward);
	const FVec3 audio_up = ToAudioSpace(up);

	mSpatial.SetListener(audio_position, audio_forward, audio_up);

	ma_engine_listener_set_position(&mpBackend->Engine, 0, audio_position.X, audio_position.Y, audio_position.Z);
	ma_engine_listener_set_direction(&mpBackend->Engine, 0, audio_forward.X, audio_forward.Y, audio_forward.Z);
	ma_engine_listener_set_world_up(&mpBackend->Engine, 0, audio_up.X, audio_up.Y, audio_up.Z);
}

void AudioSystem::UpdateMix(float32 delta_time, bool window_focused)
{
	float32 master_volume = std::max(mpVolumeCVar->FloatValue, 0.0f);

	if (mpMuteUnfocusedCVar->IntValue != 0 && !window_focused) {
		master_volume = 0.0f;
	}

	ma_engine_set_volume(&mpBackend->Engine, master_volume);

	for (uint32 i = 0; i < mSnapshotCount; i++) {
		MixSnapshot& snapshot = mSnapshots[i];

		if (snapshot.ActiveCount > 0) {
			if (snapshot.FadeIn <= 0.0f) {
				snapshot.Weight = 1.0f;
			}
			else {
				snapshot.Weight = std::min(snapshot.Weight + delta_time / snapshot.FadeIn, 1.0f);
			}
		}
		else {
			if (snapshot.FadeOut <= 0.0f) {
				snapshot.Weight = 0.0f;
			}
			else {
				snapshot.Weight = std::max(snapshot.Weight - delta_time / snapshot.FadeOut, 0.0f);
			}
		}
	}

	const float32 open_log = std::log(scLowpassOpen);

	for (uint32 i = 0; i < mBusCount; i++) {
		MixBus& bus = mBuses[i];

		bus.SnapshotDb = 0.0f;
		bus.DuckDb = 0.0f;
		bus.Lowpass = scLowpassOpen;

		for (uint32 j = 0; j < mSnapshotCount; j++) {
			const MixSnapshot& snapshot = mSnapshots[j];
			const MixSnapshotBus& settings = snapshot.Buses[i];

			if (snapshot.Weight <= 0.0f || !settings.bUsed) {
				continue;
			}

			bus.SnapshotDb += settings.VolumeDb * snapshot.Weight;

			// Cutoffs blend in octaves, so a half-faded 500 Hz filter sits near 3 kHz rather than 10 kHz
			if (settings.Lowpass > 0.0f) {
				const float32 cutoff_log = std::log(std::min(settings.Lowpass, scLowpassOpen));
				const float32 cutoff = std::exp(open_log + (cutoff_log - open_log) * snapshot.Weight);

				bus.Lowpass = std::min(bus.Lowpass, cutoff);
			}
		}
	}

	for (uint32 i = 0; i < mDuckCount; i++) {
		MixDuck& duck = mDucks[i];

		const float32 level = mpBackend->BusNodes[duck.Trigger].Meter.GetPeakDb();
		float32 target = 0.0f;

		if (level > duck.ThresholdDb) {
			target = 1.0f;
		}

		float32 time = duck.Release;

		if (target > duck.Envelope) {
			time = duck.Attack;
		}

		duck.Envelope += (target - duck.Envelope) * (1.0f - std::exp(-delta_time / time));

		MixBus& bus = mBuses[duck.Target];
		bus.DuckDb = std::min(bus.DuckDb, duck.Envelope * duck.DepthDb);
	}

	for (uint32 i = 0; i < mBusCount; i++) {
		MixBus& bus = mBuses[i];

		float32 gain = AudioMeter::FromDb(bus.VolumeDb + bus.SnapshotDb + bus.DuckDb);

		if (i < scRootBusCount) {
			gain *= std::max(mpBusVolumeCVars[i]->FloatValue, 0.0f);
		}

		bus.Gain = gain;

		BusNode& bus_node = mpBackend->BusNodes[i];
		bus_node.TargetGain.store(gain, std::memory_order_relaxed);
		bus_node.Lowpass.store(bus.Lowpass, std::memory_order_relaxed);
	}
}

void AudioSystem::Update(float32 delta_time, bool window_focused)
{
	constexpr float32 scMaxUpdateStepSeconds = 0.1f;

	if (mpBackend == nullptr) {
		return;
	}

	UpdateMix(std::clamp(delta_time, 0.0f, scMaxUpdateStepSeconds), window_focused);

	for (uint32 i = 0; i < scMaxVoices; i++) {
		const Voice& voice = mVoices[i];

		if (!voice.bActive) {
			continue;
		}

		const ma_sound* sound = &mpBackend->Voices[i];

		if (ma_sound_at_end(sound) || (voice.bStopping && !ma_sound_is_playing(sound))) {
			ReleaseVoice(i);
			continue;
		}

		if (voice.pSpatial != nullptr) {
			UpdateSpatialVoice(i);
		}
	}

	mSpatial.SetReverb(mpReverbCVar->IntValue != 0, mpReverbVolumeCVar->FloatValue, mpHrtfCVar->IntValue != 0);
	mSpatial.SetReverbQuality(static_cast<int32>(mpReverbRaysCVar->IntValue),
							  static_cast<int32>(mpReverbBouncesCVar->IntValue));
	mSpatial.Simulate();

	StartReadyVoices();
}

float32 AudioSystem::GetDistanceGain(const SoundDef& def, float32 distance) const
{
	if (def.Attenuation == eSoundAttenuation::None) {
		return 1.0f;
	}

	// Rescale the curve so it reaches silence at max_distance instead of holding its far value forever
	const float32 far_gain = GetCurveGain(def, def.MaxDistance);
	const float32 gain = GetCurveGain(def, distance);

	if (far_gain >= 1.0f) {
		return gain;
	}

	return std::max((gain - far_gain) / (1.0f - far_gain), 0.0f);
}

float32 AudioSystem::GetCurveGain(const SoundDef& def, float32 distance) const
{
	const float32 clamped = std::clamp(distance, def.MinDistance, def.MaxDistance);

	switch (def.Attenuation) {
	case eSoundAttenuation::None:
		return 1.0f;
	case eSoundAttenuation::Linear:
		if (def.MaxDistance <= def.MinDistance) {
			return 1.0f;
		}

		return std::clamp(1.0f - def.Rolloff * (clamped - def.MinDistance) / (def.MaxDistance - def.MinDistance), 0.0f,
						  1.0f);
	case eSoundAttenuation::Exponential:
		return std::pow(clamped / def.MinDistance, -def.Rolloff);
	case eSoundAttenuation::Inverse:
		break;
	}

	return def.MinDistance / (def.MinDistance + def.Rolloff * (clamped - def.MinDistance));
}

void AudioSystem::UpdateSpatialVoice(uint32 slot)
{
	const Voice& voice = mVoices[slot];
	const SoundDef& def = mDefs[voice.DefIndex];

	float32 gain = GetDistanceGain(def, (voice.Position - mListenerPosition).Length());

	if (mPathGain) {
		gain *= mPathGain(voice.Position, mListenerPosition);
	}

	mSpatial.SetVoiceSource(voice.pSpatial, ToAudioSpace(voice.Position), gain, mpHrtfCVar->IntValue != 0);
}

void AudioSystem::SetAcousticScene(const AcousticGeometry& geometry)
{
	if (!mSpatial.IsActive()) {
		return;
	}

	mSpatial.SetScene(Acoustics::ToAudioSpace(geometry));
}

void AudioSystem::SetBakedAcoustics(const SizedArray<uint8>& data)
{
	if (!mSpatial.IsActive()) {
		return;
	}

	mSpatial.SetBakedProbes(data);
}

bool AudioSystem::GetBusDebug(uint32 index, AudioBusDebug& out) const
{
	if (mpBackend == nullptr || index >= mBusCount) {
		return false;
	}

	const MixBus& bus = mBuses[index];
	const BusNode& bus_node = mpBackend->BusNodes[index];

	out.Name = bus.Name.CStr();
	out.Parent = bus.Parent;
	out.GainDb = AudioMeter::ToDb(bus.Gain);
	out.SnapshotDb = bus.SnapshotDb;
	out.DuckDb = bus.DuckDb;
	out.Lowpass = bus.Lowpass;
	out.PeakDb = bus_node.Meter.GetPeakDb();
	out.RmsDb = bus_node.Meter.GetRmsDb();

	return true;
}

bool AudioSystem::GetSnapshotDebug(uint32 index, AudioSnapshotDebug& out) const
{
	if (index >= mSnapshotCount) {
		return false;
	}

	out.Name = mSnapshots[index].Name.CStr();
	out.Weight = mSnapshots[index].Weight;
	out.ActiveCount = mSnapshots[index].ActiveCount;

	return true;
}

uint32 AudioSystem::GetVoiceDebug(AudioVoiceDebug* out, uint32 max_count) const
{
	if (mpBackend == nullptr) {
		return 0;
	}

	uint32 count = 0;

	for (uint32 i = 0; i < scMaxVoices && count < max_count; i++) {
		const Voice& voice = mVoices[i];

		if (!voice.bActive) {
			continue;
		}

		const SoundDef& def = mDefs[voice.DefIndex];

		AudioVoiceDebug& entry = out[count++];
		entry = AudioVoiceDebug {};
		entry.Name = def.Name.CStr();
		entry.Bus = mBuses[def.Bus].Name.CStr();
		entry.Position = voice.Position;
		entry.Volume = voice.Volume * voice.UserVolume;
		entry.bSpatial = voice.bSpatial;
		entry.bSteam = voice.pSpatial != nullptr;
		entry.bPending = voice.bPendingStart;
		entry.bStopping = voice.bStopping;
		entry.bLoop = def.bLoop;

		if (voice.bSpatial) {
			entry.Distance = (voice.Position - mListenerPosition).Length();
			entry.DistanceGain = GetDistanceGain(def, entry.Distance);

			if (mPathGain) {
				entry.DistanceGain *= mPathGain(voice.Position, mListenerPosition);
			}
		}

		mSpatial.GetVoiceDebug(voice.pSpatial, entry.Spatial);
	}

	return count;
}

float32 AudioSystem::GetLimiterReductionDb() const
{
	if (mpBackend == nullptr || !mpBackend->Limiter.bNodeInitialized) {
		return 0.0f;
	}

	return AudioMeter::ToDb(mpBackend->Limiter.BlockMinGain.load(std::memory_order_relaxed));
}

float32 AudioSystem::GetMasterPeakDb() const
{
	if (mpBackend == nullptr) {
		return AudioMeter::scSilenceDb;
	}

	return mpBackend->Limiter.Meter.GetPeakDb();
}

uint64 AudioSystem::ReadFrames(float32* out_frames, uint64 frame_count)
{
	if (mpBackend == nullptr) {
		return 0;
	}

	ma_uint64 frames_read = 0;
	ma_engine_read_pcm_frames(&mpBackend->Engine, out_frames, frame_count, &frames_read);

	return frames_read;
}

uint32 AudioSystem::GetSampleRate() const
{
	if (mpBackend == nullptr) {
		return 0;
	}

	return ma_engine_get_sample_rate(&mpBackend->Engine);
}

uint32 AudioSystem::GetChannelCount() const
{
	if (mpBackend == nullptr) {
		return 0;
	}

	return ma_engine_get_channels(&mpBackend->Engine);
}

} // namespace fx

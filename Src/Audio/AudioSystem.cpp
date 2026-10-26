#include "AudioSystem.hpp"

#include <Asset/ConfigFile.hpp>
#include <CVar.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Core/Random.hpp>
#include <Engine.hpp>
#include <algorithm>
#include <cstring>
#include <miniaudio.h>
#include <string>

namespace fx {

FX_SET_MODULE_NAME("AudioSystem")

namespace {

constexpr uint32 scBusCount = static_cast<uint32>(eSoundBus::Count);
constexpr uint32 scNoDeviceSampleRate = 48000;
constexpr uint32 scNoDeviceChannels = 2;

constexpr const char* scBusVolumeCVarNames[scBusCount] = {
	"s_volume_sfx",
	"s_volume_music",
	"s_volume_ui",
	"s_volume_voice",
};

float32 RandomUnit() { return static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f); }

float32 RandomInRange(const SoundRange& range) { return range.Min + (range.Max - range.Min) * RandomUnit(); }

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

eSoundBus BusFromName(const char* name)
{
	if (strcmp(name, "music") == 0) {
		return eSoundBus::Music;
	}

	if (strcmp(name, "ui") == 0) {
		return eSoundBus::Ui;
	}

	if (strcmp(name, "voice") == 0) {
		return eSoundBus::Voice;
	}

	return eSoundBus::Sfx;
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

Vec3f ToAudioSpace(const Vec3f value) { return Vec3f(value.X, value.Y, -value.Z); }

} // namespace

struct AudioSystem::Backend
{
	ma_engine Engine;
	ma_sound_group Buses[scBusCount];
	ma_sound Voices[scMaxVoices];

	std::vector<std::vector<ma_sound*>> Prototypes;
	std::vector<uint32> LastVariants;
	std::vector<bool> WarnedAboutFailedLoads;
};

bool AudioSystem::Create(const AudioCreateInfo& info)
{
	if (mpBackend != nullptr) {
		return true;
	}

	mpBackend = new Backend;

	ma_engine_config config = ma_engine_config_init();
	config.listenerCount = 1;
	config.sampleRate = info.SampleRate;
	config.channels = info.Channels;

	if (info.bNoDevice) {
		config.noDevice = MA_TRUE;

		if (config.sampleRate == 0) {
			config.sampleRate = scNoDeviceSampleRate;
		}

		if (config.channels == 0) {
			config.channels = scNoDeviceChannels;
		}
	}

	const ma_result result = ma_engine_init(&config, &mpBackend->Engine);

	if (result != MA_SUCCESS) {
		LogError(LC_AUDIO, "Could not start the audio engine ({}), sound is disabled", ma_result_description(result));

		delete mpBackend;
		mpBackend = nullptr;

		return false;
	}

	for (uint32 i = 0; i < scBusCount; i++) {
		ma_sound_group_init(&mpBackend->Engine, 0, nullptr, &mpBackend->Buses[i]);
	}

	mpVolumeCVar = gCVars->Set("s_volume", 1.0f);
	mpMuteUnfocusedCVar = gCVars->Set("s_mute_unfocused", 1);

	for (uint32 i = 0; i < scBusCount; i++) {
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

	for (uint32 i = 0; i < scBusCount; i++) {
		ma_sound_group_uninit(&mpBackend->Buses[i]);
	}

	ma_engine_uninit(&mpBackend->Engine);

	delete mpBackend;
	mpBackend = nullptr;

	mDefs.clear();
}

void AudioSystem::LoadSounds()
{
	if (mpBackend == nullptr) {
		return;
	}

	StopAll();
	UnloadDefs();
	mDefs.clear();

	const String directory = FilesystemIO::ResolvePath(scSoundDirectory);

	if (!FilesystemIO::FileExists(directory)) {
		LogWarning(LC_AUDIO, "Sound directory '{}' does not exist", directory);
		return;
	}

	PagedArray<std::string> files = FilesystemIO::DirListIfHasExtension(directory.CStr(), ".conf", false);

	for (const std::string& file : files) {
		const std::string name = file.substr(0, file.size() - strlen(".conf"));

		SoundDef def;

		if (LoadDef((std::string(scSoundDirectory) + "/" + file).c_str(), String(name.c_str()), def)) {
			mDefs.push_back(std::move(def));
		}
	}

	mpBackend->Prototypes.resize(mDefs.size());
	mpBackend->LastVariants.assign(mDefs.size(), UINT32_MAX);
	mpBackend->WarnedAboutFailedLoads.assign(mDefs.size(), false);

	constexpr ma_uint32 scPrototypeFlags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC |
										   MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;

	for (uint32 i = 0; i < mDefs.size(); i++) {
		const SoundDef& def = mDefs[i];

		if (def.bStream) {
			continue;
		}

		for (const String& path : def.Files) {
			ma_sound* prototype = new ma_sound;

			const ma_result result = ma_sound_init_from_file(&mpBackend->Engine, path.CStr(), scPrototypeFlags,
															 nullptr, nullptr, prototype);

			if (result != MA_SUCCESS) {
				LogError(LC_AUDIO, "Could not load sound file '{}' ({})", path, ma_result_description(result));

				delete prototype;
				prototype = nullptr;
			}

			mpBackend->Prototypes[i].push_back(prototype);
		}
	}

	LogInfo(LC_AUDIO, "Loaded {} sounds", mDefs.size());
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

	std::vector<const char*> file_names;

	const ConfigEntry* files = config.GetEntry(HashStr32("files"));

	if (files != nullptr && files->bIsArray && files->ArrayData.IsInited()) {
		for (uint32 i = 0; i < files->ArrayData.Size(); i++) {
			file_names.push_back(files->ArrayData[i].Get<const char*>());
		}
	}

	const ConfigEntry* file = config.GetEntry(HashStr32("file"));

	if (file != nullptr && !file->bIsArray) {
		file_names.push_back(file->GetValue<const char*>());
	}

	for (const char* file_name : file_names) {
		const String relative_path = String((std::string(scSoundDirectory) + "/" + file_name).c_str());
		const String resolved_path = FilesystemIO::ResolvePath(relative_path);

		if (!FilesystemIO::FileExists(resolved_path)) {
			LogError(LC_AUDIO, "Sound '{}' names a file that does not exist ('{}')", path, relative_path);
			continue;
		}

		out_def.Files.push_back(resolved_path);
	}

	if (out_def.Files.empty()) {
		LogError(LC_AUDIO, "Sound '{}' needs at least one file in 'file' or 'files'", path);
		return false;
	}

	out_def.Bus = BusFromName(Value<const char*>(config, "bus", "sfx"));
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

	out_def.MaxInstances = static_cast<uint32>(std::max(Value<int32>(config, "max_instances", 0), 0));

	return true;
}

void AudioSystem::UnloadDefs()
{
	if (mpBackend == nullptr) {
		return;
	}

	for (std::vector<ma_sound*>& prototypes : mpBackend->Prototypes) {
		for (ma_sound* prototype : prototypes) {
			if (prototype != nullptr) {
				ma_sound_uninit(prototype);
				delete prototype;
			}
		}
	}

	mpBackend->Prototypes.clear();
	mpBackend->LastVariants.clear();
	mpBackend->WarnedAboutFailedLoads.clear();
}

uint32 AudioSystem::FindDefIndex(Hash32 name_hash) const
{
	for (uint32 i = 0; i < mDefs.size(); i++) {
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

SoundID AudioSystem::Play(const char* name, const Vec3f position) { return Play(HashStr32(name), position); }

SoundID AudioSystem::Play(Hash32 name_hash, const Vec3f position) { return StartVoice(name_hash, position, true); }

SoundID AudioSystem::Play2D(const char* name) { return Play2D(HashStr32(name)); }

SoundID AudioSystem::PlayForSurface(const char* event, const String& surface, const Vec3f position)
{
	if (mpBackend == nullptr) {
		return {};
	}

	const std::string base(event);

	if (!surface.IsEmpty()) {
		const Hash32 surface_sound = HashStr32((base + "_" + surface.CStr()).c_str());

		if (FindDefIndex(surface_sound) != UINT32_MAX) {
			return Play(surface_sound, position);
		}
	}

	const Hash32 default_sound = HashStr32((base + "_default").c_str());

	if (FindDefIndex(default_sound) != UINT32_MAX) {
		return Play(default_sound, position);
	}

	return Play(HashStr32(event), position);
}

SoundID AudioSystem::Play2D(Hash32 name_hash) { return StartVoice(name_hash, Vec3f::sZero, false); }

uint32 AudioSystem::AcquireVoice(uint32 def_index)
{
	const SoundDef& def = mDefs[def_index];

	if (def.MaxInstances > 0) {
		uint32 instance_count = 0;
		uint32 oldest_slot = UINT32_MAX;

		for (uint32 i = 0; i < scMaxVoices; i++) {
			const Voice& voice = mVoices[i];

			if (!voice.bActive || voice.DefIndex != def_index) {
				continue;
			}

			++instance_count;

			if (oldest_slot == UINT32_MAX || voice.StartOrder < mVoices[oldest_slot].StartOrder) {
				oldest_slot = i;
			}
		}

		if (instance_count >= def.MaxInstances) {
			ReleaseVoice(oldest_slot);
			return oldest_slot;
		}
	}

	for (uint32 i = 0; i < scMaxVoices; i++) {
		if (!mVoices[i].bActive) {
			return i;
		}
	}

	uint32 oldest_slot = UINT32_MAX;

	for (uint32 i = 0; i < scMaxVoices; i++) {
		const Voice& voice = mVoices[i];

		if (mDefs[voice.DefIndex].bLoop) {
			continue;
		}

		if (oldest_slot == UINT32_MAX || voice.StartOrder < mVoices[oldest_slot].StartOrder) {
			oldest_slot = i;
		}
	}

	if (oldest_slot != UINT32_MAX) {
		ReleaseVoice(oldest_slot);
	}

	return oldest_slot;
}

SoundID AudioSystem::StartVoice(Hash32 name_hash, const Vec3f position, bool spatial)
{
	if (mpBackend == nullptr) {
		return {};
	}

	const uint32 def_index = FindDefIndex(name_hash);

	if (def_index == UINT32_MAX) {
		return {};
	}

	const SoundDef& def = mDefs[def_index];
	const uint32 file_count = static_cast<uint32>(def.Files.size());

	uint32 variant = FastRand32() % file_count;

	if (file_count > 1 && variant == mpBackend->LastVariants[def_index]) {
		variant = (variant + 1) % file_count;
	}

	ma_sound* prototype = nullptr;

	if (!def.bStream) {
		prototype = mpBackend->Prototypes[def_index][variant];

		ma_result load_result = MA_ERROR;

		if (prototype != nullptr) {
			load_result = ma_resource_manager_data_source_result(prototype->pResourceManagerDataSource);
		}

		if (load_result != MA_SUCCESS && load_result != MA_BUSY) {
			if (!mpBackend->WarnedAboutFailedLoads[def_index]) {
				LogError(LC_AUDIO, "Sound '{}' could not load '{}'", def.Name, def.Files[variant]);
				mpBackend->WarnedAboutFailedLoads[def_index] = true;
			}

			return {};
		}
	}

	const uint32 slot = AcquireVoice(def_index);

	if (slot == UINT32_MAX) {
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

	ma_result result = MA_SUCCESS;

	if (def.bStream) {
		result = ma_sound_init_from_file(&mpBackend->Engine, def.Files[variant].CStr(),
										 flags | MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC, bus, nullptr, sound);
	}
	else {
		result = ma_sound_init_copy(&mpBackend->Engine, prototype, flags, bus, sound);
	}

	if (result != MA_SUCCESS) {
		LogError(LC_AUDIO, "Could not start sound '{}' ({})", def.Name, ma_result_description(result));
		return {};
	}

	mpBackend->LastVariants[def_index] = variant;

	Voice& voice = mVoices[slot];
	voice.DefIndex = def_index;
	voice.StartOrder = mNextStartOrder++;
	voice.Volume = RandomInRange(def.Volume);
	voice.bActive = true;
	voice.bStopping = false;

	++mActiveVoiceCount;

	ma_sound_set_volume(sound, voice.Volume);
	ma_sound_set_pitch(sound, RandomInRange(def.Pitch));
	ma_sound_set_looping(sound, def.bLoop);

	if ((flags & MA_SOUND_FLAG_NO_SPATIALIZATION) == 0) {
		const Vec3f audio_position = ToAudioSpace(position);

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

	ma_sound_start(sound);

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

	voice.bActive = false;
	voice.bStopping = false;
	++voice.Generation;

	--mActiveVoiceCount;
}

void AudioSystem::SetPosition(SoundID id, const Vec3f position)
{
	if (GetVoice(id) == nullptr) {
		return;
	}

	const Vec3f audio_position = ToAudioSpace(position);

	ma_sound_set_position(&mpBackend->Voices[id.Slot], audio_position.X, audio_position.Y, audio_position.Z);
}

void AudioSystem::SetVolume(SoundID id, float32 volume)
{
	const Voice* voice = GetVoice(id);

	if (voice == nullptr) {
		return;
	}

	ma_sound_set_volume(&mpBackend->Voices[id.Slot], voice->Volume * std::max(volume, 0.0f));
}

void AudioSystem::Stop(SoundID id)
{
	Voice* voice = GetVoice(id);

	if (voice == nullptr || voice->bStopping) {
		return;
	}

	const SoundDef& def = mDefs[voice->DefIndex];

	if (def.FadeOut <= 0.0f) {
		ReleaseVoice(id.Slot);
		return;
	}

	ma_sound_stop_with_fade_in_milliseconds(&mpBackend->Voices[id.Slot], static_cast<ma_uint64>(def.FadeOut * 1000.0f));
	voice->bStopping = true;
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

void AudioSystem::SetListener(const Vec3f position, const Vec3f forward, const Vec3f up)
{
	if (mpBackend == nullptr) {
		return;
	}

	const Vec3f audio_position = ToAudioSpace(position);
	const Vec3f audio_forward = ToAudioSpace(forward);
	const Vec3f audio_up = ToAudioSpace(up);

	ma_engine_listener_set_position(&mpBackend->Engine, 0, audio_position.X, audio_position.Y, audio_position.Z);
	ma_engine_listener_set_direction(&mpBackend->Engine, 0, audio_forward.X, audio_forward.Y, audio_forward.Z);
	ma_engine_listener_set_world_up(&mpBackend->Engine, 0, audio_up.X, audio_up.Y, audio_up.Z);
}

void AudioSystem::ApplyVolumes(bool window_focused)
{
	float32 master_volume = std::max(mpVolumeCVar->FloatValue, 0.0f);

	if (mpMuteUnfocusedCVar->IntValue != 0 && !window_focused) {
		master_volume = 0.0f;
	}

	ma_engine_set_volume(&mpBackend->Engine, master_volume);

	for (uint32 i = 0; i < scBusCount; i++) {
		ma_sound_group_set_volume(&mpBackend->Buses[i], std::max(mpBusVolumeCVars[i]->FloatValue, 0.0f));
	}
}

void AudioSystem::Update(bool window_focused)
{
	if (mpBackend == nullptr) {
		return;
	}

	ApplyVolumes(window_focused);

	for (uint32 i = 0; i < scMaxVoices; i++) {
		const Voice& voice = mVoices[i];

		if (!voice.bActive) {
			continue;
		}

		const ma_sound* sound = &mpBackend->Voices[i];

		if (ma_sound_at_end(sound) || (voice.bStopping && !ma_sound_is_playing(sound))) {
			ReleaseVoice(i);
		}
	}
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

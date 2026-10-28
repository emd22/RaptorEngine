#pragma once

#include <Audio/SpatialAudio.hpp>
#include <Core/Hash.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <functional>
#include <vector>

namespace fx {

class CVarValue;

struct SoundID
{
	uint32 Slot = UINT32_MAX;
	uint32 Generation = 0;

	bool IsValid() const { return Slot != UINT32_MAX; }

	int64 Pack() const { return static_cast<int64>((static_cast<uint64>(Generation) << 32) | Slot); }

	static SoundID Unpack(int64 packed)
	{
		const uint64 bits = static_cast<uint64>(packed);
		return SoundID { .Slot = static_cast<uint32>(bits), .Generation = static_cast<uint32>(bits >> 32) };
	}
};

enum class eSoundBus : uint8
{
	Sfx,
	Music,
	Ui,
	Voice,
	Count,
};

enum class eSoundAttenuation : uint8
{
	None,
	Inverse,
	Linear,
	Exponential,
};

struct SoundRange
{
	float32 Min = 1.0f;
	float32 Max = 1.0f;
};

struct SoundClip
{
	float32 Start = 0.0f;
	float32 End = 0.0f;
};

struct SoundDef
{
	String Name;
	Hash32 NameHash = 0;
	std::vector<String> Files;
	std::vector<SoundClip> Clips;

	eSoundBus Bus = eSoundBus::Sfx;
	SoundRange Volume { 1.0f, 1.0f };
	SoundRange Pitch { 1.0f, 1.0f };

	bool bLoop = false;
	bool bStream = false;
	bool bSpatial = true;

	eSoundAttenuation Attenuation = eSoundAttenuation::Inverse;
	float32 MinDistance = 1.0f;
	float32 MaxDistance = 50.0f;
	float32 Rolloff = 1.0f;

	float32 FadeIn = 0.0f;
	float32 FadeOut = 0.0f;

	float32 Reverb = 0.0f;

	uint32 MaxInstances = 0;
};

struct AudioCreateInfo
{
	bool bNoDevice = false;
	bool bSteamAudio = true;
	uint32 SampleRate = 0;
	uint32 Channels = 0;
};

class AudioSystem
{
public:
	static constexpr uint32 scMaxVoices = 128;
	static constexpr const char* scSoundDirectory = "RaptorData/Data/Sounds";
	static constexpr float32 scSurfaceSoundOffset = 0.1f;

public:
	AudioSystem() = default;
	~AudioSystem() { Destroy(); }

	AudioSystem(const AudioSystem&) = delete;
	AudioSystem& operator=(const AudioSystem&) = delete;

	bool Create(const AudioCreateInfo& info = {});
	void Destroy();

	void LoadSounds();

	SoundID Play(const char* name, const Vec3f position);
	SoundID Play(Hash32 name_hash, const Vec3f position);
	SoundID PlayDelayed(Hash32 name_hash, const Vec3f position, float32 delay);
	SoundID Play2D(const char* name);
	SoundID Play2D(Hash32 name_hash);

	SoundID PlayForSurface(const char* event, const String& surface, const Vec3f position);

	void SetPosition(SoundID id, const Vec3f position);
	void SetVolume(SoundID id, float32 volume);
	void Stop(SoundID id);
	void Kill(SoundID id);
	bool IsPlaying(SoundID id) const;

	void StopAll();

	void SetListener(const Vec3f position, const Vec3f forward, const Vec3f up);
	void Update(bool window_focused);

	uint64 ReadFrames(float32* out_frames, uint64 frame_count);

	using PathGainFunction = std::function<float32(const Vec3f source, const Vec3f listener)>;

	void SetPathGainFunction(PathGainFunction function) { mPathGain = std::move(function); }
	void SetAcousticScene(const AcousticGeometry& geometry);
	void SetBakedAcoustics(const std::vector<uint8>& data);

	const SpatialAudio& GetSpatial() const { return mSpatial; }

	bool IsActive() const { return mpBackend != nullptr; }
	uint32 GetVoiceCount() const { return mActiveVoiceCount; }
	uint32 GetSampleRate() const;
	uint32 GetChannelCount() const;
	const SoundDef* FindDef(Hash32 name_hash) const;
	const std::vector<SoundDef>& GetDefs() const { return mDefs; }

private:
	struct Backend;

	struct Voice
	{
		uint32 DefIndex = 0;
		uint32 Generation = 0;
		uint64 StartOrder = 0;
		float32 Volume = 1.0f;
		Vec3f Position = Vec3f::sZero;
		SpatialVoice* pSpatial = nullptr;
		ReverbSend* pReverbSend = nullptr;
		bool bActive = false;
		bool bStopping = false;
	};

private:
	SoundID StartVoice(Hash32 name_hash, const Vec3f position, bool spatial, float32 delay = 0.0f);
	bool LoadDef(const char* path, const String& name, SoundDef& out_def);
	void UnloadDefs();

	uint32 FindDefIndex(Hash32 name_hash) const;
	uint32 AcquireVoice(uint32 def_index);
	Voice* GetVoice(SoundID id);
	const Voice* GetVoice(SoundID id) const;
	void ReleaseVoice(uint32 slot);

	void ApplyVolumes(bool window_focused);
	void UpdateSpatialVoice(uint32 slot);
	float32 GetDistanceGain(const SoundDef& def, float32 distance) const;

private:
	Backend* mpBackend = nullptr;

	std::vector<SoundDef> mDefs;

	Voice mVoices[scMaxVoices];
	uint32 mActiveVoiceCount = 0;
	uint64 mNextStartOrder = 0;

	CVarValue* mpVolumeCVar = nullptr;
	CVarValue* mpBusVolumeCVars[static_cast<uint32>(eSoundBus::Count)] {};
	CVarValue* mpMuteUnfocusedCVar = nullptr;
	CVarValue* mpSpatialCVar = nullptr;
	CVarValue* mpHrtfCVar = nullptr;
	CVarValue* mpReverbCVar = nullptr;
	CVarValue* mpReverbVolumeCVar = nullptr;
	CVarValue* mpReverbRaysCVar = nullptr;
	CVarValue* mpReverbBouncesCVar = nullptr;

	SpatialAudio mSpatial;
	Vec3f mListenerPosition = Vec3f::sZero;
	PathGainFunction mPathGain;

	bool mbWarnedAboutVoiceOverflow = false;
};

} // namespace fx

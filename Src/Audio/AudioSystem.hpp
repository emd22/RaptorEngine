#pragma once

#include <Audio/SpatialAudio.hpp>
#include <Core/DynArray.hpp>
#include <Core/Hash.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <functional>

namespace fx {

class CVarValue;
class ConfigEntry;

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

/// The root buses, which the volume cvars control. Buses from the mix file are children of these and come after
/// them in the bus list.
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
	DynArray<String, GrowthFunctions::Double> Files;
	DynArray<SoundClip, GrowthFunctions::Double> Clips;

	uint32 Bus = static_cast<uint32>(eSoundBus::Sfx);
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

struct MixBus
{
	String Name;
	Hash32 NameHash = 0;
	uint32 Parent = UINT32_MAX;
	float32 VolumeDb = 0.0f;

	float32 SnapshotDb = 0.0f;
	float32 DuckDb = 0.0f;
	float32 Lowpass = 0.0f;
	float32 Gain = 1.0f;
};

/// Lowers `Target` while `Trigger` is louder than `ThresholdDb`, like a sidechain compressor with a fixed depth
struct MixDuck
{
	String Name;
	uint32 Trigger = 0;
	uint32 Target = 0;
	float32 DepthDb = -6.0f;
	float32 ThresholdDb = -40.0f;
	float32 Attack = 0.02f;
	float32 Release = 0.5f;

	float32 Envelope = 0.0f;
};

struct MixSnapshotBus
{
	float32 VolumeDb = 0.0f;
	float32 Lowpass = 0.0f;
	bool bUsed = false;
};

struct AudioBusDebug
{
	const char* Name = "";
	uint32 Parent = UINT32_MAX;
	float32 GainDb = 0.0f;
	float32 SnapshotDb = 0.0f;
	float32 DuckDb = 0.0f;
	float32 Lowpass = 0.0f;
	float32 PeakDb = -120.0f;
	float32 RmsDb = -120.0f;
};

struct AudioVoiceDebug
{
	const char* Name = "";
	const char* Bus = "";
	Vec3f Position = Vec3f::sZero;
	float32 Distance = 0.0f;
	float32 DistanceGain = 1.0f;
	float32 Volume = 1.0f;
	SpatialVoiceDebug Spatial;
	bool bSpatial = false;
	bool bSteam = false;
	bool bPending = false;
	bool bStopping = false;
	bool bLoop = false;
};

struct AudioSnapshotDebug
{
	const char* Name = "";
	float32 Weight = 0.0f;
	uint32 ActiveCount = 0;
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
	static constexpr float32 scDeclickSeconds = 0.005f;

	static constexpr const char* scMixPath = "RaptorData/Data/Sounds/Mix/mix.conf";
	static constexpr uint32 scMaxBuses = 32;
	static constexpr uint32 scMaxDucks = 32;
	static constexpr uint32 scMaxSnapshots = 32;
	static constexpr float32 scLowpassOpen = 20000.0f;

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
	SoundID PlayForSurface2D(const char* event, const String& surface);

	void SetPosition(SoundID id, const Vec3f position);
	void SetVolume(SoundID id, float32 volume);
	void Stop(SoundID id);
	void Kill(SoundID id);
	bool IsPlaying(SoundID id) const;

	void StopAll();

	/// Snapshots are counted, so each start needs its own stop
	void StartSnapshot(const char* name);
	void StopSnapshot(const char* name);
	bool IsSnapshotActive(const char* name) const;

	uint32 FindBus(const char* name) const;
	uint32 GetBusCount() const { return mBusCount; }
	bool GetBusDebug(uint32 index, AudioBusDebug& out) const;

	uint32 GetSnapshotCount() const { return mSnapshotCount; }
	bool GetSnapshotDebug(uint32 index, AudioSnapshotDebug& out) const;

	uint32 GetDuckCount() const { return mDuckCount; }
	const MixDuck& GetDuck(uint32 index) const { return mDucks[index]; }

	/// Fills `out` with up to `max_count` playing voices and returns how many it wrote
	uint32 GetVoiceDebug(AudioVoiceDebug* out, uint32 max_count) const;

	float32 GetLimiterReductionDb() const;
	float32 GetMasterPeakDb() const;
	uint32 GetStolenVoiceCount() const { return mStolenVoiceCount; }
	uint32 GetDroppedVoiceCount() const { return mDroppedVoiceCount; }

	void SetListener(const Vec3f position, const Vec3f forward, const Vec3f up);
	void Update(float32 delta_time, bool window_focused);

	uint64 ReadFrames(float32* out_frames, uint64 frame_count);

	using PathGainFunction = std::function<float32(const Vec3f source, const Vec3f listener)>;

	void SetPathGainFunction(PathGainFunction function) { mPathGain = std::move(function); }
	void SetAcousticScene(const AcousticGeometry& geometry);
	void SetBakedAcoustics(const SizedArray<uint8>& data);

	const SpatialAudio& GetSpatial() const { return mSpatial; }

	bool IsActive() const { return mpBackend != nullptr; }
	uint32 GetVoiceCount() const { return mActiveVoiceCount; }
	uint32 GetSampleRate() const;
	uint32 GetChannelCount() const;
	const SoundDef* FindDef(Hash32 name_hash) const;
	const DynArray<SoundDef, GrowthFunctions::Double>& GetDefs() const { return mDefs; }

private:
	struct Backend;

	struct Voice
	{
		uint32 DefIndex = 0;
		uint32 Generation = 0;
		uint64 StartOrder = 0;
		uint64 RequestFrame = 0;
		float32 Volume = 1.0f;
		float32 UserVolume = 1.0f;
		Vec3f Position = Vec3f::sZero;
		SpatialVoice* pSpatial = nullptr;
		ReverbSend* pReverbSend = nullptr;
		bool bActive = false;
		bool bStopping = false;
		bool bPendingStart = false;
		bool bSpatial = false;
	};

	struct MixSnapshot
	{
		String Name;
		Hash32 NameHash = 0;
		float32 FadeIn = 0.25f;
		float32 FadeOut = 0.25f;
		MixSnapshotBus Buses[scMaxBuses];

		uint32 ActiveCount = 0;
		float32 Weight = 0.0f;
	};

private:
	SoundID StartVoice(Hash32 name_hash, const Vec3f position, bool spatial, float32 delay = 0.0f);
	bool LoadDef(const char* path, const String& name, SoundDef& out_def);
	void UnloadDefs();

	uint32 FindDefIndex(Hash32 name_hash) const;
	Hash32 FindSurfaceSound(const char* event, const String& surface) const;
	uint32 AcquireVoice(uint32 def_index);
	Voice* GetVoice(SoundID id);
	const Voice* GetVoice(SoundID id) const;
	void ReleaseVoice(uint32 slot);
	void FadeOutVoice(uint32 slot, float32 seconds);
	void StartReadyVoices();

	void LoadMix();
	void LoadMixBuses(const ConfigEntry& buses);
	void LoadMixDucks(const ConfigEntry& ducks);
	void LoadMixSnapshots(const ConfigEntry& snapshots);
	bool CreateBusNodes(uint32 index);
	void DestroyBusNodes(uint32 index);
	void DestroyMixBuses();
	uint32 FindSnapshot(Hash32 name_hash) const;

	void UpdateMix(float32 delta_time, bool window_focused);
	void UpdateSpatialVoice(uint32 slot);
	float32 GetCurveGain(const SoundDef& def, float32 distance) const;
	float32 GetDistanceGain(const SoundDef& def, float32 distance) const;

private:
	Backend* mpBackend = nullptr;

	DynArray<SoundDef, GrowthFunctions::Double> mDefs;

	Voice mVoices[scMaxVoices];
	uint32 mActiveVoiceCount = 0;
	uint64 mNextStartOrder = 0;
	uint32 mStolenVoiceCount = 0;
	uint32 mDroppedVoiceCount = 0;

	MixBus mBuses[scMaxBuses];
	uint32 mBusCount = 0;
	MixDuck mDucks[scMaxDucks];
	uint32 mDuckCount = 0;
	MixSnapshot mSnapshots[scMaxSnapshots];
	uint32 mSnapshotCount = 0;

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

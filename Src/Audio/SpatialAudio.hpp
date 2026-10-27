#pragma once

#include <Audio/Acoustics.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <vector>

struct ma_engine;
struct ma_node_base;

namespace fx {

struct SpatialVoice;

class SpatialAudio
{
public:
	static constexpr uint32 scFrameSize = 512;
	static constexpr float32 scOcclusionRadius = 0.4f;
	static constexpr int32 scOcclusionSamples = 8;
	static constexpr int32 scTransmissionRays = 3;

	static constexpr int32 scMaxReverbRays = 8192;
	static constexpr int32 scDefaultReverbRays = 2048;
	static constexpr int32 scDefaultReverbBounces = 64;
	static constexpr int32 scMaxReverbBounces = 256;
	static constexpr int32 scReverbDiffuseSamples = 32;
	static constexpr float32 scReverbDuration = 2.0f;
	static constexpr int32 scReverbOrder = 1;
	static constexpr uint32 scReverbChannels = (scReverbOrder + 1) * (scReverbOrder + 1);
	static constexpr float32 scReverbIntervalSeconds = 0.1f;
	static constexpr float32 scOccludedReverbSend = 0.35f;
	static constexpr float32 scOccludedGainFloorLow = 0.25f;
	static constexpr float32 scOccludedGainFloorHigh = 0.04f;

	static constexpr int32 scPathingOrder = Acoustics::scProbeOrder;
	static constexpr uint32 scPathingCoefficients = (scPathingOrder + 1) * (scPathingOrder + 1);
	static constexpr int32 scPathingVisSamples = 4;
	static constexpr float32 scPathingVisRadius = 0.5f;
	static constexpr float32 scPathingVisThreshold = 0.1f;
	static constexpr float32 scPathingVisRange = 50.0f;

public:
	SpatialAudio() = default;
	~SpatialAudio() { Destroy(); }

	SpatialAudio(const SpatialAudio&) = delete;
	SpatialAudio& operator=(const SpatialAudio&) = delete;

	bool Create(ma_engine* engine, uint32 max_voices, ma_node_base* reverb_output);
	void Destroy();

	bool IsActive() const { return mpState != nullptr; }

	SpatialVoice* CreateVoice(ma_node_base* output);
	void DestroyVoice(SpatialVoice* voice);
	ma_node_base* GetVoiceInput(SpatialVoice* voice) const;

	void SetVoiceSource(SpatialVoice* voice, const Vec3f position, float32 distance_gain, bool binaural);

	void SetListener(const Vec3f position, const Vec3f forward, const Vec3f up);
	void SetReverb(bool enabled, float32 volume, bool binaural);
	void SetReverbQuality(int32 rays, int32 bounces);
	void SetScene(const AcousticGeometry& geometry);
	void ClearScene();

	bool SetBakedProbes(const std::vector<uint8>& data);
	bool HasBakedProbes() const { return mBakedProbeCount > 0; }
	bool HasBakedPathing() const { return mbBakedPathing; }
	uint32 GetBakedProbeCount() const { return mBakedProbeCount; }
	uint32 GetPathedVoiceCount() const;

	void Simulate();

	bool HasScene() const;
	uint32 GetSceneTriangleCount() const { return mSceneTriangleCount; }
	float32 GetSimulationMs() const { return mSimulationMs; }
	float32 GetReflectionMs() const;
	void GetReverbTimes(float32* out_times) const;

	struct State;

private:
	State* mpState = nullptr;
	uint32 mSceneTriangleCount = 0;
	uint32 mBakedProbeCount = 0;
	bool mbBakedPathing = false;
	uint64 mNextVoiceId = 1;
	float32 mSimulationMs = 0.0f;
};

} // namespace fx

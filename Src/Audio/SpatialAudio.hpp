#pragma once

#include <Audio/Acoustics.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/FVec3.hpp>

struct ma_engine;
struct ma_node_base;

namespace fx {

struct SpatialVoice;
struct ReverbSend;

struct SpatialVoiceDebug
{
	float32 Occlusion = 1.0f;
	float32 Transmission[3] = { 1.0f, 1.0f, 1.0f };
	float32 AirAbsorption[3] = { 1.0f, 1.0f, 1.0f };
	float32 PathGain = 0.0f;
	float32 LevelDb = -120.0f;
	bool bSimulated = false;
	bool bHasPath = false;
	bool bStarted = false;
};

class SpatialAudio
{
public:
	static constexpr uint32 scFrameSize = 512;
	static constexpr int32 scOcclusionSamples = 8;

	static constexpr int32 scMaxReverbRays = 8192;
	static constexpr int32 scDefaultReverbRays = 2048;
	static constexpr int32 scDefaultReverbBounces = 64;
	static constexpr float32 scReverbDuration = 2.0f;
	static constexpr int32 scReverbOrder = 1;
	static constexpr uint32 scReverbChannels = (scReverbOrder + 1) * (scReverbOrder + 1);

	static constexpr int32 scPathingOrder = Acoustics::scProbeOrder;
	static constexpr uint32 scPathingCoefficients = (scPathingOrder + 1) * (scPathingOrder + 1);

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

	ReverbSend* CreateReverbSend(ma_node_base* output, float32 send);
	void DestroyReverbSend(ReverbSend* send);
	ma_node_base* GetReverbSendInput(ReverbSend* send) const;

	void SetVoiceSource(SpatialVoice* voice, const FVec3 position, float32 distance_gain, bool binaural);
	bool IsVoiceReady(const SpatialVoice* voice) const;
	void SetVoiceStarted(SpatialVoice* voice);
	bool GetVoiceDebug(const SpatialVoice* voice, SpatialVoiceDebug& out) const;

	void SetListener(const FVec3 position, const FVec3 forward, const FVec3 up);
	void SetReverb(bool enabled, float32 volume, bool binaural);
	void SetReverbQuality(int32 rays, int32 bounces);
	void SetScene(const AcousticGeometry& geometry);
	void ClearScene();

	bool SetBakedProbes(const SizedArray<uint8>& data);
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

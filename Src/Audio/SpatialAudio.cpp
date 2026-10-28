#include "SpatialAudio.hpp"

#include <Audio/AcousticScene.hpp>
#include <Core/Log.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <miniaudio.h>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace fx {

FX_SET_MODULE_NAME("SpatialAudio")

using Acoustics::ToIPL;

namespace {

constexpr uint32 scChannelsIn = 2;
constexpr uint32 scChannelsOut = 2;
constexpr uint32 scSendChannels = 1;
constexpr uint32 scVoiceFifoStride = scChannelsOut + scSendChannels;
constexpr uint32 scOutFifoFrames = SpatialAudio::scFrameSize * 4;
constexpr float32 scMinDirectionLength = 1.0e-4f;
constexpr float32 scMinPathGain = 1.0e-3f;
constexpr float32 scPathShNormalization = 3.5449077f;

using Clock = std::chrono::steady_clock;

IPLCoordinateSpace3 MakeSpace(const Vec3f origin, const Vec3f ahead, const Vec3f up)
{
	const Vec3f right = ahead.Cross(up).Normalize();

	return IPLCoordinateSpace3 {
		.right = ToIPL(right),
		.up = ToIPL(up),
		.ahead = ToIPL(ahead),
		.origin = ToIPL(origin),
	};
}

class SpinLock
{
public:
	void Lock()
	{
		while (mFlag.test_and_set(std::memory_order_acquire)) {
		}
	}

	bool TryLock() { return !mFlag.test_and_set(std::memory_order_acquire); }

	void Unlock() { mFlag.clear(std::memory_order_release); }

private:
	std::atomic_flag mFlag = ATOMIC_FLAG_INIT;
};

void PopFifo(float32* fifo, uint32& count, uint32 stride, uint32 frames)
{
	count -= frames;

	std::memmove(fifo, fifo + static_cast<size_t>(frames) * stride, static_cast<size_t>(count) * stride * sizeof(float32));
}

struct PathRequest
{
	uint64 VoiceId = 0;
	Vec3f Position = Vec3f::sZero;
};

struct PathResult
{
	uint64 VoiceId = 0;
	IPLfloat32 Eq[IPL_NUM_BANDS] = { 0.0f, 0.0f, 0.0f };
	IPLfloat32 Sh[SpatialAudio::scPathingCoefficients] {};
};

} // namespace

struct SpatialVoiceParams
{
	IPLDirectEffectParams Direct {};
	IPLVector3 Direction { 0.0f, 0.0f, -1.0f };
	IPLCoordinateSpace3 Listener = MakeSpace(Vec3f::sZero, Vec3f(0.0f, 0.0f, -1.0f), Vec3f::sUp);
	float32 ReverbSend = 0.0f;
	float32 PathGain = 0.0f;
	IPLfloat32 PathEq[IPL_NUM_BANDS] = { 0.0f, 0.0f, 0.0f };
	IPLfloat32 PathSh[SpatialAudio::scPathingCoefficients] {};
	bool bBinaural = true;
};

struct SpatialVoice
{
	ma_node_base Node;

	uint64 Id = 0;
	IPLContext Context = nullptr;
	IPLHRTF Hrtf = nullptr;

	IPLSource Source = nullptr;
	IPLDirectEffect DirectEffect = nullptr;
	IPLBinauralEffect BinauralEffect = nullptr;
	IPLPanningEffect PanningEffect = nullptr;
	IPLPathEffect PathEffect = nullptr;

	bool bNodeInitialized = false;

	Vec3f Position = Vec3f::sZero;
	float32 DistanceGain = 1.0f;
	bool bBinaural = true;

	IPLfloat32 AirAbsorption[IPL_NUM_BANDS] = { 1.0f, 1.0f, 1.0f };
	IPLfloat32 Occlusion = 1.0f;
	IPLfloat32 Transmission[IPL_NUM_BANDS] = { 1.0f, 1.0f, 1.0f };

	bool bHasPath = false;
	IPLfloat32 PathEq[IPL_NUM_BANDS] = { 0.0f, 0.0f, 0.0f };
	IPLfloat32 PathSh[SpatialAudio::scPathingCoefficients] {};

	SpinLock ParamsLock;
	SpatialVoiceParams PendingParams;
	SpatialVoiceParams ActiveParams;

	float32 MonoIn[SpatialAudio::scFrameSize] {};
	float32 MonoDirect[SpatialAudio::scFrameSize] {};
	float32 StereoLeft[SpatialAudio::scFrameSize] {};
	float32 StereoRight[SpatialAudio::scFrameSize] {};
	float32 PathIn[SpatialAudio::scFrameSize] {};
	float32 PathLeft[SpatialAudio::scFrameSize] {};
	float32 PathRight[SpatialAudio::scFrameSize] {};
	uint32 InCount = 0;

	float32 OutFifo[scOutFifoFrames * scVoiceFifoStride] {};
	uint32 OutCount = 0;
};

struct ReverbParams
{
	IPLfloat32 ReverbTimes[IPL_NUM_BANDS] = { 0.0f, 0.0f, 0.0f };
	IPLCoordinateSpace3 Orientation = MakeSpace(Vec3f::sZero, Vec3f(0.0f, 0.0f, -1.0f), Vec3f::sUp);
	bool bBinaural = true;
	bool bEnabled = true;
};

struct ReverbSend
{
	ma_node_base Node;
	float32 Send = 0.0f;
	bool bNodeInitialized = false;
};

struct ReverbNode
{
	ma_node_base Node;

	IPLHRTF Hrtf = nullptr;
	IPLReflectionEffect ReflectionEffect = nullptr;
	IPLAmbisonicsDecodeEffect DecodeEffect = nullptr;
	bool bNodeInitialized = false;

	SpinLock ParamsLock;
	ReverbParams PendingParams;
	ReverbParams ActiveParams;

	float32 MonoIn[SpatialAudio::scFrameSize] {};
	float32 Ambisonics[SpatialAudio::scReverbChannels][SpatialAudio::scFrameSize] {};
	float32 StereoLeft[SpatialAudio::scFrameSize] {};
	float32 StereoRight[SpatialAudio::scFrameSize] {};
	uint32 InCount = 0;

	float32 OutFifo[scOutFifoFrames * scChannelsOut] {};
	uint32 OutCount = 0;
};

struct SpatialAudio::State
{
	ma_engine* pEngine = nullptr;

	IPLContext Context = nullptr;
	IPLHRTF Hrtf = nullptr;
	IPLSimulator Simulator = nullptr;
	IPLScene Scene = nullptr;
	IPLStaticMesh Mesh = nullptr;
	IPLAudioSettings AudioSettings {};

	std::vector<SpatialVoice*> Voices;
	bool bCommitNeeded = true;

	Vec3f ListenerPosition = Vec3f::sZero;
	Vec3f ListenerForward = Vec3f(0.0f, 0.0f, -1.0f);
	Vec3f ListenerUp = Vec3f::sUp;

	ReverbNode* pReverb = nullptr;
	IPLSimulator ReflectionSimulator = nullptr;
	IPLSimulator PathingSimulator = nullptr;
	IPLSource ReverbSource = nullptr;

	std::thread Worker;
	std::mutex WorkerMutex;
	std::condition_variable WorkerSignal;
	bool bStopWorker = false;

	bool bSceneChanged = false;
	IPLScene PendingScene = nullptr;
	uint32 PendingSceneTriangles = 0;

	bool bProbesChanged = false;
	IPLProbeBatch PendingProbes = nullptr;
	bool bPendingPathing = false;

	IPLCoordinateSpace3 WorkerListener = MakeSpace(Vec3f::sZero, Vec3f(0.0f, 0.0f, -1.0f), Vec3f::sUp);
	bool bReverbEnabled = true;
	int32 ReverbRays = SpatialAudio::scDefaultReverbRays;
	int32 ReverbBounces = SpatialAudio::scDefaultReverbBounces;

	std::vector<PathRequest> PathRequests;
	std::vector<PathResult> PathResults;

	IPLScene WorkerScene = nullptr;
	uint32 WorkerSceneTriangles = 0;
	IPLProbeBatch WorkerProbes = nullptr;
	bool bWorkerPathing = false;
	std::unordered_map<uint64, IPLSource> PathSources;

	std::atomic<float32> ReflectionMs = 0.0f;
	std::atomic<float32> ReverbTimes[IPL_NUM_BANDS] = { 0.0f, 0.0f, 0.0f };
	std::atomic<uint32> PathedVoices = 0;
};

namespace {

void RenderVoicePath(SpatialVoice& voice)
{
	const SpatialVoiceParams& params = voice.ActiveParams;

	for (uint32 i = 0; i < SpatialAudio::scFrameSize; i++) {
		voice.PathIn[i] = voice.MonoIn[i] * params.PathGain;
	}

	float32* path_in = voice.PathIn;
	float32* path_out[2] = { voice.PathLeft, voice.PathRight };

	IPLAudioBuffer in_buffer { 1, static_cast<IPLint32>(SpatialAudio::scFrameSize), &path_in };
	IPLAudioBuffer out_buffer { 2, static_cast<IPLint32>(SpatialAudio::scFrameSize), path_out };

	IPLPathEffectParams path {};
	path.shCoeffs = voice.ActiveParams.PathSh;
	path.order = SpatialAudio::scPathingOrder;
	path.hrtf = voice.Hrtf;
	path.listener = params.Listener;
	path.binaural = IPL_FALSE;
	path.normalizeEQ = IPL_FALSE;

	if (params.bBinaural) {
		path.binaural = IPL_TRUE;
	}

	for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
		path.eqCoeffs[band] = params.PathEq[band];
	}

	iplPathEffectApply(voice.PathEffect, &path, &in_buffer, &out_buffer);

	for (uint32 i = 0; i < SpatialAudio::scFrameSize; i++) {
		voice.StereoLeft[i] += voice.PathLeft[i];
		voice.StereoRight[i] += voice.PathRight[i];
	}
}

void RenderVoiceFrame(SpatialVoice& voice)
{
	if (voice.ParamsLock.TryLock()) {
		voice.ActiveParams = voice.PendingParams;
		voice.ParamsLock.Unlock();
	}

	float32* mono_in = voice.MonoIn;
	float32* mono_direct = voice.MonoDirect;
	float32* stereo[2] = { voice.StereoLeft, voice.StereoRight };

	IPLAudioBuffer in_buffer { 1, static_cast<IPLint32>(SpatialAudio::scFrameSize), &mono_in };
	IPLAudioBuffer direct_buffer { 1, static_cast<IPLint32>(SpatialAudio::scFrameSize), &mono_direct };
	IPLAudioBuffer out_buffer { 2, static_cast<IPLint32>(SpatialAudio::scFrameSize), stereo };

	iplDirectEffectApply(voice.DirectEffect, &voice.ActiveParams.Direct, &in_buffer, &direct_buffer);

	if (voice.ActiveParams.bBinaural) {
		IPLBinauralEffectParams binaural {};
		binaural.direction = voice.ActiveParams.Direction;
		binaural.interpolation = IPL_HRTFINTERPOLATION_NEAREST;
		binaural.spatialBlend = 1.0f;
		binaural.hrtf = voice.Hrtf;

		iplBinauralEffectApply(voice.BinauralEffect, &binaural, &direct_buffer, &out_buffer);
	}
	else {
		IPLPanningEffectParams panning {};
		panning.direction = voice.ActiveParams.Direction;

		iplPanningEffectApply(voice.PanningEffect, &panning, &direct_buffer, &out_buffer);
	}

	if (voice.ActiveParams.PathGain > scMinPathGain) {
		RenderVoicePath(voice);
	}

	if (voice.OutCount + SpatialAudio::scFrameSize > scOutFifoFrames) {
		return;
	}

	float32* out = voice.OutFifo + static_cast<size_t>(voice.OutCount) * scVoiceFifoStride;
	const float32 send = voice.ActiveParams.ReverbSend;

	for (uint32 i = 0; i < SpatialAudio::scFrameSize; i++) {
		out[i * scVoiceFifoStride] = voice.StereoLeft[i];
		out[i * scVoiceFifoStride + 1] = voice.StereoRight[i];
		out[i * scVoiceFifoStride + 2] = voice.MonoIn[i] * send;
	}

	voice.OutCount += SpatialAudio::scFrameSize;
}

void ProcessVoiceNode(ma_node* node, const float** frames_in, ma_uint32* frame_count_in, float** frames_out,
					  ma_uint32* frame_count_out)
{
	SpatialVoice& voice = *static_cast<SpatialVoice*>(node);

	const float* input = frames_in[0];
	const ma_uint32 input_frames = frame_count_in[0];

	for (ma_uint32 consumed = 0; consumed < input_frames;) {
		const ma_uint32 take = std::min(input_frames - consumed, SpatialAudio::scFrameSize - voice.InCount);

		for (ma_uint32 i = 0; i < take; i++) {
			const float* frame = input + static_cast<size_t>(consumed + i) * scChannelsIn;
			voice.MonoIn[voice.InCount + i] = 0.5f * (frame[0] + frame[1]);
		}

		voice.InCount += take;
		consumed += take;

		if (voice.InCount == SpatialAudio::scFrameSize) {
			RenderVoiceFrame(voice);
			voice.InCount = 0;
		}
	}

	const ma_uint32 output_frames = *frame_count_out;
	const ma_uint32 available = std::min<ma_uint32>(voice.OutCount, output_frames);
	const ma_uint32 missing = output_frames - available;

	float* stereo_out = frames_out[0];
	float* send_out = frames_out[1];

	std::memset(stereo_out, 0, static_cast<size_t>(missing) * scChannelsOut * sizeof(float));
	std::memset(send_out, 0, static_cast<size_t>(missing) * sizeof(float));

	for (ma_uint32 i = 0; i < available; i++) {
		const float32* frame = voice.OutFifo + static_cast<size_t>(i) * scVoiceFifoStride;

		stereo_out[(missing + i) * scChannelsOut] = frame[0];
		stereo_out[(missing + i) * scChannelsOut + 1] = frame[1];
		send_out[missing + i] = frame[2];
	}

	PopFifo(voice.OutFifo, voice.OutCount, scVoiceFifoStride, available);
}

ma_node_vtable scVoiceNodeVtable = {
	ProcessVoiceNode, nullptr, 1, 2, 0,
};

void ProcessReverbSendNode(ma_node* node, const float** frames_in, ma_uint32* frame_count_in, float** frames_out,
						   ma_uint32* frame_count_out)
{
	const ReverbSend& reverb_send = *static_cast<ReverbSend*>(node);

	const float* input = frames_in[0];
	const ma_uint32 frame_count = std::min(frame_count_in[0], *frame_count_out);
	const float32 send = reverb_send.Send;

	float* stereo_out = frames_out[0];
	float* send_out = frames_out[1];

	for (ma_uint32 i = 0; i < frame_count; i++) {
		const float left = input[i * scChannelsIn];
		const float right = input[i * scChannelsIn + 1];

		stereo_out[i * scChannelsOut] = left;
		stereo_out[i * scChannelsOut + 1] = right;
		send_out[i] = 0.5f * (left + right) * send;
	}

	*frame_count_in = frame_count;
	*frame_count_out = frame_count;
}

ma_node_vtable scReverbSendNodeVtable = {
	ProcessReverbSendNode, nullptr, 1, 2, 0,
};

void RenderReverbFrame(ReverbNode& reverb)
{
	if (reverb.ParamsLock.TryLock()) {
		reverb.ActiveParams = reverb.PendingParams;
		reverb.ParamsLock.Unlock();
	}

	if (reverb.OutCount + SpatialAudio::scFrameSize > scOutFifoFrames) {
		return;
	}

	float32* out = reverb.OutFifo + static_cast<size_t>(reverb.OutCount) * scChannelsOut;
	reverb.OutCount += SpatialAudio::scFrameSize;

	const ReverbParams& params = reverb.ActiveParams;

	if (!params.bEnabled) {
		std::memset(out, 0, SpatialAudio::scFrameSize * scChannelsOut * sizeof(float32));
		return;
	}

	float32* mono_in = reverb.MonoIn;
	float32* ambisonics[SpatialAudio::scReverbChannels];
	float32* stereo[2] = { reverb.StereoLeft, reverb.StereoRight };

	for (uint32 channel = 0; channel < SpatialAudio::scReverbChannels; channel++) {
		ambisonics[channel] = reverb.Ambisonics[channel];
	}

	IPLAudioBuffer in_buffer { 1, static_cast<IPLint32>(SpatialAudio::scFrameSize), &mono_in };
	IPLAudioBuffer ambisonic_buffer { static_cast<IPLint32>(SpatialAudio::scReverbChannels),
									  static_cast<IPLint32>(SpatialAudio::scFrameSize), ambisonics };
	IPLAudioBuffer out_buffer { 2, static_cast<IPLint32>(SpatialAudio::scFrameSize), stereo };

	IPLReflectionEffectParams reflection {};
	reflection.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
	reflection.numChannels = static_cast<IPLint32>(SpatialAudio::scReverbChannels);

	for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
		reflection.reverbTimes[band] = params.ReverbTimes[band];
	}

	iplReflectionEffectApply(reverb.ReflectionEffect, &reflection, &in_buffer, &ambisonic_buffer, nullptr);

	IPLAmbisonicsDecodeEffectParams decode {};
	decode.order = SpatialAudio::scReverbOrder;
	decode.hrtf = reverb.Hrtf;
	decode.orientation = params.Orientation;
	decode.binaural = IPL_FALSE;

	if (params.bBinaural) {
		decode.binaural = IPL_TRUE;
	}

	iplAmbisonicsDecodeEffectApply(reverb.DecodeEffect, &decode, &ambisonic_buffer, &out_buffer);

	for (uint32 i = 0; i < SpatialAudio::scFrameSize; i++) {
		out[i * 2] = reverb.StereoLeft[i];
		out[i * 2 + 1] = reverb.StereoRight[i];
	}
}

void ProcessReverbNode(ma_node* node, const float** frames_in, ma_uint32* frame_count_in, float** frames_out,
					   ma_uint32* frame_count_out)
{
	ReverbNode& reverb = *static_cast<ReverbNode*>(node);

	const float* input = frames_in[0];
	const ma_uint32 input_frames = frame_count_in[0];

	for (ma_uint32 consumed = 0; consumed < input_frames;) {
		const ma_uint32 take = std::min(input_frames - consumed, SpatialAudio::scFrameSize - reverb.InCount);

		std::memcpy(reverb.MonoIn + reverb.InCount, input + consumed, static_cast<size_t>(take) * sizeof(float));

		reverb.InCount += take;
		consumed += take;

		if (reverb.InCount == SpatialAudio::scFrameSize) {
			RenderReverbFrame(reverb);
			reverb.InCount = 0;
		}
	}

	const ma_uint32 output_frames = *frame_count_out;
	const ma_uint32 available = std::min<ma_uint32>(reverb.OutCount, output_frames);
	const ma_uint32 missing = output_frames - available;

	float* output = frames_out[0];

	std::memset(output, 0, static_cast<size_t>(missing) * scChannelsOut * sizeof(float));
	std::memcpy(output + static_cast<size_t>(missing) * scChannelsOut, reverb.OutFifo,
				static_cast<size_t>(available) * scChannelsOut * sizeof(float));

	PopFifo(reverb.OutFifo, reverb.OutCount, scChannelsOut, available);
}

ma_node_vtable scReverbNodeVtable = {
	ProcessReverbNode, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING,
};

void PublishVoiceParams(SpatialVoice& voice, const SpatialAudio::State& state)
{
	SpatialVoiceParams params;

	params.Direct.flags = static_cast<IPLDirectEffectFlags>(
		IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION |
		IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION);
	params.Direct.transmissionType = IPL_TRANSMISSIONTYPE_FREQDEPENDENT;
	params.Direct.distanceAttenuation = voice.DistanceGain;
	params.Direct.directivity = 1.0f;
	params.Direct.occlusion = 0.0f;

	const float32 floors[IPL_NUM_BANDS] = {
		SpatialAudio::scOccludedGainFloorLow,
		std::sqrt(SpatialAudio::scOccludedGainFloorLow * SpatialAudio::scOccludedGainFloorHigh),
		SpatialAudio::scOccludedGainFloorHigh,
	};

	for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
		const float32 occluded_gain = voice.Occlusion + (1.0f - voice.Occlusion) * voice.Transmission[band];

		params.Direct.airAbsorption[band] = voice.AirAbsorption[band];
		params.Direct.transmission[band] = std::max(occluded_gain, floors[band]);
	}

	const Vec3f to_source = voice.Position - state.ListenerPosition;

	if (to_source.Length() > scMinDirectionLength) {
		params.Direction = iplCalculateRelativeDirection(state.Context, ToIPL(voice.Position),
														 ToIPL(state.ListenerPosition), ToIPL(state.ListenerForward),
														 ToIPL(state.ListenerUp));
	}

	params.Listener = MakeSpace(state.ListenerPosition, state.ListenerForward, state.ListenerUp);

	const float32 occluded_send = SpatialAudio::scOccludedReverbSend +
								  (1.0f - SpatialAudio::scOccludedReverbSend) * voice.Occlusion;

	params.ReverbSend = std::sqrt(std::max(voice.DistanceGain, 0.0f)) * occluded_send;

	if (voice.bHasPath) {
		const float32 straight_distance = std::max(to_source.Length(), 1.0f);

		params.PathGain = (1.0f - voice.Occlusion) * voice.DistanceGain * straight_distance * scPathShNormalization;

		std::memcpy(params.PathEq, voice.PathEq, sizeof(params.PathEq));
		std::memcpy(params.PathSh, voice.PathSh, sizeof(params.PathSh));
	}

	params.bBinaural = voice.bBinaural;

	voice.ParamsLock.Lock();
	voice.PendingParams = params;
	voice.ParamsLock.Unlock();
}

void PublishReverbTimes(SpatialAudio::State& state, const IPLfloat32* reverb_times)
{
	state.pReverb->ParamsLock.Lock();

	for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
		state.pReverb->PendingParams.ReverbTimes[band] = reverb_times[band];
	}

	state.pReverb->ParamsLock.Unlock();

	for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
		state.ReverbTimes[band].store(reverb_times[band]);
	}
}

void RecreateReverbSource(SpatialAudio::State& state)
{
	iplSourceRemove(state.ReverbSource, state.ReflectionSimulator);
	iplSourceRelease(&state.ReverbSource);

	IPLSourceSettings source_settings { IPL_SIMULATIONFLAGS_REFLECTIONS };
	iplSourceCreate(state.ReflectionSimulator, &source_settings, &state.ReverbSource);
	iplSourceAdd(state.ReverbSource, state.ReflectionSimulator);
}

void ClearPathSources(SpatialAudio::State& state)
{
	for (auto& [voice_id, source] : state.PathSources) {
		iplSourceRemove(source, state.PathingSimulator);
		iplSourceRelease(&source);
	}

	state.PathSources.clear();
}

void ApplyWorkerScene(SpatialAudio::State& state, IPLScene scene, uint32 triangles)
{
	RecreateReverbSource(state);
	ClearPathSources(state);

	iplSimulatorSetScene(state.ReflectionSimulator, scene);
	iplSimulatorCommit(state.ReflectionSimulator);

	iplSimulatorSetScene(state.PathingSimulator, scene);
	iplSimulatorCommit(state.PathingSimulator);

	if (state.WorkerScene != nullptr) {
		iplSceneRelease(&state.WorkerScene);
	}

	state.WorkerScene = scene;
	state.WorkerSceneTriangles = triangles;
}

void ApplyWorkerProbes(SpatialAudio::State& state, IPLProbeBatch probes, bool pathing)
{
	RecreateReverbSource(state);
	ClearPathSources(state);

	if (state.WorkerProbes != nullptr) {
		iplSimulatorRemoveProbeBatch(state.ReflectionSimulator, state.WorkerProbes);
		iplSimulatorRemoveProbeBatch(state.PathingSimulator, state.WorkerProbes);
	}

	if (probes != nullptr) {
		iplSimulatorAddProbeBatch(state.ReflectionSimulator, probes);
		iplSimulatorAddProbeBatch(state.PathingSimulator, probes);
	}

	iplSimulatorCommit(state.ReflectionSimulator);
	iplSimulatorCommit(state.PathingSimulator);

	if (state.WorkerProbes != nullptr) {
		iplProbeBatchRelease(&state.WorkerProbes);
	}

	state.WorkerProbes = probes;
	state.bWorkerPathing = pathing && probes != nullptr;
}

void SimulateReverb(SpatialAudio::State& state, const IPLCoordinateSpace3& listener, int32 rays, int32 bounces,
					IPLfloat32* out_reverb_times)
{
	IPLSimulationSharedInputs shared {};
	shared.listener = listener;
	shared.numRays = rays;
	shared.numBounces = bounces;
	shared.duration = SpatialAudio::scReverbDuration;
	shared.order = SpatialAudio::scReverbOrder;
	shared.irradianceMinDistance = 1.0f;

	iplSimulatorSetSharedInputs(state.ReflectionSimulator, IPL_SIMULATIONFLAGS_REFLECTIONS, &shared);

	IPLSimulationInputs inputs {};
	inputs.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
	inputs.source = listener;
	inputs.reverbScale[0] = 1.0f;
	inputs.reverbScale[1] = 1.0f;
	inputs.reverbScale[2] = 1.0f;

	if (state.WorkerProbes != nullptr) {
		inputs.baked = IPL_TRUE;
		inputs.bakedDataIdentifier = Acoustics::scReverbIdentifier;
	}

	iplSourceSetInputs(state.ReverbSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &inputs);
	iplSimulatorRunReflections(state.ReflectionSimulator);

	IPLSimulationOutputs outputs {};
	iplSourceGetOutputs(state.ReverbSource, IPL_SIMULATIONFLAGS_REFLECTIONS, &outputs);

	for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
		if (std::isfinite(outputs.reflections.reverbTimes[band])) {
			out_reverb_times[band] = outputs.reflections.reverbTimes[band];
		}
	}
}

void SimulatePathing(SpatialAudio::State& state, const IPLCoordinateSpace3& listener,
					 const std::vector<PathRequest>& requests, std::vector<PathResult>& out_results)
{
	bool changed = false;

	for (auto it = state.PathSources.begin(); it != state.PathSources.end();) {
		const bool requested = std::any_of(requests.begin(), requests.end(), [&it](const PathRequest& request)
										   { return request.VoiceId == it->first; });

		if (requested) {
			++it;
			continue;
		}

		iplSourceRemove(it->second, state.PathingSimulator);
		iplSourceRelease(&it->second);
		it = state.PathSources.erase(it);
		changed = true;
	}

	for (const PathRequest& request : requests) {
		if (state.PathSources.contains(request.VoiceId)) {
			continue;
		}

		IPLSourceSettings source_settings { IPL_SIMULATIONFLAGS_PATHING };
		IPLSource source = nullptr;

		if (iplSourceCreate(state.PathingSimulator, &source_settings, &source) != IPL_STATUS_SUCCESS) {
			continue;
		}

		iplSourceAdd(source, state.PathingSimulator);
		state.PathSources[request.VoiceId] = source;
		changed = true;
	}

	if (changed) {
		iplSimulatorCommit(state.PathingSimulator);
	}

	if (requests.empty()) {
		return;
	}

	IPLSimulationSharedInputs shared {};
	shared.listener = listener;

	iplSimulatorSetSharedInputs(state.PathingSimulator, IPL_SIMULATIONFLAGS_PATHING, &shared);

	for (const PathRequest& request : requests) {
		const auto source = state.PathSources.find(request.VoiceId);

		if (source == state.PathSources.end()) {
			continue;
		}

		IPLSimulationInputs inputs {};
		inputs.flags = IPL_SIMULATIONFLAGS_PATHING;
		inputs.source = MakeSpace(request.Position, Vec3f(0.0f, 0.0f, -1.0f), Vec3f::sUp);
		inputs.pathingProbes = state.WorkerProbes;
		inputs.visRadius = SpatialAudio::scPathingVisRadius;
		inputs.visThreshold = SpatialAudio::scPathingVisThreshold;
		inputs.visRange = SpatialAudio::scPathingVisRange;
		inputs.pathingOrder = SpatialAudio::scPathingOrder;
		inputs.enableValidation = IPL_TRUE;
		inputs.findAlternatePaths = IPL_FALSE;

		iplSourceSetInputs(source->second, IPL_SIMULATIONFLAGS_PATHING, &inputs);
	}

	iplSimulatorRunPathing(state.PathingSimulator);

	for (const PathRequest& request : requests) {
		const auto source = state.PathSources.find(request.VoiceId);

		if (source == state.PathSources.end()) {
			continue;
		}

		IPLSimulationOutputs outputs {};
		iplSourceGetOutputs(source->second, IPL_SIMULATIONFLAGS_PATHING, &outputs);

		PathResult& result = out_results.emplace_back();
		result.VoiceId = request.VoiceId;

		for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
			result.Eq[band] = outputs.pathing.eqCoeffs[band];
		}

		if (outputs.pathing.shCoeffs != nullptr) {
			std::memcpy(result.Sh, outputs.pathing.shCoeffs, sizeof(result.Sh));
		}
	}
}

void RunAcousticWorker(SpatialAudio::State& state)
{
	const auto interval = std::chrono::duration<float32>(SpatialAudio::scReverbIntervalSeconds);

	std::unique_lock lock(state.WorkerMutex);

	while (!state.bStopWorker) {
		state.WorkerSignal.wait_for(lock, interval, [&state]
									{ return state.bStopWorker || state.bSceneChanged || state.bProbesChanged; });

		if (state.bStopWorker) {
			break;
		}

		IPLScene new_scene = nullptr;
		uint32 new_scene_triangles = 0;
		const bool scene_changed = state.bSceneChanged;

		if (scene_changed) {
			new_scene = state.PendingScene;
			new_scene_triangles = state.PendingSceneTriangles;
			state.PendingScene = nullptr;
			state.bSceneChanged = false;
		}

		IPLProbeBatch new_probes = nullptr;
		const bool probes_changed = state.bProbesChanged;
		const bool new_pathing = state.bPendingPathing;

		if (probes_changed) {
			new_probes = state.PendingProbes;
			state.PendingProbes = nullptr;
			state.bProbesChanged = false;
		}

		const IPLCoordinateSpace3 listener = state.WorkerListener;
		const bool enabled = state.bReverbEnabled;
		const int32 rays = state.ReverbRays;
		const int32 bounces = state.ReverbBounces;
		const std::vector<PathRequest> requests = state.PathRequests;

		lock.unlock();

		const auto start = Clock::now();

		if (scene_changed) {
			ApplyWorkerScene(state, new_scene, new_scene_triangles);
		}

		if (probes_changed) {
			ApplyWorkerProbes(state, new_probes, new_pathing);
		}

		IPLfloat32 reverb_times[IPL_NUM_BANDS] = { 0.0f, 0.0f, 0.0f };

		if (enabled && (state.WorkerSceneTriangles > 0 || state.WorkerProbes != nullptr)) {
			SimulateReverb(state, listener, rays, bounces, reverb_times);
		}

		PublishReverbTimes(state, reverb_times);

		std::vector<PathResult> results;

		if (state.bWorkerPathing && state.WorkerSceneTriangles > 0) {
			SimulatePathing(state, listener, requests, results);
		}
		else {
			ClearPathSources(state);
		}

		state.PathedVoices.store(static_cast<uint32>(results.size()));
		state.ReflectionMs.store(std::chrono::duration<float32, std::milli>(Clock::now() - start).count());

		lock.lock();

		state.PathResults = std::move(results);
	}

	ClearPathSources(state);
}

} // namespace

bool SpatialAudio::Create(ma_engine* engine, uint32 max_voices, ma_node_base* reverb_output)
{
	if (mpState != nullptr) {
		return true;
	}

	State* state = new State;
	state->pEngine = engine;
	state->AudioSettings.samplingRate = static_cast<IPLint32>(ma_engine_get_sample_rate(engine));
	state->AudioSettings.frameSize = static_cast<IPLint32>(scFrameSize);

	const auto fail = [this, state](const char* step)
	{
		LogError(LC_AUDIO, "Could not start Steam Audio ({}), using plain panning", step);

		mpState = state;
		Destroy();

		return false;
	};

	IPLContextSettings context_settings {};
	context_settings.version = STEAMAUDIO_VERSION;
	context_settings.logCallback = Acoustics::OnSteamAudioLog;

	if (iplContextCreate(&context_settings, &state->Context) != IPL_STATUS_SUCCESS) {
		return fail("context");
	}

	IPLHRTFSettings hrtf_settings {};
	hrtf_settings.type = IPL_HRTFTYPE_DEFAULT;
	hrtf_settings.volume = 1.0f;
	hrtf_settings.normType = IPL_HRTFNORMTYPE_NONE;

	if (iplHRTFCreate(state->Context, &state->AudioSettings, &hrtf_settings, &state->Hrtf) != IPL_STATUS_SUCCESS) {
		return fail("HRTF");
	}

	IPLSimulationSettings simulation_settings {};
	simulation_settings.flags = IPL_SIMULATIONFLAGS_DIRECT;
	simulation_settings.sceneType = IPL_SCENETYPE_DEFAULT;
	simulation_settings.maxNumOcclusionSamples = scOcclusionSamples;
	simulation_settings.maxNumSources = static_cast<IPLint32>(max_voices);
	simulation_settings.numThreads = 1;
	simulation_settings.samplingRate = state->AudioSettings.samplingRate;
	simulation_settings.frameSize = state->AudioSettings.frameSize;

	if (iplSimulatorCreate(state->Context, &simulation_settings, &state->Simulator) != IPL_STATUS_SUCCESS) {
		return fail("simulator");
	}

	IPLSimulationSettings reflection_settings {};
	reflection_settings.flags = IPL_SIMULATIONFLAGS_REFLECTIONS;
	reflection_settings.sceneType = IPL_SCENETYPE_DEFAULT;
	reflection_settings.reflectionType = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
	reflection_settings.maxNumRays = scMaxReverbRays;
	reflection_settings.numDiffuseSamples = scReverbDiffuseSamples;
	reflection_settings.maxDuration = scReverbDuration;
	reflection_settings.maxOrder = scReverbOrder;
	reflection_settings.maxNumSources = 1;
	reflection_settings.numThreads = 2;
	reflection_settings.rayBatchSize = 1;
	reflection_settings.samplingRate = state->AudioSettings.samplingRate;
	reflection_settings.frameSize = state->AudioSettings.frameSize;

	if (iplSimulatorCreate(state->Context, &reflection_settings, &state->ReflectionSimulator) != IPL_STATUS_SUCCESS) {
		return fail("reflection simulator");
	}

	IPLSimulationSettings pathing_settings {};
	pathing_settings.flags = IPL_SIMULATIONFLAGS_PATHING;
	pathing_settings.sceneType = IPL_SCENETYPE_DEFAULT;
	pathing_settings.maxNumSources = static_cast<IPLint32>(max_voices);
	pathing_settings.numThreads = 1;
	pathing_settings.numVisSamples = scPathingVisSamples;
	pathing_settings.maxOrder = scPathingOrder;
	pathing_settings.samplingRate = state->AudioSettings.samplingRate;
	pathing_settings.frameSize = state->AudioSettings.frameSize;

	if (iplSimulatorCreate(state->Context, &pathing_settings, &state->PathingSimulator) != IPL_STATUS_SUCCESS) {
		return fail("pathing simulator");
	}

	IPLSourceSettings reverb_source_settings { IPL_SIMULATIONFLAGS_REFLECTIONS };

	if (iplSourceCreate(state->ReflectionSimulator, &reverb_source_settings, &state->ReverbSource) !=
		IPL_STATUS_SUCCESS) {
		return fail("reverb source");
	}

	iplSourceAdd(state->ReverbSource, state->ReflectionSimulator);
	iplSimulatorCommit(state->ReflectionSimulator);

	ReverbNode* reverb = new ReverbNode;
	reverb->Hrtf = state->Hrtf;
	state->pReverb = reverb;

	IPLReflectionEffectSettings reflection_effect_settings {};
	reflection_effect_settings.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
	reflection_effect_settings.irSize = static_cast<IPLint32>(scReverbDuration * state->AudioSettings.samplingRate);
	reflection_effect_settings.numChannels = static_cast<IPLint32>(scReverbChannels);

	if (iplReflectionEffectCreate(state->Context, &state->AudioSettings, &reflection_effect_settings,
								  &reverb->ReflectionEffect) != IPL_STATUS_SUCCESS) {
		return fail("reflection effect");
	}

	IPLAmbisonicsDecodeEffectSettings decode_settings {};
	decode_settings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
	decode_settings.hrtf = state->Hrtf;
	decode_settings.maxOrder = scReverbOrder;

	if (iplAmbisonicsDecodeEffectCreate(state->Context, &state->AudioSettings, &decode_settings,
										&reverb->DecodeEffect) != IPL_STATUS_SUCCESS) {
		return fail("ambisonics decode");
	}

	const ma_uint32 reverb_channels_in = 1;
	const ma_uint32 reverb_channels_out = scChannelsOut;

	ma_node_config reverb_config = ma_node_config_init();
	reverb_config.vtable = &scReverbNodeVtable;
	reverb_config.pInputChannels = &reverb_channels_in;
	reverb_config.pOutputChannels = &reverb_channels_out;

	if (ma_node_init(ma_engine_get_node_graph(engine), &reverb_config, nullptr, &reverb->Node) != MA_SUCCESS) {
		return fail("reverb node");
	}

	reverb->bNodeInitialized = true;
	ma_node_attach_output_bus(&reverb->Node, 0, reverb_output, 0);

	mpState = state;

	ClearScene();

	state->Worker = std::thread(RunAcousticWorker, std::ref(*state));

	LogInfo(LC_AUDIO, "Steam Audio {}.{}.{} running, {} Hz, {} frame, parametric reverb", STEAMAUDIO_VERSION_MAJOR,
			STEAMAUDIO_VERSION_MINOR, STEAMAUDIO_VERSION_PATCH, state->AudioSettings.samplingRate, scFrameSize);

	return true;
}

void SpatialAudio::Destroy()
{
	if (mpState == nullptr) {
		return;
	}

	if (mpState->Worker.joinable()) {
		{
			std::lock_guard guard(mpState->WorkerMutex);
			mpState->bStopWorker = true;
		}

		mpState->WorkerSignal.notify_all();
		mpState->Worker.join();
	}

	while (!mpState->Voices.empty()) {
		DestroyVoice(mpState->Voices.back());
	}

	if (ReverbNode* reverb = mpState->pReverb) {
		if (reverb->bNodeInitialized) {
			ma_node_uninit(&reverb->Node, nullptr);
		}

		if (reverb->ReflectionEffect != nullptr) {
			iplReflectionEffectRelease(&reverb->ReflectionEffect);
		}

		if (reverb->DecodeEffect != nullptr) {
			iplAmbisonicsDecodeEffectRelease(&reverb->DecodeEffect);
		}

		delete reverb;
		mpState->pReverb = nullptr;
	}

	if (mpState->PendingScene != nullptr) {
		iplSceneRelease(&mpState->PendingScene);
	}

	if (mpState->PendingProbes != nullptr) {
		iplProbeBatchRelease(&mpState->PendingProbes);
	}

	if (mpState->WorkerProbes != nullptr) {
		iplSimulatorRemoveProbeBatch(mpState->ReflectionSimulator, mpState->WorkerProbes);
		iplSimulatorRemoveProbeBatch(mpState->PathingSimulator, mpState->WorkerProbes);
		iplProbeBatchRelease(&mpState->WorkerProbes);
	}

	if (mpState->WorkerScene != nullptr) {
		iplSceneRelease(&mpState->WorkerScene);
	}

	if (mpState->ReverbSource != nullptr) {
		iplSourceRemove(mpState->ReverbSource, mpState->ReflectionSimulator);
		iplSourceRelease(&mpState->ReverbSource);
	}

	if (mpState->PathingSimulator != nullptr) {
		iplSimulatorRelease(&mpState->PathingSimulator);
	}

	if (mpState->ReflectionSimulator != nullptr) {
		iplSimulatorRelease(&mpState->ReflectionSimulator);
	}

	if (mpState->Mesh != nullptr) {
		iplStaticMeshRelease(&mpState->Mesh);
	}

	if (mpState->Scene != nullptr) {
		iplSceneRelease(&mpState->Scene);
	}

	if (mpState->Simulator != nullptr) {
		iplSimulatorRelease(&mpState->Simulator);
	}

	if (mpState->Hrtf != nullptr) {
		iplHRTFRelease(&mpState->Hrtf);
	}

	if (mpState->Context != nullptr) {
		iplContextRelease(&mpState->Context);
	}

	delete mpState;
	mpState = nullptr;
	mSceneTriangleCount = 0;
	mBakedProbeCount = 0;
	mbBakedPathing = false;
}

SpatialVoice* SpatialAudio::CreateVoice(ma_node_base* output)
{
	if (mpState == nullptr) {
		return nullptr;
	}

	SpatialVoice* voice = new SpatialVoice;
	voice->Id = mNextVoiceId++;
	voice->Context = mpState->Context;
	voice->Hrtf = mpState->Hrtf;

	IPLDirectEffectSettings direct_settings { 1 };
	IPLBinauralEffectSettings binaural_settings { mpState->Hrtf };

	IPLPanningEffectSettings panning_settings {};
	panning_settings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;

	IPLPathEffectSettings path_settings {};
	path_settings.maxOrder = scPathingOrder;
	path_settings.spatialize = IPL_TRUE;
	path_settings.speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
	path_settings.hrtf = mpState->Hrtf;

	IPLSourceSettings source_settings { IPL_SIMULATIONFLAGS_DIRECT };

	bool created = iplDirectEffectCreate(mpState->Context, &mpState->AudioSettings, &direct_settings,
										 &voice->DirectEffect) == IPL_STATUS_SUCCESS;
	created = created && iplBinauralEffectCreate(mpState->Context, &mpState->AudioSettings, &binaural_settings,
												 &voice->BinauralEffect) == IPL_STATUS_SUCCESS;
	created = created && iplPanningEffectCreate(mpState->Context, &mpState->AudioSettings, &panning_settings,
												&voice->PanningEffect) == IPL_STATUS_SUCCESS;
	created = created && iplPathEffectCreate(mpState->Context, &mpState->AudioSettings, &path_settings,
											 &voice->PathEffect) == IPL_STATUS_SUCCESS;
	created = created &&
			  iplSourceCreate(mpState->Simulator, &source_settings, &voice->Source) == IPL_STATUS_SUCCESS;

	const ma_uint32 channels_in = scChannelsIn;
	const ma_uint32 channels_out[2] = { scChannelsOut, scSendChannels };

	ma_node_config node_config = ma_node_config_init();
	node_config.vtable = &scVoiceNodeVtable;
	node_config.pInputChannels = &channels_in;
	node_config.pOutputChannels = channels_out;

	created = created && ma_node_init(ma_engine_get_node_graph(mpState->pEngine), &node_config, nullptr,
									  &voice->Node) == MA_SUCCESS;
	voice->bNodeInitialized = created;

	mpState->Voices.push_back(voice);

	if (!created) {
		LogError(LC_AUDIO, "Could not create a Steam Audio voice");
		DestroyVoice(voice);
		return nullptr;
	}

	iplSourceAdd(voice->Source, mpState->Simulator);
	mpState->bCommitNeeded = true;

	PublishVoiceParams(*voice, *mpState);

	ma_node_attach_output_bus(&voice->Node, 0, output, 0);
	ma_node_attach_output_bus(&voice->Node, 1, &mpState->pReverb->Node, 0);

	return voice;
}

void SpatialAudio::DestroyVoice(SpatialVoice* voice)
{
	if (mpState == nullptr || voice == nullptr) {
		return;
	}

	if (voice->bNodeInitialized) {
		ma_node_uninit(&voice->Node, nullptr);
	}

	if (voice->Source != nullptr) {
		iplSourceRemove(voice->Source, mpState->Simulator);
		iplSourceRelease(&voice->Source);
		mpState->bCommitNeeded = true;
	}

	if (voice->DirectEffect != nullptr) {
		iplDirectEffectRelease(&voice->DirectEffect);
	}

	if (voice->BinauralEffect != nullptr) {
		iplBinauralEffectRelease(&voice->BinauralEffect);
	}

	if (voice->PanningEffect != nullptr) {
		iplPanningEffectRelease(&voice->PanningEffect);
	}

	if (voice->PathEffect != nullptr) {
		iplPathEffectRelease(&voice->PathEffect);
	}

	std::erase(mpState->Voices, voice);

	delete voice;
}

ReverbSend* SpatialAudio::CreateReverbSend(ma_node_base* output, float32 send)
{
	if (mpState == nullptr) {
		return nullptr;
	}

	ReverbSend* reverb_send = new ReverbSend;
	reverb_send->Send = send;

	const ma_uint32 channels_in = scChannelsIn;
	const ma_uint32 channels_out[2] = { scChannelsOut, scSendChannels };

	ma_node_config node_config = ma_node_config_init();
	node_config.vtable = &scReverbSendNodeVtable;
	node_config.pInputChannels = &channels_in;
	node_config.pOutputChannels = channels_out;

	if (ma_node_init(ma_engine_get_node_graph(mpState->pEngine), &node_config, nullptr, &reverb_send->Node) !=
		MA_SUCCESS) {
		LogError(LC_AUDIO, "Could not create a reverb send");
		delete reverb_send;
		return nullptr;
	}

	reverb_send->bNodeInitialized = true;

	ma_node_attach_output_bus(&reverb_send->Node, 0, output, 0);
	ma_node_attach_output_bus(&reverb_send->Node, 1, &mpState->pReverb->Node, 0);

	return reverb_send;
}

void SpatialAudio::DestroyReverbSend(ReverbSend* send)
{
	if (send == nullptr) {
		return;
	}

	if (send->bNodeInitialized) {
		ma_node_uninit(&send->Node, nullptr);
	}

	delete send;
}

ma_node_base* SpatialAudio::GetReverbSendInput(ReverbSend* send) const
{
	if (send == nullptr) {
		return nullptr;
	}

	return &send->Node;
}

ma_node_base* SpatialAudio::GetVoiceInput(SpatialVoice* voice) const
{
	if (voice == nullptr) {
		return nullptr;
	}

	return &voice->Node;
}

void SpatialAudio::SetVoiceSource(SpatialVoice* voice, const Vec3f position, float32 distance_gain, bool binaural)
{
	if (mpState == nullptr || voice == nullptr) {
		return;
	}

	voice->Position = position;
	voice->DistanceGain = distance_gain;
	voice->bBinaural = binaural;

	PublishVoiceParams(*voice, *mpState);
}

void SpatialAudio::SetListener(const Vec3f position, const Vec3f forward, const Vec3f up)
{
	if (mpState == nullptr) {
		return;
	}

	mpState->ListenerPosition = position;
	mpState->ListenerForward = forward.Normalize();
	mpState->ListenerUp = up.Normalize();

	const IPLCoordinateSpace3 listener = MakeSpace(position, mpState->ListenerForward, mpState->ListenerUp);

	{
		std::lock_guard guard(mpState->WorkerMutex);
		mpState->WorkerListener = listener;
	}

	mpState->pReverb->ParamsLock.Lock();
	mpState->pReverb->PendingParams.Orientation = listener;
	mpState->pReverb->ParamsLock.Unlock();
}

void SpatialAudio::SetReverb(bool enabled, float32 volume, bool binaural)
{
	if (mpState == nullptr) {
		return;
	}

	{
		std::lock_guard guard(mpState->WorkerMutex);
		mpState->bReverbEnabled = enabled;
	}

	ReverbNode& reverb = *mpState->pReverb;

	reverb.ParamsLock.Lock();
	reverb.PendingParams.bEnabled = enabled;
	reverb.PendingParams.bBinaural = binaural;
	reverb.ParamsLock.Unlock();

	ma_node_set_output_bus_volume(&reverb.Node, 0, std::max(volume, 0.0f));
}

void SpatialAudio::SetReverbQuality(int32 rays, int32 bounces)
{
	if (mpState == nullptr) {
		return;
	}

	std::lock_guard guard(mpState->WorkerMutex);

	mpState->ReverbRays = std::clamp(rays, 1, scMaxReverbRays);
	mpState->ReverbBounces = std::clamp(bounces, 1, scMaxReverbBounces);
}

bool SpatialAudio::HasScene() const { return mSceneTriangleCount > 0; }

float32 SpatialAudio::GetReflectionMs() const
{
	if (mpState == nullptr) {
		return 0.0f;
	}

	return mpState->ReflectionMs.load();
}

uint32 SpatialAudio::GetPathedVoiceCount() const
{
	if (mpState == nullptr) {
		return 0;
	}

	return mpState->PathedVoices.load();
}

void SpatialAudio::GetReverbTimes(float32* out_times) const
{
	for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
		out_times[band] = 0.0f;

		if (mpState != nullptr) {
			out_times[band] = mpState->ReverbTimes[band].load();
		}
	}
}

void SpatialAudio::ClearScene() { SetScene(AcousticGeometry {}); }

void SpatialAudio::SetScene(const AcousticGeometry& geometry)
{
	if (mpState == nullptr) {
		return;
	}

	IPLScene scene = nullptr;
	IPLStaticMesh mesh = nullptr;

	if (!Acoustics::CreateScene(mpState->Context, geometry, scene, mesh)) {
		return;
	}

	const uint32 triangle_count = geometry.GetTriangleCount();

	iplSimulatorSetScene(mpState->Simulator, scene);
	iplSimulatorCommit(mpState->Simulator);
	mpState->bCommitNeeded = false;

	{
		std::lock_guard guard(mpState->WorkerMutex);

		if (mpState->PendingScene != nullptr) {
			iplSceneRelease(&mpState->PendingScene);
		}

		mpState->PendingScene = iplSceneRetain(scene);
		mpState->PendingSceneTriangles = triangle_count;
		mpState->bSceneChanged = true;
	}

	mpState->WorkerSignal.notify_all();

	if (mpState->Mesh != nullptr) {
		iplStaticMeshRelease(&mpState->Mesh);
	}

	if (mpState->Scene != nullptr) {
		iplSceneRelease(&mpState->Scene);
	}

	mpState->Scene = scene;
	mpState->Mesh = mesh;
	mSceneTriangleCount = triangle_count;

	if (triangle_count > 0) {
		LogInfo(LC_AUDIO, "Acoustic scene: {} triangles, {} materials", triangle_count, geometry.Surfaces.size());
	}
}

bool SpatialAudio::SetBakedProbes(const std::vector<uint8>& data)
{
	if (mpState == nullptr) {
		return false;
	}

	IPLProbeBatch probes = nullptr;
	bool pathing = false;

	const uint8* payload = nullptr;
	size_t payload_size = 0;
	String error;

	if (!data.empty() && !Acoustics::UnwrapProbeData(data, payload, payload_size, error)) {
		LogWarning(LC_AUDIO, "The baked acoustic probes cannot be used ({}), rebake the map with mapc. Using "
							 "real-time reverb.",
				   error);
	}

	if (payload != nullptr) {
		IPLSerializedObjectSettings serialized_settings {};
		serialized_settings.data = const_cast<IPLbyte*>(payload);
		serialized_settings.size = payload_size;

		IPLSerializedObject serialized = nullptr;

		if (iplSerializedObjectCreate(mpState->Context, &serialized_settings, &serialized) == IPL_STATUS_SUCCESS) {
			if (iplProbeBatchLoad(mpState->Context, serialized, &probes) != IPL_STATUS_SUCCESS) {
				probes = nullptr;
				LogWarning(LC_AUDIO, "Steam Audio could not load the baked acoustic probes, using real-time reverb");
			}

			iplSerializedObjectRelease(&serialized);
		}
	}

	mBakedProbeCount = 0;
	mbBakedPathing = false;

	if (probes != nullptr) {
		iplProbeBatchCommit(probes);

		IPLBakedDataIdentifier pathing_identifier = Acoustics::scPathingIdentifier;

		mBakedProbeCount = static_cast<uint32>(iplProbeBatchGetNumProbes(probes));
		pathing = iplProbeBatchGetDataSize(probes, &pathing_identifier) > 0;
		mbBakedPathing = pathing;

		LogInfo(LC_AUDIO, "Baked acoustics: {} probes, pathing {}", mBakedProbeCount, pathing);
	}

	{
		std::lock_guard guard(mpState->WorkerMutex);

		if (mpState->PendingProbes != nullptr) {
			iplProbeBatchRelease(&mpState->PendingProbes);
		}

		mpState->PendingProbes = probes;
		mpState->bPendingPathing = pathing;
		mpState->bProbesChanged = true;
		mpState->PathResults.clear();
	}

	mpState->WorkerSignal.notify_all();

	for (SpatialVoice* voice : mpState->Voices) {
		voice->bHasPath = false;
	}

	return probes != nullptr || data.empty();
}

void SpatialAudio::Simulate()
{
	if (mpState == nullptr) {
		mSimulationMs = 0.0f;
		return;
	}

	if (mbBakedPathing) {
		std::lock_guard guard(mpState->WorkerMutex);

		mpState->PathRequests.clear();

		for (const SpatialVoice* voice : mpState->Voices) {
			mpState->PathRequests.push_back(PathRequest { voice->Id, voice->Position });
		}

		for (SpatialVoice* voice : mpState->Voices) {
			voice->bHasPath = false;
		}

		for (const PathResult& result : mpState->PathResults) {
			const auto found = std::find_if(mpState->Voices.begin(), mpState->Voices.end(),
											[&result](const SpatialVoice* voice) { return voice->Id == result.VoiceId; });

			if (found == mpState->Voices.end()) {
				continue;
			}

			SpatialVoice& voice = **found;
			voice.bHasPath = true;

			std::memcpy(voice.PathEq, result.Eq, sizeof(voice.PathEq));
			std::memcpy(voice.PathSh, result.Sh, sizeof(voice.PathSh));
		}
	}

	if (mpState->Voices.empty()) {
		mSimulationMs = 0.0f;
		return;
	}

	const auto start = Clock::now();

	IPLSimulationSharedInputs shared {};
	shared.listener = MakeSpace(mpState->ListenerPosition, mpState->ListenerForward, mpState->ListenerUp);

	iplSimulatorSetSharedInputs(mpState->Simulator, IPL_SIMULATIONFLAGS_DIRECT, &shared);

	int direct_flags = IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION;

	if (HasScene()) {
		direct_flags |= IPL_DIRECTSIMULATIONFLAGS_OCCLUSION | IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION;
	}

	for (SpatialVoice* voice : mpState->Voices) {
		IPLSimulationInputs inputs {};
		inputs.flags = IPL_SIMULATIONFLAGS_DIRECT;
		inputs.directFlags = static_cast<IPLDirectSimulationFlags>(direct_flags);
		inputs.source = MakeSpace(voice->Position, Vec3f(0.0f, 0.0f, -1.0f), Vec3f::sUp);
		inputs.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
		inputs.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
		inputs.occlusionType = IPL_OCCLUSIONTYPE_VOLUMETRIC;
		inputs.occlusionRadius = scOcclusionRadius;
		inputs.numOcclusionSamples = scOcclusionSamples;
		inputs.numTransmissionRays = scTransmissionRays;

		iplSourceSetInputs(voice->Source, IPL_SIMULATIONFLAGS_DIRECT, &inputs);
	}

	if (mpState->bCommitNeeded) {
		iplSimulatorCommit(mpState->Simulator);
		mpState->bCommitNeeded = false;
	}

	iplSimulatorRunDirect(mpState->Simulator);

	for (SpatialVoice* voice : mpState->Voices) {
		IPLSimulationOutputs outputs {};
		iplSourceGetOutputs(voice->Source, IPL_SIMULATIONFLAGS_DIRECT, &outputs);

		voice->Occlusion = 1.0f;

		for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
			voice->AirAbsorption[band] = outputs.direct.airAbsorption[band];
			voice->Transmission[band] = 1.0f;
		}

		if (HasScene()) {
			voice->Occlusion = outputs.direct.occlusion;

			for (uint32 band = 0; band < IPL_NUM_BANDS; band++) {
				voice->Transmission[band] = outputs.direct.transmission[band];
			}
		}

		PublishVoiceParams(*voice, *mpState);
	}

	mSimulationMs = std::chrono::duration<float32, std::milli>(Clock::now() - start).count();
}

} // namespace fx

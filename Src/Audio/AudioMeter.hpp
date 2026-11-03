#pragma once

#include <Core/Types.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace fx {

/// Peak and RMS level meter written by the audio thread and read by the game thread.
/// Both readings fall off over `scReleaseSeconds`, so a reader polling once per frame does not miss short peaks.
struct AudioMeter
{
	static constexpr float32 scReleaseSeconds = 0.3f;
	static constexpr float32 scSilenceDb = -120.0f;

	std::atomic<float32> Peak = 0.0f;
	std::atomic<float32> MeanSquare = 0.0f;

	float32 PeakState = 0.0f;
	float32 MeanSquareState = 0.0f;

	void Process(const float* frames, uint32 frame_count, uint32 channels, float32 sample_rate)
	{
		if (frame_count == 0 || channels == 0) {
			return;
		}

		float32 block_peak = 0.0f;
		float32 block_square = 0.0f;

		const uint32 sample_count = frame_count * channels;

		for (uint32 i = 0; i < sample_count; i++) {
			const float32 sample = frames[i];

			block_peak = std::max(block_peak, std::abs(sample));
			block_square += sample * sample;
		}

		block_square /= static_cast<float32>(sample_count);

		const float32 decay = std::exp(-static_cast<float32>(frame_count) / (scReleaseSeconds * sample_rate));

		PeakState = std::max(block_peak, PeakState * decay);
		MeanSquareState = block_square + (MeanSquareState - block_square) * decay;

		Peak.store(PeakState, std::memory_order_relaxed);
		MeanSquare.store(MeanSquareState, std::memory_order_relaxed);
	}

	float32 GetPeakDb() const { return ToDb(Peak.load(std::memory_order_relaxed)); }
	float32 GetRmsDb() const { return ToDb(std::sqrt(MeanSquare.load(std::memory_order_relaxed))); }

	static float32 ToDb(float32 gain)
	{
		if (gain <= 1e-6f) {
			return scSilenceDb;
		}

		return 20.0f * std::log10(gain);
	}

	static float32 FromDb(float32 db) { return std::pow(10.0f, db / 20.0f); }
};

} // namespace fx

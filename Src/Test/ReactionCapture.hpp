#pragma once

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <cstdio>

namespace fx {

class NPC;

struct ReactionCaptureOptions
{
	bool bEnabled = false;
	const char* OutDir = nullptr;
	/// Time each frame stands for (s). 0 runs on the real clock, as the game does
	float32 FixedDelta = 0.0f;
	/// Saves one rendered frame in this many, 0 for none
	uint32 ImageStride = 0;
	/// Only between these times (s), since a full frame is large
	float32 ImageFrom = 0.0f;
	float32 ImageTo = 1e9f;
};

/**
 * @brief Plays scripted hits on an NPC in the game's own map, through the game's own loop, and records the pose that
 * reaches the screen each frame. The ragdoll bench steps physics on its own at a fixed rate and reads the bodies, so it
 * can't see anything that goes wrong between the physics and the drawn skeleton. See Docs/RagdollBench.md.
 */
class ReactionCapture
{
public:
	bool Begin(const ReactionCaptureOptions& options);

	/// Before the frame's physics step
	void PreStep(float32 delta_time);
	/// Once the frame has been drawn
	void PostFrame(float32 delta_time);

	FX_FORCE_INLINE bool IsFinished() const { return mbFinished; }

private:
	void SpawnTarget();
	void PlaceCamera();
	void FireEvents();
	void WriteFrame(float32 delta_time);

private:
	ReactionCaptureOptions mOptions;
	std::FILE* mpOut = nullptr;
	NPC* mpTarget = nullptr;
	Vec3f mTargetPosition = Vec3f::sZero;
	Vec3f mEye = Vec3f::sZero;
	float32 mTime = 0.0f;
	uint32 mNextEvent = 0;
	uint32 mFrame = 0;
	uint32 mSavedImages = 0;
	bool mbStarted = false;
	bool mbFinished = false;
};

} // namespace fx

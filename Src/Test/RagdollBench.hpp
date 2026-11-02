#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <cstdio>

namespace fx {

class NPC;

struct RagdollBenchOptions
{
	bool bEnabled = false;
	const char* Suite = "all";
	const char* OutPath = nullptr;
	/// Traces of the first walk phase's trials go here, one file per trial. No traces if null
	const char* TraceDir = nullptr;
	/// Goes through the game's own Tick(), drawing every frame, to check that leaving that out changes nothing
	bool bRender = false;
	uint32 ShardIndex = 0;
	uint32 ShardCount = 1;
};

/**
 * @brief Runs scripted shoves and shots on NPCs at a fixed timestep with nothing rendered and writes one JSON line per
 * trial, for Tools/RagdollBench/bench.py to turn into a report. See Docs/RagdollBench.md.
 */
class RagdollBench
{
public:
	static constexpr float32 scDeltaTime = 1.0f / 60.0f;

	bool Begin(const RagdollBenchOptions& options);
	void Tick();

	/// The two halves of Tick() around a physics step and an NPC update
	bool PreStep();
	void PostStep();

	FX_FORCE_INLINE bool IsFinished() const { return mStage == eStage::Finished; }

private:
	enum class eStage : uint8
	{
		WaitForModel,
		NextBatch,
		Running,
		Finished,
	};

	/// What stands in the way of a shove, along the direction of the push
	enum class eTerrain : uint8
	{
		Flat,
		Kerb,
		Drop,
		StairsUp,
		StairsDown,
		RampUp,
		RampDown,
	};

	struct Trial
	{
		char Name[48] = {};
		char Condition[16] = {};
		float32 Speed = 0.0f;
		float32 ShotImpulse = 0.0f;
		uint32 ShotCount = 1;
		uint32 ShotInterval = 0;
		float32 Direction = 0.0f;
		uint32 Phase = 0;
		bool bStill = false;
		/// All of the push lands on the chest and the limbs around it, as a thrown prop's does
		bool bLocalHit = false;
		bool bTrace = false;
		/// A real weapon hit: which body takes it and how far off centre (m, to the shooter's right)
		const char* HitPart = nullptr;
		float32 HitOffset = 0.0f;
		float32 Damage = 0.0f;
		eTerrain Terrain = eTerrain::Flat;
		float32 TerrainHeight = 0.0f;
	};

	/// How the whole reaction looks, standing, falling and on the ground alike
	struct Quality
	{
		SizedArray<JPH::Vec3> Spin;
		SizedArray<JPH::Vec3> PreviousSpin;
		SizedArray<float32> BodyJitter;
		SizedArray<float32> BodyLimit;
		int32 WorstGapFrame = 0;
		int32 WorstGapBody = 0;
		float32 JitterSum[4] = {};
		uint32 JitterCount = 0;
		uint32 TwitchFrames = 0;
		float32 PeakLimitExcess = 0.0f;
		uint32 LimitFrames = 0;
		float32 PeakJointGap = 0.0f;
		Vec3f HitBodyStart = Vec3f::sZero;
		Vec3f CenterStart = Vec3f::sZero;
		Vec3f ChestLeanStart = Vec3f::sZero;
		float32 YawStart = 0.0f;
		float32 HitBodyMove = 0.0f;
		float32 CenterMove[3] = {};
		float32 ChestLean = 0.0f;
		float32 YawResponse = 0.0f;
		float32 ExpectedYaw = 0.0f;
		float32 LeverHeight = 0.0f;
		float32 PeakRootJitter = 0.0f;
		Vec3f PelvisHistory[2] = { Vec3f::sZero, Vec3f::sZero };
		uint32 RestFrame = 0;
		/// Times the head turned back on itself against the chest, sideways and forwards, up to a fall and after it
		uint32 HeadSwings[2] = {};
		uint32 HeadFrames[2] = {};
		float32 HeadExtreme[2] = {};
		int32 HeadDirection[2] = {};
	};

	struct FootTrack
	{
		bool bPlanted = true;
		bool bStarted = false;
		int32 AirFrames = 0;
		int32 TakeoffFrame = 0;
		Vec3f TakeoffPosition = Vec3f::sZero;
		Vec3f LastPosition = Vec3f::sZero;
		float32 StanceHeight = 0.0f;
		float32 Slide = 0.0f;
		/// Furthest the contact point has slipped since this foot took at least a third of the body's weight. Slide
		/// adds up the foot's own movement, so rocking and rolling count there. Here they don't, since a roll leaves
		/// the contact point still
		float32 LoadedSlide = 0.0f;
		Vec3f LoadedSlip = Vec3f::sZero;
		bool bLoaded = false;
		Vec3f AirVelocity = Vec3f::sZero;
	};

	struct TrialRun
	{
		const Trial* pTrial = nullptr;
		NPC* pNPC = nullptr;
		int32 HitFrame = 0;
		uint32 ShotsFired = 0;

		uint64 Hash = 14695981039346656037ULL;
		Vec3f PreviousCenter = Vec3f::sZero;
		Vec3f PreHitVelocity = Vec3f::sZero;
		Vec3f HitOrigin = Vec3f::sZero;

		int32 FallFrame = -1;
		int32 ReturnFrame = -1;
		int32 FallReason = 0;

		float32 DeltaV = 0.0f;
		float32 Knock05 = 0.0f;
		float32 Knock10 = 0.0f;
		float32 PeakSpeed = 0.0f;
		float32 PeakTilt = 0.0f;
		float32 PeakHeadSpin = 0.0f;
		float32 PeakHandSpin = 0.0f;
		/// Fastest the trunk tips, about any horizontal axis
		float32 PeakTrunkSpin = 0.0f;
		/// The same rates from after the shove itself, once the muscles have had time to answer it
		float32 LateTrunkSpinSum = 0.0f;
		float32 LateHeadSpinSum = 0.0f;
		uint32 LateSpinCount = 0;
		/// Fastest any body moves in the whole trial, which is how a solver blowing up shows
		float32 PeakBodySpeed = 0.0f;
		float32 PlantedFootSpinSum = 0.0f;
		uint32 PlantedFootSpinCount = 0;
		float32 AssistImpulse = 0.0f;
		float32 KneeStanceSum = 0.0f;
		uint32 KneeStanceCount = 0;
		float32 PeakSwingKnee = 0.0f;

		uint32 Steps = 0;
		float32 StepLengthSum = 0.0f;
		float32 StepTimeSum = 0.0f;
		float32 MinStepTime = 0.0f;
		float32 PeakLift = 0.0f;
		float32 MinStepGap = 0.0f;
		int32 LastTakeoffFrame = -1;

		uint32 BalanceFrames = 0;
		float32 HangTime = 0.0f;
		uint32 ContactFlips = 0;
		float32 SwingOvertime = 0.0f;
		uint32 SwingRetries = 0;
		float32 PeakSwingTargetStep = 0.0f;
		float32 SwingLandingTravel = 0.0f;
		/// Times a foot in the air turned back on itself across the ground, and how long feet spent in the air
		uint32 FootReversals = 0;
		uint32 FootAirFrames = 0;
		float32 PelvisDrop = 0.0f;
		/// Height of the pelvis above where it was hit, when balance last had it (m)
		float32 PelvisRise = 0.0f;
		/// Rises and falls of the pelvis of at least a centimetre, which is what bobbing up and down looks like
		uint32 PelvisBobs = 0;
		float32 PelvisExtreme = 0.0f;
		float32 PelvisLastHeight = 0.0f;
		int32 PelvisDirection = 0;
		uint32 Stances = 0;
		float32 SlideSum = 0.0f;
		float32 PeakSlide = 0.0f;
		float32 PeakLoadedSlide = 0.0f;

		FootTrack Feet[2];
		Quality Look;
		SizedArray<float32> Trace;
		uint32 TraceFrames = 0;
	};

	void BuildTrials();
	void AddTrial(const Trial& trial);
	void AddPushTrials();
	void AddChestTrials();
	void AddStillTrials();
	void AddWeaponTrials();
	void AddReactionTrials();
	void AddTerrainTrials();
	void SampleQuality(TrialRun& run, int32 since_hit);
	int32 FindHitBody(const TrialRun& run) const;
	void AddPerfTrials();

	void CreateFloor();
	/// Builds the trial's terrain around a position on the floor and returns how far above the floor the NPC starts
	float32 CreateTerrain(const Trial& trial, const Vec3f position);
	void AddTerrainBox(const Vec3f position, const Vec3f direction, float32 near_distance, float32 far_distance,
					   float32 top);
	/// A slope going away from the position, its top face passing through the near end at the given height
	void AddTerrainRamp(const Vec3f position, const Vec3f direction, float32 near_distance, float32 near_height,
						float32 length, float32 pitch);
	void RemoveTerrain();
	bool StartNextBatch();
	void StartBatch(uint32 batch_index);
	void FinishBatch();

	void FireShots(TrialRun& run);
	void Sample(TrialRun& run);
	void SampleFeet(TrialRun& run);
	void FinishStance(TrialRun& run, FootTrack& foot);
	void WriteTrial(const TrialRun& run);
	void WriteTrace(const TrialRun& run);

private:
	RagdollBenchOptions mOptions;
	eStage mStage = eStage::WaitForModel;
	std::FILE* mpOut = nullptr;

	SizedArray<Trial> mTrials;
	uint32 mBatchSize = 16;
	uint32 mNextBatch = 0;

	SizedArray<TrialRun> mRuns;
	SizedArray<JPH::BodyID> mTerrainBodies;
	int32 mFrame = 0;
	int32 mEndFrame = 0;
	int32 mFirstHitFrame = 0;

	double mIdleMs = 0.0;
	uint32 mIdleFrames = 0;
	double mActiveMs = 0.0;
	uint32 mActiveFrames = 0;
	uint32 mActiveNPCSum = 0;
};

} // namespace fx

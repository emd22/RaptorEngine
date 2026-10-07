#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyActivationListener.h>
#include <ThirdParty/Jolt/Physics/Body/BodyManager.h>
#include <ThirdParty/Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <ThirdParty/Jolt/Physics/Collision/ObjectLayer.h>
#include <ThirdParty/Jolt/Physics/PhysicsSystem.h>

#include <Core/Log.hpp>
#include <Core/MemberRef.hpp>
#include <Core/SizedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <atomic>
#include <mutex>
#include <vector>

namespace JPH {
class JobSystemThreadPool;
class TempAllocatorImpl;
}; // namespace JPH

namespace fx {

namespace physics {

namespace PhLayer {
using Type = JPH::ObjectLayer;
static constexpr JPH::ObjectLayer Static = 0;
static constexpr JPH::ObjectLayer Dynamic = 1;
static constexpr JPH::ObjectLayer Deactivated = 2;
static constexpr JPH::ObjectLayer NumLayers = 3;
}; // namespace PhLayer

namespace PhBroadPhaseLayer {
using Type = JPH::BroadPhaseLayer;
static constexpr JPH::BroadPhaseLayer Static(0);
static constexpr JPH::BroadPhaseLayer Dynamic(1);
static constexpr JPH::BroadPhaseLayer Deactivated(2);
static constexpr uint32 NumLayers(3);
}; // namespace PhBroadPhaseLayer


// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class PhBPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
	PhBPLayerInterfaceImpl()
	{
		// Create a mapping table from object to broad phase layer
		mObjectToBroadPhase[PhLayer::Static] = PhBroadPhaseLayer::Static;
		mObjectToBroadPhase[PhLayer::Dynamic] = PhBroadPhaseLayer::Dynamic;
	}

	virtual uint32 GetNumBroadPhaseLayers() const override { return PhBroadPhaseLayer::NumLayers; }

	virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
	{
		JPH_ASSERT(inLayer < PhLayer::NumLayers);
		return mObjectToBroadPhase[inLayer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
	{
		switch ((JPH::BroadPhaseLayer::Type)inLayer) {
		case (JPH::BroadPhaseLayer::Type)PhBroadPhaseLayer::Static:
			return "Static";
		case (JPH::BroadPhaseLayer::Type)PhBroadPhaseLayer::Dynamic:
			return "Dynamic";
		default:
			JPH_ASSERT(false);
			return "Invalid";
		}
	}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
	JPH::BroadPhaseLayer mObjectToBroadPhase[PhLayer::NumLayers];
};

/// Class that determines if an object layer can collide with a broadphase layer
class BodyVsBPLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1) {
		case PhLayer::Static:
			return inLayer2 == PhBroadPhaseLayer::Dynamic;
		case PhLayer::Dynamic:
			return true;
		case PhLayer::Deactivated:
			return false;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};


/// Class that determines if two object layers can collide
class BodyLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
	virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
	{
		switch (inObject1) {
		case PhLayer::Static:
			return inObject2 == PhLayer::Dynamic; // Non moving only collides with moving
		case PhLayer::Dynamic:
			return true; // Moving collides with everything
		case PhLayer::Deactivated:
			return false;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};


namespace RagdollUserData {
static constexpr uint64 scTag = 0x52474C4400000000ull;
static constexpr uint64 scTagMask = 0xFFFFFFFF00000000ull;

FX_FORCE_INLINE bool Matches(uint64 user_data) { return (user_data & scTagMask) == scTag; }
FX_FORCE_INLINE uint32 GetSerial(uint64 user_data) { return static_cast<uint32>(user_data & 0xFFFFFFFFull); }
} // namespace RagdollUserData

struct RagdollImpact
{
	Vec3f Point = Vec3f::sZero;
	Vec3f Normal = Vec3f::sZero;
	float32 Speed = 0.0f;
	uint32 RagdollSerial = 0;
};

// An example contact listener
class PhContactListener : public JPH::ContactListener
{
public:
	static constexpr size_t scMaxQueuedImpacts = 64;

	void SetMinImpactSpeed(float32 speed) { mMinImpactSpeed.store(speed); }

	void DrainImpacts(std::vector<RagdollImpact>& out_impacts)
	{
		std::lock_guard<std::mutex> guard(mImpactsMutex);
		out_impacts.insert(out_impacts.end(), mImpacts.begin(), mImpacts.end());
		mImpacts.clear();
	}

	// See: ContactListener
	virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
												  JPH::RVec3Arg inBaseOffset,
												  const JPH::CollideShapeResult& inCollisionResult) override
	{
		const JPH::ValidateResult result = ContactListener::OnContactValidate(inBody1, inBody2, inBaseOffset,
																			  inCollisionResult);

		return result;
	}

	virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
								const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
	{
		const bool first_is_ragdoll = RagdollUserData::Matches(inBody1.GetUserData());
		const bool second_is_ragdoll = RagdollUserData::Matches(inBody2.GetUserData());

		if (first_is_ragdoll == second_is_ragdoll || inManifold.mRelativeContactPointsOn2.empty()) {
			return;
		}

		const JPH::Body& ragdoll_body = first_is_ragdoll ? inBody1 : inBody2;
		const JPH::Body& surface_body = first_is_ragdoll ? inBody2 : inBody1;

		if (!surface_body.IsStatic()) {
			return;
		}

		const JPH::Vec3 surface_normal = first_is_ragdoll ? -inManifold.mWorldSpaceNormal
														  : inManifold.mWorldSpaceNormal;
		const JPH::RVec3 point = first_is_ragdoll ? inManifold.GetWorldSpaceContactPointOn2(0)
												  : inManifold.GetWorldSpaceContactPointOn1(0);

		const float32 speed = -ragdoll_body.GetPointVelocity(point).Dot(surface_normal);

		if (speed < mMinImpactSpeed.load()) {
			return;
		}

		RagdollImpact impact;
		impact.Point = Vec3f(point.GetX(), point.GetY(), point.GetZ());
		impact.Normal = Vec3f(surface_normal.GetX(), surface_normal.GetY(), surface_normal.GetZ());
		impact.Speed = speed;
		impact.RagdollSerial = RagdollUserData::GetSerial(ragdoll_body.GetUserData());

		std::lock_guard<std::mutex> guard(mImpactsMutex);

		if (mImpacts.size() < scMaxQueuedImpacts) {
			mImpacts.push_back(impact);
		}
	}

	virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
									const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
	{
	}

	virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override {}

private:
	std::atomic<float32> mMinImpactSpeed = 1.0e9f;
	std::mutex mImpactsMutex;
	std::vector<RagdollImpact> mImpacts;
};

// An example activation listener
class PhBodyActivationListener : public JPH::BodyActivationListener
{
public:
	virtual void OnBodyActivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override {}

	virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override {}
};

struct RayResult
{
	bool bHit = false;
	Vec3f Point = Vec3f::sZero;
	/// World space normal of the surface at `Point`, facing back towards the ray
	Vec3f Normal = Vec3f::sZero;
	JPH::BodyID Body {};
};


class JoltPhysicsBackend
{
public:
	static constexpr uint32 scMaxBodies = 512;

public:
	JoltPhysicsBackend();

	void Create();
	void Update();
	void Destroy();

	void OptimizeBroadPhase();

	RayResult Raycast(const Vec3f origin, const Vec3f direction, JPH::BodyID ignore_body = JPH::BodyID()) const;
	SizedArray<JPH::BodyID> RaycastObjects(const Vec3f origin, const Vec3f direction) const;

	FLOAT4 RaycastGetFaceOfBox(JPH::Body* body, const Vec3f origin, const Vec3f direction) const;

	FX_FORCE_INLINE JPH::BodyInterface& GetBodyInterface() { return PhysicsSystem.GetBodyInterface(); }

	FX_FORCE_INLINE void SetMinRagdollImpactSpeed(float32 speed) { mContactListener.SetMinImpactSpeed(speed); }
	FX_FORCE_INLINE void DrainRagdollImpacts(std::vector<RagdollImpact>& out_impacts)
	{
		mContactListener.DrainImpacts(out_impacts);
	}

	~JoltPhysicsBackend();

public:
	JPH::PhysicsSystem PhysicsSystem;

	bool bPhysicsPaused = false;
	const float cTimeStep = 1.0f / 60.0f;

	MemberRef<JPH::TempAllocatorImpl> pTempAllocator;
	MemberRef<JPH::JobSystemThreadPool> pJobSystem;


private:
	PhBPLayerInterfaceImpl mBroadPhaseInterface;
	BodyVsBPLayerFilter mObjectVsBPLayerFilter;

	PhBodyActivationListener mBodyActivationListener;
	PhContactListener mContactListener;
	BodyLayerPairFilterImpl mObjectLayerPairFilter;


	bool mbIsInited = false;
};

} // namespace physics

} // namespace fx

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
#include <algorithm>
#include <atomic>
#include <mutex>

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

struct RagdollContact
{
	JPH::BodyID Body {};
	Vec3f Point = Vec3f::sZero;
	Vec3f Normal = Vec3f::sZero;
	float32 Speed = 0.0f;
	float32 OtherSpeed = 0.0f;
	float32 OtherMass = 0.0f;
	bool bOtherStatic = false;
	uint32 RagdollSerial = 0;

	FX_FORCE_INLINE bool IsStruck(float32 min_speed) const { return Speed >= min_speed && OtherSpeed >= min_speed; }
};

struct SoundImpact
{
	Vec3f Point = Vec3f::sZero;
	Vec3f Normal = Vec3f::sZero;
	float32 Speed = 0.0f;
	JPH::BodyID MovingBody {};
	JPH::BodyID SurfaceBody {};
	JPH::SubShapeID SurfaceSubShape {};
};

struct BodyGroundContact
{
	uint32 Count = 0;
	Vec3f Point = Vec3f::sZero;
	Vec3f Normal = Vec3f::sZero;
	float32 Speed = 0.0f;
};

// An example contact listener
class PhContactListener : public JPH::ContactListener
{
public:
	static constexpr size_t scMaxQueuedImpacts = 64;
	static constexpr size_t scMaxTrackedBodies = 512;

	PhContactListener()
	{
		mBodyContacts.InitSize(scMaxTrackedBodies);
		mRagdollContacts.InitCapacity(scMaxQueuedImpacts);
		mSoundImpacts.InitCapacity(scMaxQueuedImpacts);
	}

	void SetMinRagdollContactSpeed(float32 speed) { mMinRagdollContactSpeed.store(speed); }
	void SetMinSoundSpeed(float32 speed) { mMinSoundSpeed.store(speed); }

	void DrainSoundImpacts(SizedArray<SoundImpact>& out_impacts)
	{
		std::lock_guard<std::mutex> guard(mImpactsMutex);
		DrainQueue(mSoundImpacts, out_impacts);
	}

	void DrainRagdollContacts(SizedArray<RagdollContact>& out_contacts)
	{
		std::lock_guard<std::mutex> guard(mImpactsMutex);
		DrainQueue(mRagdollContacts, out_contacts);
	}

	void ClearBodyGroundContacts()
	{
		std::lock_guard<std::mutex> guard(mBodyContactMutex);
		for (BodyContactAccum& accum : mBodyContacts) {
			accum = BodyContactAccum {};
		}

		mGroundContactGeneration.fetch_add(1);
	}

	uint32 GetGroundContactGeneration() const { return mGroundContactGeneration.load(); }

	void GetBodyGroundContact(JPH::BodyID body_id, BodyGroundContact& out_contact) const
	{
		out_contact = BodyGroundContact {};

		const uint32 index = body_id.GetIndex();

		if (index >= mBodyContacts.Size) {
			return;
		}

		std::lock_guard<std::mutex> guard(mBodyContactMutex);
		const BodyContactAccum& accum = mBodyContacts[index];

		if (accum.Count == 0) {
			return;
		}

		out_contact.Count = accum.Count;
		out_contact.Point = Vec3f(accum.PointSum / static_cast<float32>(accum.Count));
		out_contact.Normal = Vec3f(accum.NormalSum.Normalized());
		out_contact.Speed = accum.Speed;
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
		QueueSoundImpact(inBody1, inBody2, inManifold);
		RecordBodyGroundContact(inBody1, inBody2, inManifold);

		const bool first_tagged = RagdollUserData::Matches(inBody1.GetUserData());
		const bool second_tagged = RagdollUserData::Matches(inBody2.GetUserData());

		if ((!first_tagged && !second_tagged) || inManifold.mRelativeContactPointsOn2.empty()) {
			return;
		}

		bool first_is_ragdoll = first_tagged;

		if (first_tagged && second_tagged) {
			if (inBody1.GetUserData() == inBody2.GetUserData() || inBody1.IsKinematic() == inBody2.IsKinematic()) {
				return;
			}

			first_is_ragdoll = inBody1.IsKinematic();
		}

		const JPH::Body& ragdoll_body = first_is_ragdoll ? inBody1 : inBody2;
		const JPH::Body& other_body = first_is_ragdoll ? inBody2 : inBody1;

		const JPH::Vec3 normal = first_is_ragdoll ? -inManifold.mWorldSpaceNormal : inManifold.mWorldSpaceNormal;
		const JPH::RVec3 point = first_is_ragdoll ? inManifold.GetWorldSpaceContactPointOn1(0)
												  : inManifold.GetWorldSpaceContactPointOn2(0);

		const JPH::Vec3 other_velocity = other_body.GetPointVelocity(point);
		const float32 closing = (other_velocity - ragdoll_body.GetPointVelocity(point)).Dot(normal);

		if (closing < mMinRagdollContactSpeed.load()) {
			return;
		}

		RagdollContact contact;
		contact.Body = ragdoll_body.GetID();
		contact.Point = Vec3f(point.GetX(), point.GetY(), point.GetZ());
		contact.Normal = Vec3f(normal.GetX(), normal.GetY(), normal.GetZ());
		contact.Speed = closing;
		contact.OtherSpeed = other_velocity.Dot(normal);
		contact.bOtherStatic = other_body.IsStatic();
		contact.RagdollSerial = RagdollUserData::GetSerial(ragdoll_body.GetUserData());

		if (other_body.IsDynamic()) {
			const float32 inverse_mass = other_body.GetMotionProperties()->GetInverseMass();

			if (inverse_mass > 0.0f) {
				contact.OtherMass = 1.0f / inverse_mass;
			}
		}

		std::lock_guard<std::mutex> guard(mImpactsMutex);

		if (mRagdollContacts.Size < scMaxQueuedImpacts) {
			mRagdollContacts.Insert(contact);
		}
	}

	virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
									const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
	{
		RecordBodyGroundContact(inBody1, inBody2, inManifold);
	}

	virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override {}

private:
	template <typename T>
	static void DrainQueue(SizedArray<T>& queue, SizedArray<T>& out_items)
	{
		if (!out_items.IsInited()) {
			out_items.InitCapacity(scMaxQueuedImpacts);
		}

		out_items.Clear();

		for (const T& item : queue) {
			out_items.Insert(item);
		}

		queue.Clear();
	}

	struct BodyContactAccum
	{
		uint32 Count = 0;
		JPH::Vec3 PointSum = JPH::Vec3::sZero();
		JPH::Vec3 NormalSum = JPH::Vec3::sZero();
		float32 Speed = 0.0f;
	};

	void RecordBodyGroundContact(const JPH::Body& inBody1, const JPH::Body& inBody2,
								 const JPH::ContactManifold& inManifold)
	{
		const bool first_is_body = inBody2.IsStatic() && RagdollUserData::Matches(inBody1.GetUserData());
		const bool second_is_body = inBody1.IsStatic() && RagdollUserData::Matches(inBody2.GetUserData());

		if (!first_is_body && !second_is_body) {
			return;
		}

		const JPH::Body& body = first_is_body ? inBody1 : inBody2;
		const uint32 index = body.GetID().GetIndex();

		if (index >= mBodyContacts.Size) {
			return;
		}

		const JPH::Vec3 normal = first_is_body ? -inManifold.mWorldSpaceNormal : inManifold.mWorldSpaceNormal;
		const uint32 point_count = static_cast<uint32>(first_is_body ? inManifold.mRelativeContactPointsOn1.size()
																	 : inManifold.mRelativeContactPointsOn2.size());

		if (point_count == 0) {
			return;
		}

		JPH::Vec3 point_sum = JPH::Vec3::sZero();

		for (uint32 i = 0; i < point_count; i++) {
			if (first_is_body) {
				point_sum += JPH::Vec3(inManifold.GetWorldSpaceContactPointOn1(i));
			}
			else {
				point_sum += JPH::Vec3(inManifold.GetWorldSpaceContactPointOn2(i));
			}
		}

		const JPH::Vec3 point = point_sum / static_cast<float32>(point_count);
		const float32 speed = -body.GetPointVelocity(JPH::RVec3(point)).Dot(normal);

		std::lock_guard<std::mutex> guard(mBodyContactMutex);
		BodyContactAccum& accum = mBodyContacts[index];

		accum.Count += 1;
		accum.PointSum += point;
		accum.NormalSum += normal;
		accum.Speed = std::max(accum.Speed, speed);
	}

	void QueueSoundImpact(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold)
	{
		if ((!inBody1.IsDynamic() && !inBody2.IsDynamic()) || inManifold.mRelativeContactPointsOn2.empty()) {
			return;
		}

		const JPH::RVec3 point = inManifold.GetWorldSpaceContactPointOn2(0);
		const JPH::Vec3 normal = inManifold.mWorldSpaceNormal;
		const float32 speed = (inBody1.GetPointVelocity(point) - inBody2.GetPointVelocity(point)).Dot(normal);

		if (speed < mMinSoundSpeed.load()) {
			return;
		}

		const bool second_is_surface = !inBody2.IsDynamic() || inBody1.IsDynamic();

		SoundImpact impact;
		impact.Point = Vec3f(point.GetX(), point.GetY(), point.GetZ());
		impact.Speed = speed;

		if (second_is_surface) {
			impact.Normal = Vec3f(-normal.GetX(), -normal.GetY(), -normal.GetZ());
			impact.MovingBody = inBody1.GetID();
			impact.SurfaceBody = inBody2.GetID();
			impact.SurfaceSubShape = inManifold.mSubShapeID2;
		}
		else {
			impact.Normal = Vec3f(normal.GetX(), normal.GetY(), normal.GetZ());
			impact.MovingBody = inBody2.GetID();
			impact.SurfaceBody = inBody1.GetID();
			impact.SurfaceSubShape = inManifold.mSubShapeID1;
		}

		std::lock_guard<std::mutex> guard(mImpactsMutex);

		if (mSoundImpacts.Size < scMaxQueuedImpacts) {
			mSoundImpacts.Insert(impact);
		}
	}

private:
	std::mutex mImpactsMutex;

	std::atomic<float32> mMinRagdollContactSpeed = 1.5f;
	SizedArray<RagdollContact> mRagdollContacts;

	std::atomic<float32> mMinSoundSpeed = 1.0e9f;
	SizedArray<SoundImpact> mSoundImpacts;

	mutable std::mutex mBodyContactMutex;
	std::atomic<uint32> mGroundContactGeneration = 0;
	SizedArray<BodyContactAccum> mBodyContacts;
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
	JPH::SubShapeID SubShape {};
};


class JoltPhysicsBackend
{
public:
	static constexpr uint32 scMaxBodies = 512;
	static constexpr int scCollisionSteps = 2;

public:
	JoltPhysicsBackend();

	void Create();
	void Update(float32 delta_time);
	void Destroy();

	void OptimizeBroadPhase();

	RayResult Raycast(const Vec3f origin, const Vec3f direction, JPH::BodyID ignore_body = JPH::BodyID()) const;
	RayResult RaycastStatic(const Vec3f origin, const Vec3f direction) const;
	SizedArray<JPH::BodyID> RaycastObjects(const Vec3f origin, const Vec3f direction) const;

	FLOAT4 RaycastGetFaceOfBox(JPH::Body* body, const Vec3f origin, const Vec3f direction) const;

	FX_FORCE_INLINE JPH::BodyInterface& GetBodyInterface() { return PhysicsSystem.GetBodyInterface(); }

	bool GetTriangleUserData(const JPH::BodyID body_id, const JPH::SubShapeID sub_shape, uint32& out_user_data) const;

	FX_FORCE_INLINE void SetMinRagdollContactSpeed(float32 speed) { mContactListener.SetMinRagdollContactSpeed(speed); }
	FX_FORCE_INLINE void SetMinSoundImpactSpeed(float32 speed) { mContactListener.SetMinSoundSpeed(speed); }

	FX_FORCE_INLINE void DrainSoundImpacts(SizedArray<SoundImpact>& out_impacts)
	{
		mContactListener.DrainSoundImpacts(out_impacts);
	}
	FX_FORCE_INLINE void DrainRagdollContacts(SizedArray<RagdollContact>& out_contacts)
	{
		mContactListener.DrainRagdollContacts(out_contacts);
	}

	FX_FORCE_INLINE float32 GetCollisionStepTime() const { return cTimeStep / static_cast<float32>(scCollisionSteps); }

	FX_FORCE_INLINE void ClearBodyGroundContacts() { mContactListener.ClearBodyGroundContacts(); }
	FX_FORCE_INLINE uint32 GetGroundContactGeneration() const
	{
		return mContactListener.GetGroundContactGeneration();
	}
	FX_FORCE_INLINE void GetBodyGroundContact(JPH::BodyID body_id, BodyGroundContact& out_contact) const
	{
		mContactListener.GetBodyGroundContact(body_id, out_contact);
	}

	~JoltPhysicsBackend();

public:
	JPH::PhysicsSystem PhysicsSystem;

	bool bPhysicsPaused = false;
	const float cTimeStep = 1.0f / 60.0f;
	float32 StepAccumulator = 0.0f;

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

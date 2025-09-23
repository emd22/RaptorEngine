#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/BodyActivationListener.h>
#include <ThirdParty/Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <ThirdParty/Jolt/Physics/Collision/ObjectLayer.h>
#include <ThirdParty/Jolt/Physics/PhysicsSystem.h>

#include <Core/FxTypes.hpp>

namespace JPH {
class JobSystemThreadPool;
class TempAllocatorImpl;
}; // namespace JPH

namespace FxPhysicsLayer {
using Type = JPH::ObjectLayer;
static constexpr JPH::ObjectLayer Static = 0;
static constexpr JPH::ObjectLayer Dynamic = 1;
static constexpr JPH::ObjectLayer NumLayers = 2;
}; // namespace FxPhysicsLayer

namespace FxPhysicsBroadPhaseLayer {
using Type = JPH::BroadPhaseLayer;
static constexpr JPH::BroadPhaseLayer Static(0);
static constexpr JPH::BroadPhaseLayer Dynamic(1);
static constexpr uint NumLayers(2);
}; // namespace FxPhysicsBroadPhaseLayer


// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class FxPhysicsBPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    FxPhysicsBPLayerInterfaceImpl()
    {
        // Create a mapping table from object to broad phase layer
        mObjectToBroadPhase[FxPhysicsLayer::Static] = FxPhysicsBroadPhaseLayer::Static;
        mObjectToBroadPhase[FxPhysicsLayer::Dynamic] = FxPhysicsBroadPhaseLayer::Dynamic;
    }

    virtual uint GetNumBroadPhaseLayers() const override { return FxPhysicsBroadPhaseLayer::NumLayers; }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        JPH_ASSERT(inLayer < FxPhysicsLayer::NumLayers);
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
        case (JPH::BroadPhaseLayer::Type)FxPhysicsBroadPhaseLayer::Static:
            return "Static";
        case (JPH::BroadPhaseLayer::Type)FxPhysicsBroadPhaseLayer::Dynamic:
            return "Dynamic";
        default:
            JPH_ASSERT(false);
            return "Invalid";
        }
    }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[FxPhysicsLayer::NumLayers];
};

/// Class that determines if an object layer can collide with a broadphase layer
class FxPhysicsObjectVsBPLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1) {
        case FxPhysicsLayer::Static:
            return inLayer2 == FxPhysicsBroadPhaseLayer::Dynamic;
        case FxPhysicsLayer::Dynamic:
            return true;
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};


/// Class that determines if two object layers can collide
class FxPhysicsObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
    {
        switch (inObject1) {
        case FxPhysicsLayer::Static:
            return inObject2 == FxPhysicsLayer::Dynamic; // Non moving only collides with moving
        case FxPhysicsLayer::Dynamic:
            return true; // Moving collides with everything
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};


// An example contact listener
class FxPhysicsContactListener : public JPH::ContactListener
{
public:
    // See: ContactListener
    virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                                  JPH::RVec3Arg inBaseOffset,
                                                  const JPH::CollideShapeResult& inCollisionResult) override
    {
        FxLogInfo("Contact validate callback");

        const JPH::ValidateResult result = ContactListener::OnContactValidate(inBody1, inBody2, inBaseOffset,
                                                                              inCollisionResult);

        return result;
    }

    virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
    {
        FxLogInfo("A contact was added");
    }

    virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                    const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
    {
        FxLogInfo("A contact was persisted");
    }

    virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
    {
        FxLogInfo("A contact was removed");
    }
};

// An example activation listener
class FxPhysicsBodyActivationListener : public JPH::BodyActivationListener
{
public:
    virtual void OnBodyActivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override
    {
        FxLogInfo("A body got activated");
    }

    virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64 inBodyUserData) override
    {
        FxLogInfo("A body went to sleep");
    }
};


class FxPhysicsJolt
{
public:
    FxPhysicsJolt();

    void Create();
    void Update();
    void Destroy();

    void OptimizeBroadPhase();

    ~FxPhysicsJolt();

public:
    JPH::PhysicsSystem PhysicsSystem;

    bool bPhysicsPaused = false;

private:
    FxMemberRef<JPH::TempAllocatorImpl> mpTempAllocator;
    FxMemberRef<JPH::JobSystemThreadPool> mpJobSystem;

    FxPhysicsBPLayerInterfaceImpl mBroadPhaseInterface;
    FxPhysicsObjectVsBPLayerFilter mObjectVsBPLayerFilter;

    FxPhysicsBodyActivationListener mBodyActivationListener;
    FxPhysicsContactListener mContactListener;
    FxPhysicsObjectLayerPairFilterImpl mObjectLayerPairFilter;

    const float mDeltaTime = 1.0f / 60.0f;

    bool mbIsInited = false;
};

#pragma once

#include <ThirdParty/Jolt/Jolt.h>
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

class FxPhysicsJolt
{
public:
    FxPhysicsJolt();

    void Create();
    void Update();
    void Destroy();

    ~FxPhysicsJolt();

public:
    JPH::PhysicsSystem PhysicsSystem;

    bool bPhysicsPaused = false;

private:
    FxMemberRef<JPH::TempAllocatorImpl> mpTempAllocator;
    FxMemberRef<JPH::JobSystemThreadPool> mpJobSystem;

    const float mDeltaTime = 1.0f / 60.0f;

    bool mbIsInited = false;
};

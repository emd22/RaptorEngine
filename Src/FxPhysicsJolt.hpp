#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/PhysicsSystem.h>

#include <Core/FxTypes.hpp>

namespace JPH {
class JobSystemThreadPool;
class TempAllocatorImpl;
}; // namespace JPH

class FxPhysicsJolt
{
public:
    FxPhysicsJolt();

    void Create();

    void Update();

    void Destroy();


    ~FxPhysicsJolt();

public:
private:
    JPH::PhysicsSystem mPhysicsSystem;

    FxMemberRef<JPH::TempAllocatorImpl> mpTempAllocator;
    FxMemberRef<JPH::JobSystemThreadPool> mpJobSystem;

    const float mDeltaTime = 1.0f / 60.0f;
};

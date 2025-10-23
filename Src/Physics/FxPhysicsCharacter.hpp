#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Character/CharacterVirtual.h>

#include <Core/FxTypes.hpp>

class FxPhysicsCharacter
{
    static constexpr float32 scStandingHeight = 1.75f;

public:
    FxPhysicsCharacter() {}


public:
    JPH::Ref<JPH::CharacterVirtual> CharacterVirtual;
};

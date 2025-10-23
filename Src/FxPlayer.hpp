#pragma once

#include <Physics/FxPhysicsObject.hpp>
#include <Renderer/FxCamera.hpp>

class FxPlayer
{
public:
    FxPlayer();

    void Update();

    void MoveBy(const FxVec3f by);

    ~FxPlayer();

public:
    FxPhysicsObject Physics;
    FxRef<FxPerspectiveCamera> pCamera { nullptr };
};

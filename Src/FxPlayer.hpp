#pragma once

#include <Core/FxRef.hpp>
#include <Math/FxMathUtil.hpp>
#include <Math/FxVec2.hpp>
#include <Physics/FxPhysicsPlayer.hpp>
#include <Renderer/FxCamera.hpp>

class FxPlayer
{
    const FxVec3f cMovementSpeed = FxVec3f(5.0f);

public:
    FxPlayer();

    void Update(float32 delta_time);
    void MoveBy(const FxVec3f& by);

    void TeleportBy(const FxVec3f& by)
    {
        SyncPhysicsToPlayer();

        Position += by;
        Physics.Teleport(Position);
    }

    FX_FORCE_INLINE void Move(const FxVec3f& offset)
    {
        const FxVec3f forward = Direction * offset.Z;
        const FxVec3f right = Direction.Cross(FxVec3f::sUp) * offset.X;

        Physics.ApplyMovement((forward + right) * FxVec3f(1, 0, 1) * cMovementSpeed);

        // MoveBy();
    }

    void RotateHead(const FxVec2f& xy)
    {
        pCamera->Rotate(xy.X, xy.Y);

        RequireUpdate();
        // UpdateDirection();
    }

    ~FxPlayer();

private:
    FX_FORCE_INLINE void RequireUpdate()
    {
        mbUpdateDirection = true;
        RequirePhysicsUpdate();
    }
    FX_FORCE_INLINE void RequirePhysicsUpdate() { mbUpdatePhysicsTransform = true; }

    FX_FORCE_INLINE void UpdateDirection()
    {
        // if (!mbUpdateDirection) {
        //     return;
        // }

        const float32 c_angley = std::cos(pCamera->mAngleY);
        const float32 c_anglex = std::cos(pCamera->mAngleX);
        const float32 s_angley = std::sin(pCamera->mAngleY);
        const float32 s_anglex = std::sin(pCamera->mAngleX);

        Direction.Set(c_angley * s_anglex, s_angley, c_angley * c_anglex);
        Direction.NormalizeIP();

        // pCamera->Position = Position;

        pCamera->UpdateViewMatrix(Direction);

        // mbUpdateDirection = false;
    }

    FX_FORCE_INLINE void SyncPhysicsToPlayer() { Position.FromJoltVec3(Physics.pPlayerVirt->GetPosition()); }

public:
    FxPhysicsPlayer Physics;
    FxRef<FxPerspectiveCamera> pCamera { nullptr };

    FxVec3f Direction = FxVec3f::sZero;
    FxVec3f Position = FxVec3f::sZero;

private:
    FxVec3f mCameraOffset = FxVec3f::sZero;

    bool mbUpdateDirection : 1 = true;
    bool mbUpdatePhysicsTransform : 1 = true;
};

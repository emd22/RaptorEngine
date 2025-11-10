#pragma once

#include <Core/FxRef.hpp>
#include <Math/FxMathUtil.hpp>
#include <Math/FxVec2.hpp>
#include <Physics/FxPhysicsPlayer.hpp>
#include <Renderer/FxCamera.hpp>

class FxPlayer
{
    const FxVec3f cMaxWalkSpeed = FxVec3f(2.3f);
    const FxVec3f cMaxSprintSpeed = FxVec3f(3.5f);

    const float32 cMovementLerpSpeed = 0.025f;
    const float32 cJumpForceReduction = 0.010f;

public:
    FxPlayer();

    void Update(float32 delta_time);
    void MoveBy(const FxVec3f& by);

    void Jump();

    void TeleportBy(const FxVec3f& by)
    {
        SyncPhysicsToPlayer();

        Position += by;
        Physics.Teleport(Position);
    }

    void Move(float delta_time, const FxVec3f& offset);

    void RotateHead(const FxVec2f& xy)
    {
        pCamera->Rotate(xy.X, xy.Y);

        RequireUpdate();
        // UpdateDirection();
    }


    FX_FORCE_INLINE FxVec3f GetForward() { return FxVec3f(pCamera->ViewMatrix.Columns[2]).NormalizeIP(); }

    FX_FORCE_INLINE FxVec3f GetRight() { return GetForward().Cross(FxVec3f::sUp); }
    FX_FORCE_INLINE FxVec3f GetUp() { return GetRight().Cross(Direction); }

    ~FxPlayer();

private:
    FX_FORCE_INLINE void RequireUpdate()
    {
        mbUpdateDirection = true;
        RequirePhysicsUpdate();
    }

    FX_FORCE_INLINE void MarkApplyingUserForce() { mbIsApplyingUserForce = true; }

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

    bool bIsSprinting = false;
    float JumpForce = 0.0f;

private:
    FxVec3f mCameraOffset = FxVec3f::sZero;

    FxVec3f mUserForce = FxVec3f::sZero;

    bool mbUpdateDirection : 1 = true;
    bool mbIsApplyingUserForce : 1 = false;
    bool mbUpdatePhysicsTransform : 1 = true;
};

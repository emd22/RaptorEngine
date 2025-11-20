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
    FxPlayer() = default;

    void Create();

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

        RequireDirectionUpdate();
    }

    ~FxPlayer();

private:
    FX_FORCE_INLINE void RequireDirectionUpdate() { mbUpdateDirection = true; }
    FX_FORCE_INLINE void RequirePhysicsUpdate() { mbUpdatePhysicsTransform = true; }

    FX_FORCE_INLINE void MarkApplyingUserForce() { mbIsApplyingUserForce = true; }

    FX_FORCE_INLINE void UpdateDirection()
    {
        if (!mbUpdateDirection) {
            return;
        }

        const float32 c_anglex = std::cos(pCamera->mAngleX);
        const float32 s_anglex = std::sin(pCamera->mAngleX);

        MovementDirection.Set(s_anglex, 1.0f, c_anglex);
        // CameraDirection = FxVec3f(c_angley, s_angley, c_angley) * MovementDirection;
        // Clear the movement direction's Y. This is because sin(mAngleY) = 0 when mAngleY is zero.
        MovementDirection.Y = 0.0f;

        // CameraDirection.NormalizeIP();

        mbUpdateDirection = false;
    }

    FX_FORCE_INLINE void SyncPhysicsToPlayer() { Position.FromJoltVec3(Physics.pPlayerVirt->GetPosition()); }

public:
    FxPhysicsPlayer Physics;
    FxRef<FxPerspectiveCamera> pCamera { nullptr };

    /**
     * @brief The direction that the player is facing. This is the direction the player moves in and disregards the
     * pitch of the camera.
     */
    FxVec3f MovementDirection = FxVec3f::sForward;

    FxVec3f Position = FxVec3f::sZero;

    bool bIsSprinting = false;
    float JumpForce = 0.0f;

private:
    FxVec3f mCameraOffset = FxVec3f::sZero;

    FxVec3f mUserForce = FxVec3f::sZero;

    bool mbIsApplyingUserForce : 1 = false;
    bool mbUpdateDirection : 1 = true;
    bool mbUpdatePhysicsTransform : 1 = true;
};

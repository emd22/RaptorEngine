#pragma once

#include <Core/Ref.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Vec2.hpp>
#include <Physics/PhPlayer.hpp>
#include <Renderer/Camera.hpp>

namespace fx {

class Player
{
    const Vec3f scMaxWalkSpeed = Vec3f(4.0f);
    const Vec3f scMaxSprintSpeed = Vec3f(6.5f);

    static constexpr float32 scMovementLerpSpeed = 10.0f;

    // TODO: Break these out into config vars
    static constexpr float32 scSprintFov = 85.0f;
    static constexpr float32 scWalkingFov = 80.0f;

public:
    Player() = default;

    void Create();

    void Update(float64 delta_time);
    void MoveBy(const Vec3f& by);

    void Jump();

    /**
     * @brief Move the player and its physics by `offset`.
     */
    void TeleportBy(const Vec3f& offset)
    {
        SyncPhysicsToPlayer();

        Position += offset;
        Physics.Teleport(Position);
    }

    /**
     * @brief Move the player and its physics by `offset`.
     */
    void TeleportTo(const Vec3f& position)
    {
        Position = position;
        Physics.Teleport(Position);
    }

    void SetFlyMode(bool value);
    bool IsFlyMode() const { return Physics.bDisableGravity; };

    void Move(float64 delta_time, const Vec3f& offset);

    void RotateHead(const Vec2f& xy)
    {
        pCamera->Rotate(xy.X, xy.Y);

        RequireDirectionUpdate();
    }

    ~Player();

private:
    FX_FORCE_INLINE void RequireDirectionUpdate() { mbUpdateDirection = true; }
    FX_FORCE_INLINE void RequirePhysicsUpdate() { mbUpdatePhysicsTransform = true; }

    FX_FORCE_INLINE void MarkApplyingUserForce() { mbIsApplyingUserForce = true; }

    FX_FORCE_INLINE void UpdateDirection()
    {
        if (!mbUpdateDirection) {
            return;
        }

        float32 s_anglex, c_anglex;
        MathUtil::SinCos(pCamera->mAngleX, &s_anglex, &c_anglex);

        MovementDirection.Set(s_anglex, 0.0f, c_anglex);
        if (mbIsFlymode) {
            MovementDirection.Y = sin(pCamera->mAngleY);
        }

        // MovementDirection = Vec3f(c_angley, s_angley, c_angley) * MovementDirection;
        // Clear the movement direction's Y. This is because sin(mAngleY) = 0 when mAngleY is zero.
        // MovementDirection.Y = 0.0f;

        // CameraDirection.NormalizeIP();

        mbUpdateDirection = false;
    }

    FX_FORCE_INLINE void SyncPhysicsToPlayer() { Position.FromJoltVec3(Physics.pPlayerVirt->GetPosition()); }

public:
    PhPlayer Physics;
    Ref<PerspectiveCamera> pCamera { nullptr };

    /**
     * @brief The direction that the player is facing. This is the direction the player moves in and disregards the
     * pitch of the camera.
     */
    Vec3f MovementDirection = Vec3f::sForward;
    Vec3f Position = Vec3f::sZero;

    float JumpForce = 0.0f;

    bool bIsSprinting : 1 = false;

private:
    Vec3f mCameraOffset = Vec3f::sZero;
    float32 mHeadBobX = 0.0f;
    float32 mHeadBobY = 0.0f;
    float32 mBobCounterX = 0.0f;
    float32 mBobCounter = 0.0f;

    Vec3f mUserForce = Vec3f::sZero;

    bool mbIsApplyingUserForce : 1 = false;
    bool mbUpdateDirection : 1 = true;
    bool mbUpdatePhysicsTransform : 1 = true;

    bool mbIsFlymode : 1 = false;
};

} // namespace fx

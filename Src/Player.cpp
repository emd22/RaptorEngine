#include "Player.hpp"

#include <Core/RefUtil.hpp>
#include <Engine.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx {

using namespace renderer;

void Player::Create()
{
    pCamera = MakeRef<PerspectiveCamera>();

    pCamera->SetAspectRatio(gRenderer->Swapchain.GetAspectRatio());
    pCamera->SetFov(scWalkingFov);

    Physics.Create();

    // Since the physics position is the center of the capsule, we will use standing height / 2.
    mCameraOffset = Vec3f(0, PhPlayer::scStandingHeight / 2.0f, 0);
}

void Player::MoveBy(const Vec3f& by)
{
    Position += by;
    RequirePhysicsUpdate();
}

void Player::Jump()
{
    if (Physics.bIsGrounded && !mbIsFlymode) {
        JumpForce = 2.0f;
    }
}

void Player::SetFlyMode(bool value)
{
    mbIsFlymode = value;
    Physics.bDisableGravity = value;
}

void Player::Move(float64 delta_time, const Vec3f& offset)
{
    const Vec3f forward = MovementDirection * offset.Z;
    const Vec3f right = MovementDirection.Cross(Vec3f::sUp) * -offset.X;
    const Vec3f up = Vec3f::sUp * offset.Y;


    Vec3f movement_goal = (forward + right + up);

    if (movement_goal.Length() > 1e-3) {
        movement_goal.NormalizeIP();
    }

    mUserForce.SmoothInterpolate(movement_goal * (bIsSprinting ? scMaxSprintSpeed : scMaxWalkSpeed),
                                 scMovementLerpSpeed, delta_time);

    Vec3f force = mUserForce;

    if (!mbIsFlymode) {
        force.Y = JumpForce;
        JumpForce = 0;
    }

    Physics.ApplyMovement(force);
}

void Player::Update(float64 delta_time)
{
    UpdateDirection();

    Physics.Update(delta_time);
    SyncPhysicsToPlayer();

    pCamera->MoveTo(Position + mCameraOffset);

    if (!mUserForce.IsNearZero(0.1)) {
        if (bIsSprinting && pCamera->GetFov() < scSprintFov) {
            pCamera->SetFov(MathUtil::SmoothInterpolate(pCamera->GetFov(), scSprintFov, 8.0f, delta_time));
        }
        else if (!bIsSprinting && pCamera->GetFov() > scWalkingFov) {
            pCamera->SetFov(MathUtil::SmoothInterpolate(pCamera->GetFov(), scWalkingFov, 13.0f, delta_time));
        }
    }
    pCamera->Update();

    mbUpdatePhysicsTransform = false;
}

Player::~Player() {}

} // namespace fx

#include "FxPlayer.hpp"

void FxPlayer::Create()
{
    pCamera = FxMakeRef<FxPerspectiveCamera>();

    const float32 window_width = 1024;
    const float32 window_height = 720;

    pCamera->SetAspectRatio(window_width / window_height);

    Physics.Create();

    // Since the physics position is the center of the capsule, we will use standing height / 2.
    mCameraOffset = FxVec3f(0, PhPlayer::scStandingHeight / 2.0f, 0);
}

void FxPlayer::MoveBy(const FxVec3f& by)
{
    Position += by;
    RequirePhysicsUpdate();
}

void FxPlayer::Jump()
{
    if (Physics.bIsGrounded) {
        JumpForce = 4.0f;
    }
}

void FxPlayer::SetFlyMode(bool value)
{
    mbIsFlymode = value;
    Physics.bDisableGravity = value;
}

void FxPlayer::Move(float delta_time, const FxVec3f& offset)
{
    // FxVec3f new_direction = FxVec3f(sin(pCamera->mAngleX), 0, cos(pCamera->mAngleX));
    if (mbIsFlymode) {
        // new_direction.Y = (pCamera->mAngleY);
    }

    const FxVec3f forward = MovementDirection * offset.Z;
    const FxVec3f right = MovementDirection.Cross(FxVec3f::sUp) * offset.X;

    FxVec3f movement_goal = (forward + right);

    if (movement_goal.Length() > 1e-3) {
        movement_goal.NormalizeIP();
    }

    // mUserForce = movement_goal * (bIsSprinting ? cMaxSprintSpeed : cMaxWalkSpeed);

    mUserForce.SmoothInterpolate(movement_goal * (bIsSprinting ? cMaxSprintSpeed : cMaxWalkSpeed), cMovementLerpSpeed,
                                 delta_time);

    if (mbIsFlymode) {
        // mUserForce.Y = ;
    }
    else {
        mUserForce.Y = JumpForce;
        JumpForce = 0;
    }

    Physics.ApplyMovement(mUserForce);

    // mUserForce += offset;
    // const FxVec3f max_speed = FxVec3f(cMaxWalkSpeed);
    // mUserForce = FxVec3f::Min(FxVec3f::Max(mUserForce, -max_speed), max_speed);

    // MarkApplyingUserForce();
    // MoveBy();
}

void FxPlayer::Update(float32 delta_time)
{
    UpdateDirection();

    Physics.Update(delta_time);
    SyncPhysicsToPlayer();

    pCamera->MoveTo(Position + mCameraOffset);
    pCamera->Update();

    mbUpdatePhysicsTransform = false;
}

FxPlayer::~FxPlayer() {}

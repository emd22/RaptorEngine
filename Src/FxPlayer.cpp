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
        JumpForce = 0.5f;
    }
}

void FxPlayer::Move(float delta_time, const FxVec3f& offset)
{
    const FxVec3f new_direction = FxVec3f(sin(pCamera->mAngleX), 0, cos(pCamera->mAngleX));

    const FxVec3f forward = new_direction * offset.Z;
    const FxVec3f right = new_direction.Cross(FxVec3f::sUp) * offset.X;

    FxVec3f movement_goal = (forward + right);

    if (movement_goal.Length() > 1e-3) {
        movement_goal.NormalizeIP();
    }

    mUserForce = FxVec3f::Lerp(mUserForce, movement_goal * (bIsSprinting ? cMaxSprintSpeed : cMaxWalkSpeed),
                               cMovementLerpSpeed * delta_time);

    mUserForce.Y = JumpForce;
    JumpForce = std::lerp(JumpForce, 0.0, cJumpForceReduction * delta_time);


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

    // if (!mbUpdatePhysicsTransform) {
    //     return;
    // }

    Physics.Update(delta_time);
    SyncPhysicsToPlayer();

    // Physics.ApplyMovement(Direction * FxVec3f(1.0, 0.0, 1.0));

    FxVec3f camera_updated_position = mCameraOffset;
    camera_updated_position += Position;
    camera_updated_position.Y -= Physics.HeadRecoveryYOffset;

    pCamera->MoveTo(camera_updated_position);

    pCamera->Update();

    mbUpdatePhysicsTransform = false;
}

FxPlayer::~FxPlayer() {}

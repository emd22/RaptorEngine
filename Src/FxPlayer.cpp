#include "FxPlayer.hpp"

FxPlayer::FxPlayer()
{
    pCamera = FxMakeRef<FxPerspectiveCamera>();

    const float32 window_width = 1024;
    const float32 window_height = 720;

    pCamera->SetAspectRatio(window_width / window_height);

    Physics.Create();

    mCameraOffset = FxVec3f(0, FxPhysicsPlayer::scStandingHeight / 2.0f, 0);
}


void FxPlayer::MoveBy(const FxVec3f& by)
{
    Position += by;
    RequireUpdate();
}

void FxPlayer::Update(float32 delta_time)
{
    UpdateDirection();

    if (!mbUpdatePhysicsTransform) {
        return;
    }

    Physics.Update(delta_time);
    SyncPhysicsToPlayer();

    // Physics.ApplyMovement(Direction * FxVec3f(1.0, 0.0, 1.0));

    pCamera->MoveTo(mCameraOffset + Position);

    mbUpdatePhysicsTransform = false;
}

FxPlayer::~FxPlayer() {}

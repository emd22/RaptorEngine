#include "FxPlayer.hpp"

FxPlayer::FxPlayer()
{
    pCamera = FxMakeRef<FxPerspectiveCamera>();

    const float32 window_width = 1024;
    const float32 window_height = 720;

    pCamera->SetAspectRatio(window_width / window_height);
}

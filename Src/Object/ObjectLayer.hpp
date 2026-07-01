#pragma once

enum class eObjectLayer
{
    /**
     * @brief Objects that are on the world layer using the normal camera perspective.
     * This should be used for everything that is not attached directly to the player.
     */
    WorldLayer,

    /**
     * @brief Objects that are on the player layer use a modified perspective to reduce warping.
     * This should only be used on objects directly in front of the camera and attached to the player.
     */
    PlayerLayer,
};

#pragma once

#include <Math/Util.hpp>

#include <Math/Vec3.hpp>
#include <Math/Mat4.hpp>

class FxCamera
{
public:
    virtual void Update() = 0;

public:
    FxVec3f Position = FxVec3f(0.0f);

    FxMat4f ViewMatrix = FxMat4f::Identity;
    FxMat4f ProjectionMatrix = FxMat4f::Identity;
    FxMat4f VPMatrix = FxMat4f::Identity;

    FxMat4f InvViewMatrix = FxMat4f::Identity;
    FxMat4f InvProjectionMatrix = FxMat4f::Identity;

};

class FxPerspectiveCamera : public FxCamera
{
public:
    FxPerspectiveCamera() = default;

    FxPerspectiveCamera(float32 fov, float32 aspect_ratio, float32 near_plane, float32 far_plane)
        : mAspectRatio(aspect_ratio), mNearPlane(near_plane), mFarPlane(far_plane)
    {
        SetFov(fov);
    }

    void UpdateProjectionMatrix();

    float32 GetFov() const { return FxRadToDeg(mFovRad); }
    float32 GetFovRad() const { return mFovRad; }

    void SetFov(float32 fov)
    {
        SetFovRad(FxDegToRad(fov));
    }

    void SetFovRad(float32 fov_rad)
    {
        mFovRad = fov_rad;
        UpdateProjectionMatrix();
    }


    inline void Rotate(float32 angle_x, float32 angle_y)
    {
        mAngleX += angle_x;
        mAngleY += angle_y;

        RequireUpdate();
    }

    inline void MoveBy(const FxVec3f &offset)
    {
        Position += FxVec3f(offset.X, offset.Y, offset.Z);

        RequireUpdate();
    }

    inline void Move(const FxVec3f &offset)
    {
        const FxVec3f forward = Direction * -offset.Z;
        const FxVec3f right = Direction.Cross(FxVec3f::Up) * offset.X;

        MoveBy(forward + right);
    }

    inline void SetAspectRatio(float32 aspect_ratio)
    {
        mAspectRatio = aspect_ratio;

        UpdateProjectionMatrix();
    }

    void Update() override;

    inline void RequireUpdate() { mRequiresUpdate = true; }

public:
    FxVec3f Direction = FxVec3f(0.0f);

private:
    bool mRequiresUpdate = true;

    float mAngleX = 0.0f;
    float mAngleY = 0.0f;

    float32 mFovRad = FxDegToRad(80.0f);

    float32 mAspectRatio = 1.0f;
    float32 mNearPlane = 1000.0f;
    float32 mFarPlane = 0.01f;
};

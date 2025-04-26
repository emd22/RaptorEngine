#pragma once

#include <Math/Util.hpp>

#include <Math/Vec3.hpp>
#include <Math/Mat4.hpp>

class FxCamera
{
public:
    virtual void Update() = 0;



public:
    Vec3f Position = Vec3f(0.0f);

    Mat4f ViewMatrix = Mat4f::Identity();
    Mat4f ProjectionMatrix = Mat4f::Identity();
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

    inline void SetAspectRatio(float32 aspect_ratio)
    {
        mAspectRatio = aspect_ratio;
        UpdateProjectionMatrix();
    }

    void Update() override;

    inline void RequireUpdate() { mRequiresUpdate = true; }

public:
    Vec3f Direction = Vec3f(0.0f);

private:
    bool mRequiresUpdate = true;

    float mAngleX = 0.0f;
    float mAngleY = 0.0f;

    float32 mFovRad = FxDegToRad(85.0f);

    float32 mAspectRatio = 1.0f;
    float32 mNearPlane = 0.01f;
    float32 mFarPlane = 1000.0f;
};

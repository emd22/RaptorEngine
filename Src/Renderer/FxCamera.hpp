#pragma once

#include <Math/FxMathConsts.hpp>
#include <Math/FxMathUtil.hpp>
#include <Math/FxQuat.hpp>
#include <Math/FxVec3.hpp>
#include <Math/Mat4.hpp>

enum class FxCameraType
{
    eNone,
    eOrthographic,
    ePerspective,
};

class FxCamera
{
public:
    static constexpr FxCameraType scType = FxCameraType::eNone;

public:
    virtual void Update() = 0;

    virtual void UpdateProjectionMatrix() = 0;

    FX_FORCE_INLINE FxVec3f GetForwardVector() { return Direction; }
    FX_FORCE_INLINE FxVec3f GetRightVector() { return GetForwardVector().Cross(FxVec3f::sUp).Normalize(); }
    FX_FORCE_INLINE FxVec3f GetUpVector() { return GetRightVector().Cross(Direction).Normalize(); }


    FX_FORCE_INLINE void MoveBy(const FxVec3f& offset)
    {
        Position += offset;
        RequireTransformUpdate();
    }

    FX_FORCE_INLINE void MoveTo(const FxVec3f& position)
    {
        Position = position;
        RequireTransformUpdate();
    }

    inline void RequireTransformUpdate() { mbUpdateTransform = true; }

    virtual ~FxCamera() {}

public:
    FxMat4f ViewMatrix = FxMat4f::sIdentity;
    FxMat4f ProjectionMatrix = FxMat4f::sIdentity;
    FxMat4f VPMatrix = FxMat4f::sIdentity;

    FxMat4f InvViewMatrix = FxMat4f::sIdentity;
    FxMat4f InvProjectionMatrix = FxMat4f::sIdentity;

    FxVec3f Position = FxVec3f::sZero;
    FxVec3f Direction = FxVec3f::sForward;

    bool mbUpdateTransform : 1 = true;

    float32 mNearPlane = 1000.0f;
    float32 mFarPlane = 0.01f;
};

class FxOrthoCamera : public FxCamera
{
public:
    static constexpr FxCameraType scType = FxCameraType::eOrthographic;

public:
    FxOrthoCamera() { Update(); }

    void Update() override;
};

class FxPerspectiveCamera : public FxCamera
{
public:
    static constexpr FxCameraType scType = FxCameraType::ePerspective;

public:
    FxPerspectiveCamera() { Update(); }

    FxPerspectiveCamera(float32 fov, float32 aspect_ratio, float32 near_plane, float32 far_plane)
        : mAspectRatio(aspect_ratio)
    {
        SetFov(fov);
        Update();

        mNearPlane = near_plane;
        mFarPlane = far_plane;
    }

    void UpdateProjectionMatrix() override;

    float32 GetFov() const { return FxRadToDeg(mFovRad); }
    float32 GetFovRad() const { return mFovRad; }

    void SetFov(float32 fov) { SetFovRad(FxDegToRad(fov)); }

    void SetFovRad(float32 fov_rad)
    {
        mFovRad = fov_rad;
        UpdateProjectionMatrix();
    }

    inline void Rotate(float32 angle_x, float32 angle_y)
    {
        mAngleX = LimitRotation(mAngleX + angle_x);
        mAngleY = LimitRotation(mAngleY + angle_y);

        constexpr float cOffset = 0.01f;

        constexpr float cMinY = -FX_HALF_PI + cOffset;
        constexpr float cMaxY = FX_HALF_PI - cOffset;

        mAngleY = FxMath::Clamp(mAngleY, cMinY, cMaxY);

        RequireTransformUpdate();
    }


    FX_FORCE_INLINE void SetAspectRatio(float32 aspect_ratio)
    {
        mAspectRatio = aspect_ratio;

        UpdateProjectionMatrix();
    }

    void Update() override;
    void UpdateViewMatrix();

    FX_FORCE_INLINE FxVec3f GetRotation() { return FxVec3f(mAngleY, mAngleX, 0); }

    ~FxPerspectiveCamera() override {}

private:
    inline float32 LimitRotation(float32 v)
    {
        constexpr float32 cf2Pi = FX_2PI;

        if (v < -cf2Pi) {
            return cf2Pi - 1e-5;
        }
        else if (v > cf2Pi) {
            return -cf2Pi + 1e-5;
        }
        return v;
    }

public:
    float mAngleX = 0.0f;
    float mAngleY = 0.0f;

    FxMat4f WeaponVPMatrix = FxMat4f::sIdentity;
    FxMat4f WeaponProjectionMatrix = FxMat4f::sIdentity;

private:
    float32 mFovRad = FxDegToRad(80.0f);
    float32 mWeaponFov = FxDegToRad(70.0f);

    float32 mAspectRatio = 1.0f;
};

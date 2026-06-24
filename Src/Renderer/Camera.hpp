#pragma once

#include <Math/Mat4.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectLayer.hpp>

namespace fx {

enum class eCameraType
{
    None,
    Orthographic,
    Perspective,
};

class Camera
{
public:
    void Update();

    FX_FORCE_INLINE Vec3f GetForwardVector() { return Direction; }
    FX_FORCE_INLINE Vec3f GetRightVector() { return GetForwardVector().Cross(Vec3f::sUp).Normalize(); }
    FX_FORCE_INLINE Vec3f GetUpVector() { return GetRightVector().Cross(Direction).Normalize(); }

    virtual const Mat4f& GetCameraMatrix(eObjectLayer layer) const { return mCameraMatrix; }
    virtual void OnWindowResize(const Vec2u& size) {};

    FX_FORCE_INLINE void SetNearPlane(float32 near)
    {
        RequireMatrixUpdate();
        mNearPlane = near;
    }

    FX_FORCE_INLINE void SetFarPlane(float32 far)
    {
        RequireMatrixUpdate();
        mFarPlane = far;
    }

    FX_FORCE_INLINE void MoveBy(const Vec3f& offset)
    {
        Position += offset;
        RequireTransformUpdate();
    }

    FX_FORCE_INLINE void MoveTo(const Vec3f& position)
    {
        Position = position;
        RequireTransformUpdate();
    }


    inline void RequireTransformUpdate() { mbUpdateTransform = true; }

    /**
     * @brief Notify that values for the projection matrix have changed and the matrix is out of date.
     * This is used to defer recalculating the matrix until all modifications have been made.
     */
    inline void RequireMatrixUpdate() { mbRequireMatrixUpdate = true; }

    virtual ~Camera() {}

private:
    void UpdateViewMatrix();


protected:
    virtual void UpdateProjectionMatrix() = 0;

    /**
     * @brief Updates and calculates the final matrix to be presented to the renderer.
     */
    virtual void UpdateCameraMatrix() = 0;

public:
    Mat4f ViewMatrix = Mat4f::sIdentity;
    Mat4f ProjectionMatrix = Mat4f::sIdentity;

    Mat4f InvViewMatrix = Mat4f::sIdentity;
    Mat4f InvProjectionMatrix = Mat4f::sIdentity;

    Vec3f Position = Vec3f::sZero;
    Vec3f Direction = Vec3f::sForward;

    Vec3f Target = Vec3f::sZero;

    float mAngleX = 0.0f;
    float mAngleY = 0.0f;

    bool mbUpdateTransform : 1 = true;
    bool mbRequireMatrixUpdate : 1 = true;

    float32 mNearPlane = 1000.0f;
    float32 mFarPlane = 0.01f;

    bool bLookatTarget = false;

protected:
    Mat4f mCameraMatrix = Mat4f::sIdentity;
};

///////////////////////////////////////////////
// Orthographic Camera
///////////////////////////////////////////////

class OrthoCamera final : public Camera
{
public:
    static constexpr eCameraType scType = eCameraType::Orthographic;

public:
    OrthoCamera() { Update(); }

    void UpdateProjectionMatrix() override;
    void UpdateCameraMatrix() override;

    void OnWindowResize(const Vec2u& size) override;

    void ResolveViewToTexels(float32 texture_res);

    FX_FORCE_INLINE void SetBounds(float32 width, float32 height)
    {
        mWidth = width;
        mHeight = height;

        RequireMatrixUpdate();
    }

    ~OrthoCamera() override {}

private:
    float32 mWidth = 50.0f;
    float32 mHeight = 50.0f;
};

///////////////////////////////////////////////
// Perspective Camera
///////////////////////////////////////////////

class PerspectiveCamera final : public Camera
{
public:
    static constexpr eCameraType scType = eCameraType::Perspective;

    static constexpr float32 scWeaponFarPlane = 0.01f;
    static constexpr float32 scWeaponNearPlane = 20.0f;

public:
    PerspectiveCamera() { Update(); }

    PerspectiveCamera(float32 fov_degrees, float32 aspect_ratio, float32 near_plane, float32 far_plane)
    {
        mNearPlane = near_plane;
        mFarPlane = far_plane;
        SetFov(fov_degrees);
        SetAspectRatio(aspect_ratio);

        Update();
    }

    void UpdateProjectionMatrix() override;
    void UpdateCameraMatrix() override;
    void OnWindowResize(const Vec2u& size) override;

    float32 GetFov() const { return RadToDeg(mFovRad); }
    float32 GetFovRad() const { return mFovRad; }

    void SetFov(float32 fov) { SetFovRad(DegToRad(fov)); }

    void SetFovRad(float32 fov_rad)
    {
        mFovRad = fov_rad;

        RequireMatrixUpdate();
    }

    inline void Rotate(float32 angle_x, float32 angle_y)
    {
        mAngleX = LimitRotation(mAngleX + angle_x);
        mAngleY = LimitRotation(mAngleY + angle_y);

        constexpr float cOffset = 0.01f;

        constexpr float cMinY = -FX_HALF_PI + cOffset;
        constexpr float cMaxY = FX_HALF_PI - cOffset;

        mAngleY = MathUtil::Clamp(mAngleY, cMinY, cMaxY);

        RequireTransformUpdate();
    }


    FX_FORCE_INLINE void SetAspectRatio(float32 aspect_ratio)
    {
        mAspectRatio = aspect_ratio;

        RequireMatrixUpdate();
    }

    FX_FORCE_INLINE Vec3f GetRotation() { return Vec3f(mAngleY, mAngleX, 0); }

    const Mat4f& GetCameraMatrix(eObjectLayer layer) const override
    {
        switch (layer) {
        case eObjectLayer::WorldLayer:
            return mCameraMatrix;
        case eObjectLayer::PlayerLayer:
            return mWeaponCameraMatrix;
        }

        return mCameraMatrix;
    }

    ~PerspectiveCamera() override {}

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

private:
    float32 mFovRad = DegToRad(80.0f);

    float32 mAspectRatio = 1.0f;

    /**
     * @brief A separate matrix for objects rendered from the perspective of the player.
     * Weapons, body, or additional attached objects use this.
     */
    Mat4f mWeaponCameraMatrix = Mat4f::sIdentity;
    Mat4f mWeaponProjectionMatrix = Mat4f::sIdentity;

    float32 mWeaponFov = DegToRad(75.0f);
};

} // namespace fx

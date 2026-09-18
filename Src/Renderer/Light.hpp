#pragma once

#include <Asset/MeshGen.hpp>
#include <Color.hpp>
#include <Core/Hash.hpp>
#include <Entity.hpp>
#include <Math/Mat4.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/PipelineNames.hpp>
#include <Renderer/PrimitiveMesh.hpp>
#include <Renderer/ShadowAtlas.hpp>

namespace fx {
class Camera;

namespace renderer {
class Pipeline;
struct LightGpuData;
} // namespace renderer

using LightId = uint32;
static constexpr LightId LightIdNull = UINT32_MAX;

enum eLightFlags : uint16
{
	LF_None = 0x0000,
};

enum class eLightType
{
	Unknown,
	Directional,
	Point,
	Spot,
};

FX_DEFINE_ENUM_AS_FLAGS(eLightFlags);

class LightBase : public Entity
{
public:
	using VertexType = renderer::Vertex<renderer::eVertexType::Slim>;

	static constexpr eEntityType scEntityType = eEntityType::Light;

public:
	LightBase(eLightFlags flags = LF_None);

	void SetLightVolume(const Ref<PrimitiveMesh>& volume);
	void SetLightVolume(const Ref<MeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh = false);

	void SetRadius(const float radius);

	virtual void Render(const PerspectiveCamera& camera, Camera* shadow_camera);
	virtual void RenderDebugMesh(const PerspectiveCamera& camera);

	virtual ~LightBase() {}

protected:
	/**
	 * @brief Fills out this light's slot in the light buffer. Light types extend this with their own data.
	 */
	virtual void FillGpuData(renderer::LightGpuData& data, const PerspectiveCamera& camera,
							 Camera* shadow_camera) const;

public:
	LightId Id = LightIdNull;

	Ref<PrimitiveMesh> pLightVolume { nullptr };
	Ref<MeshGen::GeneratedMesh> pLightVolumeGen { nullptr };

	struct Color Color = Color::sWhite;
	struct Color AmbientColor { 0x101f1f1f };

	eLightFlags Flags = LF_None;
	eLightType Type = eLightType::Unknown;

	bool bEnabled = true;

protected:
	Ref<PrimitiveMesh> mpDebugMesh { nullptr };

	float32 mRadius = 1.0f;
};


/////////////////////////////////////
// Point light
/////////////////////////////////////

class LightPoint : public LightBase
{
public:
	LightPoint();
};


/////////////////////////////////////
// Directional light
/////////////////////////////////////
class LightDirectional : public LightBase
{
public:
	LightDirectional();

protected:
	void FillGpuData(renderer::LightGpuData& data, const PerspectiveCamera& camera,
					 Camera* shadow_camera) const override;
};

/////////////////////////////////////
// Spotlight
/////////////////////////////////////

class LightSpot : public LightBase
{
public:
	static constexpr float32 scShadowNearPlane = 0.1f;

	struct ShadowState
	{
		renderer::ShadowTileIndex AtlasTile = renderer::ShadowTileIndexNull;

		Mat4f Matrix = Mat4f::scIdentity;

		/// Hash of everything that went into the bake (the light, the atlas generation and the casters). The tile is
		/// rebaked if this is updated
		Hash32 BakeHash = 0;
	};

public:
	LightSpot();

	void SetConeAngles(float32 inner_angle, float32 outer_angle);

	FX_FORCE_INLINE float32 GetInnerAngle() const { return mInnerAngle; }
	FX_FORCE_INLINE float32 GetOuterAngle() const { return mOuterAngle; }

	void SetDirection(const Vec3f& direction);

	Vec3f GetDirection() const;

	FX_FORCE_INLINE float32 GetRadius() const { return mRadius; }

	Mat4f CalculateShadowMatrix() const;

	/// Hands the light's shadow atlas tile back, the next bake allocates a new one.
	void ReleaseShadowTile();

	~LightSpot() override;

protected:
	void FillGpuData(renderer::LightGpuData& data, const PerspectiveCamera& camera,
					 Camera* shadow_camera) const override;

public:
	bool bCastShadows = true;

	ShadowState Shadow;

private:
	float32 mInnerAngle = MathUtil::DegreesToRadians(20.0f);
	float32 mOuterAngle = MathUtil::DegreesToRadians(30.0f);
};

////////////////////////////////////
// Entity Validations
////////////////////////////////////

FX_VALIDATE_ENTITY_TYPE(LightBase);

} // namespace fx

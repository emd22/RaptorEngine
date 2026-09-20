/*
 * File:        Light.cpp
 * Author:      emd22
 * Created:     08/07/2025
 * Description: Definitions for lights in the renderer.
 */

#include "Light.hpp"

#include "Camera.hpp"
#include "Engine.hpp"

#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PipelineCache.hpp>
#include <algorithm>

namespace fx {

using namespace fx::renderer;

using VertexType = LightBase::VertexType;

/////////////////////////////////////
// Base Light
/////////////////////////////////////

LightBase::LightBase(eLightFlags flags) : Flags(flags)
{
	this->ID = gObjectManager->NewObjectID("Light");
	LogDebug("Creating light (id={})", this->ID);
}

void LightBase::SetLightVolume(const Ref<PrimitiveMesh>& volume) { pLightVolume = volume; }

void LightBase::SetLightVolume(const Ref<MeshGen::GeneratedMesh>& volume_gen, bool create_debug_mesh)
{
	pLightVolumeGen = volume_gen;
	pLightVolume = volume_gen->AsSlimMesh();
	// Radius = LightVolume->VertexList.CalculateDimensionsFromPositions().X;

	if (create_debug_mesh) {
		mpDebugMesh = volume_gen->AsDefaultMesh();
	}
}

void LightBase::Render(const PerspectiveCamera& camera, Camera* shadow_camera)
{
	if (!bEnabled) {
		return;
	}

	Uniforms& light_buffer = gGraphics->LightBuffer;

	// Writing past the end would spill into the next frame's page of the light buffer
	if (light_buffer.SlotIndex >= light_buffer.Capacity) {
		static bool sbWarned = false;
		if (!sbWarned) {
			LogWarning(LC_RENDER, "Light buffer is full ({} lights), extra lights will not be rendered",
					   light_buffer.Capacity);
			sbWarned = true;
		}
		return;
	}

	UpdateIfOutOfDate();

	LightGpuData data {};
	FillGpuData(data, camera, shadow_camera);

	light_buffer.Write(data);
	light_buffer.FlushToGpu();
	light_buffer.NextSlot();

	// pLightVolume->Render(frame->CmdBuffer, 1);
}

void LightBase::FillGpuData(LightGpuData& data, const PerspectiveCamera& camera, Camera* shadow_camera) const
{
	// No shadow map by default (ShadowAtlasRect stays zero), shadowed lights fill these in their own FillGpuData
	memcpy(data.LightCameraMatrix, Mat4f::scIdentity.RawData, sizeof(Mat4f));

	data.Radius = mRadius;

	// The shading pass only ever needs the reciprocal, and dividing here keeps it out of the per pixel light loop.
	// Clamped rather than left to divide by zero, so a zero radius light stays off instead of writing an infinity.
	data.InvRadiusSq = 1.0f / std::max(mRadius * mRadius, 1e-8f);

	data.Position[0] = mPosition.X;
	data.Position[1] = mPosition.Y;
	data.Position[2] = mPosition.Z;
	data.Color = Color.Value;

	data.Ambient = AmbientColor.Value;
	data.Type = static_cast<uint32>(Type);
}


void LightBase::RenderDebugMesh(const PerspectiveCamera& camera)
{
	if (!mpDebugMesh) {
		return;
	}

	FrameData* frame = gGraphics->GetFrame();

	DrawPushConstants push_constants { .TargetSize = { gGraphics->Swapchain.Extent.X, gGraphics->Swapchain.Extent.Y } };
	memcpy(push_constants.CameraMatrix, camera.GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));
	push_constants.ObjectId = ID.GetID();
	push_constants.TileColumns = gGraphics->pRenderer->GetLightTileColumns();

	gGraphics->SubmitPushConstants(frame->CmdBuffer, gPipelineCache->Request(ePipelineName::Geometry),
								   eShaderType::Vertex | eShaderType::Pixel, push_constants);

	mpDebugMesh->Render(frame->CmdBuffer, 1);
}


void LightBase::SetRadius(const float radius)
{
	mRadius = radius;
	SetScale(mRadius * 2);
}

LightPoint::LightPoint() { Type = eLightType::Point; }


/////////////////////////////////////
// Directional Light
/////////////////////////////////////

LightDirectional::LightDirectional() { Type = eLightType::Directional; }

void LightDirectional::FillGpuData(LightGpuData& data, const PerspectiveCamera& camera, Camera* shadow_camera) const
{
	LightBase::FillGpuData(data, camera, shadow_camera);

	// Position holds the direction towards the light for this type. The shading pass takes it pre-normalized.
	const Vec3f direction = mPosition.Normalize();
	data.Position[0] = direction.X;
	data.Position[1] = direction.Y;
	data.Position[2] = direction.Z;

	const Camera* light_camera = (shadow_camera != nullptr) ? shadow_camera : &camera;
	memcpy(data.LightCameraMatrix, light_camera->GetCameraMatrix(eObjectLayer::WorldLayer).RawData, sizeof(Mat4f));

	if (gShadowAtlas != nullptr) {
		gShadowAtlas->GetRegionUVTransform(gShadowAtlas->GetDirectionalRegion(), data.ShadowAtlasRect);
	}
}


/////////////////////////////////////
// Spotlight
/////////////////////////////////////

/// Widens the shadow frustum past the cone, so the filter footprint at the edge of the cone stays inside the shadow map
static constexpr float32 scSpotShadowFovPadding = MathUtil::DegreesToRadians(1.0f);

/// One perspective shadow map can't cover a hemisphere. Cones wider than this lose their shadows around the edge.
static constexpr float32 scSpotShadowMaxHalfFov = MathUtil::DegreesToRadians(80.0f);

LightSpot::LightSpot() { Type = eLightType::Spot; }

LightSpot::~LightSpot() { ReleaseShadowTile(); }

void LightSpot::SetConeAngles(float32 inner_angle, float32 outer_angle)
{
	// Past 90 degrees the cone no longer fits the bounding sphere used by light culling
	mOuterAngle = MathUtil::Clamp(outer_angle, 0.0f, MathUtil::DegreesToRadians(90.0f));
	mInnerAngle = MathUtil::Clamp(inner_angle, 0.0f, mOuterAngle);
}

void LightSpot::SetDirection(const Vec3f& direction)
{
	const Vec3f dir = direction.Normalize();

	// Shortest arc from forward (+Z) to `dir`: axis is (+Z x dir), w is 1 + (+Z . dir)
	const float32 w = 1.0f + dir.Z;

	if (w < 1e-6f) {
		// Pointing straight back, so any half turn around an axis perpendicular to +Z works
		SetRotation(Quat(0.0f, 1.0f, 0.0f, 0.0f));
		return;
	}

	SetRotation(Quat(-dir.Y, dir.X, 0.0f, w).Normalize());
}

Vec3f LightSpot::GetDirection() const
{
	// const float32 x = mRotation.GetX();
	// const float32 y = mRotation.GetY();
	// const float32 z = mRotation.GetZ();
	// const float32 w = mRotation.GetW();


	// Forward (+Z) rotated by q, i.e. the third row of the rotation matrix
	// return Vec3f(2.0f * (x * z + w * y), 2.0f * (y * z - w * x), 1.0f - 2.0f * (x * x + y * y));
	return mRotation.GetDirection();
}

Mat4f LightSpot::CalculateShadowMatrix() const
{
	const Vec3f direction = GetDirection().Normalize();

	// The cone is round, so any up vector works as long as it isn't parallel to the cone
	const Vec3f up = (std::fabs(direction.Y) > 0.99f) ? Vec3f(0.0f, 0.0f, 1.0f) : Vec3f::sUp;

	Mat4f view = Mat4f::scIdentity;
	view.LookAt(mPosition, mPosition + direction, up);

	const float32 half_fov = std::min(mOuterAngle + scSpotShadowFovPadding, scSpotShadowMaxHalfFov);
	const float32 far_plane = std::max(mRadius, scShadowNearPlane * 2.0f);

	Mat4f projection = Mat4f::scIdentity;

	// Reverse-Z takes the far plane first, see Mat4f::LoadPerspectiveMatrix()
	projection.LoadPerspectiveMatrix(half_fov * 2.0f, 1.0f, far_plane, scShadowNearPlane);

	return view * projection;
}

void LightSpot::ReleaseShadowTile()
{
	if (Shadow.AtlasTile != ShadowTileIndexNull && gShadowAtlas != nullptr) {
		gShadowAtlas->FreeSpotTile(Shadow.AtlasTile);
	}

	Shadow = ShadowState {};
}

void LightSpot::FillGpuData(LightGpuData& data, const PerspectiveCamera& camera, Camera* shadow_camera) const
{
	LightBase::FillGpuData(data, camera, shadow_camera);

	const Vec3f direction = GetDirection().Normalize();

	data.SpotDirection[0] = direction.X;
	data.SpotDirection[1] = direction.Y;
	data.SpotDirection[2] = direction.Z;

	const float32 cos_inner = cosf(mInnerAngle);
	const float32 cos_outer = cosf(mOuterAngle);

	data.SpotCosOuter = cos_outer;
	// Keeps the falloff finite when both angles match (a hard edged cone)
	data.SpotAngleScale = 1.0f / std::max(cos_inner - cos_outer, 1e-4f);

	// World::UpdateSpotShadows() runs before the lights are uploaded, so a light with a tile has been baked by the time
	// the forward pass samples it
	if (Shadow.AtlasTile != ShadowTileIndexNull && gShadowAtlas != nullptr) {
		memcpy(data.LightCameraMatrix, Shadow.Matrix.RawData, sizeof(Mat4f));
		gShadowAtlas->GetRegionUVTransform(gShadowAtlas->GetSpotTileRegion(Shadow.AtlasTile), data.ShadowAtlasRect);
	}
}

} // namespace fx

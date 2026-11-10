#include "Entity.hpp"

#include <Engine.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/TiledForwardRenderer.hpp>

namespace fx {


EntityManager& EntityManager::GetGlobalManager()
{
	static EntityManager global_manager;
	return global_manager;
}

void EntityManager::Create(uint32 entities_per_page) { GetGlobalManager().mEntityPool.Create(entities_per_page); }

Ref<Entity> EntityManager::New()
{
	EntityManager& global_manager = GetGlobalManager();

	if (!global_manager.mEntityPool.IsInited()) {
		global_manager.Create();
	}

	return Ref<Entity>::New(global_manager.mEntityPool.Insert());
}

void Entity::ScaleBy(const float scale) { SetScale(mScale * scale); }

void Entity::SetScale(const float scale)
{
	mScale = scale;
	MarkTransformOutOfDate();
}

// Go through SetRotation so overrides (e.g. Object syncing its physics body and attached objects) see the change.
// Renormalize so that repeated small rotations do not accumulate scale into the model matrix.
void Entity::RotateX(float32 rad) { SetRotation((mRotation * Quat::FromAxisAngle(FVec3::sRight, rad)).Normalize()); }

void Entity::RotateY(float32 rad) { SetRotation((mRotation * Quat::FromAxisAngle(FVec3::sUp, rad)).Normalize()); }

void Entity::RotateZ(float32 rad) { SetRotation((mRotation * Quat::FromAxisAngle(FVec3::sForward, rad)).Normalize()); }

void Entity::SetModelMatrix(const Mat4f& other)
{
	// We do not want the next update to replace the new matrix
	mbMatrixOutOfDate = false;

	BeginMatrixChange();
	mWorldMatrix = other;
	mMatrixSubmittedFrames = 0;

	// Trigger an immediate update
	SubmitMatrixIfNeeded();
}

// const Mat4f& Entity::GetNormalMatrix()
// {
//     if (mbMatrixOutOfDate) {
//         RecalculateModelMatrix();
//     }

//     // return mNormalMatrix;
// }

Mat4f& Entity::GetWorldMatrix()
{
	if (mbMatrixOutOfDate) {
		RecalculateModelMatrix();
	}

	return mWorldMatrix;
}

void Entity::SubmitMatrixIfNeeded()
{
	// There is no object id assigned to the object yet, break
	if (ID.IsInvalid()) {
		return;
	}

	const uint8 frame_bit = static_cast<uint8>(1U << renderer::gGraphics->GetFrameNumber());
	const bool is_moving = mbChangeIsMotion && (mMatrixChangedFrame == renderer::gGraphics->GetElapsedFrameCount());

	if (!is_moving && (mMatrixSubmittedFrames & frame_bit)) {
		return;
	}

	ObjectGpuEntry entry;
	memcpy(entry.ModelMatrix, mWorldMatrix.RawData, sizeof(Mat4f));

	if (is_moving) {
		memcpy(entry.PrevModelMatrix, mPrevWorldMatrix.RawData, sizeof(Mat4f));
	}
	else {
		memcpy(entry.PrevModelMatrix, mWorldMatrix.RawData, sizeof(Mat4f));
	}

	gObjectManager->Submit(ID, entry);

	// A page written with motion has to be written again once the object stops
	if (!is_moving) {
		mMatrixSubmittedFrames |= frame_bit;
	}
}

void Entity::BeginMatrixChange()
{
	const uint32 frame = renderer::gGraphics->GetElapsedFrameCount();

	if (mMatrixChangedFrame == frame) {
		return;
	}

	mPrevWorldMatrix = mWorldMatrix;
	mMatrixChangedFrame = frame;

	// The first matrix an entity gets is not a movement
	mbChangeIsMotion = mbHasWorldMatrix;
	mbHasWorldMatrix = true;
}

void Entity::RecalculateModelMatrix()
{
	BeginMatrixChange();

	if (TransformMode == eTransformMode::Default) {
		mWorldMatrix = Mat4f::AsScale(FVec3(mScale)) * Mat4f::AsRotation(mRotation) * Mat4f::AsTranslation(mPosition);
	}
	else if (TransformMode == eTransformMode::TransformFromOrigin) {
		mWorldMatrix = Mat4f::AsScale(FVec3(mScale)) * Mat4f::AsTranslation(RotationOrigin) *
					   Mat4f::AsRotation(mRotation) * Mat4f::AsTranslation(-RotationOrigin) *
					   Mat4f::AsTranslation(mPosition);
	}

	if (mbHasPreTransform) {
		mWorldMatrix = mPreTransform * mWorldMatrix;
	}

	mMatrixSubmittedFrames = 0;

	// mNormalMatrix = mWorldMatrix.Inverse().Transposed();
	mbMatrixOutOfDate = false;
}

} // namespace fx

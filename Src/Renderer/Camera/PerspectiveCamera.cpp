#include "PerspectiveCamera.hpp"

#include <Math/Vec4.hpp>


namespace fx {


bool PerspectiveCamera::Update()
{
	// There is nothing to update, return
	if (!mbTransformDirty && !mbRequireProjectionMatrixUpdate) {
		return false;
	}

	// Update the projection matrix if we have new camera parameters
	if (mbRequireProjectionMatrixUpdate) {
		mProjectionMatrix.LoadPerspectiveMatrix(mFovY, mAspectRatio, mZNearClip, mZFarClip);
		mbRequireProjectionMatrixUpdate = false;
	}

	// Update the view matrix if the transform has changed
	if (mbTransformDirty) {
		RebuildViewMatrix();
	}


	// Something was updated, so we need to recalc the camera matrix.

	mCameraMatrix = mViewMatrix * mProjectionMatrix;

	// Calculate the inverse matrices for view and projection
	mInvViewMatrix = mViewMatrix.RigidInverse();
	mInvProjectionMatrix = mProjectionMatrix.Inverse();

	mbTransformDirty = false;

	return true;
}

} // namespace fx

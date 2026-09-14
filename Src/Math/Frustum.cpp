#include "Frustum.hpp"

#include "SIMDHelper.hpp"

#include <Renderer/Camera.hpp>


namespace fx {
// We need to find the tiles that can possibly intersect with the frustum.


// static bool Check2DFrustum(const fx::PerspectiveCamera& cam)
// {
// 	// Since world tiles are 2D, we flatten the frustum to a 2D set of points.

// 	// const fx::Mat4f& vp_src = cam.GetCameraMatrix(eObjectLayer::WorldLayer);

// 	// FLOAT4 row0 = vp_src.Rows[0].mIntrin;

// 	// // Extract first column, add X and W

// 	// // We have a vector [X Y Z W]. We want to add X and W, so get the low components (X and Y) and high
// 	// // components (Z and W). Reverse the high components to get (W and Z), sum the two vectors and we get [X+W Y+Z].
// 	// Get
// 	// // the lowest lane to get the final X+W.
// 	// const float32 left_plane = vget_lane_f32(vadd_f32(vget_low_f32(col0), vrev64_f32(vget_high_f32(col0))), 0);
// }

} // namespace fx

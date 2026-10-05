#pragma once

#include <Color.hpp>
#include <Core/Ref.hpp>
#include <Core/Types.hpp>
#include <Math/BoundingBox.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {

class Camera;

namespace renderer {

class CommandBuffer;

enum class eDebugShape : uint8
{
	Line,
	WireBox,
	SolidBox,
	Count,
};


class DebugDraw
{
public:
	static constexpr uint32 scMaxShapes = 4096U;

public:
	DebugDraw();
	~DebugDraw();

	DebugDraw(const DebugDraw&) = delete;
	DebugDraw& operator=(const DebugDraw&) = delete;

	void Draw(eDebugShape shape, const Mat4f& world_matrix, Color color);

	void Line(const Vec3f& from, const Vec3f& to, Color color);

	void WireBox(const Mat4f& world_matrix, Color color) { Draw(eDebugShape::WireBox, world_matrix, color); }
	void WireBox(const Vec3f& center, const Vec3f& half_extent, const Quat& rotation, Color color);
	void WireBox(const AABB& box, Color color);

	void SolidBox(const Mat4f& world_matrix, Color color) { Draw(eDebugShape::SolidBox, world_matrix, color); }
	void SolidBox(const Vec3f& center, const Vec3f& half_extent, const Quat& rotation, Color color);
	void SolidBox(const AABB& box, Color color);


	void Render(const CommandBuffer& cmd, const Camera& camera);

	uint32 GetQueuedCount() const { return rx_debug_draw_count(mpQueue); }

	void Clear();
	void Destroy();

private:
	/**
	 * @brief Makes the meshes a shape needs on first use, as this is created before there is a device to upload to.
	 */
	PrimitiveMesh& GetMesh(eDebugShape shape);

private:
	/// The queued shapes, and the work of turning them into draws, are in Rust
	RxDebugDraw* mpQueue = nullptr;

	Ref<PrimitiveMesh> mMeshes[static_cast<uint32>(eDebugShape::Count)];
};

} // namespace renderer

} // namespace fx

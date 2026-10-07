#pragma once

#include <Color.hpp>
#include <Core/DynArray.hpp>
#include <Core/Ref.hpp>
#include <Core/Types.hpp>
#include <Math/BBox.hpp>
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
	DebugDraw() = default;
	~DebugDraw() { Destroy(); }

	DebugDraw(const DebugDraw&) = delete;
	DebugDraw& operator=(const DebugDraw&) = delete;

	void Draw(eDebugShape shape, const Mat4f& world_matrix, Color color);

	void Line(const Vec3f from, const Vec3f to, Color color);

	void WireBox(const Mat4f& world_matrix, Color color) { Draw(eDebugShape::WireBox, world_matrix, color); }
	void WireBox(const Vec3f center, const Vec3f half_extent, const Quat rotation, Color color);
	void WireBox(const BBox& box, Color color);

	void SolidBox(const Mat4f& world_matrix, Color color) { Draw(eDebugShape::SolidBox, world_matrix, color); }
	void SolidBox(const Vec3f center, const Vec3f half_extent, const Quat rotation, Color color);
	void SolidBox(const BBox& box, Color color);


	void Render(const CommandBuffer& cmd, const Camera& camera);

	uint32 GetQueuedCount() const { return static_cast<uint32>(mShapes.Size); }

	void Clear();
	void Destroy();

private:
	struct QueuedShape
	{
		Mat4f WorldMatrix;
		uint32 Color;
		eDebugShape Shape;
	};

	/**
	 * @brief Makes the meshes a shape needs on first use, as this is created before there is a device to upload to.
	 */
	PrimitiveMesh& GetMesh(eDebugShape shape);

private:
	DynArray<QueuedShape> mShapes;

	Ref<PrimitiveMesh> mMeshes[static_cast<uint32>(eDebugShape::Count)];

	bool bWarnedAboutOverflow = false;
};

} // namespace renderer

} // namespace fx

#pragma once

#include <Color.hpp>
#include <Core/DynArray.hpp>
#include <Core/Ref.hpp>
#include <Core/Types.hpp>
#include <Math/BBox.hpp>
#include <Math/FMat4.hpp>
#include <Math/FQuat.hpp>
#include <Math/FVec3.hpp>
#include <Renderer/PrimitiveMesh.hpp>

namespace fx {

class BaseCamera;

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

	void Draw(eDebugShape shape, const FMat4& world_matrix, Color color);

	void Line(const FVec3 from, const FVec3 to, Color color);

	void WireBox(const FMat4& world_matrix, Color color) { Draw(eDebugShape::WireBox, world_matrix, color); }
	void WireBox(const FVec3 center, const FVec3 half_extent, const FQuat rotation, Color color);
	void WireBox(const BBox& box, Color color);

	void SolidBox(const FMat4& world_matrix, Color color) { Draw(eDebugShape::SolidBox, world_matrix, color); }
	void SolidBox(const FVec3 center, const FVec3 half_extent, const FQuat rotation, Color color);
	void SolidBox(const BBox& box, Color color);

	void Overlay(const Ref<PrimitiveMesh>& mesh, const FMat4& world_matrix, Color color);


	void Render(const CommandBuffer& cmd, const BaseCamera& camera);

	uint32 GetQueuedCount() const { return static_cast<uint32>(mShapes.Size + mOverlays.Size); }

	void Clear();
	void Destroy();

private:
	struct QueuedShape
	{
		FMat4 WorldMatrix;
		uint32 Color;
		eDebugShape Shape;
	};

	struct QueuedOverlay
	{
		Ref<PrimitiveMesh> Mesh;
		FMat4 WorldMatrix;
		uint32 Color;
	};

	void RenderOverlays(const CommandBuffer& cmd, const FMat4& camera_matrix);

	/**
	 * @brief Makes the meshes a shape needs on first use, as this is created before there is a device to upload to.
	 */
	PrimitiveMesh& GetMesh(eDebugShape shape);

private:
	DynArray<QueuedShape> mShapes;
	DynArray<QueuedOverlay> mOverlays;

	Ref<PrimitiveMesh> mMeshes[static_cast<uint32>(eDebugShape::Count)];

	bool bWarnedAboutOverflow = false;
};

} // namespace renderer

} // namespace fx

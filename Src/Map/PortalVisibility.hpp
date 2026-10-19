#pragma once

#include <Core/SizedArray.hpp>
#include <Map/AreaGraph.hpp>
#include <Map/AreaSet.hpp>
#include <Math/BBox.hpp>
#include <Math/Mat4.hpp>
#include <Math/Vec3.hpp>
#include <Math/Vec4.hpp>

namespace fx {

struct ScreenRect
{
	float32 MinX = 1.0f;
	float32 MinY = 1.0f;
	float32 MaxX = -1.0f;
	float32 MaxY = -1.0f;

	static ScreenRect MakeFull() { return ScreenRect { -1.0f, -1.0f, 1.0f, 1.0f }; }

	bool IsEmpty() const { return MinX > MaxX || MinY > MaxY; }

	bool Overlaps(const ScreenRect& other) const
	{
		return MinX <= other.MaxX && other.MinX <= MaxX && MinY <= other.MaxY && other.MinY <= MaxY;
	}

	bool Contains(const ScreenRect& other) const
	{
		return !IsEmpty() && MinX <= other.MinX && MinY <= other.MinY && MaxX >= other.MaxX && MaxY >= other.MaxY;
	}

	ScreenRect Intersect(const ScreenRect& other) const;
	ScreenRect Union(const ScreenRect& other) const;
};

enum class ePortalState : uint8
{
	Unreached,
	Rejected,
	Passed,
	Closed,
};

class PortalVisibility
{
public:
	static constexpr uint32 scMaxDepth = 32;
	static constexpr float32 scNearMargin = 0.25f;

public:
	void Disable();
	void Update(const AreaGraph& graph, const Mat4f& view_projection, const Vec3f& camera_position);

	bool IsActive() const { return mbActive; }

	bool IsAreaVisible(uint32 area) const { return !mbActive || !mAreaRects[area].IsEmpty(); }
	const ScreenRect& GetAreaRect(uint32 area) const { return mAreaRects[area]; }

	int32 GetCameraArea() const { return mCameraArea; }

	ePortalState GetPortalState(uint32 portal) const
	{
		if (!mbActive || portal >= mPortalStates.Size) {
			return ePortalState::Unreached;
		}

		return mPortalStates[portal];
	}

	const Mat4f& GetViewProjection() const { return mViewProjection; }
	const Vec3f& GetCameraPosition() const { return mCameraPosition; }

	bool IsVisible(const AreaSet& areas, const BBox& bounds) const;

public:
	uint32 VisibleAreaCount = 0;
	uint32 PortalsTested = 0;
	uint32 PortalsPassed = 0;

private:
	void Flow(uint32 area, const ScreenRect& rect, uint32 depth);

	bool GetPortalRect(const MapPortal& portal, uint32 from_area, const ScreenRect& parent_rect, ScreenRect& out_rect);

	Vec4f ToClip(const Vec3f& point) const;
	bool ProjectBounds(const BBox& bounds, ScreenRect& out_rect) const;

private:
	bool mbActive = false;
	int32 mCameraArea = -1;

	const AreaGraph* mpGraph = nullptr;
	Mat4f mViewProjection;
	Vec3f mCameraPosition = Vec3f::sZero;

	SizedArray<ScreenRect> mAreaRects;
	SizedArray<ePortalState> mPortalStates;

	SizedArray<Vec4f> mClipPoints;
	SizedArray<Vec4f> mClippedPoints;
};

} // namespace fx

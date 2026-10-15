#pragma once

#ifdef FX_IS_EDITOR

#include "EditorPanelState.hpp"
#include "EditorThread.hpp"
#include "EditorTool.hpp"
#include "TopViewPlane.hpp"

#include <wx/bitmap.h>
#include <wx/window.h>

#include <Core/DynArray.hpp>
#include <Core/Types.hpp>
#include <Math/Rect.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <functional>

class wxGraphicsContext;
class wxZoomGestureEvent;

namespace fx::editor {

enum class eTopViewTool : uint8
{
	Select,
	Face,
	Vertex,
	Create,
	Arch,
	Rotate,

	Count,
};

struct ArchSettings
{
	uint32 Sides = 8;
	float32 WallWidth = 0.25f;
	float32 ArcDegrees = 180.0f;
	float32 StartDegrees = 0.0f;
};

class TopViewCanvas : public wxWindow
{
public:
	explicit TopViewCanvas(wxWindow* parent);

	void ApplyState(const TopViewState& state);

	void SetTool(eTopViewTool tool);
	FX_FORCE_INLINE eTopViewTool GetTool() const { return mTool; }

	void SetPlane(eViewPlane plane);
	FX_FORCE_INLINE eViewPlane GetPlane() const { return mPlane; }

	void SetCreateVolume(eViewPlane plane, float32 base, float32 size);
	void SetDropToGround(bool enabled) { mbDropToGround = enabled; }
	void SetArchSettings(const ArchSettings& settings);

	void FitAll();
	void FitSelection();

	void SetOnToolChanged(std::function<void(eTopViewTool)> handler) { mpOnToolChanged = std::move(handler); }
	void SetOnPlaneChanged(std::function<void(eViewPlane)> handler) { mpOnPlaneChanged = std::move(handler); }
	void SetOnStatus(std::function<void(const wxString&)> handler) { mpOnStatus = std::move(handler); }

	wxBitmap Render(const wxSize& size);

	wxPoint WorldToClient(const Vec2d uv) const;
	Vec2d ClientToWorld(const wxPoint& point) const;

	FX_FORCE_INLINE const TopViewState& GetState() const { return mState; }
	FX_FORCE_INLINE double GetScale() const { return mScale; }

private:
	enum class eDrag : uint8
	{
		None,
		Pan,
		Marquee,
		Move,
		Face,
		Vertex,
		Create,
		Arch,
		Rotate,
	};

	struct OutlinePreview
	{
		ObjectID BrushId = ObjectID::scNull;
		DynArray<Vec2f> Polygon;
	};

	struct Preview
	{
		eDrag Kind = eDrag::None;
		//
		// 		double OffsetU = 0.0;
		// 		double OffsetV = 0.0;

		Vec2d OffsetUV { Vec2d::sZero };
		Vec2d PivotUV { Vec2d::sZero };
		Vec2d NormalUV { Vec2d::sZero };

		double Angle = 0.0;
		//
		// 		double PivotU = 0.0;
		// 		double PivotV = 0.0;

		// double MinU = 0.0;
		// double MinV = 0.0;
		// double MaxU = 0.0;
		// double MaxV = 0.0;

		DRect RectUV {};


		double Shift = 0.0;
		// double NormalU = 0.0;
		// double NormalV = 0.0;
		DynArray<OutlinePreview> Outlines;
	};

	struct ViewBounds
	{
		// double MinU = 0.0;
		// double MinV = 0.0;
		// double MaxU = 0.0;
		// double MaxV = 0.0;

		DRect Rect {};

		bool bValid = false;
	};

	struct SavedView
	{
		// double CenterU = 0.0;
		// double CenterV = 0.0;
		Vec2d Center { Vec2d::sZero };

		double Scale = 24.0;
		bool bFitted = false;
	};

private:
	void OnPaint(wxPaintEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnPanDown(wxMouseEvent& event);
	void OnPanUp(wxMouseEvent& event);
	void EndPan();
	void OnMotion(wxMouseEvent& event);
	void OnWheel(wxMouseEvent& event);
	void OnZoomGesture(wxZoomGestureEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);
	void OnLeave(wxMouseEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	void Paint(wxGraphicsContext& gc, const wxSize& size);
	void DrawGrid(wxGraphicsContext& gc, const wxSize& size);
	void DrawBrushes(wxGraphicsContext& gc, const wxSize& size);
	void DrawOverlays(wxGraphicsContext& gc, const wxSize& size);
	void DrawPlayer(wxGraphicsContext& gc);

	wxSize GetViewSize() const;

	Vec2d ToScreenCoords(const Vec2d uv) const;

	const ViewPlaneAxes& Axes() const;
	const TopViewProjection& Projection(const TopViewBrush& brush) const;
	Vec3f ToWorldVector(const Vec2d uv, double w) const;

	void BeginPan(const wxPoint& position);
	void BeginSelect();
	void BeginFace();
	void BeginVertex();
	void BeginCreate();
	void BeginArch();
	void BeginRotate();

	void FinishFace();
	void FinishVertex();
	void FinishMarquee();
	void FinishMove();
	void FinishCreate();
	void FinishArch();
	void FinishRotate();

	DynArray<Vec2d> BuildArchSegments(const DRect& rect) const;

	void UpdatePreview();
	void ClearPreview();
	void HoldPreview();
	void CancelDrag();

	void ZoomAt(const wxPoint& anchor, double factor);
	void PanByPixels(const Vec2d delta);
	void FitBounds(const ViewBounds& bounds);
	void TryInitialFit();

	double Snap(double value) const;
	Vec2d Snap(const Vec2d uv) const;
	double SnapAngle(double radians) const;

	DynArray<int32> PickAt(const Vec2d uv) const;
	int PickFaceAt(const Vec2d uv, int& out_face) const;
	void UpdateHover();
	bool PickVertexAt(const Vec2d uv, TopViewVertexHandle& out_handle) const;
	bool IsVertexSelected(ObjectID object_id, const Vec2d uv) const;
	const TopViewBrush* FindBrush(ObjectID object_id) const;
	ViewBounds GetSelectionBounds() const;
	bool GetSelectionPivot(Vec2d& out_pivot) const;
	uint32 GetSelectedCount() const;

	void SelectBrush(ObjectID object_id, eTopViewSelectMode mode);
	void SelectBrushes(DynArray<ObjectID> object_ids, eTopViewSelectMode mode);
	void ApplyLocalSelection();
	void SelectAll();
	void InvertSelection();
	void SendCommand(thread::Task command);

	void NudgeSelection(const Vec2d delta);
	void SetSnapBypass(bool bypass);
	void SetToolFromKey(eTopViewTool tool);
	bool IsToolAvailable(eTopViewTool tool) const;
	void UpdateCursor();
	void UpdateStatus();

private:
	TopViewState mState;

	eTopViewTool mTool = eTopViewTool::Select;
	eViewPlane mPlane = eViewPlane::Top;

	// double mCenterU = 0.0;
	// double mCenterV = 0.0;

	Vec2d mCenter { Vec2d::sZero };

	double mScale = 24.0;
	bool mbHasFitted = false;

	SavedView mSavedViews[scViewPlaneCount];

	wxSize mOverrideSize = wxDefaultSize;

	eDrag mDrag = eDrag::None;
	wxPoint mPressScreen;
	Vec2d mPress { Vec2d::sZero };
	Vec2d mCurrent { Vec2d::sZero };
	Vec2d mHover { Vec2d::sZero };
	bool mbHovering = false;
	bool mbDragMoved = false;
	bool mbAdditive = false;
	bool mbSpaceDown = false;
	bool mbAltDown = false;
	bool mbSnapBypass = false;

	double mPinchStartScale = 24.0;

	Vec2d mPanStartCenter { Vec2d::sZero };

	bool mbHasPressedBrush = false;
	ObjectID mPressedId = ObjectID::scNull;
	bool mbPressedWasSelected = false;
	DynArray<ObjectID> mPressedHitIds;
	ViewBounds mDragBounds;
	Vec2d mRotatePivot { Vec2d::sZero };
	double mRotateStartAngle = 0.0;

	wxPoint mLastClickScreen = wxPoint(-1000, -1000);

	ObjectID mGrabbedBrushId = ObjectID::scNull;
	TopViewFace mGrabbedFace;
	double mFaceBaseOffset = 0.0;

	bool mbHoverFace = false;
	TopViewFace mHoverFace;

	DynArray<TopViewVertexHandle> mSelectedVertices;
	TopViewVertexHandle mGrabbedVertex;
	bool mbVertexToggleOnRelease = false;
	bool mbHoverVertex = false;
	TopViewVertexHandle mHoverVertex;

	Preview mPreview;
	bool mbPreviewHeld = false;
	long long mHoldDeadline = 0;

	uint32 mSentCommands = 0;
	bool mbLocalSelection = false;
	DynArray<ObjectID> mLocalSelection;

	float32 mCreateBase[scViewPlaneCount] = {};
	float32 mCreateSize[scViewPlaneCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
	bool mbDropToGround = true;

	ArchSettings mArch;

	std::function<void(eTopViewTool)> mpOnToolChanged;
	std::function<void(eViewPlane)> mpOnPlaneChanged;
	std::function<void(const wxString&)> mpOnStatus;
};

} // namespace fx::editor

#endif

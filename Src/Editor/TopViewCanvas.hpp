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
#include <Patch.hpp>
#include <chrono>
#include <functional>

class wxGraphicsContext;
class wxZoomGestureEvent;

namespace fx::editor {

struct ArchSettings
{
	uint32 Sides = 8;
	float32 WallWidth = 0.25f;
	float32 ArcDegrees = 180.0f;
	float32 StartDegrees = 0.0f;
};

struct CurveSettings
{
	ePatchPrimitive Primitive = ePatchPrimitive::Cylinder;
	uint32 SubdivisionsU = Patch::scDefaultSubdivisions;
	uint32 SubdivisionsV = Patch::scDefaultSubdivisions;
	bool bSmooth = true;
	ePatchClosure Closure = ePatchClosure::None;
	float32 Thickness = Patch::scDefaultThickness;
	float32 Weight = 1.0f;
	PatchColor Color;
	PatchTextureSettings Texture;
};

class TopViewCanvas : public wxWindow
{
public:
	explicit TopViewCanvas(wxWindow* parent);

	void ApplyState(const TopViewState& state);

	void SetTool(eEditorTool tool);
	void ShowTool(eEditorTool tool);
	FX_FORCE_INLINE eEditorTool GetTool() const { return mTool; }

	void SetAvailableTools(uint32 available_tools);

	void Subtract(bool keep_cutters);

	void SetPlane(eViewPlane plane);
	FX_FORCE_INLINE eViewPlane GetPlane() const { return mPlane; }

	void SetCreateVolume(eViewPlane plane, float32 base, float32 size);
	void SetDropToGround(bool enabled) { mbDropToGround = enabled; }
	void SetArchSettings(const ArchSettings& settings);
	FX_FORCE_INLINE bool HasPendingArch() const { return mbArchPending && mDrag != eDrag::Arch; }
	void CommitArch();
	void DiscardArch();
	void SetCurveSettings(const CurveSettings& settings);
	void RunPatchCommand(ePatchCommand type);
	void RunTextureCommand(const TextureCommand& command);

	bool GetPickedFace(ObjectID& out_object, Vec3f& out_normal) const;

	void FitAll();
	void FitSelection();

	void SetOnToolChanged(std::function<void(eEditorTool)> handler) { mpOnToolChanged = std::move(handler); }
	void SetOnPlaneChanged(std::function<void(eViewPlane)> handler) { mpOnPlaneChanged = std::move(handler); }
	void SetOnStatus(std::function<void(const wxString&)> handler) { mpOnStatus = std::move(handler); }
	void SetOnArchChanged(std::function<void(const ArchSettings&)> handler) { mpOnArchChanged = std::move(handler); }
	void SetOnCurveChanged(std::function<void(const CurveSettings&)> handler) { mpOnCurveChanged = std::move(handler); }
	void SetOnFaceTextureChanged(std::function<void(const TopViewFace&)> handler)
	{
		mpOnFaceTextureChanged = std::move(handler);
	}

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
		ArchEdit,
		Rotate,
		Clip,
		MarkerMove,
		MarkerAim,
	};

	enum class eArchHandle : uint8
	{
		None,
		Body,
		Wall,
		Start,
		End,
		MinMin,
		MaxMin,
		MaxMax,
		MinMax,
		Left,
		Right,
		Bottom,
		Top,
	};

	struct ArchEllipse
	{
		Vec2d Center { Vec2d::sZero };
		Vec2d Radius { Vec2d::sZero };
		bool bValid = false;
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

		Vec2d ClipA { Vec2d::sZero };
		Vec2d ClipB { Vec2d::sZero };

		bool bSnappedToPatch = false;
		Vec2d PatchSnapUV { Vec2d::sZero };

		Vec2d AimUV { Vec2d::sZero };
		double AimDepth = 0.0;
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
	void DrawMarkers(wxGraphicsContext& gc);
	void DrawPlayer(wxGraphicsContext& gc);

	wxSize GetViewSize() const;

	Vec2d ToScreenCoords(const Vec2d uv) const;

	const ViewPlaneAxes& Axes() const;
	const TopViewProjection& Projection(const TopViewBrush& brush) const;
	DynArray<Vec2d> CollectVertexHandles(const TopViewBrush& brush) const;
	Vec3f ToWorldVector(const Vec2d uv, double w) const;
	Vec2d ProjectPoint(const Vec3f& point) const;
	double DepthOf(const Vec3f& vector) const;

	void BeginPan(const wxPoint& position);
	void BeginSelect();
	void BeginFace();
	void BeginVertex();
	void BeginCreate();
	void BeginArch(bool double_click);
	void BeginCurve();
	bool BeginHandleDrag();
	void BeginRotate();
	void BeginClip();
	void BeginLight(bool double_click);
	void BeginTool(const wxMouseEvent& event);
	bool BeginMarker();

	void FinishFace();
	void FinishVertex();
	void FinishMarquee();
	void FinishMove();
	void FinishCreate();
	void FinishArch();
	void FinishArchEdit();
	void FinishRotate();
	void FinishClip();
	void FinishMarkerMove();
	void FinishMarkerAim();

	DynArray<Vec2d> BuildArchSegments(const DRect& rect) const;
	ArchEllipse FitArch(const DRect& rect) const;
	DRect GetArchRect(const ArchEllipse& ellipse) const;
	Vec2d GetArchHandle(eArchHandle handle) const;
	eArchHandle PickArchHandle(const Vec2d uv) const;
	void BeginArchEdit(eArchHandle handle);
	void UpdateArchEdit();
	void DrawArch(wxGraphicsContext& gc, const DRect& rect, bool show_handles);
	void SetArchSides(int32 sides);
	void NotifyArchChanged();

	bool UsesHandles() const;
	void DrawPatchLattices(wxGraphicsContext& gc);
	void NotifyCurveChanged();
	void SyncCurveFromSelection();
	void SyncTextureFromPickedFace();
	void SyncPickedFaceToGame();
	void SyncWeightFromHandles();

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
	bool FindPatchSnapPoint(const Vec2d uv, Vec2d& out_point) const;
	const TopViewBrush* FindBrush(ObjectID object_id) const;
	ViewBounds GetSelectionBounds() const;
	bool GetSelectionPivot(Vec2d& out_pivot) const;
	Vec2d GetRotateSnapOffset(const Vec2d pivot, double angle) const;
	Vec2d SnapNudge(const Vec2d origin, const Vec2d delta) const;
	uint32 GetSelectedCount() const;

	int32 PickMarkerAt(const Vec2d uv, bool& out_tip) const;
	int32 FindMarker(eTopViewMarkerKind kind, uint32 light_id) const;
	int32 FindSelectedMarker() const;
	Vec2d GetMarkerTip(const TopViewMarker& marker, const Vec2d direction_uv) const;
	void SelectMarker(int32 index);
	void DeselectMarkers();
	void ApplyLocalMarkerSelection();
	void NudgeMarker(const TopViewMarker& marker, const Vec2d delta);

	void SelectBrush(ObjectID object_id, eTopViewSelectMode mode);
	void SelectBrushes(DynArray<ObjectID> object_ids, eTopViewSelectMode mode);
	void ApplyLocalSelection();
	void SelectAll();
	void InvertSelection();
	void SendCommand(thread::Task command);

	void NudgeSelection(const Vec2d delta, bool snap);
	void SetSnapBypass(bool bypass);
	void SetToolFromKey(eEditorTool tool);
	void ApplyTool(eEditorTool tool);
	bool IsToolAvailable(eEditorTool tool) const;
	void UpdateCursor();
	void UpdateStatus();

private:
	TopViewState mState;

	eEditorTool mTool = eEditorTool::Select;
	uint32 mAvailableTools = 0;
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

	bool mbHasPickedFace = false;
	ObjectID mPickedFaceBrushId = ObjectID::scNull;
	Vec2f mPickedFaceNormal = Vec2f::sZero;

	ObjectID mSentPickedFaceId = ObjectID::scNull;
	Vec3f mSentPickedFaceNormal = Vec3f::sZero;

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

	eTopViewMarkerKind mGrabbedMarkerKind = eTopViewMarkerKind::Light;
	uint32 mGrabbedMarkerLightId = 0;

	bool mbLocalMarkerSelection = false;
	bool mbLocalMarkerSelected = false;
	eTopViewMarkerKind mLocalMarkerKind = eTopViewMarkerKind::Light;
	uint32 mLocalMarkerLightId = 0;

	float32 mCreateBase[scViewPlaneCount] = {};
	float32 mCreateSize[scViewPlaneCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
	bool mbDropToGround = true;

	ArchSettings mArch;
	bool mbArchPending = false;
	DRect mArchRect {};
	eArchHandle mArchGrab = eArchHandle::None;
	eArchHandle mArchHover = eArchHandle::None;
	DRect mArchGrabRect {};
	ArchSettings mArchGrabSettings;
	ArchEllipse mArchGrabEllipse;
	double mArchLastAngle = 0.0;
	double mArchAngleTravel = 0.0;

	CurveSettings mCurve;

	std::function<void(eEditorTool)> mpOnToolChanged;
	std::function<void(eViewPlane)> mpOnPlaneChanged;
	std::function<void(const wxString&)> mpOnStatus;

	wxString mNotice;
	uint32 mNoticeSerial = 0;
	std::chrono::steady_clock::time_point mNoticeTime;
	std::function<void(const ArchSettings&)> mpOnArchChanged;
	std::function<void(const CurveSettings&)> mpOnCurveChanged;
	std::function<void(const TopViewFace&)> mpOnFaceTextureChanged;
};

} // namespace fx::editor

#endif

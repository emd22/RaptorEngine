#pragma once

#ifdef FX_IS_EDITOR

#include "EditorPanelState.hpp"
#include "EditorTool.hpp"
#include "TopViewPlane.hpp"

#include <wx/bitmap.h>
#include <wx/window.h>

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <functional>
#include <vector>

class wxGraphicsContext;

namespace fx::editor {

enum class eTopViewTool : uint8
{
	Select,
	Face,
	Vertex,
	Create,
	Rotate,

	Count,
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

	void FitAll();

	void SetOnToolChanged(std::function<void(eTopViewTool)> handler) { mpOnToolChanged = std::move(handler); }
	void SetOnPlaneChanged(std::function<void(eViewPlane)> handler) { mpOnPlaneChanged = std::move(handler); }
	void SetOnStatus(std::function<void(const wxString&)> handler) { mpOnStatus = std::move(handler); }

	wxBitmap Render(const wxSize& size);

	wxPoint WorldToClient(double u, double v) const;
	void ClientToWorld(const wxPoint& point, double& out_u, double& out_v) const;

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
		Rotate,
	};

	struct OutlinePreview
	{
		uint32 BrushId = 0;
		std::vector<float32> Polygon;
	};

	struct Preview
	{
		eDrag Kind = eDrag::None;

		double OffsetU = 0.0;
		double OffsetV = 0.0;

		double Angle = 0.0;
		double PivotU = 0.0;
		double PivotV = 0.0;

		double MinU = 0.0;
		double MinV = 0.0;
		double MaxU = 0.0;
		double MaxV = 0.0;

		double Shift = 0.0;
		double NormalU = 0.0;
		double NormalV = 0.0;
		std::vector<OutlinePreview> Outlines;
	};

	struct ViewBounds
	{
		double MinU = 0.0;
		double MinV = 0.0;
		double MaxU = 0.0;
		double MaxV = 0.0;
		bool bValid = false;
	};

	struct SavedView
	{
		double CenterU = 0.0;
		double CenterV = 0.0;
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
	double ToScreenX(double u) const;
	double ToScreenY(double v) const;

	const ViewPlaneAxes& Axes() const;
	const TopViewProjection& Projection(const TopViewBrush& brush) const;
	Vec3f ToWorldVector(double u, double v, double w) const;

	void BeginPan(const wxPoint& position);
	void BeginSelect();
	void BeginFace();
	void BeginVertex();
	void BeginCreate();
	void BeginRotate();

	void FinishFace();
	void FinishVertex();
	void FinishMarquee();
	void FinishMove();
	void FinishCreate();
	void FinishRotate();

	void UpdatePreview();
	void ClearPreview();
	void HoldPreview();
	void CancelDrag();

	void ZoomAt(const wxPoint& anchor, double factor);
	void TryInitialFit();

	double Snap(double value) const;
	double SnapAngle(double radians) const;

	std::vector<int> PickAt(double u, double v) const;
	int PickFaceAt(double u, double v, int& out_face) const;
	void UpdateHover();
	bool PickVertexAt(double u, double v, TopViewVertexHandle& out_handle) const;
	bool IsVertexSelected(uint32 object_id, double u, double v) const;
	const TopViewBrush* FindBrush(uint32 object_id) const;
	ViewBounds GetSelectionBounds() const;
	bool GetSelectionPivot(double& out_u, double& out_v) const;
	uint32 GetSelectedCount() const;

	void SelectBrushes(const std::vector<uint32>& object_ids, eTopViewSelectMode mode);
	void ApplyLocalSelection();
	void SendCommand(std::function<void()> command);

	void NudgeSelection(double du, double dv);
	void SetToolFromKey(eTopViewTool tool);
	bool IsToolAvailable(eTopViewTool tool) const;
	void UpdateCursor();
	void UpdateStatus();

private:
	TopViewState mState;

	eTopViewTool mTool = eTopViewTool::Select;
	eViewPlane mPlane = eViewPlane::Top;

	double mCenterU = 0.0;
	double mCenterV = 0.0;
	double mScale = 24.0;
	bool mbHasFitted = false;

	SavedView mSavedViews[scViewPlaneCount];

	wxSize mOverrideSize = wxDefaultSize;

	eDrag mDrag = eDrag::None;
	wxPoint mPressScreen;
	double mPressU = 0.0;
	double mPressV = 0.0;
	double mCurrentU = 0.0;
	double mCurrentV = 0.0;
	double mHoverU = 0.0;
	double mHoverV = 0.0;
	bool mbHovering = false;
	bool mbDragMoved = false;
	bool mbAdditive = false;
	bool mbSpaceDown = false;
	bool mbAltDown = false;

	double mPanStartCenterU = 0.0;
	double mPanStartCenterV = 0.0;

	bool mbHasPressedBrush = false;
	uint32 mPressedId = 0;
	bool mbPressedWasSelected = false;
	std::vector<uint32> mPressedHitIds;
	ViewBounds mDragBounds;
	double mRotatePivotU = 0.0;
	double mRotatePivotV = 0.0;
	double mRotateStartAngle = 0.0;

	wxPoint mLastClickScreen = wxPoint(-1000, -1000);

	uint32 mGrabbedBrushId = 0;
	TopViewFace mGrabbedFace;
	double mFaceBaseOffset = 0.0;

	bool mbHoverFace = false;
	TopViewFace mHoverFace;

	std::vector<TopViewVertexHandle> mSelectedVertices;
	TopViewVertexHandle mGrabbedVertex;
	bool mbVertexToggleOnRelease = false;
	bool mbHoverVertex = false;
	TopViewVertexHandle mHoverVertex;

	Preview mPreview;
	bool mbPreviewHeld = false;
	long long mHoldDeadline = 0;

	uint32 mSentCommands = 0;
	bool mbLocalSelection = false;
	std::vector<uint32> mLocalSelection;

	float32 mCreateBase[scViewPlaneCount] = { 0.0f, 0.0f, 0.0f };
	float32 mCreateSize[scViewPlaneCount] = { 1.0f, 1.0f, 1.0f };

	std::function<void(eTopViewTool)> mpOnToolChanged;
	std::function<void(eViewPlane)> mpOnPlaneChanged;
	std::function<void(const wxString&)> mpOnStatus;
};

} // namespace fx::editor

#endif

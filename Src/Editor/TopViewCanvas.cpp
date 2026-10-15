#include "TopViewCanvas.hpp"

#ifdef FX_IS_EDITOR

#include "EditorPlatform.hpp"
#include "EditorThread.hpp"
#include "RaptorEditor.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/graphics.h>
#include <wx/time.h>
#include <wx/utils.h>

#include <Engine.hpp>
#include <Math/MathConsts.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace fx::editor {

namespace {

constexpr double scMinScale = 0.5;
constexpr double scMaxScale = 4000.0;
constexpr double scDefaultScale = 24.0;
constexpr double scFitMargin = 1.25;

constexpr double scPickTolerancePixels = 5.0;
constexpr double scDragThresholdPixels = 4.0;
constexpr double scClickCycleRadiusPixels = 4.0;
constexpr double scMinCreateSize = 0.05;
constexpr double scMinCreateDepth = 0.05;
constexpr uint32 scArchCornersPerSegment = 4;
constexpr uint32 scMinArchSides = 3;
constexpr uint32 scMaxArchSides = 128;
constexpr double scMinRotateAngle = 1e-4;
constexpr long long scHoldMilliseconds = 1500;

constexpr double scMinorGridPixels = 14.0;
constexpr double scSnapGridPixels = 8.0;
constexpr uint32 scMaxSnapLines = 800;
constexpr double scWheelZoomPerUnit = 0.08;
constexpr double scWheelZoomLimit = 0.3;
constexpr double scPreciseZoomPerPixel = 0.01;
constexpr double scWheelPanPixelsPerUnit = 40.0;
constexpr double scNudgeCoarseFactor = 4.0;
constexpr double scNudgeFineFactor = 0.25;
constexpr double scDimensionLabelMargin = 6.0;
constexpr double scDimensionLabelTop = 18.0;


constexpr double scFacePickBonusPixels = 3.0;
constexpr double scFaceMatchDot = 0.999;
constexpr double scFaceSameLineTolerance = 0.05;
constexpr double scClipExtent = 1e4;
constexpr double scMinMarkerDirection = 0.15;

constexpr double scVertexPickPixels = 7.0;
constexpr double scHandleMergeDistance = 2e-3;
constexpr double scHandleHalfSize = 3.5;

#ifdef __WXMAC__
#define FX_TOP_VIEW_FREE_KEY "Cmd"
#else
#define FX_TOP_VIEW_FREE_KEY "Ctrl"
#endif

constexpr const char* scToolHints[] = {
	"Drag to move, Shift adds, " FX_TOP_VIEW_FREE_KEY " ignores snap, Z frames selection",
	"Drag an edge to move its face, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag corners, Shift adds, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag to create a brush, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag a box to fill with an arch, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag around the pivot, " FX_TOP_VIEW_FREE_KEY " ignores snap",
};

static_assert(std::size(scToolHints) == static_cast<size_t>(eTopViewTool::Count));

constexpr double scMetricSteps[] = { 0.1, 0.5, 1.0, 5.0, 10.0, 50.0, 100.0, 500.0, 1000.0, 5000.0, 10000.0 };

const wxColour scBackground(24, 26, 30);
const wxColour scSnapGridColour(34, 38, 44);
const wxColour scMinorGridColour(44, 49, 57);
const wxColour scMajorGridColour(66, 74, 86);
const wxColour scLabelColour(110, 120, 134);
const wxColour scViewLabelColour(150, 160, 176);

const wxColour scAxisColours[3] = { wxColour(150, 62, 62), wxColour(66, 140, 76), wxColour(62, 100, 170) };

const wxColour scGeometryColour(206, 211, 221);
const wxColour scProbeVolumeColour(60, 220, 255);
const wxColour scReflectionColour(255, 120, 220);
const wxColour scVolumeColour(120, 255, 90);
const wxColour scNvBrushColour(255, 150, 40);
const wxColour scDBoundColour(90, 130, 255);
const wxColour scSelectedColour(255, 92, 72);
const wxColour scSelectedFillColour(255, 92, 72, 46);
const wxColour scCreateColour(120, 255, 140);
const wxColour scCreateFillColour(120, 255, 140, 36);
const wxColour scMarqueeColour(120, 170, 255);
const wxColour scMarqueeFillColour(120, 170, 255, 28);
const wxColour scRotateColour(255, 205, 70);
const wxColour scFaceColour(255, 205, 70);
const wxColour scPlayerColour(255, 220, 60);
const wxColour scVertexColour(255, 205, 70);
const wxColour scHandleColour(30, 32, 38);
const wxColour scHandleOutlineColour(235, 240, 250);

constexpr uint32 scKindCount = 6;
constexpr uint32 scBucketCount = scKindCount * 2;

constexpr int scDimGeometryAlpha = 70;
constexpr int scDimVolumeAlpha = 170;
constexpr int scVolumeFillAlpha = 16;

wxColour GetKindColour(eTopViewBrushKind kind)
{
	switch (kind) {
	case eTopViewBrushKind::ProbeVolume:
		return scProbeVolumeColour;
	case eTopViewBrushKind::ReflectionProbe:
		return scReflectionColour;
	case eTopViewBrushKind::Volume:
		return scVolumeColour;
	case eTopViewBrushKind::NvBrush:
		return scNvBrushColour;
	case eTopViewBrushKind::DBound:
		return scDBoundColour;
	default:
		return scGeometryColour;
	}
}

long long NowMilliseconds() { return wxGetLocalTimeMillis().GetValue(); }

double WrapAngle(double angle)
{
	while (angle > FX_PI) {
		angle -= 2.0 * FX_PI;
	}

	while (angle < -FX_PI) {
		angle += 2.0 * FX_PI;
	}

	return angle;
}

double DistanceToSegment(const Vec2d point, const Vec2d a, const Vec2d b)
{
	const Vec2d delta = b - a;
	const double length_sq = delta.LengthSquared();

	double t = 0.0;

	if (length_sq > 0.0) {
		t = std::clamp((point - a).Dot(delta) / length_sq, 0.0, 1.0);
	}

	return (point - (a + delta * t)).Length();
}

Vec2d ToVec2d(const Vec2f& vector) { return Vec2d(vector.X, vector.Y); }

Vec2f ToVec2f(const Vec2d& vector) { return Vec2f(static_cast<float32>(vector.X), static_cast<float32>(vector.Y)); }

bool IsSameHandle(const Vec2d& a, const Vec2d& b)
{
	return std::fabs(a.X - b.X) < scHandleMergeDistance && std::fabs(a.Y - b.Y) < scHandleMergeDistance;
}

double DistanceToHull(const DynArray<Vec2f>& hull, const Vec2d point)
{
	if (hull.IsEmpty()) {
		return DBL_MAX;
	}

	if (hull.Size == 1) {
		return (point - ToVec2d(hull[0])).Length();
	}

	bool inside = (hull.Size >= 3);
	double best = DBL_MAX;

	for (uint32 i = 0; i < hull.Size; i++) {
		const Vec2d a = ToVec2d(hull[i]);
		const Vec2d b = ToVec2d(hull[(i + 1) % hull.Size]);

		if ((b - a).Cross(point - a) < 0.0) {
			inside = false;
		}

		best = std::min(best, DistanceToSegment(point, a, b));
	}

	if (inside) {
		return 0.0;
	}

	return best;
}

double ThicknessAlong(const TopViewProjection& projection, const Vec2d normal)
{
	double low = DBL_MAX;
	double high = -DBL_MAX;

	for (const Vec2f& point : projection.Hull) {
		const double along = ToVec2d(point).Dot(normal);

		low = std::min(low, along);
		high = std::max(high, along);
	}

	if (low > high) {
		return 0.0;
	}

	return high - low;
}

const TopViewFace* FindMatchingFace(const TopViewProjection& projection, const Vec2d normal)
{
	const TopViewFace* best = nullptr;
	double best_dot = scFaceMatchDot;

	for (const TopViewFace& face : projection.Faces) {
		const double dot = ToVec2d(face.Normal).Dot(normal);

		if (dot > best_dot) {
			best_dot = dot;
			best = &face;
		}
	}

	return best;
}

DynArray<Vec2f> BuildShiftedPolygon(const TopViewProjection& projection, const TopViewFace& face, double shift)
{
	struct HalfPlane
	{
		Vec2d Normal;
		double Offset;
	};

	DynArray<Vec2f> result;

	const DynArray<Vec2f>& hull = projection.Hull;

	if (hull.Size < 3) {
		return result;
	}

	const Vec2d face_normal = ToVec2d(face.Normal);
	const double face_offset = face_normal.Dot(ToVec2d(face.A));

	DynArray<HalfPlane> planes;
	Vec2d center(0.0, 0.0);

	for (uint32 i = 0; i < hull.Size; i++) {
		const Vec2d from = ToVec2d(hull[i]);
		const Vec2d along = ToVec2d(hull[(i + 1) % hull.Size]) - from;
		const double length = along.Length();

		center += from / static_cast<double>(hull.Size);

		if (length < 1e-6) {
			continue;
		}

		const Vec2d normal(along.Y / length, -along.X / length);
		const double offset = normal.Dot(from);

		if (normal.Dot(face_normal) > scFaceMatchDot && std::fabs(offset - face_offset) < scFaceSameLineTolerance) {
			continue;
		}

		planes.Insert(HalfPlane { normal, offset });
	}

	planes.Insert(HalfPlane { face_normal, face_offset + shift });

	DynArray<Vec2d> polygon;
	polygon.Insert(Vec2d(center.X - scClipExtent, center.Y - scClipExtent));
	polygon.Insert(Vec2d(center.X + scClipExtent, center.Y - scClipExtent));
	polygon.Insert(Vec2d(center.X + scClipExtent, center.Y + scClipExtent));
	polygon.Insert(Vec2d(center.X - scClipExtent, center.Y + scClipExtent));

	for (const HalfPlane& plane : planes) {
		DynArray<Vec2d> clipped;

		for (uint32 i = 0; i < polygon.Size; i++) {
			const Vec2d& current = polygon[i];
			const Vec2d& previous = polygon[(i + polygon.Size - 1) % polygon.Size];

			const double current_side = plane.Normal.Dot(current) - plane.Offset;
			const double previous_side = plane.Normal.Dot(previous) - plane.Offset;

			const bool current_inside = current_side <= 0.0;
			const bool previous_inside = previous_side <= 0.0;

			if (current_inside != previous_inside) {
				const double t = previous_side / (previous_side - current_side);
				clipped.Insert(previous + (current - previous) * t);
			}

			if (current_inside) {
				clipped.Insert(current);
			}
		}

		polygon = std::move(clipped);

		if (polygon.Size < 3) {
			return result;
		}
	}

	for (const Vec2d& point : polygon) {
		result.Insert(ToVec2f(point));
	}

	return result;
}

DynArray<Vec2d> CollectHandles(const TopViewProjection& projection)
{
	DynArray<Vec2d> handles;

	const auto add = [&handles](const Vec2f& position)
	{
		const Vec2d point = ToVec2d(position);

		for (const Vec2d& handle : handles) {
			if (IsSameHandle(handle, point)) {
				return;
			}
		}

		handles.Insert(point);
	};

	for (const TopViewEdge& edge : projection.Edges) {
		add(edge.A);
		add(edge.B);
	}

	for (const Vec2f& point : projection.Hull) {
		add(point);
	}

	return handles;
}

DynArray<Vec2f> ConvexHull2D(DynArray<Vec2d> points)
{
	std::sort(points.begin(), points.end(),
			  [](const Vec2d& a, const Vec2d& b) { return a.X < b.X || (a.X == b.X && a.Y < b.Y); });

	DynArray<Vec2f> result;

	if (points.Size < 3) {
		return result;
	}

	const auto is_non_left_turn = [](const Vec2d& origin, const Vec2d& a, const Vec2d& b)
	{ return (a - origin).Cross(b - origin) <= 0.0; };

	DynArray<Vec2d> hull;

	for (uint32 i = 0; i < points.Size; i++) {
		while (hull.Size >= 2 && is_non_left_turn(hull[hull.Size - 2], hull[hull.Size - 1], points[i])) {
			hull.RemoveLast();
		}

		hull.Insert(points[i]);
	}

	const uint32 lower_size = hull.Size + 1;

	for (uint32 i = points.Size - 1; i > 0; i--) {
		while (hull.Size >= lower_size && is_non_left_turn(hull[hull.Size - 2], hull[hull.Size - 1], points[i - 1])) {
			hull.RemoveLast();
		}

		hull.Insert(points[i - 1]);
	}

	for (uint32 i = 0; i + 1 < hull.Size; i++) {
		result.Insert(ToVec2f(hull[i]));
	}

	return result;
}

struct ViewTransform
{
	bool bActive = false;
	bool bRotate = false;

	// double OffsetU = 0.0;
	// double OffsetV = 0.0;
	Vec2d OffsetUV = Vec2d::sZero;

	double Cos = 1.0;
	double Sin = 0.0;

	Vec2d PivotUV = Vec2d::sZero;
	// double PivotU = 0.0;
	// double PivotV = 0.0;

	Vec2d Apply(const Vec2d uv) const
	{
		if (!bActive) {
			// out_u = u;
			// out_v = v;
			return uv;
		}

		if (bRotate) {
			const Vec2d rel_uv = uv - PivotUV;

			// const double ru = u - PivotU;
			// const double rv = v - PivotV;


			// out_u = PivotU + Cos * ru - Sin * rv;
			// out_v = PivotV + Sin * ru + Cos * rv;

			return PivotUV + Vec2d(Cos * rel_uv.X - Sin * rel_uv.Y, Sin * rel_uv.X + Cos * rel_uv.Y);
		}

		return (uv + OffsetUV);
		// out_u = u + OffsetU;
		// out_v = v + OffsetV;
	}
};

} // namespace


TopViewCanvas::TopViewCanvas(wxWindow* parent)
	: wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(64, 64), wxWANTS_CHARS | wxBORDER_NONE)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	Bind(wxEVT_PAINT, &TopViewCanvas::OnPaint, this);
	Bind(wxEVT_SIZE, &TopViewCanvas::OnSize, this);

	Bind(wxEVT_LEFT_DOWN, &TopViewCanvas::OnLeftDown, this);
	Bind(wxEVT_LEFT_DCLICK, &TopViewCanvas::OnLeftDown, this);
	Bind(wxEVT_LEFT_UP, &TopViewCanvas::OnLeftUp, this);
	Bind(wxEVT_MIDDLE_DOWN, &TopViewCanvas::OnPanDown, this);
	Bind(wxEVT_MIDDLE_UP, &TopViewCanvas::OnPanUp, this);
	Bind(wxEVT_RIGHT_DOWN, &TopViewCanvas::OnPanDown, this);
	Bind(wxEVT_RIGHT_UP, &TopViewCanvas::OnPanUp, this);
	Bind(wxEVT_MOTION, &TopViewCanvas::OnMotion, this);
	Bind(wxEVT_MOUSEWHEEL, &TopViewCanvas::OnWheel, this);

	if (EnableTouchEvents(wxTOUCH_ZOOM_GESTURE)) {
		Bind(wxEVT_GESTURE_ZOOM, &TopViewCanvas::OnZoomGesture, this);
	}

	Bind(wxEVT_KEY_DOWN, &TopViewCanvas::OnKeyDown, this);
	Bind(wxEVT_KEY_UP, &TopViewCanvas::OnKeyUp, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &TopViewCanvas::OnCaptureLost, this);
	Bind(wxEVT_LEAVE_WINDOW, &TopViewCanvas::OnLeave, this);
	Bind(wxEVT_KILL_FOCUS, &TopViewCanvas::OnKillFocus, this);
}


/////////////////////////////////////
// State
/////////////////////////////////////

const ViewPlaneAxes& TopViewCanvas::Axes() const { return GetViewPlaneAxes(mPlane); }

const TopViewProjection& TopViewCanvas::Projection(const TopViewBrush& brush) const
{
	return brush.Views[static_cast<uint32>(mPlane)];
}

Vec3f TopViewCanvas::ToWorldVector(const Vec2d uv, double w) const
{
	const ViewPlaneAxes& axes = Axes();

	float32 components[3] = { 0.0f, 0.0f, 0.0f };
	components[axes.U] = static_cast<float32>(uv.X * axes.USign);
	components[axes.V] = static_cast<float32>(uv.Y);
	components[axes.W] = static_cast<float32>(w);

	return Vec3f(components[0], components[1], components[2]);
}

void TopViewCanvas::ApplyState(const TopViewState& state)
{
	if (state.CommandSerial >= mSentCommands) {
		mbLocalSelection = false;

		if (mbPreviewHeld) {
			mbPreviewHeld = false;
			ClearPreview();
		}
	}

	mState = state;

	if (mbLocalSelection) {
		ApplyLocalSelection();
	}

	if (!IsToolAvailable(mTool)) {
		SetTool(eTopViewTool::Select);
	}

	if (!mbPreviewHeld && mSelectedVertices.IsNotEmpty()) {
		DynArray<TopViewVertexHandle> kept;

		for (const TopViewVertexHandle& handle : mSelectedVertices) {
			const TopViewBrush* brush = FindBrush(handle.ObjectId);
			bool found = false;

			if (brush != nullptr && brush->bSelected) {
				for (const Vec2d& point : CollectHandles(Projection(*brush))) {
					found = found || IsSameHandle(point, ToVec2d(handle.Position));
				}
			}

			if (found) {
				kept.Insert(handle);
			}
		}

		mSelectedVertices = std::move(kept);
	}

	TryInitialFit();
	UpdateHover();
	UpdateStatus();
	Refresh();
}

bool TopViewCanvas::IsToolAvailable(eTopViewTool tool) const
{
	switch (tool) {
	case eTopViewTool::Face:
	case eTopViewTool::Vertex:
		return mState.bCanFace;
	case eTopViewTool::Create:
		return mState.bCanCreate;
	case eTopViewTool::Arch:
		return mState.bCanCreate && !mState.bDataMode;
	case eTopViewTool::Rotate:
		return mState.bCanRotate;
	default:
		return true;
	}
}

void TopViewCanvas::ApplyLocalSelection()
{
	for (TopViewBrush& brush : mState.Brushes) {
		brush.bSelected = std::find(mLocalSelection.begin(), mLocalSelection.end(), brush.ObjectId) !=
						  mLocalSelection.end();
	}
}

void TopViewCanvas::SetTool(eTopViewTool tool)
{
	if (!IsToolAvailable(tool)) {
		tool = eTopViewTool::Select;
	}

	if (mDrag != eDrag::None) {
		CancelDrag();
	}

	mTool = tool;

	if (tool != eTopViewTool::Vertex) {
		mSelectedVertices.Clear();
	}

	if (mpOnToolChanged) {
		mpOnToolChanged(tool);
	}

	UpdateHover();
	UpdateCursor();
	UpdateStatus();
	Refresh();
}

void TopViewCanvas::SetPlane(eViewPlane plane)
{
	if (mDrag != eDrag::None) {
		CancelDrag();
	}

	if (plane != mPlane) {
		SavedView& saved = mSavedViews[static_cast<uint32>(mPlane)];
		// saved.CenterU = mCenterU;
		// saved.CenterV = mCenterV;

		saved.Center = mCenter;

		saved.Scale = mScale;
		saved.bFitted = mbHasFitted;

		mPlane = plane;

		const SavedView& restored = mSavedViews[static_cast<uint32>(plane)];
		// mCenterU = restored.CenterU;
		// mCenterV = restored.CenterV;

		mCenter = restored.Center;

		mScale = restored.Scale;
		mbHasFitted = restored.bFitted;

		mbPreviewHeld = false;
		ClearPreview();
		mbHoverFace = false;
		mbHoverVertex = false;
		mSelectedVertices.Clear();
	}

	if (mpOnPlaneChanged) {
		mpOnPlaneChanged(mPlane);
	}

	TryInitialFit();
	UpdateHover();
	UpdateCursor();
	UpdateStatus();
	Refresh();
}

void TopViewCanvas::SetCreateVolume(eViewPlane plane, float32 base, float32 size)
{
	mCreateBase[static_cast<uint32>(plane)] = base;
	mCreateSize[static_cast<uint32>(plane)] = size;
}

void TopViewCanvas::SetArchSettings(const ArchSettings& settings)
{
	mArch = settings;
	mArch.Sides = std::clamp(mArch.Sides, scMinArchSides, scMaxArchSides);

	if (mDrag == eDrag::Arch) {
		UpdatePreview();
		Refresh();
	}
}

void TopViewCanvas::SendCommand(thread::Task command)
{
	++mSentCommands;

	thread::PostToGame(
		[command = std::move(command)]() mutable
		{
			command();
			gEditor->NoteTopViewCommand();
		});
}

void TopViewCanvas::TryInitialFit()
{
	const wxSize size = GetViewSize();

	if (!mbHasFitted && mState.Brushes.IsNotEmpty() && size.x > 16 && size.y > 16) {
		FitAll();
	}
}

void TopViewCanvas::FitAll()
{
	ViewBounds bounds {};
	// bounds.MinU = DBL_MAX;
	// bounds.MinV = DBL_MAX;
	// bounds.MaxU = -DBL_MAX;
	// bounds.MaxV = -DBL_MAX;


	for (const TopViewBrush& brush : mState.Brushes) {
		const TopViewProjection& projection = Projection(brush);

		for (const Vec2f& point : projection.Hull) {
			bounds.Rect = DRect(Vec2d::Min(bounds.Rect.Min, Vec2d(point)), Vec2d::Max(bounds.Rect.Max, Vec2d(point)));

			// bounds.MinU = std::min<double>(bounds.MinU, point.X);
			// bounds.MinV = std::min<double>(bounds.MinV, point.Y);
			//
			// bounds.MaxU = std::max<double>(bounds.MaxU, point.X);
			// bounds.MaxV = std::max<double>(bounds.MaxV, point.Y);
			bounds.bValid = true;
		}
	}

	if (!bounds.bValid) {
		const ViewPlaneAxes& axes = Axes();
		//
		// 		double center_u = 0.0;
		// 		double center_v = 0.0;

		Vec2d center { Vec2d::sZero };

		if (mState.bHasPlayer) {
			center = Vec2d(GetAxis(mState.PlayerPosition, axes.U) * axes.USign, GetAxis(mState.PlayerPosition, axes.V));
			// center_u = GetAxis(mState.PlayerPosition, axes.U) * axes.USign;
			// center_v = GetAxis(mState.PlayerPosition, axes.V);
		}

		const Vec2d margin = Vec2d(10.0);

		bounds.Rect = DRect(center - margin, center + margin);

		// bounds.MinU = center_u - 10.0;
		// bounds.MaxU = center_u + 10.0;
		// bounds.MinV = center_v - 10.0;
		// bounds.MaxV = center_v + 10.0;
	}

	FitBounds(bounds);
}

void TopViewCanvas::FitSelection()
{
	const ViewBounds bounds = GetSelectionBounds();

	if (bounds.bValid) {
		FitBounds(bounds);
	}
	else {
		FitAll();
	}
}

void TopViewCanvas::FitBounds(const ViewBounds& bounds)
{
	const wxSize size = GetViewSize();

	if (size.x <= 16 || size.y <= 16) {
		return;
	}

	// 	const double width = std::max(bounds.MaxU - bounds.MinU, 1.0);
	// 	const double height = std::max(bounds.MaxV - bounds.MinV, 1.0);

	const Vec2d bounds_extent = Vec2d::Max(bounds.Rect.GetExtent(), Vec2d(1.0));

	// 	mCenterU = (bounds.MinU + bounds.MaxU) * 0.5;
	// 	mCenterV = (bounds.MinV + bounds.MaxV) * 0.5;

	mCenter = bounds.Rect.GetCenter();


	const Vec2d scale_comps = Vec2d(size.x, size.y) / (bounds_extent * Vec2d(scFitMargin));
	mScale = std::clamp(std::min(scale_comps.X, scale_comps.Y), scMinScale, scMaxScale);

	mbHasFitted = true;

	UpdateStatus();
	Refresh();
}


/////////////////////////////////////
// View transform
/////////////////////////////////////

wxSize TopViewCanvas::GetViewSize() const { return (mOverrideSize != wxDefaultSize) ? mOverrideSize : GetClientSize(); }

Vec2d TopViewCanvas::ToScreenCoords(const Vec2d uv) const
{
	wxSize view_size = GetViewSize();
	Vec2d half_view = Vec2d(static_cast<double>(view_size.x), static_cast<double>(view_size.y)) * Vec2d(0.5);

	// We need to add the U and subtract the V but I just put the negation in the scale step
	Vec2d offset = (uv - mCenter) * Vec2d(mScale, -mScale);

	return (half_view + offset);
}


wxPoint TopViewCanvas::WorldToClient(const Vec2d uv) const
{
	Vec2d screen = ToScreenCoords(uv);
	return wxPoint(static_cast<int>(std::lround(screen.X)), static_cast<int>(std::lround(screen.Y)));
}

Vec2d TopViewCanvas::ClientToWorld(const wxPoint& point) const
{
	const wxSize size = GetViewSize();
	const Vec2d half_size = Vec2d(static_cast<double>(size.x), static_cast<double>(size.y)) * 0.5;

	// Note the negation of the Y component
	Vec2d tmp = Vec2d(static_cast<double>(point.x) - half_size.X, -(static_cast<double>(point.y) - half_size.Y));

	return mCenter + (tmp / Vec2d(mScale, mScale));

	// out_u = mCenterU + (point.x - size.x * 0.5) / mScale;
	// out_v = mCenterV - (point.y - size.y * 0.5) / mScale;
}

void TopViewCanvas::ZoomAt(const wxPoint& anchor, double factor)
{
	const Vec2d v_before = ClientToWorld(anchor);
	mScale = std::clamp(mScale * factor, scMinScale, scMaxScale);
	const Vec2d v_after = ClientToWorld(anchor);

	mCenter += (v_before - v_after);

	Refresh();
}

void TopViewCanvas::PanByPixels(const Vec2d delta)
{
	mCenter -= Vec2d::FlipSigns<1, 0>((delta / Vec2d(mScale)));
	//
	// 	mCenterU -= dx / mScale;
	// 	mCenterV += dy / mScale;

	Refresh();
}

double TopViewCanvas::Snap(double value) const
{
	if (mbSnapBypass || !mState.bSnapEnabled || mState.SnapStep <= 0.0f) {
		return value;
	}

	return std::round(value / mState.SnapStep) * mState.SnapStep;
}

Vec2d TopViewCanvas::Snap(const Vec2d uv) const { return Vec2d(Snap(uv.X), Snap(uv.Y)); }

double TopViewCanvas::SnapAngle(double radians) const
{
	if (mbSnapBypass || !mState.bSnapEnabled || mState.AngleSnapDegrees <= 0.0f) {
		return radians;
	}

	const double step = mState.AngleSnapDegrees * FX_PI / 180.0;

	return std::round(radians / step) * step;
}


/////////////////////////////////////
// Painting
/////////////////////////////////////

void TopViewCanvas::OnPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC buffered(this);
	wxGCDC dc(buffered);

	if (wxGraphicsContext* gc = dc.GetGraphicsContext(); gc != nullptr) {
		Paint(*gc, GetClientSize());
	}
}

wxBitmap TopViewCanvas::Render(const wxSize& size)
{
	wxBitmap bitmap(size.x, size.y);

	{
		wxMemoryDC memory(bitmap);
		wxGCDC dc(memory);

		mOverrideSize = size;

		if (wxGraphicsContext* gc = dc.GetGraphicsContext(); gc != nullptr) {
			Paint(*gc, size);
		}

		mOverrideSize = wxDefaultSize;
	}

	return bitmap;
}

void TopViewCanvas::Paint(wxGraphicsContext& gc, const wxSize& size)
{
	if (mbPreviewHeld && NowMilliseconds() > mHoldDeadline) {
		mbPreviewHeld = false;
		mbLocalSelection = false;
		ClearPreview();
	}

	gc.SetAntialiasMode(wxANTIALIAS_DEFAULT);
	gc.SetBrush(wxBrush(scBackground));
	gc.SetPen(*wxTRANSPARENT_PEN);
	gc.DrawRectangle(0, 0, size.x, size.y);

	DrawGrid(gc, size);
	DrawBrushes(gc, size);
	DrawOverlays(gc, size);
	DrawPlayer(gc);
}

void TopViewCanvas::DrawGrid(wxGraphicsContext& gc, const wxSize& size)
{
	const Vec2d left_bottom = ClientToWorld(wxPoint(0, size.y));
	const Vec2d right_top = ClientToWorld(wxPoint(size.x, 0));

	const auto draw_lines = [&](double step, const wxColour& colour, int width)
	{
		wxGraphicsPath path = gc.CreatePath();

		for (long i = static_cast<long>(std::ceil(left_bottom.X / step)); i * step <= right_top.X; i++) {
			const double x = std::floor(ToScreenCoords(Vec2d(i * step, 0.0)).X) + 0.5;

			path.MoveToPoint(x, 0.0);
			path.AddLineToPoint(x, size.y);
		}

		for (long i = static_cast<long>(std::ceil(left_bottom.Y / step)); i * step <= right_top.Y; i++) {
			const double y = std::floor(ToScreenCoords(Vec2d(0.0, i * step)).Y) + 0.5;

			path.MoveToPoint(0.0, y);
			path.AddLineToPoint(size.x, y);
		}

		gc.SetPen(wxPen(colour, width));
		gc.StrokePath(path);
	};

	const double snap_step = mState.SnapStep;

	if (mState.bSnapEnabled && snap_step > 0.0 && snap_step * mScale >= scSnapGridPixels) {
		const Vec2d extent = right_top - left_bottom;
		const double lines = (extent.X + extent.Y) / snap_step;

		if (lines <= scMaxSnapLines) {
			draw_lines(snap_step, scSnapGridColour, 1);
		}
	}

	size_t minor_index = 0;

	while (minor_index + 2 < std::size(scMetricSteps) && scMetricSteps[minor_index] * mScale < scMinorGridPixels) {
		minor_index++;
	}

	const double minor = scMetricSteps[minor_index];
	const double major = scMetricSteps[minor_index + 1];

	draw_lines(minor, scMinorGridColour, 1);
	draw_lines(major, scMajorGridColour, 1);

	const ViewPlaneAxes& axes = Axes();

	const Vec2d origin = ToScreenCoords(Vec2d::sZero);
	const double origin_x = std::floor(origin.X) + 0.5;
	const double origin_y = std::floor(origin.Y) + 0.5;

	gc.SetPen(wxPen(scAxisColours[axes.U], 1));
	gc.StrokeLine(0.0, origin_y, size.x, origin_y);

	gc.SetPen(wxPen(scAxisColours[axes.V], 1));
	gc.StrokeLine(origin_x, 0.0, origin_x, size.y);

	wxFont font = wxFont(wxFontInfo(9));
	gc.SetFont(font, scLabelColour);

	for (long i = static_cast<long>(std::ceil(left_bottom.X / major)); i * major <= right_top.X; i++) {
		const double x = ToScreenCoords(Vec2d(i * major, 0.0)).X;
		gc.DrawText(wxString::Format("%g", (i == 0) ? 0.0 : i * major * axes.USign), x + 3.0, 2.0);
	}

	for (long i = static_cast<long>(std::ceil(left_bottom.Y / major)); i * major <= right_top.Y; i++) {
		const double y = ToScreenCoords(Vec2d(0.0, i * major)).Y;
		gc.DrawText(wxString::Format("%g", i * major), 3.0, y + 2.0);
	}

	const wxString view_label = wxString::Format("%s (%s%c / %c)", axes.pName, (axes.USign < 0.0f) ? "-" : "",
												 scAxisNames[axes.U], scAxisNames[axes.V]);

	double label_width = 0.0;
	double label_height = 0.0;

	gc.SetFont(wxFont(wxFontInfo(11).Bold()), scViewLabelColour);
	gc.GetTextExtent(view_label, &label_width, &label_height);
	gc.DrawText(view_label, size.x - label_width - 10.0, size.y - label_height - 8.0);
}

void TopViewCanvas::DrawBrushes(wxGraphicsContext& gc, const wxSize& size)
{
	ViewTransform transform;

	if (mPreview.Kind == eDrag::Move) {
		transform.bActive = true;
		transform.OffsetUV = mPreview.OffsetUV;
		// transform.OffsetU = mPreview.OffsetU;
		// transform.OffsetV = mPreview.OffsetV;
	}
	else if (mPreview.Kind == eDrag::Rotate) {
		transform.bActive = true;
		transform.bRotate = true;

		// I do not currently have a double precision sincos
		transform.Cos = std::cos(mPreview.Angle);
		transform.Sin = std::sin(mPreview.Angle);

		transform.PivotUV = mPreview.PivotUV;
		// 		transform.PivotU = mPreview.PivotU;
		// 		transform.PivotV = mPreview.PivotV;
	}

	DynArray<int32> order;

	for (uint32 i = 0; i < mState.Brushes.Size; i++) {
		order.Insert(static_cast<int32>(i));
	}

	std::stable_sort(order.begin(), order.end(),
					 [this](int32 a, int32 b)
					 {
						 const TopViewBrush& brush_a = mState.Brushes[a];
						 const TopViewBrush& brush_b = mState.Brushes[b];

						 if (brush_a.bSelected != brush_b.bSelected) {
							 return !brush_a.bSelected;
						 }

						 return Projection(brush_a).NearDepth < Projection(brush_b).NearDepth;
					 });

	wxGraphicsPath paths[scBucketCount];

	for (wxGraphicsPath& path : paths) {
		path = gc.CreatePath();
	}

	wxGraphicsPath volume_fills[scKindCount];

	for (wxGraphicsPath& path : volume_fills) {
		path = gc.CreatePath();
	}

	wxGraphicsPath selected_path = gc.CreatePath();
	wxGraphicsPath selected_fill = gc.CreatePath();
	wxGraphicsPath ghost_path = gc.CreatePath();

	const auto find_face_preview = [this](ObjectID object_id) -> const OutlinePreview*
	{
		if (mPreview.Kind != eDrag::Face && mPreview.Kind != eDrag::Vertex) {
			return nullptr;
		}

		for (const OutlinePreview& preview : mPreview.Outlines) {
			if (preview.BrushId == object_id) {
				return &preview;
			}
		}

		return nullptr;
	};

	for (const int32 index : order) {
		const TopViewBrush& brush = mState.Brushes[index];
		const TopViewProjection& projection = Projection(brush);

		const ViewTransform& brush_transform = brush.bSelected ? transform : ViewTransform();

		Vec2d min_uv = Vec2d(DBL_MAX);
		Vec2d max_uv = Vec2d(-DBL_MAX);

		// double min_u = DBL_MAX;
		// double max_u = -DBL_MAX;
		// double min_v = DBL_MAX;
		// double max_v = -DBL_MAX;

		for (const Vec2f& point : projection.Hull) {
			Vec2d result_uv = brush_transform.Apply(ToVec2d(point));

			min_uv = Vec2d::Min(min_uv, result_uv);
			max_uv = Vec2d::Max(max_uv, result_uv);

			// min_u = std::min(min_u, result_uv.X);
			// max_u = std::max(max_u, result_uv.X);
			// min_v = std::min(min_v, result_uv.Y);
			// max_v = std::max(max_v, result_uv.Y);
		}

		const Vec2d screen_min = ToScreenCoords(min_uv);
		const Vec2d screen_max = ToScreenCoords(max_uv);

		if (screen_max.X < 0.0 || screen_min.X > size.x || screen_min.Y < 0.0 || screen_max.Y > size.y) {
			continue;
		}

		if (const OutlinePreview* face_preview = find_face_preview(brush.ObjectId); face_preview != nullptr) {
			const DynArray<Vec2f>& polygon = face_preview->Polygon;

			for (const TopViewEdge& edge : projection.Edges) {
				const Vec2d a = ToScreenCoords(ToVec2d(edge.A));
				const Vec2d b = ToScreenCoords(ToVec2d(edge.B));

				ghost_path.MoveToPoint(a.X, a.Y);
				ghost_path.AddLineToPoint(b.X, b.Y);
			}

			for (uint32 i = 0; i < polygon.Size; i++) {
				const Vec2d screen = ToScreenCoords(ToVec2d(polygon[i]));
				const Vec2d next = ToScreenCoords(ToVec2d(polygon[(i + 1) % polygon.Size]));

				if (i == 0) {
					selected_fill.MoveToPoint(screen.X, screen.Y);
				}
				else {
					selected_fill.AddLineToPoint(screen.X, screen.Y);
				}

				selected_path.MoveToPoint(screen.X, screen.Y);
				selected_path.AddLineToPoint(next.X, next.Y);
			}

			selected_fill.CloseSubpath();
			continue;
		}

		if (!brush.bSelected && brush.Kind != eTopViewBrushKind::Geometry && projection.Hull.Size >= 3) {
			wxGraphicsPath& fill = volume_fills[static_cast<uint32>(brush.Kind)];

			for (uint32 i = 0; i < projection.Hull.Size; i++) {
				const Vec2d screen = ToScreenCoords(ToVec2d(projection.Hull[i]));

				if (i == 0) {
					fill.MoveToPoint(screen.X, screen.Y);
				}
				else {
					fill.AddLineToPoint(screen.X, screen.Y);
				}
			}

			fill.CloseSubpath();
		}

		wxGraphicsPath& path = brush.bSelected
								   ? selected_path
								   : paths[static_cast<uint32>(brush.Kind) * 2 + (brush.bSelectable ? 0 : 1)];

		for (const TopViewEdge& edge : projection.Edges) {
			const Vec2d a = ToScreenCoords(brush_transform.Apply(ToVec2d(edge.A)));
			const Vec2d b = ToScreenCoords(brush_transform.Apply(ToVec2d(edge.B)));

			path.MoveToPoint(a.X, a.Y);
			path.AddLineToPoint(b.X, b.Y);
		}

		if (brush.bSelected && transform.bActive) {
			for (const TopViewEdge& edge : projection.Edges) {
				const Vec2d a = ToScreenCoords(ToVec2d(edge.A));
				const Vec2d b = ToScreenCoords(ToVec2d(edge.B));

				ghost_path.MoveToPoint(a.X, a.Y);
				ghost_path.AddLineToPoint(b.X, b.Y);
			}
		}

		if (brush.bSelected && projection.Hull.Size >= 3) {
			for (uint32 i = 0; i < projection.Hull.Size; i++) {
				const Vec2d screen = ToScreenCoords(brush_transform.Apply(ToVec2d(projection.Hull[i])));

				if (i == 0) {
					selected_fill.MoveToPoint(screen.X, screen.Y);
				}
				else {
					selected_fill.AddLineToPoint(screen.X, screen.Y);
				}
			}

			selected_fill.CloseSubpath();
		}
	}

	gc.SetPen(*wxTRANSPARENT_PEN);

	for (uint32 kind = 1; kind < scKindCount; kind++) {
		const wxColour colour = GetKindColour(static_cast<eTopViewBrushKind>(kind));

		gc.SetBrush(wxBrush(wxColour(colour.Red(), colour.Green(), colour.Blue(), scVolumeFillAlpha)));
		gc.FillPath(volume_fills[kind], wxWINDING_RULE);
	}

	for (uint32 kind = 0; kind < scKindCount; kind++) {
		const wxColour colour = GetKindColour(static_cast<eTopViewBrushKind>(kind));
		const bool is_geometry = (kind == static_cast<uint32>(eTopViewBrushKind::Geometry));

		gc.SetPen(wxPen(colour, 1));
		gc.StrokePath(paths[kind * 2]);

		gc.SetPen(wxPen(
			wxColour(colour.Red(), colour.Green(), colour.Blue(), is_geometry ? scDimGeometryAlpha : scDimVolumeAlpha),
			1, is_geometry ? wxPENSTYLE_SOLID : wxPENSTYLE_SHORT_DASH));
		gc.StrokePath(paths[kind * 2 + 1]);
	}

	gc.SetPen(wxPen(wxColour(scSelectedColour.Red(), scSelectedColour.Green(), scSelectedColour.Blue(), 80), 1));
	gc.StrokePath(ghost_path);

	gc.SetBrush(wxBrush(scSelectedFillColour));
	gc.SetPen(*wxTRANSPARENT_PEN);
	gc.FillPath(selected_fill, wxWINDING_RULE);

	gc.SetPen(wxPen(scSelectedColour, 2));
	gc.StrokePath(selected_path);
}

void TopViewCanvas::DrawOverlays(wxGraphicsContext& gc, const wxSize& size)
{
	wxFont font = wxFont(wxFontInfo(10));

	const auto to_screen_rect = [this](const DRect& rect)
	{
		const Vec2d top_left = ToScreenCoords(Vec2d(rect.Min.X, rect.Max.Y));
		const Vec2d bottom_right = ToScreenCoords(Vec2d(rect.Max.X, rect.Min.Y));

		return DRect(top_left, bottom_right);
	};

	if (mTool == eTopViewTool::Vertex) {
		const bool live = (mDrag == eDrag::Vertex) && (mPreview.Kind == eDrag::Vertex);

		for (const TopViewBrush& brush : mState.Brushes) {
			if (!brush.bSelected) {
				continue;
			}

			for (const Vec2d& point : CollectHandles(Projection(brush))) {
				const bool selected = IsVertexSelected(brush.ObjectId, point);
				const bool hovered = mbHoverVertex && mHoverVertex.ObjectId == brush.ObjectId &&
									 IsSameHandle(point, ToVec2d(mHoverVertex.Position));

				Vec2d position = point;

				if (live && selected) {
					position += mPreview.OffsetUV;
				}

				const Vec2d screen = ToScreenCoords(position);
				const double half = hovered ? scHandleHalfSize + 1.5 : scHandleHalfSize;

				gc.SetBrush(wxBrush(selected ? scVertexColour : scHandleColour));
				gc.SetPen(wxPen(hovered ? scVertexColour : scHandleOutlineColour, hovered ? 2 : 1));
				gc.DrawRectangle(screen.X - half, screen.Y - half, half * 2.0, half * 2.0);
			}
		}
	}

	if (mbHoverFace && mPreview.Kind == eDrag::None) {
		gc.SetPen(wxPen(scFaceColour, 3));
		const Vec2d a = ToScreenCoords(ToVec2d(mHoverFace.A));
		const Vec2d b = ToScreenCoords(ToVec2d(mHoverFace.B));

		gc.StrokeLine(a.X, a.Y, b.X, b.Y);
	}

	if (mPreview.Kind == eDrag::Face) {
		const double shift = mPreview.Shift;
		const Vec2d shift_uv = mPreview.NormalUV * shift;

		const Vec2d a = ToScreenCoords(ToVec2d(mGrabbedFace.A) + shift_uv);
		const Vec2d b = ToScreenCoords(ToVec2d(mGrabbedFace.B) + shift_uv);
		const Vec2d middle = (a + b) * 0.5;

		gc.SetPen(wxPen(scFaceColour, 3));
		gc.StrokeLine(a.X, a.Y, b.X, b.Y);

		gc.SetFont(font, scFaceColour);
		gc.DrawText(wxString::Format("%+.2f", shift), middle.X + 8.0, middle.Y + 6.0);
		return;
	}

	if (mDrag == eDrag::Marquee && mbDragMoved) {
		const DRect screen = to_screen_rect(DRect(Vec2d::Min(mPress, mCurrent), Vec2d::Max(mPress, mCurrent)));
		const Vec2d screen_extent = screen.GetExtent();

		gc.SetBrush(wxBrush(scMarqueeFillColour));
		gc.SetPen(wxPen(scMarqueeColour, 1));
		gc.DrawRectangle(screen.Min.X, screen.Min.Y, screen_extent.X, screen_extent.Y);
		return;
	}

	if (mPreview.Kind == eDrag::Create) {
		const DRect screen = to_screen_rect(mPreview.RectUV);
		const Vec2d screen_extent = screen.GetExtent();
		const Vec2d extent = mPreview.RectUV.GetExtent();

		gc.SetBrush(wxBrush(scCreateFillColour));
		gc.SetPen(wxPen(scCreateColour, 2));
		gc.DrawRectangle(screen.Min.X, screen.Min.Y, screen_extent.X, screen_extent.Y);

		gc.SetFont(font, scCreateColour);
		gc.DrawText(wxString::Format("%.2f x %.2f", extent.X, extent.Y), screen.Min.X + 4.0, screen.Max.Y + 4.0);
		return;
	}

	if (mPreview.Kind == eDrag::Arch) {
		wxGraphicsPath outlines = gc.CreatePath();

		for (const OutlinePreview& outline : mPreview.Outlines) {
			const Vec2d first = ToScreenCoords(ToVec2d(outline.Polygon[0]));
			outlines.MoveToPoint(first.X, first.Y);

			for (uint32 i = 1; i < outline.Polygon.Size; i++) {
				const Vec2d screen_point = ToScreenCoords(ToVec2d(outline.Polygon[i]));
				outlines.AddLineToPoint(screen_point.X, screen_point.Y);
			}

			outlines.CloseSubpath();
		}

		const DRect screen = to_screen_rect(mPreview.RectUV);
		const Vec2d screen_extent = screen.GetExtent();
		const Vec2d extent = mPreview.RectUV.GetExtent();

		gc.SetPen(wxPen(scCreateColour, 1, wxPENSTYLE_SHORT_DASH));
		gc.SetBrush(*wxTRANSPARENT_BRUSH);
		gc.DrawRectangle(screen.Min.X, screen.Min.Y, screen_extent.X, screen_extent.Y);

		gc.SetBrush(wxBrush(scCreateFillColour));
		gc.SetPen(wxPen(scCreateColour, 1));
		gc.FillPath(outlines, wxWINDING_RULE);
		gc.StrokePath(outlines);

		gc.SetFont(font, scCreateColour);
		gc.DrawText(wxString::Format("%.2f x %.2f, %u sides", extent.X, extent.Y, mArch.Sides), screen.Min.X + 4.0,
					screen.Max.Y + 4.0);
		return;
	}

	if (mPreview.Kind == eDrag::Rotate) {
		const Vec2d pivot = ToScreenCoords(mPreview.PivotUV);
		const double pivot_x = pivot.X;
		const double pivot_y = pivot.Y;

		gc.SetPen(wxPen(scRotateColour, 1));
		gc.StrokeLine(pivot_x - 7.0, pivot_y, pivot_x + 7.0, pivot_y);
		gc.StrokeLine(pivot_x, pivot_y - 7.0, pivot_x, pivot_y + 7.0);

		if (mDrag == eDrag::Rotate) {
			const Vec2d current = ToScreenCoords(mCurrent);
			gc.StrokeLine(pivot_x, pivot_y, current.X, current.Y);
		}

		gc.SetFont(font, scRotateColour);
		gc.DrawText(wxString::Format("%.1f deg", mPreview.Angle * 180.0 / FX_PI), pivot_x + 10.0, pivot_y + 8.0);
		return;
	}

	if (mPreview.Kind == eDrag::Move) {
		const ViewBounds bounds = mDragBounds;

		if (bounds.bValid) {
			const Vec2d anchor = ToScreenCoords(bounds.Rect.Min + mPreview.OffsetUV);

			gc.SetFont(font, scSelectedColour);
			gc.DrawText(wxString::Format("%+.2f, %+.2f", mPreview.OffsetUV.X * Axes().USign, mPreview.OffsetUV.Y),
						anchor.X, anchor.Y + 4.0);
		}

		return;
	}

	if (mPreview.Kind == eDrag::None) {
		const ViewBounds bounds = GetSelectionBounds();

		if (bounds.bValid) {
			const Vec2d extent = bounds.Rect.GetExtent();
			const Vec2d anchor = ToScreenCoords(bounds.Rect.Min);
			const wxString label = wxString::Format("%.2f x %.2f", extent.X, extent.Y);

			double label_width = 0.0;
			double label_height = 0.0;

			gc.SetFont(font, scSelectedColour);
			gc.GetTextExtent(label, &label_width, &label_height);

			const double x = std::clamp(anchor.X, scDimensionLabelMargin,
										std::max(scDimensionLabelMargin, size.x - label_width - scDimensionLabelMargin));
			const double y = std::clamp(anchor.Y + 4.0, scDimensionLabelTop,
										std::max(scDimensionLabelTop, size.y - label_height - scDimensionLabelMargin));

			gc.DrawText(label, x, y);
		}
	}
}

void TopViewCanvas::DrawPlayer(wxGraphicsContext& gc)
{
	if (!mState.bHasPlayer) {
		return;
	}

	const ViewPlaneAxes& axes = Axes();

	const Vec2d position = ToScreenCoords(
		Vec2d(GetAxis(mState.PlayerPosition, axes.U) * axes.USign, GetAxis(mState.PlayerPosition, axes.V)));
	const double x = position.X;
	const double y = position.Y;

	double dir_x = GetAxis(mState.PlayerDirection, axes.U) * axes.USign;
	double dir_y = -GetAxis(mState.PlayerDirection, axes.V);

	const double length = std::hypot(dir_x, dir_y);

	if (length < scMinMarkerDirection) {
		gc.SetBrush(wxBrush(wxColour(scPlayerColour.Red(), scPlayerColour.Green(), scPlayerColour.Blue(), 90)));
		gc.SetPen(wxPen(scPlayerColour, 2));
		gc.DrawEllipse(x - 7.0, y - 7.0, 14.0, 14.0);
		return;
	}

	dir_x /= length;
	dir_y /= length;

	constexpr double scSize = 9.0;

	const double side_x = -dir_y;
	const double side_y = dir_x;

	wxGraphicsPath path = gc.CreatePath();
	path.MoveToPoint(x + dir_x * scSize * 1.4, y + dir_y * scSize * 1.4);
	path.AddLineToPoint(x - dir_x * scSize + side_x * scSize * 0.8, y - dir_y * scSize + side_y * scSize * 0.8);
	path.AddLineToPoint(x - dir_x * scSize * 0.4, y - dir_y * scSize * 0.4);
	path.AddLineToPoint(x - dir_x * scSize - side_x * scSize * 0.8, y - dir_y * scSize - side_y * scSize * 0.8);
	path.CloseSubpath();

	gc.SetBrush(wxBrush(wxColour(scPlayerColour.Red(), scPlayerColour.Green(), scPlayerColour.Blue(), 90)));
	gc.SetPen(wxPen(scPlayerColour, 2));
	gc.DrawPath(path);
}

void TopViewCanvas::OnSize(wxSizeEvent& event)
{
	TryInitialFit();
	Refresh();
	event.Skip();
}


/////////////////////////////////////
// Picking
/////////////////////////////////////

DynArray<int32> TopViewCanvas::PickAt(const Vec2d uv) const
{
	struct Candidate
	{
		int32 Index;
		double Distance;
		float32 Depth;
	};

	DynArray<Candidate> candidates;

	const double tolerance = scPickTolerancePixels / mScale;

	for (uint32 i = 0; i < mState.Brushes.Size; i++) {
		const TopViewBrush& brush = mState.Brushes[i];

		if (!brush.bSelectable) {
			continue;
		}

		const TopViewProjection& projection = Projection(brush);
		const double distance = DistanceToHull(projection.Hull, uv);

		if (distance <= tolerance) {
			candidates.Insert(Candidate { static_cast<int32>(i), distance, projection.NearDepth });
		}
	}

	std::stable_sort(candidates.begin(), candidates.end(),
					 [](const Candidate& a, const Candidate& b)
					 {
						 if ((a.Distance > 0.0) != (b.Distance > 0.0)) {
							 return a.Distance <= 0.0;
						 }

						 return a.Depth > b.Depth;
					 });

	DynArray<int32> hits;

	for (const Candidate& candidate : candidates) {
		hits.Insert(candidate.Index);
	}

	return hits;
}

int TopViewCanvas::PickFaceAt(const Vec2d uv, int& out_face) const
{
	const double tolerance = scPickTolerancePixels / mScale;
	const double bonus = scFacePickBonusPixels / mScale;

	double best_score = DBL_MAX;
	int best_brush = -1;

	for (uint32 i = 0; i < mState.Brushes.Size; i++) {
		const TopViewBrush& brush = mState.Brushes[i];

		if (!brush.bSelectable) {
			continue;
		}

		const TopViewProjection& projection = Projection(brush);

		for (uint32 j = 0; j < projection.Faces.Size; j++) {
			const TopViewFace& face = projection.Faces[j];
			const double distance = DistanceToSegment(uv, ToVec2d(face.A), ToVec2d(face.B));

			if (distance > tolerance) {
				continue;
			}

			const double score = distance - (brush.bSelected ? bonus : 0.0);

			if (score < best_score) {
				best_score = score;
				best_brush = static_cast<int>(i);
				out_face = static_cast<int>(j);
			}
		}
	}

	return best_brush;
}

bool TopViewCanvas::IsVertexSelected(ObjectID object_id, const Vec2d uv) const
{
	for (const TopViewVertexHandle& handle : mSelectedVertices) {
		if (handle.ObjectId == object_id && IsSameHandle(uv, ToVec2d(handle.Position))) {
			return true;
		}
	}

	return false;
}

bool TopViewCanvas::PickVertexAt(const Vec2d uv, TopViewVertexHandle& out_handle) const
{
	const double tolerance = scVertexPickPixels / mScale;

	double best = tolerance;
	bool found = false;

	for (const TopViewBrush& brush : mState.Brushes) {
		if (!brush.bSelected || !brush.bSelectable) {
			continue;
		}

		for (const Vec2d& point : CollectHandles(Projection(brush))) {
			const double distance = (point - uv).Length();

			if (distance <= best) {
				best = distance;
				found = true;
				out_handle = TopViewVertexHandle { brush.ObjectId, ToVec2f(point) };
			}
		}
	}

	return found;
}

void TopViewCanvas::UpdateHover()
{
	const bool had_face = mbHoverFace;
	const TopViewFace previous_face = mHoverFace;
	const bool had_vertex = mbHoverVertex;
	const TopViewVertexHandle previous_vertex = mHoverVertex;

	mbHoverFace = false;
	mbHoverVertex = false;

	if (mDrag == eDrag::None && mbHovering && !mbSpaceDown && !mbAltDown) {
		if (mTool == eTopViewTool::Face) {
			int face_index = -1;
			const int brush_index = PickFaceAt(mHover, face_index);

			if (brush_index >= 0) {
				mbHoverFace = true;
				mHoverFace = Projection(mState.Brushes[brush_index]).Faces[face_index];
			}
		}
		else if (mTool == eTopViewTool::Vertex) {
			mbHoverVertex = PickVertexAt(mHover, mHoverVertex);
		}
	}

	const bool vertex_changed = (mbHoverVertex != had_vertex) ||
								(mbHoverVertex && (mHoverVertex.ObjectId != previous_vertex.ObjectId ||
												   !(mHoverVertex.Position == previous_vertex.Position)));

	if (mbHoverFace != had_face || (mbHoverFace && !(mHoverFace == previous_face)) || vertex_changed) {
		UpdateCursor();
		Refresh();
	}
}

void TopViewCanvas::OnKillFocus(wxFocusEvent& event)
{
	mbSpaceDown = false;
	mbAltDown = false;
	SetSnapBypass(false);

	UpdateHover();
	UpdateCursor();
	event.Skip();
}

void TopViewCanvas::OnLeave(wxMouseEvent& event)
{
	mbHovering = false;

	UpdateHover();
	event.Skip();
}

const TopViewBrush* TopViewCanvas::FindBrush(ObjectID object_id) const
{
	for (const TopViewBrush& brush : mState.Brushes) {
		if (brush.ObjectId == object_id) {
			return &brush;
		}
	}

	return nullptr;
}

TopViewCanvas::ViewBounds TopViewCanvas::GetSelectionBounds() const
{
	ViewBounds bounds {};

	for (const TopViewBrush& brush : mState.Brushes) {
		if (!brush.bSelected) {
			continue;
		}

		const TopViewProjection& projection = Projection(brush);

		for (const Vec2f& point : projection.Hull) {
			bounds.Rect = DRect(Vec2d::Min(bounds.Rect.Min, Vec2d(point)), Vec2d::Max(bounds.Rect.Max, Vec2d(point)));
			bounds.bValid = true;
		}
	}

	return bounds;
}

bool TopViewCanvas::GetSelectionPivot(Vec2d& out_pivot) const
{
	const TopViewBrush* only = nullptr;
	uint32 count = 0;

	for (const TopViewBrush& brush : mState.Brushes) {
		if (brush.bSelected) {
			only = &brush;
			count++;
		}
	}

	if (count == 0) {
		return false;
	}

	if (count == 1) {
		out_pivot = ToVec2d(Projection(*only).Pivot);
		return true;
	}

	const ViewBounds bounds = GetSelectionBounds();

	out_pivot = bounds.Rect.GetCenter();

	return bounds.bValid;
}

uint32 TopViewCanvas::GetSelectedCount() const
{
	uint32 count = 0;

	for (const TopViewBrush& brush : mState.Brushes) {
		count += brush.bSelected ? 1 : 0;
	}

	return count;
}

void TopViewCanvas::SelectBrush(ObjectID object_id, eTopViewSelectMode mode)
{
	DynArray<ObjectID> object_ids;
	object_ids.Insert(object_id);

	SelectBrushes(std::move(object_ids), mode);
}

void TopViewCanvas::SelectBrushes(DynArray<ObjectID> object_ids, eTopViewSelectMode mode)
{
	for (TopViewBrush& brush : mState.Brushes) {
		const bool listed = std::find(object_ids.begin(), object_ids.end(), brush.ObjectId) != object_ids.end();

		switch (mode) {
		case eTopViewSelectMode::Replace:
			brush.bSelected = listed;
			break;
		case eTopViewSelectMode::Add:
			brush.bSelected = brush.bSelected || listed;
			break;
		case eTopViewSelectMode::Toggle:
			brush.bSelected = brush.bSelected != listed;
			break;
		}
	}

	mLocalSelection.Clear();

	for (const TopViewBrush& brush : mState.Brushes) {
		if (brush.bSelected) {
			mLocalSelection.Insert(brush.ObjectId);
		}
	}

	mbLocalSelection = true;

	SendCommand([object_ids = std::move(object_ids), mode] { gEditor->TopViewSelect(object_ids, mode); });

	UpdateStatus();
	Refresh();
}

void TopViewCanvas::SelectAll()
{
	DynArray<ObjectID> ids;

	for (const TopViewBrush& brush : mState.Brushes) {
		if (brush.bSelectable) {
			ids.Insert(brush.ObjectId);
		}
	}

	SelectBrushes(std::move(ids), eTopViewSelectMode::Replace);
}

void TopViewCanvas::InvertSelection()
{
	DynArray<ObjectID> ids;

	for (const TopViewBrush& brush : mState.Brushes) {
		if (brush.bSelectable && !brush.bSelected) {
			ids.Insert(brush.ObjectId);
		}
	}

	SelectBrushes(std::move(ids), eTopViewSelectMode::Replace);
}


/////////////////////////////////////
// Mouse
/////////////////////////////////////

void TopViewCanvas::BeginPan(const wxPoint& position)
{
	mDrag = eDrag::Pan;
	mPressScreen = position;
	mPanStartCenter = mCenter;
	mbDragMoved = false;

	if (!HasCapture()) {
		CaptureMouse();
	}

	UpdateCursor();
}

void TopViewCanvas::OnPanDown(wxMouseEvent& event)
{
	if (mDrag != eDrag::None) {
		return;
	}

	SetFocus();
	BeginPan(event.GetPosition());
}

void TopViewCanvas::OnPanUp(wxMouseEvent&) { EndPan(); }

void TopViewCanvas::EndPan()
{
	if (mDrag != eDrag::Pan) {
		return;
	}

	mDrag = eDrag::None;

	if (HasCapture()) {
		ReleaseMouse();
	}

	UpdateCursor();
}

void TopViewCanvas::OnLeftDown(wxMouseEvent& event)
{
	SetFocus();

	if (mDrag != eDrag::None) {
		return;
	}

	const wxPoint position = event.GetPosition();

	ClearPreview();
	mbPreviewHeld = false;

	mPressScreen = position;
	mPress = ClientToWorld(position);
	mCurrent = mPress;
	mbDragMoved = false;
	mbAdditive = event.ShiftDown();

	if (mbSpaceDown || event.AltDown()) {
		BeginPan(position);
		return;
	}

	switch (mTool) {
	case eTopViewTool::Face:
		BeginFace();
		break;
	case eTopViewTool::Vertex:
		BeginVertex();
		break;
	case eTopViewTool::Create:
		BeginCreate();
		break;
	case eTopViewTool::Arch:
		BeginArch();
		break;
	case eTopViewTool::Rotate:
		BeginRotate();
		break;
	default:
		BeginSelect();
		break;
	}

	if (mDrag != eDrag::None && !HasCapture()) {
		CaptureMouse();
	}

	UpdateCursor();
	Refresh();
}

void TopViewCanvas::BeginSelect()
{
	const DynArray<int32> hits = PickAt(mPress);

	mbHasPressedBrush = false;

	if (hits.IsEmpty()) {
		mDrag = eDrag::Marquee;
		return;
	}

	int32 pressed = hits[0];

	for (const int32 index : hits) {
		if (mState.Brushes[index].bSelected) {
			pressed = index;
			break;
		}
	}

	mbHasPressedBrush = true;
	mPressedId = mState.Brushes[pressed].ObjectId;
	mPressedHitIds.Clear();

	for (const int32 index : hits) {
		mPressedHitIds.Insert(mState.Brushes[index].ObjectId);
	}

	mbPressedWasSelected = mState.Brushes[pressed].bSelected;

	if (!mbPressedWasSelected) {
		SelectBrush(mState.Brushes[pressed].ObjectId,
					mbAdditive ? eTopViewSelectMode::Add : eTopViewSelectMode::Replace);
	}

	mDragBounds = GetSelectionBounds();
	mDrag = eDrag::Move;
}

void TopViewCanvas::BeginFace()
{
	int face_index = -1;
	const int brush_index = PickFaceAt(mPress, face_index);

	mbHasPressedBrush = false;

	if (brush_index < 0) {
		const DynArray<int32> hits = PickAt(mPress);

		if (hits.IsEmpty()) {
			mDrag = eDrag::Marquee;
			return;
		}

		const TopViewBrush& clicked = mState.Brushes[hits[0]];

		if (!clicked.bSelected || GetSelectedCount() > 1) {
			SelectBrush(clicked.ObjectId, mbAdditive ? eTopViewSelectMode::Add : eTopViewSelectMode::Replace);
		}

		return;
	}

	const TopViewBrush& brush = mState.Brushes[brush_index];

	if (!brush.bSelected) {
		SelectBrush(brush.ObjectId, mbAdditive ? eTopViewSelectMode::Add : eTopViewSelectMode::Replace);
	}

	mGrabbedBrushId = brush.ObjectId;
	mGrabbedFace = Projection(brush).Faces[face_index];
	mFaceBaseOffset = mGrabbedFace.Normal.X * mGrabbedFace.A.X + mGrabbedFace.Normal.Y * mGrabbedFace.A.Y;
	mbHoverFace = false;
	mDrag = eDrag::Face;
}

void TopViewCanvas::BeginVertex()
{
	TopViewVertexHandle handle;

	mbHasPressedBrush = false;
	mbVertexToggleOnRelease = false;

	if (PickVertexAt(mPress, handle)) {
		if (!IsVertexSelected(handle.ObjectId, ToVec2d(handle.Position))) {
			if (!mbAdditive) {
				mSelectedVertices.Clear();
			}

			mSelectedVertices.Insert(handle);
		}
		else if (mbAdditive) {
			mbVertexToggleOnRelease = true;
		}

		mGrabbedVertex = handle;
		mbHoverVertex = false;
		mDrag = eDrag::Vertex;
		return;
	}

	const DynArray<int32> hits = PickAt(mPress);

	if (hits.IsEmpty()) {
		mDrag = eDrag::Marquee;
		return;
	}

	mSelectedVertices.Clear();

	const TopViewBrush& clicked = mState.Brushes[hits[0]];

	if (!clicked.bSelected || GetSelectedCount() > 1) {
		SelectBrush(clicked.ObjectId, mbAdditive ? eTopViewSelectMode::Add : eTopViewSelectMode::Replace);
	}
}

void TopViewCanvas::BeginCreate()
{
	mPress = Snap(mPress);
	mDrag = eDrag::Create;
}

void TopViewCanvas::BeginArch()
{
	mPress = Snap(mPress);
	mDrag = eDrag::Arch;
}

void TopViewCanvas::BeginRotate()
{
	const DynArray<int32> hits = PickAt(mPress);

	bool hit_selected = false;

	for (const int32 index : hits) {
		hit_selected = hit_selected || mState.Brushes[index].bSelected;
	}

	if (!hits.IsEmpty() && !hit_selected) {
		SelectBrush(mState.Brushes[hits[0]].ObjectId, eTopViewSelectMode::Replace);
		return;
	}

	if (!GetSelectionPivot(mRotatePivot)) {
		return;
	}

	const Vec2d from_pivot = mPress - mRotatePivot;
	mRotateStartAngle = std::atan2(from_pivot.Y, from_pivot.X);
	mDrag = eDrag::Rotate;
}

void TopViewCanvas::OnMotion(wxMouseEvent& event)
{
	const wxPoint position = event.GetPosition();

	mHover = ClientToWorld(position);
	mbHovering = true;

	if (event.AltDown() != mbAltDown) {
		mbAltDown = event.AltDown();
		UpdateCursor();
	}

	mbSnapBypass = event.ControlDown();

	if (mDrag == eDrag::Pan && !event.LeftIsDown() && !event.MiddleIsDown() && !event.RightIsDown()) {
		EndPan();
		return;
	}

	if (mDrag != eDrag::None && mDrag != eDrag::Pan && !event.LeftIsDown()) {
		wxMouseEvent released(wxEVT_LEFT_UP);
		OnLeftUp(released);
		return;
	}

	if (mDrag == eDrag::None) {
		UpdateHover();
		UpdateStatus();
		return;
	}

	if (mDrag == eDrag::Pan) {
		const Vec2d delta = Vec2d(position.x - mPressScreen.x, position.y - mPressScreen.y) / mScale;
		mCenter = mPanStartCenter + Vec2d::FlipSigns<-1, 1>(delta);
		UpdateStatus();
		Refresh();
		return;
	}

	mCurrent = ClientToWorld(position);

	if (!mbDragMoved) {
		const double dx = position.x - mPressScreen.x;
		const double dy = position.y - mPressScreen.y;

		mbDragMoved = std::hypot(dx, dy) >= scDragThresholdPixels;
	}

	UpdatePreview();
	UpdateStatus();
	Refresh();
}

void TopViewCanvas::UpdatePreview()
{
	if (!mbDragMoved) {
		return;
	}

	switch (mDrag) {
	case eDrag::Move: {
		Vec2d delta = mCurrent - mPress;

		if (mDragBounds.bValid) {
			delta = Snap(mDragBounds.Rect.Min + delta) - mDragBounds.Rect.Min;
		}

		mPreview.Kind = eDrag::Move;
		mPreview.OffsetUV = delta;
		break;
	}
	case eDrag::Face: {
		const Vec2d normal = ToVec2d(mGrabbedFace.Normal);

		double shift = (mCurrent - mPress).Dot(normal);

		const bool axis_aligned = std::fabs(normal.X) > 0.9999 || std::fabs(normal.Y) > 0.9999;
		shift = axis_aligned ? Snap(mFaceBaseOffset + shift) - mFaceBaseOffset : Snap(shift);

		const double min_thickness = mState.MinFaceThickness;

		if (const TopViewBrush* grabbed = FindBrush(mGrabbedBrushId); grabbed != nullptr) {
			shift = std::max(shift, std::min(0.0, min_thickness - ThicknessAlong(Projection(*grabbed), normal)));
		}

		mPreview = Preview();
		mPreview.Kind = eDrag::Face;
		mPreview.Shift = shift;
		mPreview.NormalUV = normal;

		for (const TopViewBrush& brush : mState.Brushes) {
			if (!brush.bSelected) {
				continue;
			}

			const TopViewProjection& projection = Projection(brush);
			const TopViewFace* face = FindMatchingFace(projection, normal);

			if (face == nullptr) {
				continue;
			}

			const double brush_shift = std::max(shift,
												std::min(0.0, min_thickness - ThicknessAlong(projection, normal)));
			DynArray<Vec2f> polygon = BuildShiftedPolygon(projection, *face, brush_shift);

			if (polygon.Size >= 3) {
				mPreview.Outlines.Insert(OutlinePreview { brush.ObjectId, std::move(polygon) });
			}
		}

		break;
	}
	case eDrag::Vertex: {
		const Vec2d grabbed = ToVec2d(mGrabbedVertex.Position);
		const Vec2d target = Snap(grabbed + (mCurrent - mPress));

		mPreview = Preview();
		mPreview.Kind = eDrag::Vertex;
		mPreview.OffsetUV = target - grabbed;

		for (const TopViewBrush& brush : mState.Brushes) {
			if (!brush.bSelected) {
				continue;
			}

			DynArray<Vec2d> points = CollectHandles(Projection(brush));
			bool moves_any = false;

			for (Vec2d& point : points) {
				if (IsVertexSelected(brush.ObjectId, point)) {
					point += mPreview.OffsetUV;
					moves_any = true;
				}
			}

			if (!moves_any) {
				continue;
			}

			DynArray<Vec2f> polygon = ConvexHull2D(std::move(points));

			if (polygon.Size >= 3) {
				mPreview.Outlines.Insert(OutlinePreview { brush.ObjectId, std::move(polygon) });
			}
		}

		break;
	}
	case eDrag::Create: {
		const Vec2d current = Snap(mCurrent);

		mPreview.Kind = eDrag::Create;
		mPreview.RectUV = DRect(Vec2d::Min(mPress, current), Vec2d::Max(mPress, current));
		break;
	}
	case eDrag::Arch: {
		const Vec2d current = Snap(mCurrent);

		mPreview.Kind = eDrag::Arch;
		mPreview.RectUV = DRect(Vec2d::Min(mPress, current), Vec2d::Max(mPress, current));
		mPreview.Outlines.Clear();

		const DynArray<Vec2d> corners = BuildArchSegments(mPreview.RectUV);

		for (uint32 i = 0; i + scArchCornersPerSegment <= corners.Size; i += scArchCornersPerSegment) {
			DynArray<Vec2d> points;

			for (uint32 j = 0; j < scArchCornersPerSegment; j++) {
				points.Insert(corners[i + j]);
			}

			DynArray<Vec2f> polygon = ConvexHull2D(std::move(points));

			if (polygon.Size >= 3) {
				mPreview.Outlines.Insert(OutlinePreview { ObjectID::scNull, std::move(polygon) });
			}
		}

		break;
	}
	case eDrag::Rotate: {
		const Vec2d from_pivot = mCurrent - mRotatePivot;
		const double angle = std::atan2(from_pivot.Y, from_pivot.X);

		mPreview.Kind = eDrag::Rotate;
		mPreview.Angle = SnapAngle(WrapAngle(angle - mRotateStartAngle));
		mPreview.PivotUV = mRotatePivot;
		break;
	}
	default:
		break;
	}
}

void TopViewCanvas::ClearPreview() { mPreview = Preview(); }

void TopViewCanvas::HoldPreview()
{
	mbPreviewHeld = true;
	mHoldDeadline = NowMilliseconds() + scHoldMilliseconds;
}

void TopViewCanvas::CancelDrag()
{
	mDrag = eDrag::None;

	if (HasCapture()) {
		ReleaseMouse();
	}

	if (!mbPreviewHeld) {
		ClearPreview();
	}

	UpdateCursor();
	Refresh();
}

void TopViewCanvas::OnCaptureLost(wxMouseCaptureLostEvent&)
{
	if (mDrag != eDrag::None) {
		mDrag = eDrag::None;
		ClearPreview();
		UpdateCursor();
		Refresh();
	}
}

void TopViewCanvas::OnLeftUp(wxMouseEvent&)
{
	if (mDrag == eDrag::None) {
		return;
	}

	const eDrag drag = mDrag;
	mDrag = eDrag::None;

	if (HasCapture()) {
		ReleaseMouse();
	}

	switch (drag) {
	case eDrag::Marquee:
		FinishMarquee();
		break;
	case eDrag::Move:
		FinishMove();
		break;
	case eDrag::Face:
		FinishFace();
		break;
	case eDrag::Vertex:
		FinishVertex();
		break;
	case eDrag::Create:
		FinishCreate();
		break;
	case eDrag::Arch:
		FinishArch();
		break;
	case eDrag::Rotate:
		FinishRotate();
		break;
	default:
		break;
	}

	if (!mbPreviewHeld) {
		ClearPreview();
	}

	UpdateHover();
	UpdateCursor();
	UpdateStatus();
	Refresh();
}

void TopViewCanvas::FinishVertex()
{
	if (mPreview.Kind != eDrag::Vertex || mPreview.OffsetUV == Vec2d::sZero) {
		if (!mbDragMoved && mbVertexToggleOnRelease) {
			DynArray<TopViewVertexHandle> kept;

			for (const TopViewVertexHandle& handle : mSelectedVertices) {
				const bool is_grabbed = handle.ObjectId == mGrabbedVertex.ObjectId &&
										IsSameHandle(ToVec2d(handle.Position), ToVec2d(mGrabbedVertex.Position));

				if (!is_grabbed) {
					kept.Insert(handle);
				}
			}

			mSelectedVertices = std::move(kept);
		}

		ClearPreview();
		return;
	}

	DynArray<TopViewVertexHandle> handles = mSelectedVertices.Clone();
	const Vec3f offset = ToWorldVector(mPreview.OffsetUV, 0.0);
	const eViewPlane plane = mPlane;

	for (TopViewVertexHandle& handle : mSelectedVertices) {
		handle.Position += ToVec2f(mPreview.OffsetUV);
	}

	HoldPreview();
	SendCommand([plane, handles = std::move(handles), offset]
				{ gEditor->TopViewMoveVertices(plane, handles, offset); });
}

void TopViewCanvas::FinishMarquee()
{
	if (mTool == eTopViewTool::Vertex) {
		if (!mbDragMoved) {
			if (!mbAdditive) {
				mSelectedVertices.Clear();
			}

			return;
		}

		const DRect rect(Vec2d::Min(mPress, mCurrent), Vec2d::Max(mPress, mCurrent));

		if (!mbAdditive) {
			mSelectedVertices.Clear();
		}

		for (const TopViewBrush& brush : mState.Brushes) {
			if (!brush.bSelected || !brush.bSelectable) {
				continue;
			}

			for (const Vec2d& point : CollectHandles(Projection(brush))) {
				if (rect.Contains(point) && !IsVertexSelected(brush.ObjectId, point)) {
					mSelectedVertices.Insert(TopViewVertexHandle { brush.ObjectId, ToVec2f(point) });
				}
			}
		}

		return;
	}

	if (!mbDragMoved) {
		if (!mbAdditive && GetSelectedCount() > 0) {
			SelectBrushes({}, eTopViewSelectMode::Replace);
		}

		return;
	}

	const DRect rect(Vec2d::Min(mPress, mCurrent), Vec2d::Max(mPress, mCurrent));

	DynArray<ObjectID> inside;

	for (const TopViewBrush& brush : mState.Brushes) {
		const TopViewProjection& projection = Projection(brush);

		if (!brush.bSelectable || projection.Hull.IsEmpty()) {
			continue;
		}

		bool contained = true;

		for (const Vec2f& point : projection.Hull) {
			contained = contained && rect.Contains(ToVec2d(point));
		}

		if (contained) {
			inside.Insert(brush.ObjectId);
		}
	}

	eTopViewSelectMode mode = eTopViewSelectMode::Replace;

	if (mbAdditive) {
		mode = eTopViewSelectMode::Add;
	}

	SelectBrushes(std::move(inside), mode);
}

void TopViewCanvas::FinishMove()
{
	if (mbDragMoved) {
		if (mPreview.Kind != eDrag::Move || mPreview.OffsetUV == Vec2d::sZero) {
			return;
		}

		const Vec3f offset = ToWorldVector(mPreview.OffsetUV, 0.0);

		HoldPreview();
		SendCommand([offset] { gEditor->TopViewMove(offset); });
		return;
	}

	if (!mbHasPressedBrush || FindBrush(mPressedId) == nullptr) {
		return;
	}

	const ObjectID pressed_id = mPressedId;

	if (mbAdditive) {
		if (mbPressedWasSelected) {
			SelectBrush(pressed_id, eTopViewSelectMode::Toggle);
		}

		return;
	}

	if (!mbPressedWasSelected) {
		return;
	}

	const bool same_spot = std::hypot(mPressScreen.x - mLastClickScreen.x, mPressScreen.y - mLastClickScreen.y) <=
						   scClickCycleRadiusPixels;

	if (mPressedHitIds.Size > 1 && same_spot) {
		const ObjectID* found = std::find(mPressedHitIds.begin(), mPressedHitIds.end(), pressed_id);
		const uint32 next = (static_cast<uint32>(found - mPressedHitIds.begin()) + 1) % mPressedHitIds.Size;

		SelectBrush(mPressedHitIds[next], eTopViewSelectMode::Replace);
	}
	else if (GetSelectedCount() > 1) {
		SelectBrush(pressed_id, eTopViewSelectMode::Replace);
	}

	mLastClickScreen = mPressScreen;
}

void TopViewCanvas::FinishFace()
{
	if (mPreview.Kind != eDrag::Face || std::fabs(mPreview.Shift) < 1e-6) {
		ClearPreview();
		return;
	}

	const Vec3f normal = ToWorldVector(mPreview.NormalUV, 0.0);
	const float32 distance = static_cast<float32>(mPreview.Shift);

	HoldPreview();
	SendCommand([normal, distance] { gEditor->TopViewMoveFace(normal, distance); });
}

void TopViewCanvas::FinishCreate()
{
	if (mPreview.Kind != eDrag::Create) {
		return;
	}

	const Vec2d extent = mPreview.RectUV.GetExtent();

	if (extent.X < scMinCreateSize || extent.Y < scMinCreateSize) {
		ClearPreview();
		return;
	}

	const uint32 plane_index = static_cast<uint32>(mPlane);
	const double base = mCreateBase[plane_index];
	const double depth = std::max<double>(mCreateSize[plane_index], scMinCreateDepth);

	const Vec3f first = ToWorldVector(mPreview.RectUV.Min, base);
	const Vec3f second = ToWorldVector(mPreview.RectUV.Max, base + depth);

	const Vec3f min(std::min<float32>(first.X, second.X), std::min<float32>(first.Y, second.Y),
					std::min<float32>(first.Z, second.Z));
	const Vec3f max(std::max<float32>(first.X, second.X), std::max<float32>(first.Y, second.Y),
					std::max<float32>(first.Z, second.Z));

	HoldPreview();
	const bool drop_to_ground = mbDropToGround && (mPlane == eViewPlane::Top);

	SendCommand([min, max, drop_to_ground] { gEditor->TopViewCreate(min, max, drop_to_ground); });
}

DynArray<Vec2d> TopViewCanvas::BuildArchSegments(const DRect& rect) const
{
	DynArray<Vec2d> corners;

	const uint32 sides = mArch.Sides;
	const double arc = std::clamp<double>(mArch.ArcDegrees, 1.0, 360.0) * FX_PI / 180.0;
	const double start = mArch.StartDegrees * FX_PI / 180.0;

	DynArray<Vec2d> directions;

	double low_x = 0.0;
	double high_x = 0.0;
	double low_y = 0.0;
	double high_y = 0.0;

	for (uint32 i = 0; i <= sides; i++) {
		const double angle = start + arc * static_cast<double>(i) / static_cast<double>(sides);
		const Vec2d direction(std::cos(angle), std::sin(angle));

		low_x = std::min(low_x, direction.X);
		high_x = std::max(high_x, direction.X);
		low_y = std::min(low_y, direction.Y);
		high_y = std::max(high_y, direction.Y);

		directions.Insert(direction);
	}

	if (high_x - low_x < 1e-6 || high_y - low_y < 1e-6) {
		return corners;
	}

	const Vec2d extent = rect.GetExtent();
	const double radius_u = extent.X / (high_x - low_x);
	const double radius_v = extent.Y / (high_y - low_y);
	const Vec2d center(rect.Min.X - low_x * radius_u, rect.Min.Y - low_y * radius_v);

	const double wall = std::max<double>(mArch.WallWidth, 0.0);
	const double inner_u = std::max(radius_u - wall, 0.0);
	const double inner_v = std::max(radius_v - wall, 0.0);

	const auto outer = [&](const Vec2d& direction)
	{ return Vec2d(center.X + direction.X * radius_u, center.Y + direction.Y * radius_v); };
	const auto inner = [&](const Vec2d& direction)
	{ return Vec2d(center.X + direction.X * inner_u, center.Y + direction.Y * inner_v); };

	for (uint32 i = 0; i < sides; i++) {
		corners.Insert(outer(directions[i]));
		corners.Insert(outer(directions[i + 1]));
		corners.Insert(inner(directions[i + 1]));
		corners.Insert(inner(directions[i]));
	}

	return corners;
}

void TopViewCanvas::FinishArch()
{
	if (mPreview.Kind != eDrag::Arch) {
		return;
	}

	const Vec2d extent = mPreview.RectUV.GetExtent();

	if (extent.X < scMinCreateSize || extent.Y < scMinCreateSize) {
		ClearPreview();
		return;
	}

	const DynArray<Vec2d> corners = BuildArchSegments(mPreview.RectUV);

	if (corners.IsEmpty()) {
		ClearPreview();
		return;
	}

	const uint32 plane_index = static_cast<uint32>(mPlane);
	const double base = mCreateBase[plane_index];
	const double depth = std::max<double>(mCreateSize[plane_index], scMinCreateDepth);

	DynArray<Vec3f> points;

	for (uint32 i = 0; i < corners.Size; i += scArchCornersPerSegment) {
		for (uint32 j = 0; j < scArchCornersPerSegment; j++) {
			points.Insert(ToWorldVector(corners[i + j], base));
		}

		for (uint32 j = 0; j < scArchCornersPerSegment; j++) {
			points.Insert(ToWorldVector(corners[i + j], base + depth));
		}
	}

	HoldPreview();
	const bool drop_to_ground = mbDropToGround && (mPlane == eViewPlane::Top);

	SendCommand([points = std::move(points), drop_to_ground]() mutable
				{ gEditor->TopViewCreateArch(std::move(points), drop_to_ground); });
}

void TopViewCanvas::FinishRotate()
{
	if (mPreview.Kind != eDrag::Rotate || std::fabs(mPreview.Angle) < scMinRotateAngle) {
		ClearPreview();
		return;
	}

	const Vec3f axis = ToWorldVector(Vec2d::sZero, 1.0);
	const float32 angle = static_cast<float32>(Axes().RotateSign * mPreview.Angle);
	const Vec3f pivot = ToWorldVector(mPreview.PivotUV, 0.0);

	HoldPreview();
	SendCommand([axis, angle, pivot] { gEditor->TopViewRotate(axis, angle, pivot); });
}

void TopViewCanvas::OnWheel(wxMouseEvent& event)
{
	const double delta = std::max(event.GetWheelDelta(), 1);
	const double rotation = event.GetWheelRotation();
	const bool horizontal = (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL);
	const bool precise = platform::IsPreciseScrollEvent();

	if ((precise && !event.ControlDown()) || horizontal) {
		if (mDrag == eDrag::Pan) {
			return;
		}

		const double pixels = precise ? rotation : (rotation / delta) * scWheelPanPixelsPerUnit;

		Vec2d pan = Vec2d(0.0, pixels);

		if (horizontal) {
			pan = Vec2d(-pixels, 0.0);
		}

		PanByPixels(pan);
		mHover = ClientToWorld(event.GetPosition());
		UpdateStatus();
		return;
	}

	const double amount = precise ? rotation * scPreciseZoomPerPixel : (rotation / delta) * scWheelZoomPerUnit;
	const double exponent = std::clamp(amount, -scWheelZoomLimit, scWheelZoomLimit);

	ZoomAt(event.GetPosition(), std::exp(exponent));
	UpdateStatus();
}

void TopViewCanvas::OnZoomGesture(wxZoomGestureEvent& event)
{
	if (event.IsGestureStart()) {
		mPinchStartScale = mScale;
	}

	const double wanted = std::clamp(mPinchStartScale * event.GetZoomFactor(), scMinScale, scMaxScale);

	ZoomAt(event.GetPosition(), wanted / mScale);
	UpdateStatus();
}


/////////////////////////////////////
// Keyboard
/////////////////////////////////////

void TopViewCanvas::NudgeSelection(const Vec2d delta)
{
	if (GetSelectedCount() == 0) {
		return;
	}

	const Vec3f offset = ToWorldVector(delta, 0.0);

	SendCommand([offset] { gEditor->TopViewMove(offset); });
}

void TopViewCanvas::SetSnapBypass(bool bypass)
{
	if (bypass == mbSnapBypass) {
		return;
	}

	mbSnapBypass = bypass;

	if (mDrag != eDrag::None && mDrag != eDrag::Pan && mDrag != eDrag::Marquee) {
		UpdatePreview();
		Refresh();
	}
}

void TopViewCanvas::SetToolFromKey(eTopViewTool tool)
{
	if (mDrag == eDrag::None) {
		SetTool(tool);
	}
}

void TopViewCanvas::OnKeyUp(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_SPACE) {
		mbSpaceDown = false;
		UpdateCursor();
	}
	else if (event.GetKeyCode() == WXK_ALT) {
		mbAltDown = false;
		UpdateHover();
		UpdateCursor();
	}
	else if (event.GetKeyCode() == WXK_CONTROL) {
		SetSnapBypass(false);
	}

	event.Skip();
}

void TopViewCanvas::OnKeyDown(wxKeyEvent& event)
{
	const int key = event.GetKeyCode();
	const bool command = event.ControlDown();
	const bool shift = event.ShiftDown();

	if (key == WXK_SPACE) {
		mbSpaceDown = true;
		UpdateCursor();
		return;
	}

	if (key == WXK_ALT) {
		mbAltDown = true;
		UpdateHover();
		UpdateCursor();
		event.Skip();
		return;
	}

	if (key == WXK_CONTROL) {
		SetSnapBypass(true);
		event.Skip();
		return;
	}

	const double step = (mState.bSnapEnabled && mState.SnapStep > 0.0f) ? mState.SnapStep : 0.25;

	if (!command && mDrag == eDrag::None && (key == WXK_LEFT || key == WXK_RIGHT || key == WXK_UP || key == WXK_DOWN)) {
		const double distance = step * (event.AltDown() ? scNudgeFineFactor : (shift ? scNudgeCoarseFactor : 1.0));
		const double du = (key == WXK_RIGHT) ? distance : ((key == WXK_LEFT) ? -distance : 0.0);
		const double dv = (key == WXK_UP) ? distance : ((key == WXK_DOWN) ? -distance : 0.0);

		NudgeSelection(Vec2d(du, dv));
		return;
	}

	if (command) {
		if (key == 'Z') {
			SendCommand(shift ? std::function<void()>([] { gEditor->Redo(); })
							  : std::function<void()>([] { gEditor->Undo(); }));
			return;
		}

		if (key == 'D') {
			SendCommand([] { gEditor->TopViewDuplicate(); });
			return;
		}

		if (key == 'A' && mDrag == eDrag::None) {
			SelectAll();
			return;
		}

		if (key == 'I' && mDrag == eDrag::None) {
			InvertSelection();
			return;
		}

		event.Skip();
		return;
	}

	if (event.AltDown()) {
		event.Skip();
		return;
	}

	switch (key) {
	case '1':
		SetPlane(eViewPlane::Top);
		break;
	case '2':
		SetPlane(eViewPlane::Front);
		break;
	case '3':
		SetPlane(eViewPlane::Back);
		break;
	case '4':
		SetPlane(eViewPlane::Left);
		break;
	case '5':
		SetPlane(eViewPlane::Right);
		break;
	case 'T':
		SetToolFromKey(eTopViewTool::Select);
		break;
	case 'F':
		SetToolFromKey(eTopViewTool::Face);
		break;
	case 'V':
		SetToolFromKey(eTopViewTool::Vertex);
		break;
	case 'B':
		SetToolFromKey(eTopViewTool::Create);
		break;
	case 'A':
		SetToolFromKey(eTopViewTool::Arch);
		break;
	case 'R':
		SetToolFromKey(eTopViewTool::Rotate);
		break;
	case 'U':
		SendCommand([] { gEditor->TopViewToggleSnap(); });
		break;
	case 'H':
		if (mDrag != eDrag::None) {
			break;
		}

		if (shift) {
			SendCommand([] { gEditor->TopViewShowAll(); });
		}
		else {
			SendCommand([] { gEditor->TopViewHideSelection(); });
		}
		break;
	case '-':
	case WXK_NUMPAD_SUBTRACT:
		SendCommand([] { gEditor->TopViewAdjustSnap(-1); });
		break;
	case '=':
	case WXK_NUMPAD_ADD:
		SendCommand([] { gEditor->TopViewAdjustSnap(1); });
		break;
	case WXK_HOME:
		if (shift) {
			FitSelection();
		}
		else {
			FitAll();
		}
		break;
	case 'Z':
		FitSelection();
		break;
	case WXK_DELETE:
	case WXK_BACK:
		if (mDrag == eDrag::None) {
			SendCommand([] { gEditor->TopViewDelete(); });
		}
		break;
	case WXK_ESCAPE:
		if (mDrag != eDrag::None) {
			ClearPreview();
			CancelDrag();
		}
		else if (mSelectedVertices.IsNotEmpty()) {
			mSelectedVertices.Clear();
			Refresh();
		}
		else if (GetSelectedCount() > 0) {
			SelectBrushes({}, eTopViewSelectMode::Replace);
		}
		break;
	default:
		event.Skip();
		break;
	}
}


/////////////////////////////////////
// Feedback
/////////////////////////////////////

void TopViewCanvas::UpdateCursor()
{
	if (mDrag == eDrag::Pan || mbSpaceDown || mbAltDown) {
		SetCursor(wxCursor(wxCURSOR_SIZING));
		return;
	}

	switch (mTool) {
	case eTopViewTool::Face: {
		if (!mbHoverFace && mDrag != eDrag::Face) {
			SetCursor(wxNullCursor);
			break;
		}

		const TopViewFace& face = (mDrag == eDrag::Face) ? mGrabbedFace : mHoverFace;

		if (std::fabs(face.Normal.X) > 0.9) {
			SetCursor(wxCursor(wxCURSOR_SIZEWE));
		}
		else if (std::fabs(face.Normal.Y) > 0.9) {
			SetCursor(wxCursor(wxCURSOR_SIZENS));
		}
		else {
			SetCursor(wxCursor(wxCURSOR_SIZING));
		}

		break;
	}
	case eTopViewTool::Vertex:
		SetCursor((mbHoverVertex || mDrag == eDrag::Vertex) ? wxCursor(wxCURSOR_SIZING) : wxNullCursor);
		break;
	case eTopViewTool::Create:
	case eTopViewTool::Arch:
		SetCursor(wxCursor(wxCURSOR_CROSS));
		break;
	case eTopViewTool::Rotate:
		SetCursor(wxCursor(wxCURSOR_HAND));
		break;
	default:
		SetCursor(wxNullCursor);
		break;
	}
}

void TopViewCanvas::UpdateStatus()
{
	if (!mpOnStatus) {
		return;
	}

	const ViewPlaneAxes& axes = Axes();

	wxString text = wxString::Format("%s   |   ", axes.pName);

	if (mbHovering) {
		text += wxString::Format("%c %.2f   %c %.2f   |   ", scAxisNames[axes.U], mHover.X * axes.USign,
								 scAxisNames[axes.V], mHover.Y);
	}

	text += wxString::Format("%d brushes, %u selected   |   Snap %s %.2f   |   Zoom %.0f%%   |   %s",
							 static_cast<int>(mState.Brushes.Size), GetSelectedCount(),
							 mState.bSnapEnabled ? "on" : "off", mState.SnapStep, mScale / scDefaultScale * 100.0,
							 scToolHints[static_cast<uint32>(mTool)]);

	mpOnStatus(text);
}

} // namespace fx::editor

#endif

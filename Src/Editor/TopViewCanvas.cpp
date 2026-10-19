#include "TopViewCanvas.hpp"

#ifdef FX_IS_EDITOR

#include "EditorPlatform.hpp"
#include "EditorTheme.hpp"
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
constexpr std::chrono::seconds scNoticeDuration(6);
constexpr float32 scTextureRotateDegrees = 15.0f;
constexpr double scDragThresholdPixels = 4.0;
constexpr double scClickCycleRadiusPixels = 4.0;
constexpr double scMinCreateSize = 0.05;
constexpr double scMinCreateDepth = 0.05;
constexpr uint32 scArchCornersPerSegment = 4;
constexpr uint32 scMinArchSides = 3;
constexpr uint32 scMaxArchSides = 128;
constexpr double scMinArchWall = 0.01;
constexpr double scArchHandleRadius = 4.0;
constexpr double scArchCenterCross = 5.0;
constexpr double scArchKnobPixels = 16.0;
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
constexpr double scNudgeSnapTolerance = 1e-3;
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

constexpr double scMarkerPickPixels = 7.0;
constexpr double scMarkerHalfSize = 4.0;
constexpr double scMarkerArrowPixels = 34.0;
constexpr double scMarkerMinArrow = 0.35;
constexpr double scMarkerTipRadius = 4.0;
constexpr double scMinAimPlanar = 0.15;
constexpr double scMinClipLength = 1e-3;
constexpr double scClipHandleRadius = 3.5;

#ifdef __WXMAC__
#define FX_TOP_VIEW_FREE_KEY "Cmd"
#else
#define FX_TOP_VIEW_FREE_KEY "Ctrl"
#endif

constexpr const char* scToolHints[] = {
	"Drag to move, Shift adds, " FX_TOP_VIEW_FREE_KEY " ignores snap, Z frames selection",
	"Drag an edge to move its face, click an edge to pick that face for texturing, " FX_TOP_VIEW_FREE_KEY
	" ignores snap",
	"Drag corners, Shift adds, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag to create a brush, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag a box to lay out an arch, then shape it with the handles, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag to create a patch, drag the control points of the selected patches, Shift adds, [ ] "
	"subdivisions, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag a box to project a decal onto the surface behind it, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag around the pivot, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Drag a line across the selected brushes to split them, Esc deselects, " FX_TOP_VIEW_FREE_KEY " ignores snap",
	"Click a light to select it, drag to move, drag its handle to aim, double-click to add one",
};

static_assert(std::size(scToolHints) == static_cast<size_t>(eEditorTool::Count));

constexpr const char* scPickedFaceHint = "Alt+Left/Right Shift UVs, Alt+[ ] scale, Alt+, . rotate";

constexpr const char* scArchPendingHint = "Drag the handles to reshape, [ ] sides, arrows nudge, Enter or double-click "
										  "builds, Esc cancels, " FX_TOP_VIEW_FREE_KEY " ignores snap";

constexpr double scMetricSteps[] = { 0.1, 0.5, 1.0, 5.0, 10.0, 50.0, 100.0, 500.0, 1000.0, 5000.0, 10000.0 };

constexpr int scDimMarkerAlpha = 110;

constexpr uint32 scKindCount = 8;
constexpr uint32 scBucketCount = scKindCount * 2;

constexpr int scDimGeometryAlpha = 70;
constexpr int scDimVolumeAlpha = 170;
constexpr int scVolumeFillAlpha = 16;

wxColour GetKindColour(const EditorTheme& theme, eTopViewBrushKind kind)
{
	switch (kind) {
	case eTopViewBrushKind::ProbeVolume:
		return ToWxColour(theme.Brush.ProbeVolume);
	case eTopViewBrushKind::ReflectionProbe:
		return ToWxColour(theme.Brush.Reflection);
	case eTopViewBrushKind::Volume:
		return ToWxColour(theme.Brush.Volume);
	case eTopViewBrushKind::NvBrush:
		return ToWxColour(theme.Brush.NvBrush);
	case eTopViewBrushKind::DBound:
		return ToWxColour(theme.Brush.DBound);
	case eTopViewBrushKind::Decal:
		return ToWxColour(theme.Brush.Decal);
	case eTopViewBrushKind::Patch:
		return ToWxColour(theme.Brush.Patch);
	default:
		return ToWxColour(theme.Brush.Geometry);
	}
}

bool IsSolidKind(eTopViewBrushKind kind)
{
	return kind == eTopViewBrushKind::Geometry || kind == eTopViewBrushKind::Patch;
}

wxColour GetAxisColour(const EditorTheme& theme, uint32 axis)
{
	if (axis == 0) {
		return ToWxColour(theme.Grid.AxisX);
	}

	if (axis == 1) {
		return ToWxColour(theme.Grid.AxisY);
	}

	return ToWxColour(theme.Grid.AxisZ);
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

double GetArchArc(const ArchSettings& arch) { return std::clamp<double>(arch.ArcDegrees, 1.0, 360.0) * FX_PI / 180.0; }

Vec2d GetArchDirection(const ArchSettings& arch, double index)
{
	const double angle = arch.StartDegrees * FX_PI / 180.0 + GetArchArc(arch) * index / static_cast<double>(arch.Sides);

	return Vec2d(std::cos(angle), std::sin(angle));
}

Vec2d GetArchWallDirection(const ArchSettings& arch)
{
	const double middle = static_cast<double>(arch.Sides) * 0.5;

	return (GetArchDirection(arch, std::floor(middle)) + GetArchDirection(arch, std::ceil(middle))) * 0.5;
}

void GetArchDirectionBounds(const ArchSettings& arch, Vec2d& out_low, Vec2d& out_high)
{
	out_low = Vec2d::sZero;
	out_high = Vec2d::sZero;

	for (uint32 i = 0; i <= arch.Sides; i++) {
		const Vec2d direction = GetArchDirection(arch, static_cast<double>(i));

		out_low = Vec2d::Min(out_low, direction);
		out_high = Vec2d::Max(out_high, direction);
	}
}

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

DynArray<Vec2f> ClipPolygon(const DynArray<Vec2f>& polygon, const Vec2d normal, double offset)
{
	DynArray<Vec2f> clipped;

	for (uint32 i = 0; i < polygon.Size; i++) {
		const Vec2d current = ToVec2d(polygon[i]);
		const Vec2d previous = ToVec2d(polygon[(i + polygon.Size - 1) % polygon.Size]);

		const double current_side = normal.Dot(current) - offset;
		const double previous_side = normal.Dot(previous) - offset;

		const bool current_inside = current_side <= 0.0;
		const bool previous_inside = previous_side <= 0.0;

		if (current_inside != previous_inside) {
			const double t = previous_side / (previous_side - current_side);
			clipped.Insert(ToVec2f(previous + (current - previous) * t));
		}

		if (current_inside) {
			clipped.Insert(ToVec2f(current));
		}
	}

	return clipped;
}

wxColour WithAlpha(const wxColour& colour, int alpha)
{
	return wxColour(colour.Red(), colour.Green(), colour.Blue(), alpha);
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

			return PivotUV + Vec2d(Cos * rel_uv.X - Sin * rel_uv.Y, Sin * rel_uv.X + Cos * rel_uv.Y) + OffsetUV;
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

DynArray<Vec2d> TopViewCanvas::CollectVertexHandles(const TopViewBrush& brush) const
{
	if (brush.Kind == eTopViewBrushKind::Patch) {
		DynArray<Vec2d> handles;

		for (const Vec2f& control_point : Projection(brush).ControlPoints) {
			const Vec2d point = ToVec2d(control_point);
			bool is_merged = false;

			for (const Vec2d& handle : handles) {
				is_merged = is_merged || IsSameHandle(handle, point);
			}

			if (!is_merged) {
				handles.Insert(point);
			}
		}

		return handles;
	}

	if (mTool == eEditorTool::Curve) {
		return {};
	}

	return CollectHandles(Projection(brush));
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

Vec2d TopViewCanvas::ProjectPoint(const Vec3f& point) const
{
	const ViewPlaneAxes& axes = Axes();

	return Vec2d(GetAxis(point, axes.U) * axes.USign, GetAxis(point, axes.V));
}

double TopViewCanvas::DepthOf(const Vec3f& vector) const { return GetAxis(vector, Axes().W); }

void TopViewCanvas::ApplyState(const TopViewState& state)
{
	if (state.CommandSerial >= mSentCommands) {
		mbLocalSelection = false;
		mbLocalMarkerSelection = false;

		if (mbPreviewHeld) {
			mbPreviewHeld = false;
			ClearPreview();
		}
	}

	mState = state;
	mAvailableTools = state.AvailableTools;

	if (state.NoticeSerial != mNoticeSerial) {
		mNoticeSerial = state.NoticeSerial;
		mNotice = wxString::FromUTF8(state.Notice.CStr());
		mNoticeTime = std::chrono::steady_clock::now();
	}

	if (mbLocalSelection) {
		ApplyLocalSelection();
	}

	if (mbLocalMarkerSelection) {
		ApplyLocalMarkerSelection();
	}

	if (!mbPreviewHeld && mSelectedVertices.IsNotEmpty()) {
		DynArray<TopViewVertexHandle> kept;

		for (const TopViewVertexHandle& handle : mSelectedVertices) {
			const TopViewBrush* brush = FindBrush(handle.ObjectId);
			bool found = false;

			if (brush != nullptr && brush->bSelected) {
				for (const Vec2d& point : CollectVertexHandles(*brush)) {
					found = found || IsSameHandle(point, ToVec2d(handle.Position));
				}
			}

			if (found) {
				kept.Insert(handle);
			}
		}

		mSelectedVertices = std::move(kept);
	}

	if (mbHasPickedFace) {
		const TopViewBrush* brush = FindBrush(mPickedFaceBrushId);

		if (brush == nullptr || !brush->bSelected ||
			FindMatchingFace(Projection(*brush), ToVec2d(mPickedFaceNormal)) == nullptr) {
			mbHasPickedFace = false;
		}
	}

	SyncCurveFromSelection();

	TryInitialFit();
	UpdateHover();
	UpdateStatus();
	Refresh();
}

bool TopViewCanvas::IsToolAvailable(eEditorTool tool) const
{
	return tool == eEditorTool::Select || (mAvailableTools & (1u << static_cast<uint32>(tool))) != 0;
}

void TopViewCanvas::SetAvailableTools(uint32 available_tools) { mAvailableTools = available_tools; }

void TopViewCanvas::ApplyLocalSelection()
{
	for (TopViewBrush& brush : mState.Brushes) {
		brush.bSelected = std::find(mLocalSelection.begin(), mLocalSelection.end(), brush.ObjectId) !=
						  mLocalSelection.end();
	}
}

void TopViewCanvas::ApplyLocalMarkerSelection()
{
	for (TopViewMarker& marker : mState.Markers) {
		const bool same_kind = (marker.Kind == mLocalMarkerKind);
		const bool same_light = (marker.Kind == eTopViewMarkerKind::Spawn) || (marker.LightId == mLocalMarkerLightId);

		marker.bSelected = mbLocalMarkerSelected && same_kind && same_light;
	}
}

void TopViewCanvas::SetTool(eEditorTool tool)
{
	if (!IsToolAvailable(tool)) {
		return;
	}

	if (tool != mTool) {
		SendCommand([tool] { gEditor->SetTool(tool); });
	}

	ApplyTool(tool);
}

void TopViewCanvas::ShowTool(eEditorTool tool)
{
	if (tool != mTool) {
		ApplyTool(tool);
	}
	else if (mpOnToolChanged) {
		mpOnToolChanged(tool);
	}
}

void TopViewCanvas::Subtract(bool keep_cutters)
{
	SendCommand([keep_cutters] { gEditor->TopViewSubtract(keep_cutters); });
}

void TopViewCanvas::ApplyTool(eEditorTool tool)
{
	if (mDrag != eDrag::None) {
		CancelDrag();
	}

	mTool = tool;

	if (tool != eEditorTool::Vertex && tool != eEditorTool::Curve) {
		mSelectedVertices.Clear();
	}

	if (tool != eEditorTool::Arch) {
		mbArchPending = false;
		mArchHover = eArchHandle::None;
	}

	if (tool != eEditorTool::Face) {
		mbHasPickedFace = false;
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
		mbHasPickedFace = false;
		mbHoverVertex = false;
		mSelectedVertices.Clear();
		mbArchPending = false;
		mArchHover = eArchHandle::None;
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

	if (mbArchPending) {
		UpdateHover();
		Refresh();
	}
}

void TopViewCanvas::SetArchSides(int32 sides)
{
	mArch.Sides = static_cast<uint32>(
		std::clamp<int32>(sides, static_cast<int32>(scMinArchSides), static_cast<int32>(scMaxArchSides)));

	NotifyArchChanged();
	UpdateHover();
	Refresh();
}

void TopViewCanvas::NotifyArchChanged()
{
	if (mpOnArchChanged) {
		mpOnArchChanged(mArch);
	}

	UpdateStatus();
}

void TopViewCanvas::SetCurveSettings(const CurveSettings& settings)
{
	mCurve = settings;
	mCurve.SubdivisionsU = std::clamp(mCurve.SubdivisionsU, Patch::scMinSubdivisions, Patch::scMaxSubdivisions);
	mCurve.SubdivisionsV = std::clamp(mCurve.SubdivisionsV, Patch::scMinSubdivisions, Patch::scMaxSubdivisions);
	mCurve.Weight = std::max(mCurve.Weight, Patch::scMinWeight);
}

void TopViewCanvas::NotifyCurveChanged()
{
	if (mpOnCurveChanged) {
		mpOnCurveChanged(mCurve);
	}
}

void TopViewCanvas::RunPatchCommand(ePatchCommand type)
{
	PatchCommand command;
	command.Type = type;
	command.SubdivisionsU = mCurve.SubdivisionsU;
	command.SubdivisionsV = mCurve.SubdivisionsV;
	command.bSmooth = mCurve.bSmooth;
	command.Closure = mCurve.Closure;
	command.Thickness = mCurve.Thickness;
	command.Weight = mCurve.Weight;
	command.Texture = mCurve.Texture;

	DynArray<TopViewVertexHandle> handles = mSelectedVertices.Clone();
	const eViewPlane plane = mPlane;

	if (type != ePatchCommand::SetSubdivisions && type != ePatchCommand::SetSmooth &&
		type != ePatchCommand::MapTexture && type != ePatchCommand::SetClosure && type != ePatchCommand::SetWeight &&
		type != ePatchCommand::RoundWeights) {
		mSelectedVertices.Clear();
	}

	SendCommand([plane, command, handles = std::move(handles)]
				{ gEditor->TopViewPatchCommand(plane, command, handles); });

	Refresh();
}

void TopViewCanvas::RunTextureCommand(const TextureCommand& command)
{
	ObjectID face_object = ObjectID::scNull;
	Vec3f face_normal = Vec3f::sZero;

	if (mbHasPickedFace) {
		face_object = mPickedFaceBrushId;
		face_normal = ToWorldVector(ToVec2d(mPickedFaceNormal), 0.0);
	}

	SendCommand([face_object, face_normal, command]
				{ gEditor->TopViewEditTextures(face_object, face_normal, command); });

	Refresh();
}

bool TopViewCanvas::GetPickedFace(ObjectID& out_object, Vec3f& out_normal) const
{
	if (mTool != eEditorTool::Face || !mbHasPickedFace) {
		return false;
	}

	out_object = mPickedFaceBrushId;
	out_normal = ToWorldVector(ToVec2d(mPickedFaceNormal), 0.0);

	return true;
}

void TopViewCanvas::SyncWeightFromHandles()
{
	for (const TopViewVertexHandle& handle : mSelectedVertices) {
		const TopViewBrush* brush = FindBrush(handle.ObjectId);

		if (brush == nullptr || brush->Kind != eTopViewBrushKind::Patch) {
			continue;
		}

		const TopViewProjection& projection = Projection(*brush);
		const size_t count = std::min(projection.ControlPoints.Size, projection.ControlWeights.Size);

		for (size_t i = 0; i < count; i++) {
			if (!IsSameHandle(ToVec2d(projection.ControlPoints[i]), ToVec2d(handle.Position))) {
				continue;
			}

			if (projection.ControlWeights[i] != mCurve.Weight) {
				mCurve.Weight = projection.ControlWeights[i];
				NotifyCurveChanged();
			}

			return;
		}
	}
}

void TopViewCanvas::SyncCurveFromSelection()
{
	SyncWeightFromHandles();

	for (const TopViewBrush& brush : mState.Brushes) {
		if (!brush.bSelected || brush.Kind != eTopViewBrushKind::Patch) {
			continue;
		}

		float32 thickness = mCurve.Thickness;

		if (brush.PatchClosure == ePatchClosure::Thicken) {
			thickness = brush.PatchThickness;
		}

		if (brush.PatchSubdivisionsU != mCurve.SubdivisionsU || brush.PatchSubdivisionsV != mCurve.SubdivisionsV ||
			brush.bPatchSmooth != mCurve.bSmooth || brush.PatchClosure != mCurve.Closure ||
			thickness != mCurve.Thickness) {
			mCurve.SubdivisionsU = brush.PatchSubdivisionsU;
			mCurve.SubdivisionsV = brush.PatchSubdivisionsV;
			mCurve.bSmooth = brush.bPatchSmooth;
			mCurve.Closure = brush.PatchClosure;
			mCurve.Thickness = thickness;
			NotifyCurveChanged();
		}

		return;
	}
}

bool TopViewCanvas::UsesHandles() const { return mTool == eEditorTool::Vertex || mTool == eEditorTool::Curve; }

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
	const EditorTheme& theme = GetEditorTheme();

	if (mbPreviewHeld && NowMilliseconds() > mHoldDeadline) {
		mbPreviewHeld = false;
		mbLocalSelection = false;
		ClearPreview();
	}

	gc.SetAntialiasMode(wxANTIALIAS_DEFAULT);
	gc.SetBrush(wxBrush(ToWxColour(theme.Grid.Background)));
	gc.SetPen(*wxTRANSPARENT_PEN);
	gc.DrawRectangle(0, 0, size.x, size.y);

	DrawGrid(gc, size);
	DrawBrushes(gc, size);
	DrawMarkers(gc);
	DrawOverlays(gc, size);
	DrawPlayer(gc);
}

void TopViewCanvas::DrawGrid(wxGraphicsContext& gc, const wxSize& size)
{
	const EditorTheme& theme = GetEditorTheme();

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
			draw_lines(snap_step, ToWxColour(theme.Grid.Snap), 1);
		}
	}

	size_t minor_index = 0;

	while (minor_index + 2 < std::size(scMetricSteps) && scMetricSteps[minor_index] * mScale < scMinorGridPixels) {
		minor_index++;
	}

	const double minor = scMetricSteps[minor_index];
	const double major = scMetricSteps[minor_index + 1];

	draw_lines(minor, ToWxColour(theme.Grid.Minor), 1);
	draw_lines(major, ToWxColour(theme.Grid.Major), 1);

	const ViewPlaneAxes& axes = Axes();

	const Vec2d origin = ToScreenCoords(Vec2d::sZero);
	const double origin_x = std::floor(origin.X) + 0.5;
	const double origin_y = std::floor(origin.Y) + 0.5;

	gc.SetPen(wxPen(GetAxisColour(theme, axes.U), 1));
	gc.StrokeLine(0.0, origin_y, size.x, origin_y);

	gc.SetPen(wxPen(GetAxisColour(theme, axes.V), 1));
	gc.StrokeLine(origin_x, 0.0, origin_x, size.y);

	wxFont font = wxFont(wxFontInfo(9));
	gc.SetFont(font, ToWxColour(theme.Grid.Label));

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

	gc.SetFont(wxFont(wxFontInfo(11).Bold()), ToWxColour(theme.Grid.ViewLabel));
	gc.GetTextExtent(view_label, &label_width, &label_height);
	gc.DrawText(view_label, size.x - label_width - 10.0, size.y - label_height - 8.0);
}

void TopViewCanvas::DrawBrushes(wxGraphicsContext& gc, const wxSize& size)
{
	const EditorTheme& theme = GetEditorTheme();

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
		transform.OffsetUV = mPreview.OffsetUV;
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

		if (!brush.bSelected && !IsSolidKind(brush.Kind) && projection.Hull.Size >= 3) {
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
		const wxColour colour = GetKindColour(theme, static_cast<eTopViewBrushKind>(kind));

		gc.SetBrush(wxBrush(wxColour(colour.Red(), colour.Green(), colour.Blue(), scVolumeFillAlpha)));
		gc.FillPath(volume_fills[kind], wxWINDING_RULE);
	}

	for (uint32 kind = 0; kind < scKindCount; kind++) {
		const wxColour colour = GetKindColour(theme, static_cast<eTopViewBrushKind>(kind));
		const bool is_geometry = IsSolidKind(static_cast<eTopViewBrushKind>(kind));

		gc.SetPen(wxPen(colour, 1));
		gc.StrokePath(paths[kind * 2]);

		gc.SetPen(wxPen(
			wxColour(colour.Red(), colour.Green(), colour.Blue(), is_geometry ? scDimGeometryAlpha : scDimVolumeAlpha),
			1, is_geometry ? wxPENSTYLE_SOLID : wxPENSTYLE_SHORT_DASH));
		gc.StrokePath(paths[kind * 2 + 1]);
	}

	gc.SetPen(wxPen(WithAlpha(ToWxColour(theme.Tool.Selected), 80), 1));
	gc.StrokePath(ghost_path);

	gc.SetBrush(wxBrush(ToWxColour(theme.Tool.SelectedFill)));
	gc.SetPen(*wxTRANSPARENT_PEN);
	gc.FillPath(selected_fill, wxWINDING_RULE);

	gc.SetPen(wxPen(ToWxColour(theme.Tool.Selected), 2));
	gc.StrokePath(selected_path);
}

void TopViewCanvas::DrawOverlays(wxGraphicsContext& gc, const wxSize& size)
{
	const EditorTheme& theme = GetEditorTheme();

	wxFont font = wxFont(wxFontInfo(10));

	const auto to_screen_rect = [this](const DRect& rect)
	{
		const Vec2d top_left = ToScreenCoords(Vec2d(rect.Min.X, rect.Max.Y));
		const Vec2d bottom_right = ToScreenCoords(Vec2d(rect.Max.X, rect.Min.Y));

		return DRect(top_left, bottom_right);
	};

	if (UsesHandles()) {
		DrawPatchLattices(gc);

		const bool live = (mDrag == eDrag::Vertex) && (mPreview.Kind == eDrag::Vertex);

		for (const TopViewBrush& brush : mState.Brushes) {
			if (!brush.bSelected) {
				continue;
			}

			for (const Vec2d& point : CollectVertexHandles(brush)) {
				const bool selected = IsVertexSelected(brush.ObjectId, point);
				const bool hovered = mbHoverVertex && mHoverVertex.ObjectId == brush.ObjectId &&
									 IsSameHandle(point, ToVec2d(mHoverVertex.Position));

				Vec2d position = point;

				if (live && selected) {
					position += mPreview.OffsetUV;
				}

				const Vec2d screen = ToScreenCoords(position);
				const double half = hovered ? scHandleHalfSize + 1.5 : scHandleHalfSize;

				gc.SetBrush(wxBrush(selected ? ToWxColour(theme.Tool.Vertex) : ToWxColour(theme.Tool.Handle)));
				gc.SetPen(wxPen(hovered ? ToWxColour(theme.Tool.Vertex) : ToWxColour(theme.Tool.HandleOutline),
								hovered ? 2 : 1));
				gc.DrawRectangle(screen.X - half, screen.Y - half, half * 2.0, half * 2.0);
			}
		}

		if (live && mPreview.bSnappedToPatch) {
			const Vec2d screen = ToScreenCoords(mPreview.PatchSnapUV);
			const double radius = scHandleHalfSize + 4.0;

			gc.SetBrush(*wxTRANSPARENT_BRUSH);
			gc.SetPen(wxPen(ToWxColour(theme.Tool.Vertex), 2));
			gc.DrawEllipse(screen.X - radius, screen.Y - radius, radius * 2.0, radius * 2.0);
		}
	}

	if (mTool == eEditorTool::Face && mbHasPickedFace && mPreview.Kind == eDrag::None) {
		const TopViewBrush* brush = FindBrush(mPickedFaceBrushId);
		const TopViewFace* face = nullptr;

		if (brush != nullptr) {
			face = FindMatchingFace(Projection(*brush), ToVec2d(mPickedFaceNormal));
		}

		if (face != nullptr) {
			const Vec2d a = ToScreenCoords(ToVec2d(face->A));
			const Vec2d b = ToScreenCoords(ToVec2d(face->B));

			gc.SetPen(wxPen(ToWxColour(theme.Tool.Face), 5));
			gc.StrokeLine(a.X, a.Y, b.X, b.Y);
		}
	}

	if (mbHoverFace && mPreview.Kind == eDrag::None) {
		gc.SetPen(wxPen(ToWxColour(theme.Tool.Face), 3));
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

		gc.SetPen(wxPen(ToWxColour(theme.Tool.Face), 3));
		gc.StrokeLine(a.X, a.Y, b.X, b.Y);

		gc.SetFont(font, ToWxColour(theme.Tool.Face));
		gc.DrawText(wxString::Format("%+.2f", shift), middle.X + 8.0, middle.Y + 6.0);
		return;
	}

	if (mPreview.Kind == eDrag::Clip) {
		wxGraphicsPath pieces[2] = { gc.CreatePath(), gc.CreatePath() };

		for (uint32 i = 0; i < mPreview.Outlines.Size; i++) {
			const DynArray<Vec2f>& polygon = mPreview.Outlines[i].Polygon;
			wxGraphicsPath& path = pieces[i % 2];

			for (uint32 j = 0; j < polygon.Size; j++) {
				const Vec2d screen = ToScreenCoords(ToVec2d(polygon[j]));

				if (j == 0) {
					path.MoveToPoint(screen.X, screen.Y);
				}
				else {
					path.AddLineToPoint(screen.X, screen.Y);
				}
			}

			path.CloseSubpath();
		}

		gc.SetPen(*wxTRANSPARENT_PEN);

		for (uint32 i = 0; i < 2; i++) {
			gc.SetBrush(wxBrush(ToWxColour(i == 0 ? theme.Tool.ClipFrontFill : theme.Tool.ClipBackFill)));
			gc.FillPath(pieces[i], wxWINDING_RULE);
		}

		const Vec2d a = ToScreenCoords(mPreview.ClipA);
		const Vec2d b = ToScreenCoords(mPreview.ClipB);
		const Vec2d along = b - a;
		const double length = along.Length();

		if (length > 0.0) {
			const Vec2d unit = along / length;
			const double reach = static_cast<double>(size.x + size.y);

			gc.SetPen(wxPen(WithAlpha(ToWxColour(theme.Tool.Clip), 120), 1, wxPENSTYLE_SHORT_DASH));
			gc.StrokeLine(a.X - unit.X * reach, a.Y - unit.Y * reach, b.X + unit.X * reach, b.Y + unit.Y * reach);
		}

		gc.SetPen(wxPen(ToWxColour(theme.Tool.Clip), 2));
		gc.StrokeLine(a.X, a.Y, b.X, b.Y);

		gc.SetBrush(wxBrush(ToWxColour(theme.Tool.Clip)));
		gc.SetPen(*wxTRANSPARENT_PEN);
		gc.DrawEllipse(a.X - scClipHandleRadius, a.Y - scClipHandleRadius, scClipHandleRadius * 2.0,
					   scClipHandleRadius * 2.0);
		gc.DrawEllipse(b.X - scClipHandleRadius, b.Y - scClipHandleRadius, scClipHandleRadius * 2.0,
					   scClipHandleRadius * 2.0);

		const Vec2d delta = mPreview.ClipB - mPreview.ClipA;

		gc.SetFont(font, ToWxColour(theme.Tool.Clip));
		gc.DrawText(wxString::Format("%.2f", delta.Length()), b.X + 8.0, b.Y + 6.0);
		return;
	}

	if (mDrag == eDrag::Marquee && mbDragMoved) {
		const DRect screen = to_screen_rect(DRect(Vec2d::Min(mPress, mCurrent), Vec2d::Max(mPress, mCurrent)));
		const Vec2d screen_extent = screen.GetExtent();

		gc.SetBrush(wxBrush(ToWxColour(theme.Tool.MarqueeFill)));
		gc.SetPen(wxPen(ToWxColour(theme.Tool.Marquee), 1));
		gc.DrawRectangle(screen.Min.X, screen.Min.Y, screen_extent.X, screen_extent.Y);
		return;
	}

	if (mPreview.Kind == eDrag::Create) {
		const DRect screen = to_screen_rect(mPreview.RectUV);
		const Vec2d screen_extent = screen.GetExtent();
		const Vec2d extent = mPreview.RectUV.GetExtent();

		gc.SetBrush(wxBrush(ToWxColour(theme.Tool.CreateFill)));
		gc.SetPen(wxPen(ToWxColour(theme.Tool.Create), 2));
		gc.DrawRectangle(screen.Min.X, screen.Min.Y, screen_extent.X, screen_extent.Y);

		gc.SetFont(font, ToWxColour(theme.Tool.Create));
		gc.DrawText(wxString::Format("%.2f x %.2f", extent.X, extent.Y), screen.Min.X + 4.0, screen.Max.Y + 4.0);
		return;
	}

	if (mbArchPending) {
		DrawArch(gc, mArchRect, mDrag != eDrag::Arch);
		return;
	}

	if (mPreview.Kind == eDrag::Arch) {
		DrawArch(gc, mPreview.RectUV, false);
		return;
	}

	if (mPreview.Kind == eDrag::Rotate) {
		const Vec2d pivot = ToScreenCoords(mPreview.PivotUV);
		const double pivot_x = pivot.X;
		const double pivot_y = pivot.Y;

		gc.SetPen(wxPen(ToWxColour(theme.Tool.Rotate), 1));
		gc.StrokeLine(pivot_x - 7.0, pivot_y, pivot_x + 7.0, pivot_y);
		gc.StrokeLine(pivot_x, pivot_y - 7.0, pivot_x, pivot_y + 7.0);

		if (mDrag == eDrag::Rotate) {
			const Vec2d current = ToScreenCoords(mCurrent);
			gc.StrokeLine(pivot_x, pivot_y, current.X, current.Y);
		}

		gc.SetFont(font, ToWxColour(theme.Tool.Rotate));
		gc.DrawText(wxString::Format("%.1f deg", mPreview.Angle * 180.0 / FX_PI), pivot_x + 10.0, pivot_y + 8.0);
		return;
	}

	if (mPreview.Kind == eDrag::Move) {
		const ViewBounds bounds = mDragBounds;

		if (bounds.bValid) {
			const Vec2d anchor = ToScreenCoords(bounds.Rect.Min + mPreview.OffsetUV);

			gc.SetFont(font, ToWxColour(theme.Tool.Selected));
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

			gc.SetFont(font, ToWxColour(theme.Tool.Selected));
			gc.GetTextExtent(label, &label_width, &label_height);

			const double x = std::clamp(
				anchor.X, scDimensionLabelMargin,
				std::max(scDimensionLabelMargin, size.x - label_width - scDimensionLabelMargin));
			const double y = std::clamp(anchor.Y + 4.0, scDimensionLabelTop,
										std::max(scDimensionLabelTop, size.y - label_height - scDimensionLabelMargin));

			gc.DrawText(label, x, y);
		}
	}
}

Vec2d TopViewCanvas::GetMarkerTip(const TopViewMarker& marker, const Vec2d direction_uv) const
{
	const double planar = direction_uv.Length();

	Vec2d unit(1.0, 0.0);

	if (planar > 1e-6) {
		unit = direction_uv / planar;
	}

	const double length = std::max(planar, scMarkerMinArrow) * scMarkerArrowPixels / mScale;

	return ProjectPoint(marker.AimOrigin) + unit * length;
}

void TopViewCanvas::DrawMarkers(wxGraphicsContext& gc)
{
	const EditorTheme& theme = GetEditorTheme();

	for (const TopViewMarker& marker : mState.Markers) {
		Vec2d offset = Vec2d::sZero;
		Vec2d direction_uv = ProjectPoint(marker.Direction);

		if (marker.bSelected && mPreview.Kind == eDrag::MarkerMove) {
			offset = mPreview.OffsetUV;
		}

		if (marker.bSelected && mPreview.Kind == eDrag::MarkerAim) {
			direction_uv = mPreview.AimUV;
		}

		wxColour colour = ToWxColour(theme.Marker.Light);
		wxColour outline = ToWxColour(theme.Tool.Handle);
		int fill_alpha = 24;
		int width = 1;

		if (marker.Kind == eTopViewMarkerKind::Spawn) {
			colour = ToWxColour(theme.Marker.SpawnIdle);
		}

		if (marker.bSelected) {
			colour = ToWxColour(theme.Marker.LightSelected);
			outline = ToWxColour(theme.Marker.SelectedOutline);
			fill_alpha = 60;
			width = 2;
		}

		if (marker.bSelected && marker.Kind == eTopViewMarkerKind::Spawn) {
			colour = ToWxColour(theme.Marker.Spawn);
		}

		if (!marker.bSelectable) {
			colour = WithAlpha(colour, scDimMarkerAlpha);
		}

		const Vec2d origin = ToScreenCoords(ProjectPoint(marker.AimOrigin) + offset);
		const Vec2d tip = ToScreenCoords(GetMarkerTip(marker, direction_uv) + offset);

		if (marker.Kind == eTopViewMarkerKind::Spawn) {
			const Vec2d corner_a = ToScreenCoords(ProjectPoint(marker.BoundsMin) + offset);
			const Vec2d corner_b = ToScreenCoords(ProjectPoint(marker.BoundsMax) + offset);
			const Vec2d low = Vec2d::Min(corner_a, corner_b);
			const Vec2d extent = Vec2d::Max(corner_a, corner_b) - low;

			gc.SetBrush(wxBrush(WithAlpha(colour, fill_alpha)));
			gc.SetPen(wxPen(colour, width));
			gc.DrawRectangle(low.X, low.Y, std::max(extent.X, 2.0), std::max(extent.Y, 2.0));
		}
		else {
			gc.SetBrush(wxBrush(colour));
			gc.SetPen(wxPen(outline, 1));
			gc.DrawRectangle(origin.X - scMarkerHalfSize, origin.Y - scMarkerHalfSize, scMarkerHalfSize * 2.0,
							 scMarkerHalfSize * 2.0);
		}

		gc.SetPen(wxPen(colour, width));
		gc.StrokeLine(origin.X, origin.Y, tip.X, tip.Y);

		if (ProjectPoint(marker.Direction).Length() < scMinAimPlanar && mPreview.Kind != eDrag::MarkerAim) {
			gc.SetBrush(*wxTRANSPARENT_BRUSH);
			gc.DrawEllipse(origin.X - scMarkerHalfSize * 2.0, origin.Y - scMarkerHalfSize * 2.0, scMarkerHalfSize * 4.0,
						   scMarkerHalfSize * 4.0);
		}

		if (marker.bSelected) {
			gc.SetBrush(wxBrush(ToWxColour(theme.Tool.Handle)));
			gc.SetPen(wxPen(colour, 2));
			gc.DrawEllipse(tip.X - scMarkerTipRadius, tip.Y - scMarkerTipRadius, scMarkerTipRadius * 2.0,
						   scMarkerTipRadius * 2.0);
		}
	}
}

void TopViewCanvas::DrawPlayer(wxGraphicsContext& gc)
{
	const EditorTheme& theme = GetEditorTheme();

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
		gc.SetBrush(wxBrush(WithAlpha(ToWxColour(theme.Marker.Player), 90)));
		gc.SetPen(wxPen(ToWxColour(theme.Marker.Player), 2));
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

	gc.SetBrush(wxBrush(WithAlpha(ToWxColour(theme.Marker.Player), 90)));
	gc.SetPen(wxPen(ToWxColour(theme.Marker.Player), 2));
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

		for (const Vec2d& point : CollectVertexHandles(brush)) {
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

bool TopViewCanvas::FindPatchSnapPoint(const Vec2d uv, Vec2d& out_point) const
{
	if (mbSnapBypass) {
		return false;
	}

	const double tolerance = scVertexPickPixels / mScale;

	double best = tolerance;
	bool found = false;

	for (const TopViewBrush& brush : mState.Brushes) {
		if (brush.Kind != eTopViewBrushKind::Patch) {
			continue;
		}

		const TopViewProjection& projection = Projection(brush);
		bool is_moving = false;

		if (brush.bSelected) {
			for (const Vec2f& control_point : projection.ControlPoints) {
				is_moving = is_moving || IsVertexSelected(brush.ObjectId, ToVec2d(control_point));
			}
		}

		if (is_moving) {
			continue;
		}

		for (const TopViewEdge& edge : projection.Edges) {
			const Vec2d ends[2] = { ToVec2d(edge.A), ToVec2d(edge.B) };

			for (const Vec2d& point : ends) {
				const double distance = (point - uv).Length();

				if (distance <= best) {
					best = distance;
					found = true;
					out_point = point;
				}
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
	const eArchHandle previous_arch = mArchHover;

	mbHoverFace = false;
	mbHoverVertex = false;
	mArchHover = eArchHandle::None;

	if (mDrag == eDrag::None && mbHovering && !mbSpaceDown && !mbAltDown) {
		if (mTool == eEditorTool::Face) {
			int face_index = -1;
			const int brush_index = PickFaceAt(mHover, face_index);

			if (brush_index >= 0) {
				mbHoverFace = true;
				mHoverFace = Projection(mState.Brushes[brush_index]).Faces[face_index];
			}
		}
		else if (UsesHandles()) {
			mbHoverVertex = PickVertexAt(mHover, mHoverVertex);
		}
		else if (mTool == eEditorTool::Arch) {
			mArchHover = PickArchHandle(mHover);
		}
	}

	const bool vertex_changed = (mbHoverVertex != had_vertex) ||
								(mbHoverVertex && (mHoverVertex.ObjectId != previous_vertex.ObjectId ||
												   !(mHoverVertex.Position == previous_vertex.Position)));

	if (mbHoverFace != had_face || (mbHoverFace && !(mHoverFace == previous_face)) || vertex_changed ||
		mArchHover != previous_arch) {
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

Vec2d TopViewCanvas::GetRotateSnapOffset(const Vec2d pivot, double angle) const
{
	const double cos_angle = std::cos(angle);
	const double sin_angle = std::sin(angle);

	bool valid = false;
	Vec2d min = Vec2d::sZero;

	for (const TopViewBrush& brush : mState.Brushes) {
		if (!brush.bSelected) {
			continue;
		}

		for (const Vec2f& point : Projection(brush).Hull) {
			const Vec2d relative = Vec2d(point) - pivot;
			const Vec2d rotated = pivot + Vec2d(cos_angle * relative.X - sin_angle * relative.Y,
												sin_angle * relative.X + cos_angle * relative.Y);

			if (valid) {
				min = Vec2d::Min(min, rotated);
			}
			else {
				min = rotated;
				valid = true;
			}
		}
	}

	if (!valid) {
		return Vec2d::sZero;
	}

	return Snap(min) - min;
}

Vec2d TopViewCanvas::SnapNudge(const Vec2d origin, const Vec2d delta) const
{
	if (!mState.bSnapEnabled || mState.SnapStep <= 0.0f) {
		return delta;
	}

	const double step = mState.SnapStep;

	const auto snap_axis = [step](double from, double distance)
	{
		if (distance == 0.0) {
			return 0.0;
		}

		const double target = (from + distance) / step;

		if (distance > 0.0) {
			return std::floor(target + scNudgeSnapTolerance) * step - from;
		}

		return std::ceil(target - scNudgeSnapTolerance) * step - from;
	};

	return Vec2d(snap_axis(origin.X, delta.X), snap_axis(origin.Y, delta.Y));
}

uint32 TopViewCanvas::GetSelectedCount() const
{
	uint32 count = 0;

	for (const TopViewBrush& brush : mState.Brushes) {
		count += brush.bSelected ? 1 : 0;
	}

	return count;
}

int32 TopViewCanvas::PickMarkerAt(const Vec2d uv, bool& out_tip) const
{
	const double tolerance = scMarkerPickPixels / mScale;

	out_tip = false;

	for (uint32 i = 0; i < mState.Markers.Size; i++) {
		const TopViewMarker& marker = mState.Markers[i];

		if (marker.bSelected && marker.bSelectable &&
			(GetMarkerTip(marker, ProjectPoint(marker.Direction)) - uv).Length() <= tolerance) {
			out_tip = true;
			return static_cast<int32>(i);
		}
	}

	int32 best = -1;
	double best_distance = tolerance;

	for (uint32 i = 0; i < mState.Markers.Size; i++) {
		const TopViewMarker& marker = mState.Markers[i];

		if (!marker.bSelectable) {
			continue;
		}

		double distance = (ProjectPoint(marker.AimOrigin) - uv).Length();

		if (marker.Kind == eTopViewMarkerKind::Spawn) {
			const Vec2d corner_a = ProjectPoint(marker.BoundsMin);
			const Vec2d corner_b = ProjectPoint(marker.BoundsMax);
			const DRect rect(Vec2d::Min(corner_a, corner_b) - Vec2d(tolerance),
							 Vec2d::Max(corner_a, corner_b) + Vec2d(tolerance));

			if (rect.Contains(uv)) {
				distance = 0.0;
			}
		}

		if (distance < best_distance || (distance <= best_distance && marker.bSelected)) {
			best = static_cast<int32>(i);
			best_distance = distance;
		}
	}

	return best;
}

int32 TopViewCanvas::FindMarker(eTopViewMarkerKind kind, uint32 light_id) const
{
	for (uint32 i = 0; i < mState.Markers.Size; i++) {
		const TopViewMarker& marker = mState.Markers[i];

		if (marker.Kind == kind && (kind == eTopViewMarkerKind::Spawn || marker.LightId == light_id)) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

int32 TopViewCanvas::FindSelectedMarker() const
{
	for (uint32 i = 0; i < mState.Markers.Size; i++) {
		if (mState.Markers[i].bSelected && mState.Markers[i].bSelectable) {
			return static_cast<int32>(i);
		}
	}

	return -1;
}

void TopViewCanvas::SelectMarker(int32 index)
{
	const eTopViewMarkerKind kind = mState.Markers[index].Kind;
	const uint32 light_id = mState.Markers[index].LightId;

	for (uint32 i = 0; i < mState.Markers.Size; i++) {
		mState.Markers[i].bSelected = (static_cast<int32>(i) == index);
	}

	mbLocalMarkerSelection = true;
	mbLocalMarkerSelected = true;
	mLocalMarkerKind = kind;
	mLocalMarkerLightId = light_id;

	if (kind == eTopViewMarkerKind::Spawn) {
		for (TopViewBrush& brush : mState.Brushes) {
			brush.bSelected = false;
		}

		mLocalSelection.Clear();
		mbLocalSelection = true;

		SendCommand([] { gEditor->TopViewSelectSpawn(); });
	}
	else {
		SendCommand([light_id] { gEditor->TopViewSelectLight(light_id); });
	}

	UpdateStatus();
	Refresh();
}

void TopViewCanvas::DeselectMarkers()
{
	const int32 selected = FindSelectedMarker();

	if (selected < 0) {
		return;
	}

	if (mState.Markers[selected].Kind == eTopViewMarkerKind::Spawn) {
		SelectBrushes({}, eTopViewSelectMode::Replace);
		return;
	}

	for (TopViewMarker& marker : mState.Markers) {
		marker.bSelected = false;
	}

	mbLocalMarkerSelection = true;
	mbLocalMarkerSelected = false;

	SendCommand([] { gEditor->TopViewSelectLight(LightID::scNull.ID); });

	Refresh();
}

void TopViewCanvas::SelectBrush(ObjectID object_id, eTopViewSelectMode mode)
{
	DynArray<ObjectID> object_ids;
	object_ids.Insert(object_id);

	SelectBrushes(std::move(object_ids), mode);
}

void TopViewCanvas::SelectBrushes(DynArray<ObjectID> object_ids, eTopViewSelectMode mode)
{
	if (mode == eTopViewSelectMode::Replace || object_ids.IsNotEmpty()) {
		for (TopViewMarker& marker : mState.Markers) {
			if (marker.Kind == eTopViewMarkerKind::Spawn && marker.bSelected) {
				marker.bSelected = false;
				mbLocalMarkerSelection = true;
				mbLocalMarkerSelected = false;
			}
		}
	}

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

	if (mbSpaceDown || mState.bSimulating) {
		BeginPan(position);
		return;
	}

	if (event.AltDown()) {
		mDrag = eDrag::Marquee;
	}
	else {
		BeginTool(event);
	}

	if (mDrag != eDrag::None && !HasCapture()) {
		CaptureMouse();
	}

	UpdateCursor();
	Refresh();
}

void TopViewCanvas::BeginTool(const wxMouseEvent& event)
{
	switch (mTool) {
	case eEditorTool::Face:
		BeginFace();
		break;
	case eEditorTool::Vertex:
		BeginVertex();
		break;
	case eEditorTool::Create:
	case eEditorTool::Decal:
		BeginCreate();
		break;
	case eEditorTool::Arch:
		BeginArch(event.ButtonDClick());
		break;
	case eEditorTool::Curve:
		BeginCurve();
		break;
	case eEditorTool::Rotate:
		BeginRotate();
		break;
	case eEditorTool::Clip:
		BeginClip();
		break;
	case eEditorTool::Light:
		BeginLight(event.ButtonDClick());
		break;
	default:
		BeginSelect();
		break;
	}
}

bool TopViewCanvas::BeginMarker()
{
	bool tip = false;
	const int32 index = PickMarkerAt(mPress, tip);

	if (index < 0) {
		return false;
	}

	const TopViewMarker& marker = mState.Markers[index];

	mGrabbedMarkerKind = marker.Kind;
	mGrabbedMarkerLightId = marker.LightId;

	if (tip && marker.bSelected) {
		mDrag = eDrag::MarkerAim;
		return true;
	}

	if (!marker.bSelected) {
		SelectMarker(index);
	}

	mDrag = eDrag::MarkerMove;

	return true;
}

void TopViewCanvas::BeginLight(bool double_click)
{
	if (BeginMarker()) {
		return;
	}

	if (!double_click) {
		DeselectMarkers();
		return;
	}

	double depth = 0.0;
	const int32 selected = FindSelectedMarker();

	if (selected >= 0) {
		depth = DepthOf(mState.Markers[selected].Position);
	}
	else if (mState.bHasPlayer) {
		depth = DepthOf(mState.PlayerPosition);
	}

	const Vec3f position = ToWorldVector(Snap(mPress), depth);

	SendCommand([position] { gEditor->TopViewCreateLight(position); });
}

void TopViewCanvas::BeginClip()
{
	if (GetSelectedCount() == 0 || mbAdditive) {
		const DynArray<int32> hits = PickAt(mPress);

		if (hits.IsEmpty()) {
			mDrag = eDrag::Marquee;
			return;
		}

		SelectBrush(mState.Brushes[hits[0]].ObjectId,
					mbAdditive ? eTopViewSelectMode::Toggle : eTopViewSelectMode::Replace);
		return;
	}

	mPress = Snap(mPress);
	mDrag = eDrag::Clip;
}

void TopViewCanvas::BeginSelect()
{
	if (BeginMarker()) {
		return;
	}

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
		mbHasPickedFace = false;

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

bool TopViewCanvas::BeginHandleDrag()
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
		return true;
	}

	return false;
}

void TopViewCanvas::BeginVertex()
{
	if (BeginHandleDrag()) {
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

void TopViewCanvas::BeginArch(bool double_click)
{
	const eArchHandle handle = PickArchHandle(mPress);

	if (double_click && HasPendingArch() && mArchRect.Contains(mPress)) {
		CommitArch();
		return;
	}

	if (handle != eArchHandle::None) {
		BeginArchEdit(handle);
		return;
	}

	mbArchPending = false;
	mArchHover = eArchHandle::None;

	mPress = Snap(mPress);
	mDrag = eDrag::Arch;
}

void TopViewCanvas::BeginArchEdit(eArchHandle handle)
{
	mDrag = eDrag::ArchEdit;
	mArchGrab = handle;
	mArchGrabRect = mArchRect;
	mArchGrabSettings = mArch;
	mArchGrabEllipse = FitArch(mArchRect);
	mArchAngleTravel = 0.0;

	if (mArchGrabEllipse.bValid) {
		const Vec2d local = (mPress - mArchGrabEllipse.Center) / mArchGrabEllipse.Radius;
		mArchLastAngle = std::atan2(local.Y, local.X);
	}
}

void TopViewCanvas::BeginCurve()
{
	if (BeginHandleDrag()) {
		return;
	}

	for (const int32 hit : PickAt(mPress)) {
		const TopViewBrush& clicked = mState.Brushes[hit];

		if (clicked.Kind != eTopViewBrushKind::Patch) {
			continue;
		}

		mSelectedVertices.Clear();

		if (!clicked.bSelected || GetSelectedCount() > 1) {
			SelectBrush(clicked.ObjectId, mbAdditive ? eTopViewSelectMode::Add : eTopViewSelectMode::Replace);
		}

		return;
	}

	mSelectedVertices.Clear();
	BeginCreate();
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
		const Vec2d moved = grabbed + (mCurrent - mPress);

		Vec2d patch_point = Vec2d::sZero;
		const bool snapped_to_patch = FindPatchSnapPoint(moved, patch_point);

		Vec2d target = patch_point;

		if (!snapped_to_patch) {
			target = Snap(moved);
		}

		mPreview = Preview();
		mPreview.Kind = eDrag::Vertex;
		mPreview.OffsetUV = target - grabbed;
		mPreview.bSnappedToPatch = snapped_to_patch;
		mPreview.PatchSnapUV = patch_point;

		for (const TopViewBrush& brush : mState.Brushes) {
			if (!brush.bSelected || brush.Kind == eTopViewBrushKind::Patch) {
				continue;
			}

			DynArray<Vec2d> points = CollectVertexHandles(brush);
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

		mArchRect = DRect(Vec2d::Min(mPress, current), Vec2d::Max(mPress, current));
		mbArchPending = true;
		break;
	}
	case eDrag::ArchEdit:
		UpdateArchEdit();
		break;
	case eDrag::Rotate: {
		const Vec2d from_pivot = mCurrent - mRotatePivot;
		const double angle = std::atan2(from_pivot.Y, from_pivot.X);

		mPreview.Kind = eDrag::Rotate;
		mPreview.Angle = SnapAngle(WrapAngle(angle - mRotateStartAngle));
		mPreview.PivotUV = mRotatePivot;
		mPreview.OffsetUV = GetRotateSnapOffset(mRotatePivot, mPreview.Angle);
		break;
	}
	case eDrag::Clip: {
		const Vec2d current = Snap(mCurrent);

		mPreview = Preview();
		mPreview.Kind = eDrag::Clip;
		mPreview.ClipA = mPress;
		mPreview.ClipB = current;

		const Vec2d along = current - mPress;
		const double length = along.Length();

		if (length < scMinClipLength) {
			break;
		}

		const Vec2d normal(along.Y / length, -along.X / length);
		const double offset = normal.Dot(mPress);

		for (const TopViewBrush& brush : mState.Brushes) {
			const DynArray<Vec2f>& hull = Projection(brush).Hull;

			if (!brush.bSelected || hull.Size < 3) {
				continue;
			}

			DynArray<Vec2f> front = ClipPolygon(hull, normal, offset);
			DynArray<Vec2f> back = ClipPolygon(hull, normal * -1.0, -offset);

			if (front.Size >= 3 && back.Size >= 3) {
				mPreview.Outlines.Insert(OutlinePreview { brush.ObjectId, std::move(front) });
				mPreview.Outlines.Insert(OutlinePreview { brush.ObjectId, std::move(back) });
			}
		}

		break;
	}
	case eDrag::MarkerMove: {
		const int32 index = FindMarker(mGrabbedMarkerKind, mGrabbedMarkerLightId);

		if (index < 0) {
			break;
		}

		const Vec2d position = ProjectPoint(mState.Markers[index].Position);

		mPreview.Kind = eDrag::MarkerMove;
		mPreview.OffsetUV = Snap(position + (mCurrent - mPress)) - position;
		break;
	}
	case eDrag::MarkerAim: {
		const int32 index = FindMarker(mGrabbedMarkerKind, mGrabbedMarkerLightId);

		if (index < 0) {
			break;
		}

		const TopViewMarker& marker = mState.Markers[index];
		const Vec2d to_cursor = mCurrent - ProjectPoint(marker.AimOrigin);
		const double distance = to_cursor.Length();

		if (distance * mScale < 2.0) {
			break;
		}

		double depth = DepthOf(marker.Direction);
		double planar = std::sqrt(std::max(0.0, 1.0 - depth * depth));

		if (planar < scMinAimPlanar) {
			planar = 1.0;
			depth = 0.0;
		}

		mPreview.Kind = eDrag::MarkerAim;
		mPreview.AimUV = (to_cursor / distance) * planar;
		mPreview.AimDepth = depth;
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
	if (mDrag == eDrag::Arch) {
		mbArchPending = false;
	}
	else if (mDrag == eDrag::ArchEdit) {
		mArchRect = mArchGrabRect;
		mArch = mArchGrabSettings;
		mArchGrab = eArchHandle::None;
		NotifyArchChanged();
	}

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
	if (mDrag == eDrag::Arch) {
		mDrag = eDrag::None;
		FinishArch();
	}
	else if (mDrag == eDrag::ArchEdit) {
		mDrag = eDrag::None;
		FinishArchEdit();
	}

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
	case eDrag::ArchEdit:
		FinishArchEdit();
		break;
	case eDrag::Rotate:
		FinishRotate();
		break;
	case eDrag::Clip:
		FinishClip();
		break;
	case eDrag::MarkerMove:
		FinishMarkerMove();
		break;
	case eDrag::MarkerAim:
		FinishMarkerAim();
		break;
	default:
		break;
	}

	if (!mbPreviewHeld) {
		ClearPreview();
	}

	SyncWeightFromHandles();
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
	if (UsesHandles()) {
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

			for (const Vec2d& point : CollectVertexHandles(brush)) {
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
	if (!mbDragMoved) {
		mbHasPickedFace = true;
		mPickedFaceBrushId = mGrabbedBrushId;
		mPickedFaceNormal = mGrabbedFace.Normal;
	}

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
	const bool is_decal = (mTool == eEditorTool::Decal);
	const double depth = is_decal ? 0.0 : std::max<double>(mCreateSize[plane_index], scMinCreateDepth);

	const Vec3f first = ToWorldVector(mPreview.RectUV.Min, base);
	const Vec3f second = ToWorldVector(mPreview.RectUV.Max, base + depth);

	const Vec3f min(std::min<float32>(first.X, second.X), std::min<float32>(first.Y, second.Y),
					std::min<float32>(first.Z, second.Z));
	const Vec3f max(std::max<float32>(first.X, second.X), std::max<float32>(first.Y, second.Y),
					std::max<float32>(first.Z, second.Z));

	HoldPreview();
	const bool drop_to_ground = mbDropToGround;
	const eViewPlane plane = mPlane;

	if (is_decal) {
		SendCommand([plane, min, max, drop_to_ground]
					{ gEditor->TopViewCreateDecal(plane, min, max, drop_to_ground); });
		return;
	}

	if (mTool == eEditorTool::Curve) {
		const ePatchPrimitive primitive = mCurve.Primitive;

		PatchCommand settings;
		settings.SubdivisionsU = mCurve.SubdivisionsU;
		settings.SubdivisionsV = mCurve.SubdivisionsV;
		settings.bSmooth = mCurve.bSmooth;
		settings.Closure = mCurve.Closure;
		settings.Thickness = mCurve.Thickness;

		SendCommand([plane, primitive, min, max, drop_to_ground, settings]
					{ gEditor->TopViewCreatePatch(plane, primitive, min, max, drop_to_ground, settings); });
		return;
	}

	SendCommand([plane, min, max, drop_to_ground] { gEditor->TopViewCreate(plane, min, max, drop_to_ground); });
}

TopViewCanvas::ArchEllipse TopViewCanvas::FitArch(const DRect& rect) const
{
	ArchEllipse ellipse;

	Vec2d low;
	Vec2d high;
	GetArchDirectionBounds(mArch, low, high);

	const Vec2d span = high - low;

	if (span.X < 1e-6 || span.Y < 1e-6) {
		return ellipse;
	}

	const Vec2d extent = rect.GetExtent();

	ellipse.Radius = extent / span;
	ellipse.Center = rect.Min - low * ellipse.Radius;
	ellipse.bValid = ellipse.Radius.X > 0.0 && ellipse.Radius.Y > 0.0;

	return ellipse;
}

DRect TopViewCanvas::GetArchRect(const ArchEllipse& ellipse) const
{
	Vec2d low;
	Vec2d high;
	GetArchDirectionBounds(mArch, low, high);

	return DRect(ellipse.Center + low * ellipse.Radius, ellipse.Center + high * ellipse.Radius);
}

DynArray<Vec2d> TopViewCanvas::BuildArchSegments(const DRect& rect) const
{
	DynArray<Vec2d> corners;

	const ArchEllipse ellipse = FitArch(rect);

	if (!ellipse.bValid) {
		return corners;
	}

	const double wall = std::max<double>(mArch.WallWidth, 0.0);
	const Vec2d inner_radius = Vec2d::Max(ellipse.Radius - Vec2d(wall), Vec2d::sZero);

	const auto outer = [&](uint32 index)
	{ return ellipse.Center + GetArchDirection(mArch, static_cast<double>(index)) * ellipse.Radius; };
	const auto inner = [&](uint32 index)
	{ return ellipse.Center + GetArchDirection(mArch, static_cast<double>(index)) * inner_radius; };

	for (uint32 i = 0; i < mArch.Sides; i++) {
		corners.Insert(outer(i));
		corners.Insert(outer(i + 1));
		corners.Insert(inner(i + 1));
		corners.Insert(inner(i));
	}

	return corners;
}

Vec2d TopViewCanvas::GetArchHandle(eArchHandle handle) const
{
	const DRect& rect = mArchRect;
	const Vec2d middle = rect.GetCenter();

	switch (handle) {
	case eArchHandle::Wall:
	case eArchHandle::Start:
	case eArchHandle::End: {
		const ArchEllipse ellipse = FitArch(rect);

		if (handle == eArchHandle::Start || handle == eArchHandle::End) {
			const double index = (handle == eArchHandle::Start) ? 0.0 : static_cast<double>(mArch.Sides);
			const Vec2d direction = GetArchDirection(mArch, index);

			return ellipse.Center + direction * ellipse.Radius + direction * (scArchKnobPixels / mScale);
		}

		const double wall = std::max<double>(mArch.WallWidth, 0.0);
		const Vec2d inner_radius = Vec2d::Max(ellipse.Radius - Vec2d(wall), Vec2d::sZero);

		return ellipse.Center + GetArchWallDirection(mArch) * inner_radius;
	}
	case eArchHandle::MinMin:
		return rect.Min;
	case eArchHandle::MaxMin:
		return Vec2d(rect.Max.X, rect.Min.Y);
	case eArchHandle::MaxMax:
		return rect.Max;
	case eArchHandle::MinMax:
		return Vec2d(rect.Min.X, rect.Max.Y);
	case eArchHandle::Left:
		return Vec2d(rect.Min.X, middle.Y);
	case eArchHandle::Right:
		return Vec2d(rect.Max.X, middle.Y);
	case eArchHandle::Bottom:
		return Vec2d(middle.X, rect.Min.Y);
	case eArchHandle::Top:
		return Vec2d(middle.X, rect.Max.Y);
	default:
		return middle;
	}
}

TopViewCanvas::eArchHandle TopViewCanvas::PickArchHandle(const Vec2d uv) const
{
	if (!mbArchPending || mDrag == eDrag::Arch) {
		return eArchHandle::None;
	}

	const Vec2d screen = ToScreenCoords(uv);

	constexpr eArchHandle point_handles[] = {
		eArchHandle::End,	 eArchHandle::Start,  eArchHandle::Wall,   eArchHandle::MinMin,
		eArchHandle::MaxMin, eArchHandle::MaxMax, eArchHandle::MinMax,
	};

	for (const eArchHandle handle : point_handles) {
		if ((ToScreenCoords(GetArchHandle(handle)) - screen).Length() <= scVertexPickPixels) {
			return handle;
		}
	}

	const Vec2d corner_a = ToScreenCoords(mArchRect.Min);
	const Vec2d corner_b = ToScreenCoords(Vec2d(mArchRect.Max.X, mArchRect.Min.Y));
	const Vec2d corner_c = ToScreenCoords(mArchRect.Max);
	const Vec2d corner_d = ToScreenCoords(Vec2d(mArchRect.Min.X, mArchRect.Max.Y));

	if (DistanceToSegment(screen, corner_d, corner_a) <= scPickTolerancePixels) {
		return eArchHandle::Left;
	}

	if (DistanceToSegment(screen, corner_b, corner_c) <= scPickTolerancePixels) {
		return eArchHandle::Right;
	}

	if (DistanceToSegment(screen, corner_a, corner_b) <= scPickTolerancePixels) {
		return eArchHandle::Bottom;
	}

	if (DistanceToSegment(screen, corner_c, corner_d) <= scPickTolerancePixels) {
		return eArchHandle::Top;
	}

	if (mArchRect.Contains(uv)) {
		return eArchHandle::Body;
	}

	return eArchHandle::None;
}

void TopViewCanvas::UpdateArchEdit()
{
	const Vec2d delta = mCurrent - mPress;
	const ArchEllipse& ellipse = mArchGrabEllipse;

	switch (mArchGrab) {
	case eArchHandle::Body: {
		const Vec2d min = Snap(mArchGrabRect.Min + delta);
		mArchRect = DRect(min, min + mArchGrabRect.GetExtent());
		break;
	}
	case eArchHandle::Wall: {
		if (!ellipse.bValid) {
			break;
		}

		const Vec2d direction = GetArchWallDirection(mArch);
		const double length_sq = direction.LengthSquared();

		if (length_sq < 1e-9) {
			break;
		}

		const Vec2d outer = ellipse.Center + direction * ellipse.Radius;
		const double wall = Snap((outer - mCurrent).Dot(direction) / length_sq);

		mArch.WallWidth = static_cast<float32>(
			std::clamp(wall, scMinArchWall, std::max(std::max(ellipse.Radius.X, ellipse.Radius.Y), scMinArchWall)));
		NotifyArchChanged();
		break;
	}
	case eArchHandle::Start:
	case eArchHandle::End: {
		if (!ellipse.bValid) {
			break;
		}

		const Vec2d local = (mCurrent - ellipse.Center) / ellipse.Radius;
		const double angle = std::atan2(local.Y, local.X);

		mArchAngleTravel += WrapAngle(angle - mArchLastAngle);
		mArchLastAngle = angle;

		const double to_degrees = 180.0 / FX_PI;
		const double travel = mArchAngleTravel * to_degrees;
		const double grab_start = mArchGrabSettings.StartDegrees;
		const double grab_end = grab_start + std::clamp<double>(mArchGrabSettings.ArcDegrees, 1.0, 360.0);

		double start = grab_start;
		double arc = grab_end - grab_start;

		if (mArchGrab == eArchHandle::End) {
			const double end = SnapAngle((grab_end + travel) / to_degrees) * to_degrees;
			arc = std::clamp(end - start, 1.0, 360.0);
		}
		else {
			start = SnapAngle((grab_start + travel) / to_degrees) * to_degrees;
			arc = std::clamp(grab_end - start, 1.0, 360.0);
			start = grab_end - arc;
		}

		mArch.StartDegrees = static_cast<float32>(std::fmod(start, 360.0));
		mArch.ArcDegrees = static_cast<float32>(arc);
		mArchRect = GetArchRect(ellipse);
		NotifyArchChanged();
		break;
	}
	default: {
		const bool left = mArchGrab == eArchHandle::MinMin || mArchGrab == eArchHandle::MinMax ||
						  mArchGrab == eArchHandle::Left;
		const bool right = mArchGrab == eArchHandle::MaxMin || mArchGrab == eArchHandle::MaxMax ||
						   mArchGrab == eArchHandle::Right;
		const bool bottom = mArchGrab == eArchHandle::MinMin || mArchGrab == eArchHandle::MaxMin ||
							mArchGrab == eArchHandle::Bottom;
		const bool top = mArchGrab == eArchHandle::MinMax || mArchGrab == eArchHandle::MaxMax ||
						 mArchGrab == eArchHandle::Top;

		Vec2d a = mArchGrabRect.Min;
		Vec2d b = mArchGrabRect.Max;

		if (left) {
			a.X = Snap(a.X + delta.X);
		}

		if (right) {
			b.X = Snap(b.X + delta.X);
		}

		if (bottom) {
			a.Y = Snap(a.Y + delta.Y);
		}

		if (top) {
			b.Y = Snap(b.Y + delta.Y);
		}

		mArchRect = DRect(Vec2d::Min(a, b), Vec2d::Max(a, b));
		break;
	}
	}
}

void TopViewCanvas::FinishArch()
{
	if (!mbArchPending) {
		return;
	}

	const Vec2d extent = mArchRect.GetExtent();

	if (extent.X < scMinCreateSize || extent.Y < scMinCreateSize || !FitArch(mArchRect).bValid) {
		mbArchPending = false;
	}
}

void TopViewCanvas::FinishArchEdit()
{
	const Vec2d extent = mArchRect.GetExtent();

	if (extent.X < scMinCreateSize || extent.Y < scMinCreateSize || !FitArch(mArchRect).bValid) {
		mArchRect = mArchGrabRect;
		mArch = mArchGrabSettings;
		NotifyArchChanged();
	}

	mArchGrab = eArchHandle::None;
}

void TopViewCanvas::DiscardArch()
{
	if (mDrag == eDrag::Arch || mDrag == eDrag::ArchEdit) {
		CancelDrag();
	}

	mbArchPending = false;
	mArchHover = eArchHandle::None;

	UpdateCursor();
	UpdateStatus();
	Refresh();
}

void TopViewCanvas::CommitArch()
{
	if (!HasPendingArch() || mDrag == eDrag::ArchEdit) {
		return;
	}

	const DRect rect = mArchRect;

	mbArchPending = false;
	mArchHover = eArchHandle::None;

	const DynArray<Vec2d> corners = BuildArchSegments(rect);

	if (corners.IsEmpty()) {
		UpdateStatus();
		Refresh();
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

	mPreview = Preview();
	mPreview.Kind = eDrag::Arch;
	mPreview.RectUV = rect;
	HoldPreview();

	const bool drop_to_ground = mbDropToGround;
	const eViewPlane plane = mPlane;

	SendCommand([plane, points = std::move(points), drop_to_ground]() mutable
				{ gEditor->TopViewCreateArch(plane, std::move(points), drop_to_ground); });

	UpdateCursor();
	UpdateStatus();
	Refresh();
}

void TopViewCanvas::DrawArch(wxGraphicsContext& gc, const DRect& rect, bool show_handles)
{
	const EditorTheme& theme = GetEditorTheme();
	const wxColour colour = ToWxColour(theme.Tool.Create);
	const wxFont font = wxFont(wxFontInfo(10));

	const DynArray<Vec2d> corners = BuildArchSegments(rect);

	wxGraphicsPath outlines = gc.CreatePath();

	for (uint32 i = 0; i + scArchCornersPerSegment <= corners.Size; i += scArchCornersPerSegment) {
		DynArray<Vec2d> points;

		for (uint32 j = 0; j < scArchCornersPerSegment; j++) {
			points.Insert(corners[i + j]);
		}

		const DynArray<Vec2f> polygon = ConvexHull2D(std::move(points));

		if (polygon.Size < 3) {
			continue;
		}

		const Vec2d first = ToScreenCoords(ToVec2d(polygon[0]));
		outlines.MoveToPoint(first.X, first.Y);

		for (uint32 j = 1; j < polygon.Size; j++) {
			const Vec2d screen_point = ToScreenCoords(ToVec2d(polygon[j]));
			outlines.AddLineToPoint(screen_point.X, screen_point.Y);
		}

		outlines.CloseSubpath();
	}

	const Vec2d top_left = ToScreenCoords(Vec2d(rect.Min.X, rect.Max.Y));
	const Vec2d bottom_right = ToScreenCoords(Vec2d(rect.Max.X, rect.Min.Y));
	const Vec2d screen_extent = bottom_right - top_left;
	const Vec2d extent = rect.GetExtent();

	gc.SetPen(wxPen(colour, 1, wxPENSTYLE_SHORT_DASH));
	gc.SetBrush(*wxTRANSPARENT_BRUSH);
	gc.DrawRectangle(top_left.X, top_left.Y, screen_extent.X, screen_extent.Y);

	gc.SetBrush(wxBrush(ToWxColour(theme.Tool.CreateFill)));
	gc.SetPen(wxPen(colour, 1));
	gc.FillPath(outlines, wxWINDING_RULE);
	gc.StrokePath(outlines);

	gc.SetFont(font, colour);
	gc.DrawText(wxString::Format("%.2f x %.2f   %u sides   wall %.2f   arc %.0f   start %.0f", extent.X, extent.Y,
								 mArch.Sides, mArch.WallWidth, mArch.ArcDegrees, mArch.StartDegrees),
				top_left.X + 4.0, bottom_right.Y + 4.0);

	if (!show_handles) {
		return;
	}

	const ArchEllipse ellipse = FitArch(rect);

	if (ellipse.bValid) {
		const Vec2d center = ToScreenCoords(ellipse.Center);
		const Vec2d start = ToScreenCoords(GetArchHandle(eArchHandle::Start));
		const Vec2d end = ToScreenCoords(GetArchHandle(eArchHandle::End));

		gc.SetPen(wxPen(WithAlpha(colour, 140), 1, wxPENSTYLE_SHORT_DASH));
		gc.StrokeLine(center.X, center.Y, start.X, start.Y);
		gc.StrokeLine(center.X, center.Y, end.X, end.Y);

		gc.SetPen(wxPen(colour, 1));
		gc.StrokeLine(center.X - scArchCenterCross, center.Y, center.X + scArchCenterCross, center.Y);
		gc.StrokeLine(center.X, center.Y - scArchCenterCross, center.X, center.Y + scArchCenterCross);
	}

	const eArchHandle active = (mDrag == eDrag::ArchEdit) ? mArchGrab : mArchHover;

	constexpr eArchHandle box_handles[] = {
		eArchHandle::MinMin,
		eArchHandle::MaxMin,
		eArchHandle::MaxMax,
		eArchHandle::MinMax,
	};

	for (const eArchHandle handle : box_handles) {
		const Vec2d screen = ToScreenCoords(GetArchHandle(handle));
		const bool hot = (handle == active);
		const double half = hot ? scHandleHalfSize + 1.5 : scHandleHalfSize;

		gc.SetBrush(wxBrush(hot ? colour : ToWxColour(theme.Tool.Handle)));
		gc.SetPen(wxPen(hot ? colour : ToWxColour(theme.Tool.HandleOutline), 1));
		gc.DrawRectangle(screen.X - half, screen.Y - half, half * 2.0, half * 2.0);
	}

	if (!ellipse.bValid) {
		return;
	}

	constexpr eArchHandle shape_handles[] = { eArchHandle::Wall, eArchHandle::Start, eArchHandle::End };

	for (const eArchHandle handle : shape_handles) {
		const Vec2d screen = ToScreenCoords(GetArchHandle(handle));
		const bool hot = (handle == active);
		const double radius = hot ? scArchHandleRadius + 1.5 : scArchHandleRadius;

		gc.SetBrush(wxBrush(hot ? ToWxColour(theme.Tool.Handle) : colour));
		gc.SetPen(wxPen(ToWxColour(theme.Tool.HandleOutline), 1));
		gc.DrawEllipse(screen.X - radius, screen.Y - radius, radius * 2.0, radius * 2.0);
	}

	if (active == eArchHandle::Wall || active == eArchHandle::Start || active == eArchHandle::End) {
		const Vec2d screen = ToScreenCoords(GetArchHandle(active));
		wxString label = wxString::Format("%.0f", mArch.StartDegrees);

		if (active == eArchHandle::Wall) {
			label = wxString::Format("wall %.2f", mArch.WallWidth);
		}
		else if (active == eArchHandle::End) {
			label = wxString::Format("%.0f", mArch.StartDegrees + mArch.ArcDegrees);
		}

		gc.SetFont(font, colour);
		gc.DrawText(label, screen.X + 8.0, screen.Y - scDimensionLabelTop);
	}
}

void TopViewCanvas::DrawPatchLattices(wxGraphicsContext& gc)
{
	const EditorTheme& theme = GetEditorTheme();
	const bool live = (mDrag == eDrag::Vertex) && (mPreview.Kind == eDrag::Vertex);

	wxGraphicsPath path = gc.CreatePath();

	for (const TopViewBrush& brush : mState.Brushes) {
		if (!brush.bSelected || brush.Kind != eTopViewBrushKind::Patch) {
			continue;
		}

		const TopViewProjection& projection = Projection(brush);
		const uint32 width = brush.PatchWidth;
		const uint32 height = brush.PatchHeight;

		if (width == 0 || projection.ControlPoints.Size != width * height) {
			continue;
		}

		DynArray<Vec2d> points;

		for (const Vec2f& control_point : projection.ControlPoints) {
			Vec2d point = ToVec2d(control_point);

			if (live && IsVertexSelected(brush.ObjectId, point)) {
				point += mPreview.OffsetUV;
			}

			points.Insert(ToScreenCoords(point));
		}

		const auto add_line = [&](uint32 from, uint32 to)
		{
			path.MoveToPoint(points[from].X, points[from].Y);
			path.AddLineToPoint(points[to].X, points[to].Y);
		};

		for (uint32 row = 0; row < height; row++) {
			for (uint32 column = 0; column + 1 < width; column++) {
				add_line(row * width + column, row * width + column + 1);
			}
		}

		for (uint32 column = 0; column < width; column++) {
			for (uint32 row = 0; row + 1 < height; row++) {
				add_line(row * width + column, (row + 1) * width + column);
			}
		}
	}

	gc.SetPen(wxPen(WithAlpha(ToWxColour(theme.Tool.Vertex), 140), 1, wxPENSTYLE_SHORT_DASH));
	gc.StrokePath(path);
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
	const Vec3f offset = ToWorldVector(mPreview.OffsetUV, 0.0);

	HoldPreview();
	SendCommand([axis, angle, pivot, offset] { gEditor->TopViewRotate(axis, angle, pivot, offset); });
}

void TopViewCanvas::FinishClip()
{
	if (mPreview.Kind != eDrag::Clip || (mPreview.ClipB - mPreview.ClipA).Length() < scMinClipLength) {
		ClearPreview();
		return;
	}

	const Vec3f point_a = ToWorldVector(mPreview.ClipA, 0.0);
	const Vec3f point_b = ToWorldVector(mPreview.ClipB, 0.0);
	const Vec3f view_axis = ToWorldVector(Vec2d::sZero, 1.0);

	HoldPreview();
	SendCommand([point_a, point_b, view_axis] { gEditor->TopViewClip(point_a, point_b, view_axis); });
}

void TopViewCanvas::FinishMarkerMove()
{
	if (!mbDragMoved || mPreview.Kind != eDrag::MarkerMove || mPreview.OffsetUV == Vec2d::sZero) {
		return;
	}

	const Vec3f offset = ToWorldVector(mPreview.OffsetUV, 0.0);

	HoldPreview();

	if (mGrabbedMarkerKind == eTopViewMarkerKind::Spawn) {
		SendCommand([offset] { gEditor->TopViewMoveSpawn(offset); });
	}
	else {
		SendCommand([offset] { gEditor->TopViewMoveLight(offset); });
	}
}

void TopViewCanvas::FinishMarkerAim()
{
	if (mPreview.Kind != eDrag::MarkerAim) {
		return;
	}

	const Vec3f direction = ToWorldVector(mPreview.AimUV, mPreview.AimDepth);

	HoldPreview();

	if (mGrabbedMarkerKind == eTopViewMarkerKind::Spawn) {
		SendCommand([direction] { gEditor->TopViewAimSpawn(direction); });
	}
	else {
		SendCommand([direction] { gEditor->TopViewAimLight(direction); });
	}
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

void TopViewCanvas::NudgeMarker(const TopViewMarker& marker, const Vec2d delta)
{
	const Vec3f offset = ToWorldVector(delta, 0.0);

	if (marker.Kind == eTopViewMarkerKind::Spawn) {
		SendCommand([offset] { gEditor->TopViewMoveSpawn(offset); });
	}
	else {
		SendCommand([offset] { gEditor->TopViewMoveLight(offset); });
	}
}

void TopViewCanvas::NudgeSelection(const Vec2d delta, bool snap)
{
	if (const int32 marker = FindSelectedMarker(); marker >= 0) {
		const TopViewMarker& selected = mState.Markers[marker];

		if (snap) {
			NudgeMarker(selected, SnapNudge(ProjectPoint(selected.Position), delta));
		}
		else {
			NudgeMarker(selected, delta);
		}

		return;
	}

	const ViewBounds bounds = GetSelectionBounds();

	if (!bounds.bValid) {
		return;
	}

	Vec2d moved = delta;

	if (snap) {
		moved = SnapNudge(bounds.Rect.Min, delta);
	}

	const Vec3f offset = ToWorldVector(moved, 0.0);

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

void TopViewCanvas::SetToolFromKey(eEditorTool tool)
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

	if (event.AltDown() && !command && mTool == eEditorTool::Face && mbHasPickedFace && mDrag == eDrag::None) {
		TextureCommand texture_command;
		bool is_texture_key = true;

		if (key == WXK_LEFT || key == WXK_RIGHT || key == WXK_UP || key == WXK_DOWN) {
			texture_command.Type = eTextureCommand::Shift;

			if (key == WXK_LEFT) {
				texture_command.Amount = Vec2f(-1.0f, 0.0f);
			}
			else if (key == WXK_RIGHT) {
				texture_command.Amount = Vec2f(1.0f, 0.0f);
			}
			else if (key == WXK_UP) {
				texture_command.Amount = Vec2f(0.0f, -1.0f);
			}
			else {
				texture_command.Amount = Vec2f(0.0f, 1.0f);
			}
		}
		else if (key == '[' || key == ']') {
			texture_command.Type = eTextureCommand::Scale;
			texture_command.Amount = Vec2f(2.0f);

			if (key == '[') {
				texture_command.Amount = Vec2f(0.5f);
			}
		}
		else if (key == ',' || key == '.') {
			texture_command.Type = eTextureCommand::Rotate;
			texture_command.Amount = Vec2f(scTextureRotateDegrees, 0.0f);

			if (key == ',') {
				texture_command.Amount = Vec2f(-scTextureRotateDegrees, 0.0f);
			}
		}
		else {
			is_texture_key = false;
		}

		if (is_texture_key) {
			RunTextureCommand(texture_command);
			return;
		}
	}

	const double step = (mState.bSnapEnabled && mState.SnapStep > 0.0f) ? mState.SnapStep : 0.25;

	if (!command && mDrag == eDrag::None && (key == WXK_LEFT || key == WXK_RIGHT || key == WXK_UP || key == WXK_DOWN)) {
		const bool fine = event.AltDown();
		const double distance = step * (fine ? scNudgeFineFactor : (shift ? scNudgeCoarseFactor : 1.0));
		const double du = (key == WXK_RIGHT) ? distance : ((key == WXK_LEFT) ? -distance : 0.0);
		const double dv = (key == WXK_UP) ? distance : ((key == WXK_DOWN) ? -distance : 0.0);

		if (HasPendingArch()) {
			Vec2d moved = Vec2d(du, dv);

			if (!fine) {
				moved = SnapNudge(mArchRect.Min, moved);
			}

			mArchRect = DRect(mArchRect.Min + moved, mArchRect.Max + moved);
			UpdateHover();
			Refresh();
			return;
		}

		NudgeSelection(Vec2d(du, dv), !fine);
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

		if (key == 'G' && mDrag == eDrag::None) {
			SendCommand([] { gEditor->CreateSubviewFromSelection(""); });
			return;
		}

		if (key == 'B' && mDrag == eDrag::None) {
			SendCommand([] { gEditor->SnapSelectionToGrid(); });
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
		SetToolFromKey(eEditorTool::Select);
		break;
	case 'F':
		SetToolFromKey(eEditorTool::Face);
		break;
	case 'V':
		SetToolFromKey(eEditorTool::Vertex);
		break;
	case 'B':
		SetToolFromKey(eEditorTool::Create);
		break;
	case 'A':
		SetToolFromKey(eEditorTool::Arch);
		break;
	case 'N':
		SetToolFromKey(eEditorTool::Curve);
		break;
	case 'D':
		SetToolFromKey(eEditorTool::Decal);
		break;
	case 'R':
		SetToolFromKey(eEditorTool::Rotate);
		break;
	case 'C':
		SetToolFromKey(eEditorTool::Clip);
		break;
	case 'I':
		SetToolFromKey(eEditorTool::Light);
		break;
	case 'S':
		if (mDrag == eDrag::None) {
			Subtract(shift);
		}
		break;
	case 'G':
		if (mDrag == eDrag::None) {
			SendCommand([] { gEditor->TopViewToggleSubview(); });
		}
		break;
	case WXK_RETURN:
	case WXK_NUMPAD_ENTER:
		if (HasPendingArch()) {
			if (mDrag == eDrag::None) {
				CommitArch();
			}
		}
		else if (const int32 marker = FindSelectedMarker();
				 marker >= 0 && mState.Markers[marker].Kind == eTopViewMarkerKind::Spawn) {
			SendCommand([] { gEditor->TopViewRespawn(); });
		}
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
		if (HasPendingArch()) {
			DiscardArch();
		}
		else if (mDrag == eDrag::None) {
			SendCommand([] { gEditor->TopViewDelete(); });
		}
		break;
	case '[':
	case ']':
		if (mTool == eEditorTool::Arch && mDrag != eDrag::ArchEdit) {
			SetArchSides(static_cast<int32>(mArch.Sides) + ((key == ']') ? 1 : -1));
		}
		else if (mTool == eEditorTool::Curve && mDrag == eDrag::None) {
			const int32 change = (key == ']') ? 1 : -1;
			const int32 subdivisions = static_cast<int32>(std::max(mCurve.SubdivisionsU, mCurve.SubdivisionsV)) +
									   change;
			const uint32 clamped = static_cast<uint32>(
				std::clamp<int32>(subdivisions, Patch::scMinSubdivisions, Patch::scMaxSubdivisions));

			mCurve.SubdivisionsU = clamped;
			mCurve.SubdivisionsV = clamped;
			NotifyCurveChanged();
			RunPatchCommand(ePatchCommand::SetSubdivisions);
		}
		else {
			event.Skip();
		}
		break;
	case WXK_ESCAPE:
		if (mDrag != eDrag::None) {
			ClearPreview();
			CancelDrag();
		}
		else if (mbHasPickedFace) {
			mbHasPickedFace = false;
			UpdateStatus();
			Refresh();
		}
		else if (HasPendingArch()) {
			DiscardArch();
		}
		else if (mSelectedVertices.IsNotEmpty()) {
			mSelectedVertices.Clear();
			Refresh();
		}
		else if (FindSelectedMarker() >= 0) {
			DeselectMarkers();
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
	if (mDrag == eDrag::Pan || mbSpaceDown) {
		SetCursor(wxCursor(wxCURSOR_SIZING));
		return;
	}

	if (mDrag == eDrag::Marquee || mbAltDown) {
		SetCursor(wxCursor(wxCURSOR_CROSS));
		return;
	}

	switch (mTool) {
	case eEditorTool::Face: {
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
	case eEditorTool::Vertex:
		SetCursor((mbHoverVertex || mDrag == eDrag::Vertex) ? wxCursor(wxCURSOR_SIZING) : wxNullCursor);
		break;
	case eEditorTool::Arch: {
		const eArchHandle handle = (mDrag == eDrag::ArchEdit) ? mArchGrab : mArchHover;

		switch (handle) {
		case eArchHandle::Body:
			SetCursor(wxCursor(wxCURSOR_SIZING));
			break;
		case eArchHandle::Wall:
		case eArchHandle::Start:
		case eArchHandle::End:
			SetCursor(wxCursor(wxCURSOR_HAND));
			break;
		case eArchHandle::MinMin:
		case eArchHandle::MaxMax:
			SetCursor(wxCursor(wxCURSOR_SIZENESW));
			break;
		case eArchHandle::MaxMin:
		case eArchHandle::MinMax:
			SetCursor(wxCursor(wxCURSOR_SIZENWSE));
			break;
		case eArchHandle::Left:
		case eArchHandle::Right:
			SetCursor(wxCursor(wxCURSOR_SIZEWE));
			break;
		case eArchHandle::Bottom:
		case eArchHandle::Top:
			SetCursor(wxCursor(wxCURSOR_SIZENS));
			break;
		default:
			SetCursor(wxCursor(wxCURSOR_CROSS));
			break;
		}

		break;
	}
	case eEditorTool::Curve:
		if (mbHoverVertex || mDrag == eDrag::Vertex) {
			SetCursor(wxCursor(wxCURSOR_HAND));
		}
		else {
			SetCursor(wxCursor(wxCURSOR_CROSS));
		}
		break;
	case eEditorTool::Create:
	case eEditorTool::Decal:
	case eEditorTool::Clip:
		SetCursor(wxCursor(wxCURSOR_CROSS));
		break;
	case eEditorTool::Rotate:
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

	if (!mNotice.IsEmpty() && std::chrono::steady_clock::now() - mNoticeTime < scNoticeDuration) {
		text += mNotice + "   |   ";
	}

	if (mState.bSimulating) {
		text += "Simulating, editing is paused   |   ";
	}

	if (mbHovering) {
		text += wxString::Format("%c %.2f   %c %.2f   |   ", scAxisNames[axes.U], mHover.X * axes.USign,
								 scAxisNames[axes.V], mHover.Y);
	}

	const char* hint = scToolHints[static_cast<uint32>(mTool)];

	if (mTool == eEditorTool::Arch && HasPendingArch()) {
		hint = scArchPendingHint;
	}

	if (mTool == eEditorTool::Face && mbHasPickedFace) {
		hint = scPickedFaceHint;
	}

	text += wxString::Format(
		"%d brushes, %u selected   |   Snap %s %.2f   |   Zoom %.0f%%   |   %s", static_cast<int>(mState.Brushes.Size),
		GetSelectedCount(), mState.bSnapEnabled ? "on" : "off", mState.SnapStep, mScale / scDefaultScale * 100.0, hint);

	mpOnStatus(text);
}

} // namespace fx::editor

#endif

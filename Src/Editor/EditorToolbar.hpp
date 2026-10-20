#pragma once

#ifdef FX_IS_EDITOR

#include "EditorPanelState.hpp"
#include "EditorTool.hpp"
#include "TopViewCanvas.hpp"
#include "TopViewPlane.hpp"

#include <Core/Types.hpp>

class wxBoxSizer;
class wxButton;
class wxChoice;
class wxColourPickerCtrl;
class wxSizer;
class wxWindow;
class wxWrapSizer;

namespace fx::editor {

class FloatField;
class OutlineToggleButton;

class EditorToolbar
{
public:
	EditorToolbar(wxWindow* parent, TopViewCanvas* canvas);

	wxSizer* GetSizer();

	void ShowView(eEditorView view);
	void ShowMode(eEditorMode mode, eDataFilter filter, uint32 available_tools);
	void ShowSimulating(bool simulating);

	void ApplyState(const TopViewState& state);

private:
	void ShowTool(eEditorTool tool);
	void ShowPlane(eViewPlane plane);
	void UpdateVisibility();

	wxBoxSizer* AddGroup(wxWrapSizer* row, bool separated);

private:
	wxWindow* mpParent = nullptr;
	TopViewCanvas* mpCanvas = nullptr;

	wxBoxSizer* mpSizer = nullptr;
	wxWrapSizer* mpViewRow = nullptr;
	wxWrapSizer* mpToolRow = nullptr;

	OutlineToggleButton* mpSimulateButton = nullptr;

	OutlineToggleButton* mpPerspectiveButton = nullptr;
	OutlineToggleButton* mpTopButton = nullptr;
	OutlineToggleButton* mpSplitButton = nullptr;

	OutlineToggleButton* mpVisButton = nullptr;
	OutlineToggleButton* mpDataButton = nullptr;
	wxChoice* mpDataFilterChoice = nullptr;

	wxChoice* mpPlaneChoice = nullptr;
	OutlineToggleButton* mpToolButtons[static_cast<uint32>(eEditorTool::Count)] = {};
	OutlineToggleButton* mpSubtractButton = nullptr;
	wxButton* mpShowAllButton = nullptr;
	wxButton* mpSnapButton = nullptr;
	wxButton* mpHollowButton = nullptr;

	wxBoxSizer* mpPlaneGroup = nullptr;
	wxBoxSizer* mpToolGroup = nullptr;
	wxBoxSizer* mpViewGroup = nullptr;
	wxBoxSizer* mpVolumeRows[scViewPlaneCount] = {};
	OutlineToggleButton* mpGroundButtons[scViewPlaneCount] = {};
	wxBoxSizer* mpArchRow = nullptr;
	FloatField* mpArchSidesField = nullptr;
	FloatField* mpArchWallField = nullptr;
	FloatField* mpArchArcField = nullptr;
	FloatField* mpArchStartField = nullptr;
	wxBoxSizer* mpCurveRow = nullptr;
	wxBoxSizer* mpFaceRow = nullptr;
	FloatField* mpCurveSubdivisionsUField = nullptr;
	FloatField* mpCurveSubdivisionsVField = nullptr;
	OutlineToggleButton* mpCurveSmoothButton = nullptr;
	FloatField* mpCurveThicknessField = nullptr;
	FloatField* mpCurveWeightField = nullptr;
	wxColourPickerCtrl* mpCurveColorPicker = nullptr;
	FloatField* mpCurveAlphaField = nullptr;
	OutlineToggleButton* mpCurveThickenButton = nullptr;
	OutlineToggleButton* mpCurveCapButton = nullptr;
	FloatField* mpTextureOffsetUField = nullptr;
	FloatField* mpTextureOffsetVField = nullptr;
	FloatField* mpTextureScaleField = nullptr;
	FloatField* mpTextureRotationField = nullptr;

	bool mbShows2D = false;
	eEditorTool mTool = eEditorTool::Select;
	eViewPlane mPlane = eViewPlane::Top;
	uint32 mShownHiddenCount = 0;
	bool mbShownHasSelection = false;

	float32 mBase[scViewPlaneCount] = {};
	float32 mSize[scViewPlaneCount] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

	ArchSettings mArch;
	CurveSettings mCurve;
};

} // namespace fx::editor

#endif

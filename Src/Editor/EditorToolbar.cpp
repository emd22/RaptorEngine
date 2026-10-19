#include "EditorToolbar.hpp"

#ifdef FX_IS_EDITOR

#include "Common.hpp"
#include "EditorThread.hpp"
#include "OutlineToggleButton.hpp"
#include "RaptorEditor.hpp"

#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/utils.h>
#include <wx/wrapsizer.h>

#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Math/Vec2.hpp>
#include <cmath>

namespace fx::editor {

namespace {

constexpr int scItemBorder = 2;
constexpr int scSeparatorBorder = 6;
constexpr int scLetterButtonSize = 28;

struct ToolButtonInfo
{
	const char* pName;
	const char* pToolTip;
	const char* pIconPath;
};

constexpr ToolButtonInfo scToolButtons[] = {
	{ "Select", "Select and move brushes (T)", "Textures/editor_new/translate.png" },
	{ "Face", "Drag a brush edge to move its face, click an edge to pick the face for texturing (F)",
	  "Textures/editor_new/face.png" },
	{ "Vertex", "Drag the corners of the selected brushes (V)", "Textures/editor_new/vertex.png" },
	{ "Create", "Drag to create a brush (B)", "Textures/editor_new/brush.png" },
	{ "Arch", "Drag a box to fill it with an arch made of brushes (A)", "Textures/editor_new/arch.png" },
	{ "Curve", "Drag to create a curve patch, drag the control points of the selected patches (N)",
	  "Textures/editor_new/bend.png" },
	{ "Decal", "Drag a box to project a decal onto the surfaces behind it (D)", "Textures/editor_new/decal.png" },
	{ "Rotate", "Rotate the selected brushes (R)", "Textures/editor_new/rotate.png" },
	{ "Clip", "Drag a line across the selected brushes to split them in two (C)", "Textures/editor_new/cut.png" },
	{ "Light", "Select, move, aim and add spot lights (I)", "Textures/editor_new/light.png" },
};

static_assert(std::size(scToolButtons) == static_cast<size_t>(eEditorTool::Count));

constexpr ToolButtonInfo scSimulateButton = {
	"Simulate", "Play the level in the 3D view. Nothing can be edited while simulating (X in the 3D view)",
	"Textures/editor_new/simulate.png"
};

constexpr ToolButtonInfo scSubtractButton = {
	"Subtract", "Cut the selected brushes out of the brushes they overlap (S). Hold Shift to keep the cutters",
	"Textures/editor_new/subtract.png"
};

constexpr const char* scPlaneNames[scViewPlaneCount] = {
	"Top", "Forward", "Back", "Left", "Right",
};

constexpr const char* scPatchPrimitiveNames[] = { "Flat", "Cylinder", "Cone", "Bevel", "Endcap" };

static_assert(std::size(scPatchPrimitiveNames) == static_cast<size_t>(ePatchPrimitive::Count));

struct PatchCommandButtonInfo
{
	const char* pName;
	const char* pToolTip;
	ePatchCommand Command;
};

constexpr PatchCommandButtonInfo scPatchCommandButtons[] = {
	{ "Invert", "Flip which side of the selected patches faces out", ePatchCommand::Invert },
	{ "Transpose", "Swap the rows and columns of the selected patches", ePatchCommand::Transpose },
	{ "+Col", "Split a column of the selected patches in two, at the selected control point",
	  ePatchCommand::InsertColumns },
	{ "-Col", "Merge two columns of the selected patches, at the selected control point",
	  ePatchCommand::RemoveColumns },
	{ "+Row", "Split a row of the selected patches in two, at the selected control point", ePatchCommand::InsertRows },
	{ "-Row", "Merge two rows of the selected patches, at the selected control point", ePatchCommand::RemoveRows },
};

struct TextureButtonInfo
{
	const char* pLabel;
	const char* pToolTip;
	TextureCommand Command;
	bool bSeparated;
};

const TextureButtonInfo scTextureButtons[] = {
	{ "\u25C0 U",
	  "Slide the texture back along U by one grid step (Alt+Left)",
	  { eTextureCommand::Shift, Vec2f(-1.0f, 0.0f) },
	  false },
	{ "U \u25B6",
	  "Slide the texture along U by one grid step (Alt+Right)",
	  { eTextureCommand::Shift, Vec2f(1.0f, 0.0f) },
	  false },
	{ "\u25B2 V",
	  "Slide the texture back along V by one grid step, up on walls (Alt+Up)",
	  { eTextureCommand::Shift, Vec2f(0.0f, -1.0f) },
	  false },
	{ "V \u25BC",
	  "Slide the texture along V by one grid step, down on walls (Alt+Down)",
	  { eTextureCommand::Shift, Vec2f(0.0f, 1.0f) },
	  false },
	{ "\u00F72", "Halve the size of the texture (Alt+[)", { eTextureCommand::Scale, Vec2f(0.5f) }, true },
	{ "\u00D72", "Double the size of the texture (Alt+])", { eTextureCommand::Scale, Vec2f(2.0f) }, false },
	{ "\u21BA 15\u00B0",
	  "Turn the texture 15 degrees one way (Alt+,)",
	  { eTextureCommand::Rotate, Vec2f(-15.0f, 0.0f) },
	  true },
	{ "\u21BB 15\u00B0",
	  "Turn the texture 15 degrees the other way (Alt+.)",
	  { eTextureCommand::Rotate, Vec2f(15.0f, 0.0f) },
	  false },
	{ "Reset", "Put the texture back to how a new box of this size lays it out", { eTextureCommand::Reset }, true },
	{ "World Align",
	  "Line the texture up with the world grid, so it carries on across neighbouring brushes",
	  { eTextureCommand::WorldAlign },
	  false },
};

struct PatchMappingInfo
{
	const char* pName;
	const char* pDescription;
};

constexpr PatchMappingInfo scPatchMappings[] = {
	{ "Natural", "Follow the surface" },
	{ "World", "Projected from world space along one axis" },
	{ "Fit", "Stretched a set number of times across the patch" },
};

static_assert(std::size(scPatchMappings) == static_cast<size_t>(ePatchTextureMapping::Count));

constexpr const char* scPatchAxisNames[] = { "Auto", "X", "Y", "Z" };

static_assert(std::size(scPatchAxisNames) == static_cast<size_t>(ePatchTextureAxis::Count));

constexpr int scDialogBorder = 10;
constexpr int scDescriptionIndent = 22;
constexpr int scDescriptionWrapWidth = 320;

bool ShowPatchTextureDialog(wxWindow* parent, PatchTextureSettings& settings)
{
	wxDialog dialog(parent, wxID_ANY, "Patch Texture");

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	sizer->Add(new wxStaticText(&dialog, wxID_ANY, "Lay out the texture on the selected patches:"),
			   wxSizerFlags().Border(wxALL, scDialogBorder));

	wxRadioButton* mapping_buttons[static_cast<uint32>(ePatchTextureMapping::Count)] = {};

	for (uint32 i = 0; i < static_cast<uint32>(ePatchTextureMapping::Count); i++) {
		long style = 0;

		if (i == 0) {
			style = wxRB_GROUP;
		}

		mapping_buttons[i] = new wxRadioButton(&dialog, wxID_ANY, scPatchMappings[i].pName, wxDefaultPosition,
											   wxDefaultSize, style);
		mapping_buttons[i]->SetValue(i == static_cast<uint32>(settings.Mapping));

		wxStaticText* description = new wxStaticText(&dialog, wxID_ANY, scPatchMappings[i].pDescription);
		description->Wrap(scDescriptionWrapWidth);
		description->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

		sizer->Add(mapping_buttons[i], wxSizerFlags().Border(wxLEFT | wxRIGHT, scDialogBorder));
		wxBoxSizer* description_row = new wxBoxSizer(wxHORIZONTAL);
		description_row->AddSpacer(scDialogBorder + scDescriptionIndent);
		description_row->Add(description);
		description_row->AddSpacer(scDialogBorder);

		sizer->Add(description_row);
		sizer->AddSpacer(6);
	}

	wxFlexGridSizer* options = new wxFlexGridSizer(2, 6, 10);

	wxSpinCtrlDouble* scale_field = new wxSpinCtrlDouble(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
														 wxSP_ARROW_KEYS, 0.01, 1000.0, settings.Scale, 0.25);
	scale_field->SetDigits(2);
	scale_field->SetToolTip("Units of the surface per repeat of the texture. Brushes use 2");

	wxChoice* axis_choice = new wxChoice(&dialog, wxID_ANY);

	for (const char* name : scPatchAxisNames) {
		axis_choice->Append(name);
	}

	axis_choice->SetSelection(static_cast<int32>(settings.Axis));
	axis_choice->SetToolTip("The world axis the texture is projected along. Auto picks the one the patch faces most");

	wxSpinCtrlDouble* repeats_u_field = new wxSpinCtrlDouble(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
															 wxSP_ARROW_KEYS, 0.01, 1000.0, settings.Repeats.X, 0.5);
	repeats_u_field->SetDigits(2);
	repeats_u_field->SetToolTip("How many times the texture repeats across the patch's columns");

	wxSpinCtrlDouble* repeats_v_field = new wxSpinCtrlDouble(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
															 wxSP_ARROW_KEYS, 0.01, 1000.0, settings.Repeats.Y, 0.5);
	repeats_v_field->SetDigits(2);
	repeats_v_field->SetToolTip("How many times the texture repeats across the patch's rows");

	options->Add(new wxStaticText(&dialog, wxID_ANY, "Scale"), wxSizerFlags().CenterVertical());
	options->Add(scale_field);
	options->Add(new wxStaticText(&dialog, wxID_ANY, "Axis"), wxSizerFlags().CenterVertical());
	options->Add(axis_choice);
	options->Add(new wxStaticText(&dialog, wxID_ANY, "Repeats U"), wxSizerFlags().CenterVertical());
	options->Add(repeats_u_field);
	options->Add(new wxStaticText(&dialog, wxID_ANY, "Repeats V"), wxSizerFlags().CenterVertical());
	options->Add(repeats_v_field);

	sizer->Add(options, wxSizerFlags().Border(wxALL, scDialogBorder));

	const auto get_mapping = [&]()
	{
		for (uint32 i = 0; i < static_cast<uint32>(ePatchTextureMapping::Count); i++) {
			if (mapping_buttons[i]->GetValue()) {
				return static_cast<ePatchTextureMapping>(i);
			}
		}

		return ePatchTextureMapping::Natural;
	};

	const auto update_fields = [&]()
	{
		const ePatchTextureMapping mapping = get_mapping();

		scale_field->Enable(mapping != ePatchTextureMapping::Fit);
		axis_choice->Enable(mapping == ePatchTextureMapping::World);
		repeats_u_field->Enable(mapping == ePatchTextureMapping::Fit);
		repeats_v_field->Enable(mapping == ePatchTextureMapping::Fit);
	};

	for (wxRadioButton* button : mapping_buttons) {
		button->Bind(wxEVT_RADIOBUTTON, [&](wxCommandEvent&) { update_fields(); });
	}

	update_fields();

	wxStdDialogButtonSizer* buttons = new wxStdDialogButtonSizer();
	wxButton* finish_button = new wxButton(&dialog, wxID_OK, "Finish");
	finish_button->SetDefault();
	buttons->AddButton(finish_button);
	buttons->AddButton(new wxButton(&dialog, wxID_CANCEL, "Cancel"));
	buttons->Realize();

	sizer->Add(buttons, wxSizerFlags().Expand().Border(wxALL, scDialogBorder));

	dialog.SetSizerAndFit(sizer);
	dialog.CentreOnParent();

	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	settings.Mapping = get_mapping();
	settings.Scale = static_cast<float32>(scale_field->GetValue());
	settings.Repeats = Vec2f(static_cast<float32>(repeats_u_field->GetValue()),
							 static_cast<float32>(repeats_v_field->GetValue()));

	if (axis_choice->GetSelection() != wxNOT_FOUND) {
		settings.Axis = static_cast<ePatchTextureAxis>(axis_choice->GetSelection());
	}

	return true;
}

constexpr const char* scDataFilterNames[] = {
	"All", "Probe Volumes", "Reflection Probes", "Spawn Points", "Volumes", "Lights", "nvbrush", "dbound",
};

static_assert(std::size(scDataFilterNames) == static_cast<size_t>(eDataFilter::Count));

OutlineToggleButton* MakeButton(wxWindow* parent, const ToolButtonInfo& info)
{
	wxBitmap icon;

	if (info.pIconPath != nullptr) {
		const String icon_path = FilesystemIO::ResolvePath(info.pIconPath);

		// wx shows an error dialog for images it can't load, so check the icon is there first
		if (FilesystemIO::FileExists(icon_path)) {
			icon.LoadFile(wxString::FromUTF8(icon_path.CStr()), wxBITMAP_TYPE_PNG);
		}

		if (!icon.IsOk()) {
			LogWarning(LC_CORE, "Could not load editor icon '{}'", icon_path);
		}
	}

	OutlineToggleButton* button = new OutlineToggleButton(parent, icon, info.pName);
	button->SetToolTip(info.pToolTip);

	return button;
}

} // namespace


EditorToolbar::EditorToolbar(wxWindow* parent, TopViewCanvas* canvas) : mpParent(parent), mpCanvas(canvas)
{
	mpSizer = new wxBoxSizer(wxVERTICAL);
	mpViewRow = new wxWrapSizer(wxHORIZONTAL);
	mpToolRow = new wxWrapSizer(wxHORIZONTAL);
	mpSizer->Add(mpViewRow, wxSizerFlags().Expand());
	mpSizer->Add(mpToolRow, wxSizerFlags().Expand());

	{
		wxBoxSizer* group = AddGroup(mpViewRow, false);

		mpSimulateButton = MakeButton(parent, scSimulateButton);
		mpSimulateButton->Bind(wxEVT_TOGGLEBUTTON, [](wxCommandEvent&)
							   { thread::PostToGame([] { gEditor->SetSimulating(!gEditor->IsSimulationMode()); }); });

		group->Add(mpSimulateButton, wxSizerFlags().CenterVertical().Border(wxALL, scItemBorder));
	}

	{
		wxBoxSizer* group = AddGroup(mpViewRow, true);

		mpPerspectiveButton = new OutlineToggleButton(parent, wxBitmap(), "3D");
		mpPerspectiveButton->SetButtonSize(wxSize(scLetterButtonSize, scLetterButtonSize));
		mpPerspectiveButton->SetToolTip("Fly around the level in 3D (Ctrl+1)");
		mpPerspectiveButton->Bind(wxEVT_TOGGLEBUTTON, [](wxCommandEvent&)
								  { thread::PostToGame([] { gEditor->SetView(eEditorView::Perspective); }); });

		mpTopButton = new OutlineToggleButton(parent, wxBitmap(), "2D");
		mpTopButton->SetButtonSize(wxSize(scLetterButtonSize, scLetterButtonSize));
		mpTopButton->SetToolTip("Top, front and side wireframe views for laying out brushes (Ctrl+2)");
		mpTopButton->Bind(wxEVT_TOGGLEBUTTON,
						  [](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetView(eEditorView::Top); }); });

		mpSplitButton = new OutlineToggleButton(parent, wxBitmap(), "Split");
		mpSplitButton->SetToolTip("The 3D view and the 2D views together, with a draggable divider (Ctrl+3)");
		mpSplitButton->Bind(wxEVT_TOGGLEBUTTON,
							[](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetView(eEditorView::Split); }); });

		group->Add(mpPerspectiveButton, wxSizerFlags().Expand().Border(wxALL, scItemBorder));
		group->Add(mpTopButton, wxSizerFlags().Expand().Border(wxALL, scItemBorder));
		group->Add(mpSplitButton, wxSizerFlags().Expand().Border(wxALL, scItemBorder));
	}

	{
		wxBoxSizer* group = AddGroup(mpViewRow, true);

		mpVisButton = new OutlineToggleButton(parent, wxBitmap(), "V");
		mpVisButton->SetButtonSize(wxSize(scLetterButtonSize, scLetterButtonSize));
		mpVisButton->SetToolTip("Select and edit visible brushes and models");
		mpVisButton->Bind(wxEVT_TOGGLEBUTTON,
						  [](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetMode(eEditorMode::Vis); }); });

		mpDataButton = new OutlineToggleButton(parent, wxBitmap(), "D");
		mpDataButton->SetButtonSize(wxSize(scLetterButtonSize, scLetterButtonSize));
		mpDataButton->SetToolTip("Select and edit probe volumes, reflection probes, volumes, lights and spawn points");
		mpDataButton->Bind(wxEVT_TOGGLEBUTTON,
						   [](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetMode(eEditorMode::Data); }); });

		mpDataFilterChoice = new wxChoice(parent, wxID_ANY);

		for (const char* name : scDataFilterNames) {
			mpDataFilterChoice->Append(name);
		}

		mpDataFilterChoice->SetSelection(0);
		mpDataFilterChoice->SetToolTip("Which data brushes can be selected");
		mpDataFilterChoice->Bind(
			wxEVT_CHOICE,
			[this](wxCommandEvent&)
			{
				const int32 selection = mpDataFilterChoice->GetSelection();

				if (selection != wxNOT_FOUND) {
					thread::PostToGame([selection] { gEditor->SetDataFilter(static_cast<eDataFilter>(selection)); });
				}
			});

		wxButton* fit_bounds_button = new wxButton(parent, wxID_ANY, "Fit Bounds", wxDefaultPosition, wxDefaultSize,
												   wxBU_EXACTFIT);
		fit_bounds_button->SetToolTip("Replace the map bounds (dbound) with six boxes sealing the level, each one "
									  "touching the edge of the level geometry");
		fit_bounds_button->Bind(wxEVT_BUTTON,
								[](wxCommandEvent&) { thread::PostToGame([] { gEditor->FitDBoundsToLevel(); }); });

		group->Add(mpVisButton, wxSizerFlags().Expand().Border(wxALL, scItemBorder));
		group->Add(mpDataButton, wxSizerFlags().Expand().Border(wxALL, scItemBorder));
		group->Add(mpDataFilterChoice, wxSizerFlags().CenterVertical().Border(wxALL, scItemBorder));
		group->Add(fit_bounds_button, wxSizerFlags().CenterVertical().Border(wxALL, scItemBorder));
	}

	{
		mpPlaneGroup = AddGroup(mpToolRow, false);

		mpPlaneChoice = new wxChoice(parent, wxID_ANY);

		for (const char* name : scPlaneNames) {
			mpPlaneChoice->Append(name);
		}

		mpPlaneChoice->SetToolTip("Which way the 2D view looks at the level: top (1), forward along +Z (2), back along "
								  "-Z (3), left along +X (4) or right along -X (5)");
		mpPlaneChoice->Bind(wxEVT_CHOICE,
							[this](wxCommandEvent&)
							{
								const int32 selection = mpPlaneChoice->GetSelection();

								if (selection != wxNOT_FOUND) {
									mpCanvas->SetPlane(static_cast<eViewPlane>(selection));
								}

								mpCanvas->SetFocus();
							});

		mpPlaneGroup->Add(mpPlaneChoice, wxSizerFlags().CenterVertical().Border(wxALL, scItemBorder));
	}

	{
		mpToolGroup = AddGroup(mpToolRow, true);

		for (uint32 i = 0; i < static_cast<uint32>(eEditorTool::Count); i++) {
			OutlineToggleButton* button = MakeButton(parent, scToolButtons[i]);

			const eEditorTool tool = static_cast<eEditorTool>(i);
			button->Bind(wxEVT_TOGGLEBUTTON,
						 [this, tool](wxCommandEvent&)
						 {
							 mpCanvas->SetTool(tool);
							 mpCanvas->SetFocus();
						 });

			mpToolButtons[i] = button;
			mpToolGroup->Add(button, wxSizerFlags().CenterVertical().Border(wxALL, scItemBorder));
		}

		mpSubtractButton = MakeButton(parent, scSubtractButton);
		mpSubtractButton->Bind(wxEVT_TOGGLEBUTTON,
							   [this](wxCommandEvent&)
							   {
								   mpSubtractButton->SetValue(false);
								   mpCanvas->Subtract(wxGetKeyState(WXK_SHIFT));
								   mpCanvas->SetFocus();
							   });

		mpToolGroup->Add(mpSubtractButton, wxSizerFlags().CenterVertical().Border(wxLEFT, scSeparatorBorder));

		mpSnapButton = new wxButton(parent, wxID_ANY, "Snap to Grid", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
		mpSnapButton->SetToolTip("Move the selected brushes so their corners line up with the grid again, even when "
								 "snapping is off (Ctrl+B)");
		mpSnapButton->Enable(false);
		mpSnapButton->Bind(wxEVT_BUTTON,
						   [this](wxCommandEvent&)
						   {
							   thread::PostToGame(
								   []
								   {
									   gEditor->SnapSelectionToGrid();
									   gEditor->NoteTopViewCommand();
								   });
							   mpCanvas->SetFocus();
						   });

		mpToolGroup->Add(mpSnapButton, wxSizerFlags().CenterVertical().Border(wxLEFT, scSeparatorBorder));

		mpHollowButton = new wxButton(parent, wxID_ANY, "Hollow", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
		mpHollowButton->SetToolTip("Turn the selected brushes into rooms, with walls as thick as the grid step");
		mpHollowButton->Enable(false);
		mpHollowButton->Bind(wxEVT_BUTTON,
							 [this](wxCommandEvent&)
							 {
								 thread::PostToGame(
									 []
									 {
										 gEditor->HollowSelection();
										 gEditor->NoteTopViewCommand();
									 });
								 mpCanvas->SetFocus();
							 });

		mpToolGroup->Add(mpHollowButton, wxSizerFlags().CenterVertical().Border(wxLEFT, scItemBorder));
	}

	for (uint32 i = 0; i < scViewPlaneCount; i++) {
		const eViewPlane plane = static_cast<eViewPlane>(i);

		FloatField* base_field = new FloatField(parent, scViewPlaneAxes[i].pBaseLabel, Vec2f(-100000.0f, 100000.0f));
		base_field->SetValue(mBase[i]);
		base_field->SetOnChange(
			[this, i, plane](const float32 value)
			{
				mBase[i] = value;
				mpCanvas->SetCreateVolume(plane, mBase[i], mSize[i]);
			});

		FloatField* size_field = new FloatField(parent, scViewPlaneAxes[i].pSizeLabel, Vec2f(0.05f, 100000.0f));
		size_field->SetValue(mSize[i]);
		size_field->SetOnChange(
			[this, i, plane](const float32 value)
			{
				mSize[i] = value;
				mpCanvas->SetCreateVolume(plane, mBase[i], mSize[i]);
			});

		mpVolumeRows[i] = new wxBoxSizer(wxHORIZONTAL);
		mpVolumeRows[i]->Add(new wxStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL),
							 wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, scSeparatorBorder));
		mpVolumeRows[i]->Add(base_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 4));
		mpVolumeRows[i]->Add(size_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));

		OutlineToggleButton* ground_button = new OutlineToggleButton(parent, wxBitmap(), "Ground");

		ground_button->SetToolTip(wxString::Format("New brushes rest on the furthest surface the view faces that they "
												   "fit on. %s is used over empty space",
												   scViewPlaneAxes[i].pBaseLabel));

		ground_button->SetValue(true);
		ground_button->Bind(wxEVT_TOGGLEBUTTON,
							[this, ground_button](wxCommandEvent&)
							{
								const bool enabled = ground_button->GetValue();

								for (OutlineToggleButton* button : mpGroundButtons) {
									button->SetValue(enabled);
								}

								mpCanvas->SetDropToGround(enabled);
								mpCanvas->SetFocus();
							});

		mpGroundButtons[i] = ground_button;
		mpVolumeRows[i]->Add(ground_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 10));

		mpToolRow->Add(mpVolumeRows[i], wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, scItemBorder));
	}

	{
		FloatField* sides_field = new FloatField(parent, "Sides", Vec2f(3.0f, 128.0f));
		sides_field->SetValue(static_cast<float32>(mArch.Sides));
		sides_field->SetOnChange(
			[this](const float32 value)
			{
				mArch.Sides = static_cast<uint32>(std::lround(value));
				mpCanvas->SetArchSettings(mArch);
			});

		FloatField* wall_field = new FloatField(parent, "Wall", Vec2f(0.01f, 100000.0f));
		wall_field->SetValue(mArch.WallWidth);
		wall_field->SetOnChange(
			[this](const float32 value)
			{
				mArch.WallWidth = value;
				mpCanvas->SetArchSettings(mArch);
			});

		FloatField* arc_field = new FloatField(parent, "Arc", Vec2f(1.0f, 360.0f));
		arc_field->SetValue(mArch.ArcDegrees);
		arc_field->SetOnChange(
			[this](const float32 value)
			{
				mArch.ArcDegrees = value;
				mpCanvas->SetArchSettings(mArch);
			});

		FloatField* start_field = new FloatField(parent, "Start", Vec2f(-360.0f, 360.0f));
		start_field->SetValue(mArch.StartDegrees);
		start_field->SetOnChange(
			[this](const float32 value)
			{
				mArch.StartDegrees = value;
				mpCanvas->SetArchSettings(mArch);
			});

		mpArchRow = new wxBoxSizer(wxHORIZONTAL);
		mpArchRow->Add(new wxStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL),
					   wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, scSeparatorBorder));
		mpArchRow->Add(sides_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 4));
		mpArchRow->Add(wall_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));
		mpArchRow->Add(arc_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));
		mpArchRow->Add(start_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));

		wxButton* build_button = new wxButton(parent, wxID_ANY, "Build", wxDefaultPosition, wxDefaultSize,
											  wxBU_EXACTFIT);
		build_button->SetToolTip("Build the pending arch (Enter)");
		build_button->Bind(wxEVT_BUTTON,
						   [this](wxCommandEvent&)
						   {
							   mpCanvas->CommitArch();
							   mpCanvas->SetFocus();
						   });

		mpArchRow->Add(build_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 10));
		mpToolRow->Add(mpArchRow, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, scItemBorder));

		mpArchSidesField = sides_field;
		mpArchWallField = wall_field;
		mpArchArcField = arc_field;
		mpArchStartField = start_field;

		mpCanvas->SetArchSettings(mArch);
		mpCanvas->SetOnArchChanged(
			[this](const ArchSettings& settings)
			{
				mArch = settings;
				mpArchSidesField->SetValue(static_cast<float32>(settings.Sides));
				mpArchWallField->SetValue(settings.WallWidth);
				mpArchArcField->SetValue(settings.ArcDegrees);
				mpArchStartField->SetValue(settings.StartDegrees);
			});
	}

	{
		wxChoice* primitive_choice = new wxChoice(parent, wxID_ANY);

		for (const char* name : scPatchPrimitiveNames) {
			primitive_choice->Append(name);
		}

		primitive_choice->SetSelection(static_cast<int>(mCurve.Primitive));
		primitive_choice->SetToolTip("The shape a drag creates. Round shapes go around the axis the view looks along");
		primitive_choice->Bind(wxEVT_CHOICE,
							   [this, primitive_choice](wxCommandEvent&)
							   {
								   const int32 selection = primitive_choice->GetSelection();

								   if (selection != wxNOT_FOUND) {
									   mCurve.Primitive = static_cast<ePatchPrimitive>(selection);
									   mpCanvas->SetCurveSettings(mCurve);
								   }

								   mpCanvas->SetFocus();
							   });

		const Vec2f subdivision_range(static_cast<float32>(Patch::scMinSubdivisions),
									  static_cast<float32>(Patch::scMaxSubdivisions));

		FloatField* subdivisions_u_field = new FloatField(parent, "Subdiv U", subdivision_range);
		subdivisions_u_field->SetValue(static_cast<float32>(mCurve.SubdivisionsU));
		subdivisions_u_field->SetOnChange(
			[this](const float32 value)
			{
				mCurve.SubdivisionsU = static_cast<uint32>(std::lround(value));
				mpCanvas->SetCurveSettings(mCurve);
				mpCanvas->RunPatchCommand(ePatchCommand::SetSubdivisions);
			});

		FloatField* subdivisions_v_field = new FloatField(parent, "Subdiv V", subdivision_range);
		subdivisions_v_field->SetValue(static_cast<float32>(mCurve.SubdivisionsV));
		subdivisions_v_field->SetOnChange(
			[this](const float32 value)
			{
				mCurve.SubdivisionsV = static_cast<uint32>(std::lround(value));
				mpCanvas->SetCurveSettings(mCurve);
				mpCanvas->RunPatchCommand(ePatchCommand::SetSubdivisions);
			});

		OutlineToggleButton* smooth_button = new OutlineToggleButton(parent, wxBitmap(), "Smooth");
		smooth_button->SetToolTip("Smooth normals across the patch. Off shows every triangle as a flat facet");
		smooth_button->SetValue(mCurve.bSmooth);
		smooth_button->Bind(wxEVT_TOGGLEBUTTON,
							[this, smooth_button](wxCommandEvent&)
							{
								mCurve.bSmooth = smooth_button->GetValue();
								mpCanvas->SetCurveSettings(mCurve);
								mpCanvas->RunPatchCommand(ePatchCommand::SetSmooth);
								mpCanvas->SetFocus();
							});

		mpCurveRow = new wxBoxSizer(wxHORIZONTAL);
		mpCurveRow->Add(new wxStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL),
						wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, scSeparatorBorder));
		mpCurveRow->Add(primitive_choice, wxSizerFlags().CenterVertical().Border(wxLEFT, 4));
		mpCurveRow->Add(subdivisions_u_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));
		mpCurveRow->Add(subdivisions_v_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));
		mpCurveRow->Add(smooth_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 10));

		FloatField* thickness_field = new FloatField(parent, "Thickness", Vec2f(0.01f, 100.0f));
		thickness_field->SetValue(mCurve.Thickness);
		thickness_field->SetOnChange(
			[this](const float32 value)
			{
				mCurve.Thickness = value;
				mpCanvas->SetCurveSettings(mCurve);

				if (mCurve.Closure == ePatchClosure::Thicken) {
					mpCanvas->RunPatchCommand(ePatchCommand::SetClosure);
				}
			});

		OutlineToggleButton* thicken_button = new OutlineToggleButton(parent, wxBitmap(), "Thicken");
		thicken_button->SetToolTip("Give the selected patches a back face Thickness behind them, joined to them along "
								   "the open edges. It follows the patch as it is edited");
		thicken_button->SetValue(mCurve.Closure == ePatchClosure::Thicken);

		OutlineToggleButton* cap_button = new OutlineToggleButton(parent, wxBitmap(), "Cap");
		cap_button->SetToolTip(
			"Close the open ends of the selected patches: cylinder ends become discs, bevel and "
			"endcap ends are filled out to their square corners. It follows the patch as it is edited");
		cap_button->SetValue(mCurve.Closure == ePatchClosure::Cap);

		const auto set_closure = [this](ePatchClosure closure, bool enabled)
		{
			mCurve.Closure = ePatchClosure::None;

			if (enabled) {
				mCurve.Closure = closure;
			}

			mpCurveThickenButton->SetValue(mCurve.Closure == ePatchClosure::Thicken);
			mpCurveCapButton->SetValue(mCurve.Closure == ePatchClosure::Cap);
			mpCanvas->SetCurveSettings(mCurve);
			mpCanvas->RunPatchCommand(ePatchCommand::SetClosure);
			mpCanvas->SetFocus();
		};

		thicken_button->Bind(wxEVT_TOGGLEBUTTON, [set_closure, thicken_button](wxCommandEvent&)
							 { set_closure(ePatchClosure::Thicken, thicken_button->GetValue()); });
		cap_button->Bind(wxEVT_TOGGLEBUTTON, [set_closure, cap_button](wxCommandEvent&)
						 { set_closure(ePatchClosure::Cap, cap_button->GetValue()); });

		mpCurveRow->Add(thickness_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));
		mpCurveRow->Add(thicken_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 6));
		mpCurveRow->Add(cap_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 6));

		FloatField* weight_field = new FloatField(parent, "Weight", Vec2f(Patch::scMinWeight, 100.0f));
		weight_field->SetValue(mCurve.Weight);
		weight_field->GetLabel()->SetToolTip(
			"How strongly the selected control points pull the curve toward them. Below 1 rounds it off, above 1 "
			"tightens it into the corner");
		weight_field->SetOnChange(
			[this](const float32 value)
			{
				mCurve.Weight = value;
				mpCanvas->SetCurveSettings(mCurve);
				mpCanvas->RunPatchCommand(ePatchCommand::SetWeight);
			});

		wxButton* round_button = new wxButton(parent, wxID_ANY, "Round", wxDefaultPosition, wxDefaultSize,
											  wxBU_EXACTFIT);
		round_button->SetToolTip("Weight the selected control points (or every point of the selected patches) so each "
								 "corner becomes a true circular arc");
		round_button->Bind(wxEVT_BUTTON,
						   [this](wxCommandEvent&)
						   {
							   mpCanvas->RunPatchCommand(ePatchCommand::RoundWeights);
							   mpCanvas->SetFocus();
						   });

		mpCurveRow->Add(weight_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));
		mpCurveRow->Add(round_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 6));

		for (const PatchCommandButtonInfo& info : scPatchCommandButtons) {
			wxButton* button = new wxButton(parent, wxID_ANY, info.pName, wxDefaultPosition, wxDefaultSize,
											wxBU_EXACTFIT);
			button->SetToolTip(info.pToolTip);

			const ePatchCommand command = info.Command;

			button->Bind(wxEVT_BUTTON,
						 [this, command](wxCommandEvent&)
						 {
							 mpCanvas->RunPatchCommand(command);
							 mpCanvas->SetFocus();
						 });

			mpCurveRow->Add(button, wxSizerFlags().CenterVertical().Border(wxLEFT, 6));
		}

		wxButton* texture_button = new wxButton(parent, wxID_ANY, "Texture...", wxDefaultPosition, wxDefaultSize,
												wxBU_EXACTFIT);
		texture_button->SetToolTip("Choose how the texture is laid out on the selected patches: natural, world "
								   "projected or fitted");
		texture_button->Bind(wxEVT_BUTTON,
							 [this](wxCommandEvent&)
							 {
								 PatchTextureSettings settings = mCurve.Texture;

								 if (ShowPatchTextureDialog(mpParent, settings)) {
									 mCurve.Texture = settings;
									 mpCanvas->SetCurveSettings(mCurve);
									 mpCanvas->RunPatchCommand(ePatchCommand::MapTexture);
								 }

								 mpCanvas->SetFocus();
							 });

		mpCurveRow->Add(texture_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 6));

		mpToolRow->Add(mpCurveRow, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, scItemBorder));

		mpFaceRow = new wxBoxSizer(wxHORIZONTAL);
		mpFaceRow->Add(new wxStaticLine(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL),
					   wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, scSeparatorBorder));

		wxStaticText* texture_label = new wxStaticText(parent, wxID_ANY, "Texture");
		texture_label->SetToolTip("Changes the face picked with the Face tool, or every face of the selected brushes "
								  "when no face is picked");
		mpFaceRow->Add(texture_label, wxSizerFlags().CenterVertical().Border(wxLEFT, 4));

		for (const TextureButtonInfo& info : scTextureButtons) {
			wxButton* button = new wxButton(parent, wxID_ANY, wxString::FromUTF8(info.pLabel), wxDefaultPosition,
											wxDefaultSize, wxBU_EXACTFIT);
			button->SetToolTip(info.pToolTip);

			const TextureCommand command = info.Command;

			button->Bind(wxEVT_BUTTON,
						 [this, command](wxCommandEvent&)
						 {
							 mpCanvas->RunTextureCommand(command);
							 mpCanvas->SetFocus();
						 });

			int border = 2;

			if (info.bSeparated) {
				border = 10;
			}

			mpFaceRow->Add(button, wxSizerFlags().CenterVertical().Border(wxLEFT, border));
		}

		mpToolRow->Add(mpFaceRow, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, scItemBorder));

		mpCurveSubdivisionsUField = subdivisions_u_field;
		mpCurveSubdivisionsVField = subdivisions_v_field;
		mpCurveSmoothButton = smooth_button;
		mpCurveThicknessField = thickness_field;
		mpCurveWeightField = weight_field;
		mpCurveThickenButton = thicken_button;
		mpCurveCapButton = cap_button;

		mpCanvas->SetCurveSettings(mCurve);
		mpCanvas->SetOnCurveChanged(
			[this](const CurveSettings& settings)
			{
				mCurve.SubdivisionsU = settings.SubdivisionsU;
				mCurve.SubdivisionsV = settings.SubdivisionsV;
				mCurve.bSmooth = settings.bSmooth;
				mCurve.Closure = settings.Closure;
				mCurve.Thickness = settings.Thickness;
				mCurve.Weight = settings.Weight;
				mpCurveWeightField->SetValue(settings.Weight);
				mpCurveSubdivisionsUField->SetValue(static_cast<float32>(settings.SubdivisionsU));
				mpCurveSubdivisionsVField->SetValue(static_cast<float32>(settings.SubdivisionsV));
				mpCurveSmoothButton->SetValue(settings.bSmooth);
				mpCurveThicknessField->SetValue(settings.Thickness);
				mpCurveThickenButton->SetValue(settings.Closure == ePatchClosure::Thicken);
				mpCurveCapButton->SetValue(settings.Closure == ePatchClosure::Cap);
			});
	}

	{
		mpViewGroup = AddGroup(mpToolRow, true);

		wxButton* fit_button = new wxButton(parent, wxID_ANY, "Fit", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
		fit_button->SetToolTip("Frame every brush (Home)");
		fit_button->Bind(wxEVT_BUTTON,
						 [this](wxCommandEvent&)
						 {
							 mpCanvas->FitAll();
							 mpCanvas->SetFocus();
						 });

		wxButton* hide_button = new wxButton(parent, wxID_ANY, "Hide", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
		hide_button->SetToolTip("Hide the selection from the 2D views, so what is behind it can be selected (H)");
		hide_button->Bind(wxEVT_BUTTON,
						  [this](wxCommandEvent&)
						  {
							  thread::PostToGame(
								  []
								  {
									  gEditor->TopViewHideSelection();
									  gEditor->NoteTopViewCommand();
								  });
							  mpCanvas->SetFocus();
						  });

		mpShowAllButton = new wxButton(parent, wxID_ANY, "Show All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
		mpShowAllButton->SetToolTip("Bring back everything hidden from the 2D views (Shift+H)");
		mpShowAllButton->Enable(false);
		mpShowAllButton->Bind(wxEVT_BUTTON,
							  [this](wxCommandEvent&)
							  {
								  thread::PostToGame(
									  []
									  {
										  gEditor->TopViewShowAll();
										  gEditor->NoteTopViewCommand();
									  });
								  mpCanvas->SetFocus();
							  });

		mpViewGroup->Add(fit_button, wxSizerFlags().CenterVertical().Border(wxALL, scItemBorder));
		mpViewGroup->Add(hide_button, wxSizerFlags().CenterVertical().Border(wxALL, scItemBorder));
		mpViewGroup->Add(mpShowAllButton, wxSizerFlags().CenterVertical().Border(wxALL, scItemBorder));
	}

	mpCanvas->SetOnToolChanged([this](eEditorTool tool) { ShowTool(tool); });
	mpCanvas->SetOnPlaneChanged([this](eViewPlane plane) { ShowPlane(plane); });

	ShowTool(mpCanvas->GetTool());
	ShowPlane(mpCanvas->GetPlane());
	ShowView(eEditorView::Perspective);
}

wxSizer* EditorToolbar::GetSizer() { return mpSizer; }

wxBoxSizer* EditorToolbar::AddGroup(wxWrapSizer* row, bool separated)
{
	wxBoxSizer* group = new wxBoxSizer(wxHORIZONTAL);

	if (separated) {
		group->Add(new wxStaticLine(mpParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL),
				   wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, scSeparatorBorder));
	}

	row->Add(group, wxSizerFlags().Expand().Border(wxTOP | wxBOTTOM, scItemBorder));

	return group;
}

void EditorToolbar::UpdateVisibility()
{
	mpSizer->Show(mpToolRow, mbShows2D);

	for (uint32 i = 0; i < scViewPlaneCount; i++) {
		mpToolRow->Show(mpVolumeRows[i], mbShows2D && i == static_cast<uint32>(mPlane), true);
	}

	mpToolRow->Show(mpArchRow, mbShows2D && mTool == eEditorTool::Arch, true);
	mpToolRow->Show(mpCurveRow, mbShows2D && mTool == eEditorTool::Curve, true);
	mpToolRow->Show(mpFaceRow, mbShows2D && mTool == eEditorTool::Face, true);

	mpParent->Layout();
}

void EditorToolbar::ShowView(eEditorView view)
{
	mpPerspectiveButton->SetValue(view == eEditorView::Perspective);
	mpTopButton->SetValue(view == eEditorView::Top);
	mpSplitButton->SetValue(view == eEditorView::Split);

	mbShows2D = (view != eEditorView::Perspective);

	UpdateVisibility();
}

void EditorToolbar::ShowMode(eEditorMode mode, eDataFilter filter, uint32 available_tools)
{
	const bool data_mode = (mode == eEditorMode::Data);

	mpVisButton->SetValue(!data_mode);
	mpDataButton->SetValue(data_mode);

	mpDataFilterChoice->SetSelection(static_cast<int32>(filter));
	mpDataFilterChoice->Enable(data_mode);

	for (uint32 i = 0; i < static_cast<uint32>(eEditorTool::Count); i++) {
		mpToolButtons[i]->Enable((available_tools & (1u << i)) != 0);
	}

	mpSubtractButton->Enable(!data_mode);
}

void EditorToolbar::ShowSimulating(bool simulating) { mpSimulateButton->SetValue(simulating); }

void EditorToolbar::ShowTool(eEditorTool tool)
{
	mTool = tool;

	for (uint32 i = 0; i < static_cast<uint32>(eEditorTool::Count); i++) {
		mpToolButtons[i]->SetValue(i == static_cast<uint32>(tool));
	}

	UpdateVisibility();
}

void EditorToolbar::ShowPlane(eViewPlane plane)
{
	mPlane = plane;

	mpPlaneChoice->SetSelection(static_cast<int32>(plane));

	UpdateVisibility();
}

void EditorToolbar::ApplyState(const TopViewState& state)
{
	bool has_selection = false;

	for (const TopViewBrush& brush : state.Brushes) {
		if (brush.bSelected) {
			has_selection = true;
			break;
		}
	}

	if (has_selection != mbShownHasSelection) {
		mbShownHasSelection = has_selection;
		mpSnapButton->Enable(has_selection);
		mpHollowButton->Enable(has_selection);
	}

	if (state.HiddenCount == mShownHiddenCount) {
		return;
	}

	mShownHiddenCount = state.HiddenCount;

	if (state.HiddenCount > 0) {
		mpShowAllButton->SetLabel(wxString::Format("Show All (%u)", state.HiddenCount));
	}
	else {
		mpShowAllButton->SetLabel("Show All");
	}

	mpShowAllButton->Enable(state.HiddenCount > 0);
	mpParent->Layout();
}

} // namespace fx::editor

#endif

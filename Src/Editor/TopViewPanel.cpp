#include "TopViewPanel.hpp"

#ifdef FX_IS_EDITOR

#include "Common.hpp"
#include "EditorThread.hpp"
#include "OutlineToggleButton.hpp"
#include "RaptorEditor.hpp"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/wrapsizer.h>

#include <Engine.hpp>
#include <Math/Vec2.hpp>

namespace fx::editor {

namespace {

const wxColour scPanelColour(30, 30, 30);
const wxColour scStatusColour(170, 175, 185);

constexpr int scGroupSpacing = 14;

struct ToolInfo
{
	const char* pLabel;
	const char* pToolTip;
};

constexpr ToolInfo scTools[] = {
	{ "Select", "Select and move brushes (T)" },
	{ "Face", "Drag a brush edge to move its face (F)" },
	{ "Vertex", "Drag the corners of the selected brushes (V)" },
	{ "Create", "Drag to create a brush (B)" },
	{ "Rotate", "Rotate the selected brushes (R)" },
};

static_assert(std::size(scTools) == static_cast<size_t>(eTopViewTool::Count));

constexpr const char* scPlaneToolTips[scViewPlaneCount] = {
	"Look down on the level (1)",
	"Look at the level from the front, along +Z (2)",
	"Look at the level from the back, along -Z (3)",
	"Look at the level from the left, along +X (4)",
	"Look at the level from the right, along -X (5)",
};

wxBoxSizer* WrapItem(wxWindow* window)
{
	wxBoxSizer* wrapper = new wxBoxSizer(wxHORIZONTAL);
	wrapper->Add(window, wxSizerFlags().CenterVertical().Border(wxALL, 2));

	return wrapper;
}

} // namespace


TopViewPanel::TopViewPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	SetBackgroundColour(scPanelColour);

	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);
	mpToolbar = new wxWrapSizer(wxHORIZONTAL);

	mpCanvas = new TopViewCanvas(this);

	for (uint32 i = 0; i < scViewPlaneCount; i++) {
		OutlineToggleButton* button = new OutlineToggleButton(this, wxBitmap(), scViewPlaneAxes[i].pName);
		button->SetToolTip(scPlaneToolTips[i]);

		const eViewPlane plane = static_cast<eViewPlane>(i);
		button->Bind(wxEVT_TOGGLEBUTTON, [this, plane](wxCommandEvent&) { mpCanvas->SetPlane(plane); });

		mpPlaneButtons[i] = button;
		mpToolbar->Add(WrapItem(button), wxSizerFlags());
	}

	mpToolbar->AddSpacer(scGroupSpacing);

	for (uint32 i = 0; i < static_cast<uint32>(eTopViewTool::Count); i++) {
		OutlineToggleButton* button = new OutlineToggleButton(this, wxBitmap(), scTools[i].pLabel);
		button->SetToolTip(scTools[i].pToolTip);

		const eTopViewTool tool = static_cast<eTopViewTool>(i);
		button->Bind(wxEVT_TOGGLEBUTTON, [this, tool](wxCommandEvent&) { mpCanvas->SetTool(tool); });

		mpToolButtons[i] = button;
		mpToolbar->Add(WrapItem(button), wxSizerFlags());
	}

	mpToolbar->AddSpacer(scGroupSpacing);

	for (uint32 i = 0; i < scViewPlaneCount; i++) {
		const eViewPlane plane = static_cast<eViewPlane>(i);

		FloatField* base_field = new FloatField(this, scViewPlaneAxes[i].pBaseLabel, Vec2f(-100000.0f, 100000.0f));
		base_field->SetValue(mBase[i]);
		base_field->SetOnChange(
			[this, i, plane](const float32 value)
			{
				mBase[i] = value;
				mpCanvas->SetCreateVolume(plane, mBase[i], mSize[i]);
			});

		FloatField* size_field = new FloatField(this, scViewPlaneAxes[i].pSizeLabel, Vec2f(0.05f, 100000.0f));
		size_field->SetValue(mSize[i]);
		size_field->SetOnChange(
			[this, i, plane](const float32 value)
			{
				mSize[i] = value;
				mpCanvas->SetCreateVolume(plane, mBase[i], mSize[i]);
			});

		mpVolumeRows[i] = new wxBoxSizer(wxHORIZONTAL);
		mpVolumeRows[i]->Add(base_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 4));
		mpVolumeRows[i]->Add(size_field->GetSizer(), wxSizerFlags().CenterVertical().Border(wxLEFT, 10));

		if (plane == eViewPlane::Top) {
			OutlineToggleButton* ground_button = new OutlineToggleButton(this, wxBitmap(), "Ground");
			ground_button->SetToolTip(
				"New brushes rest on the lowest surface under them that they fit on. Base Y is used over empty space");
			ground_button->SetValue(true);
			ground_button->Bind(wxEVT_TOGGLEBUTTON,
								[this, ground_button](wxCommandEvent&)
								{
									mpCanvas->SetDropToGround(ground_button->GetValue());
									mpCanvas->SetFocus();
								});

			mpVolumeRows[i]->Add(ground_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 10));
		}

		mpToolbar->Add(mpVolumeRows[i], wxSizerFlags().CenterVertical().Border(wxTOP | wxBOTTOM, 2));
	}

	wxButton* fit_button = new wxButton(this, wxID_ANY, "Fit", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	fit_button->SetToolTip("Frame every brush (Home)");
	fit_button->Bind(wxEVT_BUTTON,
					 [this](wxCommandEvent&)
					 {
						 mpCanvas->FitAll();
						 mpCanvas->SetFocus();
					 });
	wxButton* hide_button = new wxButton(this, wxID_ANY, "Hide", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
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

	mpShowAllButton = new wxButton(this, wxID_ANY, "Show All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
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

	wxBoxSizer* fit_row = new wxBoxSizer(wxHORIZONTAL);
	fit_row->Add(fit_button, wxSizerFlags().CenterVertical().Border(wxALL, 4));
	fit_row->Add(hide_button, wxSizerFlags().CenterVertical().Border(wxALL, 4));
	fit_row->Add(mpShowAllButton, wxSizerFlags().CenterVertical().Border(wxALL, 4));
	mpToolbar->Add(fit_row, wxSizerFlags().CenterVertical());

	root_sizer->Add(mpToolbar, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, 2));
	root_sizer->Add(mpCanvas, wxSizerFlags(1).Expand());

	mpStatus = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
								wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
	mpStatus->SetForegroundColour(scStatusColour);
	root_sizer->Add(mpStatus, wxSizerFlags().Expand().Border(wxALL, 4));

	SetSizer(root_sizer);

	mpCanvas->SetOnToolChanged([this](eTopViewTool tool) { ShowTool(tool); });
	mpCanvas->SetOnPlaneChanged([this](eViewPlane plane) { ShowPlane(plane); });
	mpCanvas->SetOnStatus([this](const wxString& text) { mpStatus->SetLabel(text); });

	ShowTool(mpCanvas->GetTool());
	ShowPlane(mpCanvas->GetPlane());
}

void TopViewPanel::ShowTool(eTopViewTool tool)
{
	for (uint32 i = 0; i < static_cast<uint32>(eTopViewTool::Count); i++) {
		mpToolButtons[i]->SetValue(i == static_cast<uint32>(tool));
	}
}

void TopViewPanel::ShowPlane(eViewPlane plane)
{
	for (uint32 i = 0; i < scViewPlaneCount; i++) {
		const bool active = (i == static_cast<uint32>(plane));

		mpPlaneButtons[i]->SetValue(active);
		mpToolbar->Show(mpVolumeRows[i], active, true);
	}

	Layout();
}

void TopViewPanel::ApplyState(const TopViewState& state)
{
	mpToolButtons[static_cast<uint32>(eTopViewTool::Face)]->Enable(state.bCanFace);
	mpToolButtons[static_cast<uint32>(eTopViewTool::Vertex)]->Enable(state.bCanFace);
	mpToolButtons[static_cast<uint32>(eTopViewTool::Create)]->Enable(state.bCanCreate);
	mpToolButtons[static_cast<uint32>(eTopViewTool::Rotate)]->Enable(state.bCanRotate);

	if (state.HiddenCount != mShownHiddenCount) {
		mShownHiddenCount = state.HiddenCount;

		if (state.HiddenCount > 0) {
			mpShowAllButton->SetLabel(wxString::Format("Show All (%u)", state.HiddenCount));
		}
		else {
			mpShowAllButton->SetLabel("Show All");
		}

		mpShowAllButton->Enable(state.HiddenCount > 0);
		Layout();
	}

	mpCanvas->ApplyState(state);
}

} // namespace fx::editor

#endif

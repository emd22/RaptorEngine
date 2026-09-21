#include "EditorFrame.hpp"

#include "EditorViewport.hpp"
#include "ObjectPropertiesPanel.hpp"

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/tglbtn.h>

#include <Controls.hpp>

namespace fx::editor {

static constexpr int32 scObjectPropertyPanelWidth = 240;

static constexpr const char* scToolNames[] = { "Transform", "Face", "Rotate" };

static_assert(std::size(scToolNames) == static_cast<size_t>(eEditorTool::Count));

EditorFrame::EditorFrame(const wxString& title, const wxSize& viewport_size) : wxFrame(nullptr, wxID_ANY, title)
{
	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

	// Tool bar
	wxBoxSizer* tool_sizer = new wxBoxSizer(wxHORIZONTAL);

	for (uint32 i = 0; i < static_cast<uint32>(eEditorTool::Count); i++) {
		wxToggleButton* button = new wxToggleButton(root, wxID_ANY, scToolNames[i]);

		const eEditorTool tool = static_cast<eEditorTool>(i);
		button->Bind(wxEVT_TOGGLEBUTTON, [this, tool](wxCommandEvent&) { SelectTool(tool); });

		tool_sizer->Add(button, wxSizerFlags().Border(wxRIGHT, 4));
		mToolButtons.Insert(button);
	}

	root_sizer->Add(tool_sizer, wxSizerFlags().Border(wxALL, 6));

	// Viewport and component panel
	wxBoxSizer* body_sizer = new wxBoxSizer(wxHORIZONTAL);

	mpViewport = new EditorViewport(root, viewport_size);
	mpViewport->SetMinSize(viewport_size);
	body_sizer->Add(mpViewport, wxSizerFlags(1).Expand());

	mpComponentPanel = new ObjectPropertiesPanel(root);
	mpComponentPanel->SetMinSize(wxSize(scObjectPropertyPanelWidth, -1));
	body_sizer->Add(mpComponentPanel, wxSizerFlags().Expand());

	root_sizer->Add(body_sizer, wxSizerFlags(1).Expand());

	root->SetSizer(root_sizer);

	// Size the frame so the viewport starts at the requested size, then let it shrink
	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizerAndFit(frame_sizer);

	mpViewport->SetMinSize(wxSize(64, 64));
	SetMinSize(wxDefaultSize);

	SelectTool(eEditorTool::Transform);

	Bind(wxEVT_CLOSE_WINDOW, &EditorFrame::OnClose, this);
	Bind(wxEVT_ACTIVATE, &EditorFrame::OnActivate, this);
}

void EditorFrame::SelectTool(eEditorTool tool)
{
	mSelectedTool = tool;

	// Only one tool at a time, and clicking the selected one again keeps it selected
	for (uint32 i = 0; i < mToolButtons.Size; i++) {
		mToolButtons[i]->SetValue(i == static_cast<uint32>(tool));
	}

	// Give the keyboard back to the viewport
	if (mpViewport != nullptr) {
		mpViewport->SetFocus();
	}
}

void EditorFrame::OnClose(wxCloseEvent& event)
{
	// The renderer still presents to the viewport, so the frame is destroyed in editor::Shutdown() once the game has
	// shut down. Not skipping the event keeps wxWidgets from destroying it now.
	mbCloseRequested = true;
}

void EditorFrame::OnActivate(wxActivateEvent& event)
{
	// Don't keep the cursor locked while another app is in front
	if (!event.GetActive() && ControlManager::IsMouseLocked()) {
		ControlManager::ReleaseMouse();
	}

	event.Skip();
}

} // namespace fx::editor

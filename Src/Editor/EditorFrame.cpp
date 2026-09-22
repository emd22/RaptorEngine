#include "EditorFrame.hpp"

#include "EditorViewport.hpp"
#include "ObjectPropertiesPanel.hpp"

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/tglbtn.h>

#include <Controls.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>

namespace fx::editor {

static constexpr int32 scObjectPropertyPanelWidth = 240;

struct ToolButtonInfo
{
	/// Shown as the tool tip, and as the label if the icon can't be loaded
	const char* pName;
	const char* pIconPath;
};

static constexpr ToolButtonInfo scToolButtons[] = {
	{ "Simulate", "Textures/editor/simulate.png" },
	{ "Transform", "Textures/editor/move.png" },
	{ "Face", "Textures/editor/face.png" },
	{ "Rotate", "Textures/editor/rotate.png" },
};

static_assert(std::size(scToolButtons) == static_cast<size_t>(eEditorTool::Count));

/// Creates an icon button for the tool, or a text button if its icon can't be loaded
static wxToggleButton* MakeToolButton(wxWindow* parent, const ToolButtonInfo& info)
{
	const std::string icon_path = FilesystemIO::ResolvePath(info.pIconPath);
	const wxBitmap icon(wxString::FromUTF8(icon_path), wxBITMAP_TYPE_PNG);

	wxToggleButton* button = nullptr;

	if (icon.IsOk()) {
		button = new wxBitmapToggleButton(parent, wxID_ANY, icon);
	}
	else {
		LogWarning(LC_CORE, "Could not load editor icon '{}'", icon_path);
		button = new wxToggleButton(parent, wxID_ANY, info.pName);
	}

	button->SetToolTip(info.pName);

	return button;
}


EditorFrame::EditorFrame(const wxString& title, const wxSize& viewport_size) : wxFrame(nullptr, wxID_ANY, title)
{
	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

	// Top bar
	wxBoxSizer* top_bar = new wxBoxSizer(wxHORIZONTAL);

	wxButton* reload_world = new wxButton(this, wxID_ANY, "Reload");
	top_bar->AddStretchSpacer();
	top_bar->Add(reload_world);

	// Tools
	wxBoxSizer* tool_sizer = new wxBoxSizer(wxVERTICAL);

	for (uint32 i = 0; i < static_cast<uint32>(eEditorTool::Count); i++) {
		wxToggleButton* button = MakeToolButton(root, scToolButtons[i]);

		const eEditorTool tool = static_cast<eEditorTool>(i);
		button->Bind(wxEVT_TOGGLEBUTTON, [this, tool](wxCommandEvent&) { SetEditorTool(tool); });

		tool_sizer->Add(button, wxSizerFlags().Border(wxALL, 2));
		mToolButtons.Insert(button);
	}

	// root_sizer->Add(tool_sizer, wxSizerFlags().Border(wxALL, 6));

	root_sizer->Add(top_bar, wxSizerFlags().Border(wxALL, 2));

	// Viewport and component panel
	wxBoxSizer* body_sizer = new wxBoxSizer(wxHORIZONTAL);

	body_sizer->Add(tool_sizer, wxSizerFlags().Border(wxALL, 6));

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

	SetEditorTool(eEditorTool::Transform);

	Bind(wxEVT_CLOSE_WINDOW, &EditorFrame::OnClose, this);
	Bind(wxEVT_ACTIVATE, &EditorFrame::OnActivate, this);
}

void EditorFrame::SetEditorTool(const eEditorTool tool)
{
	mSelectedTool = tool;

	// Only one tool at a time, and clicking the selected one again keeps it selected
	for (uint32 i = 0; i < mToolButtons.Size; i++) {
		mToolButtons[i]->SetValue(i == static_cast<uint32>(tool));
	}

	// Transfer the changed editor tool info back to the script
	if (gSelectedEditorMode != nullptr) {
		auto set_tool = gSelectedEditorMode->pScript->GetFunction<void (*)(eEditorTool)>("__set_editor_tool");

		if (set_tool != nullptr) {
			set_tool(tool);
		}
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

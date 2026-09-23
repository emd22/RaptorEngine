#include "EditorFrame.hpp"

#include "EditorApp.hpp"
#include "EditorViewport.hpp"
#include "ObjectListWindow.hpp"
#include "ObjectPropertiesPanel.hpp"
#include "WorldPropertiesPanel.hpp"

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/menu.h>
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
	{ "Create", "Textures/editor/create.png" },
	{ "Clip", "Textures/editor/clip.png" },
	{ "Set Material", "Textures/editor/set_material.png" },
};

static_assert(std::size(scToolButtons) == static_cast<size_t>(eEditorTool::Count));

static const wxColor scSelectedColor = wxColor(168, 50, 50);

/// Creates an icon button for the tool, or a text button if its icon can't be loaded
static wxToggleButton* MakeToolButton(wxWindow* parent, const ToolButtonInfo& info)
{
	const std::string icon_path = FilesystemIO::ResolvePath(info.pIconPath);

	// wx shows an error dialog for images it can't load, so check the icon is there first
	wxBitmap icon;
	if (FilesystemIO::FileExists(icon_path)) {
		icon.LoadFile(wxString::FromUTF8(icon_path), wxBITMAP_TYPE_PNG);
	}

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
	// Top bar
	wxMenuBar* menu_bar = new wxMenuBar;

	wxMenu* file_menu = new wxMenu;

	wxMenuItem* reload_world_item = file_menu->Append(wxID_ANY, "Reload World", "Reloads the world from disk");
	wxMenuItem* reload_prototype_item = file_menu->Append(wxID_ANY, "Reload Prototype",
														  "Reloads prototype geometry from disk");
	wxMenuItem* reload_scripts_item = file_menu->Append(wxID_ANY, "Reload Scripts", "Reloads all loaded scripts");

	menu_bar->Append(file_menu, "&File");

	wxMenu* window_menu = new wxMenu;
	wxMenuItem* object_list_item = window_menu->Append(wxID_ANY, "Open Object List",
													   "View all objects currently in ObjectManager");

	menu_bar->Append(window_menu, "&Object");

	SetMenuBar(menu_bar);

	Bind(wxEVT_MENU, [](wxCommandEvent&) { InvokeReloadHandler(eReloadTarget::World); }, reload_world_item->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { InvokeReloadHandler(eReloadTarget::Prototype); },
		reload_prototype_item->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { InvokeReloadHandler(eReloadTarget::Scripts); }, reload_scripts_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowObjectListWindow(); }, object_list_item->GetId());

	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

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

	// Viewport and component panel
	wxBoxSizer* body_sizer = new wxBoxSizer(wxHORIZONTAL);

	body_sizer->Add(tool_sizer, wxSizerFlags().Border(wxALL, 6));

	mpViewport = new EditorViewport(root, viewport_size);
	mpViewport->SetMinSize(viewport_size);
	body_sizer->Add(mpViewport, wxSizerFlags(1).Expand());


	wxBoxSizer* component_panel = new wxBoxSizer(wxVERTICAL);

	mpWorldPropertiesPanel = new WorldPropertiesPanel(root);
	component_panel->Add(mpWorldPropertiesPanel, wxSizerFlags().Expand());

	mpObjectPropertiesPanel = new ObjectPropertiesPanel(root);
	mpObjectPropertiesPanel->SetMinSize(wxSize(scObjectPropertyPanelWidth, -1));
	component_panel->Add(mpObjectPropertiesPanel, wxSizerFlags().Expand());

	body_sizer->Add(component_panel, wxSizerFlags().Expand());

	root_sizer->Add(body_sizer, wxSizerFlags(1).Expand());

	root->SetSizer(root_sizer);

	// Size the frame so the viewport starts at the requested size, then let it shrink
	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizerAndFit(frame_sizer);

	mpViewport->SetMinSize(wxSize(64, 64));
	SetMinSize(wxDefaultSize);

	SetEditorTool(eEditorTool::Translate);

	Bind(wxEVT_CLOSE_WINDOW, &EditorFrame::OnClose, this);
	Bind(wxEVT_ACTIVATE, &EditorFrame::OnActivate, this);
	Bind(wxEVT_ICONIZE, &EditorFrame::OnIconize, this);
}

void EditorFrame::SetEditorTool(const eEditorTool tool)
{
	const bool was_simulating = IsSimulationMode();
	const bool will_simulate = (tool == eEditorTool::None);

	mSelectedTool = tool;

	if (gPrototypeEditor != nullptr) {
		if (was_simulating && !will_simulate) {
			gPrototypeEditor->Reload();
		}
		else if (!was_simulating && will_simulate) {
			gPrototypeEditor->Unload();
		}
	}

	mSelectedTool = tool;

	// Only one tool at a time, and clicking the selected one again keeps it selected
	for (uint32 i = 0; i < mToolButtons.Size; i++) {
		mToolButtons[i]->SetValue(i == static_cast<uint32>(tool));
	}

	// Transfer the changed editor tool info back to the script
	if (gPrototypeEditor != nullptr) {
		auto set_tool = gPrototypeEditor->pScript->GetFunction<void (*)(eEditorTool)>("__set_editor_tool");

		if (set_tool != nullptr) {
			set_tool(tool);
		}
	}

	// Give the keyboard back to the viewport
	if (mpViewport != nullptr) {
		mpViewport->SetFocus();
	}
}

void EditorFrame::SetDefaultTool() { SetEditorTool(eEditorTool::Translate); }

void EditorFrame::ShowObjectListWindow()
{
	if (mpObjectListWindow == nullptr) {
		mpObjectListWindow = new ObjectListWindow(this);
	}
	else {
		mpObjectListWindow->RefreshList();
	}

	mpObjectListWindow->Show();
	mpObjectListWindow->Raise();
}

void EditorFrame::OnClose(wxCloseEvent& event)
{
	// The renderer still presents to the viewport, so the frame is destroyed in editor::Shutdown() once the game has
	// shut down. Not skipping the event keeps wxWidgets from destroying it now.
	mbCloseRequested = true;
}

void EditorFrame::OnActivate(wxActivateEvent& event)
{
	mbIsActive = event.GetActive();

	// Don't keep the cursor locked while another app is in front
	if (!mbIsActive && ControlManager::IsMouseLocked()) {
		ControlManager::ReleaseMouse();
	}

	event.Skip();
}

void EditorFrame::OnIconize(wxIconizeEvent& event)
{
	// wxEVT_ACTIVATE doesn't fire reliably on minimize on every platform
	mbIsActive = !event.IsIconized();

	event.Skip();
}

} // namespace fx::editor

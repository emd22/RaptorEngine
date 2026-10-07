#include "EditorFrame.hpp"

#include "CVarListWindow.hpp"
#include "EditorViewport.hpp"
#include "MaterialPickerWindow.hpp"
#include "ObjectListWindow.hpp"
#include "ObjectPropertiesPanel.hpp"
#include "RaptorEditor.hpp"
#include "ToolSettingsPanel.hpp"
#include "WorldPropertiesPanel.hpp"

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/tglbtn.h>

#include <Blockout.hpp>
#include <Controls.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/LightProbe.hpp>
#include <World.hpp>
#include <filesystem>

namespace fx::editor {

static constexpr int32 scObjectPropertyPanelWidth = 240;
static constexpr const char* scDefaultBlockoutPath = "RaptorData/Data/blockouts/btemp.prx";

struct ToolButtonInfo
{
	/// Shown as the tool tip, and as the label if the icon can't be loaded
	const char* pName;
	const char* pIconPath;
};

static constexpr ToolButtonInfo scToolButtons[] = {
	{ "Simulate", "Textures/editor/simulate.png" }, { "Transform", "Textures/editor/move.png" },
	{ "Face", "Textures/editor/face.png" },			{ "Rotate", "Textures/editor/rotate.png" },
	{ "Create", "Textures/editor/create.png" },		{ "Clip", "Textures/editor/clip.png" },
	{ "Light", "Textures/editor/lamp.png" },		{ "Bounds", "Textures/editor/bounds.png" },
	{ "Grab", "Textures/editor/grab.png" },			{ "Spawn", "Textures/editor/spawn.png" },
	{ "Subtract", "Textures/editor/subtract.png" },
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


static std::string ToBlockoutPath(const std::filesystem::path& chosen)
{
	std::error_code error;
	const std::filesystem::path relative = std::filesystem::relative(chosen, std::filesystem::current_path(), error);

	return (error || relative.empty()) ? chosen.string() : relative.string();
}

void EditorFrame::OpenPrototype()
{
	if (gWorld == nullptr || gWorld->pBlockout == nullptr) {
		return;
	}

#if 0
	if (wxMessageBox("Open another blockout? Anything you haven't saved will be lost.", "Open blockout",
					 wxYES_NO | wxICON_QUESTION, this) != wxYES) {
		return;
	}
#endif

	const std::filesystem::path current = std::filesystem::absolute(
		(gWorld->BlockoutPath.GetLength() > 0) ? gWorld->BlockoutPath.Str() : std::string(scDefaultBlockoutPath));

	wxFileDialog dialog(this, "Open blockout", wxString::FromUTF8(current.parent_path().lexically_normal().string()),
						wxString::FromUTF8(current.filename().string()), "Blockout files (*.prx)|*.prx",
						wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const std::string path = ToBlockoutPath(std::filesystem::path(dialog.GetPath().ToStdString()));

	LogInfo("Opening blockout '{}'", path);

	const String previous_path = gWorld->BlockoutPath;
	const String new_path(path.c_str());

	if (gWorld->pBlockout->Load(new_path)) {
		gWorld->BlockoutPath = new_path;

		gProbeManager->LoadProbes();
		gWorld->RespawnPlayer();
	}
	else {
		gWorld->BlockoutPath = previous_path;
		wxMessageBox("That file could not be loaded as a blockout.", "Open blockout", wxOK | wxICON_ERROR, this);
	}
}

void EditorFrame::NewPrototype()
{
	if (gWorld == nullptr || gWorld->pBlockout == nullptr) {
		return;
	}

	// gWorld->pBlockout->
}

void EditorFrame::SavePrototype()
{
	if (gWorld == nullptr || gWorld->pBlockout == nullptr) {
		return;
	}

	if (gWorld->BlockoutPath.GetLength() == 0) {
		SaveProtoTypeAs();
		return;
	}

	LogInfo("Saving blockout to '{}'", gWorld->BlockoutPath);
	gWorld->pBlockout->Save(gWorld->BlockoutPath);
}

void EditorFrame::SaveProtoTypeAs()
{
	if (gWorld == nullptr || gWorld->pBlockout == nullptr) {
		return;
	}

	const std::filesystem::path current = std::filesystem::absolute(
		(gWorld->BlockoutPath.GetLength() > 0) ? gWorld->BlockoutPath.Str() : std::string(scDefaultBlockoutPath));

	wxFileDialog dialog(this, "Save blockout as", wxString::FromUTF8(current.parent_path().lexically_normal().string()),
						wxString::FromUTF8(current.filename().string()), "Blockout files (*.prx)|*.prx",
						wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	std::filesystem::path chosen(dialog.GetPath().ToStdString());

	if (chosen.extension().empty()) {
		chosen += ".prx";
	}

	const std::string path = ToBlockoutPath(chosen);

	LogInfo("Saving blockout as '{}'", path);

	gWorld->BlockoutPath = String(path.c_str());
	gWorld->pBlockout->Save(gWorld->BlockoutPath);
}

EditorFrame::EditorFrame(const wxString& title, const wxSize& viewport_size) : wxFrame(nullptr, wxID_ANY, title)
{
	// Top bar
	wxMenuBar* menu_bar = new wxMenuBar;

	wxMenu* file_menu = new wxMenu;

	wxMenuItem* new_item = file_menu->Append(wxID_ANY, "New", "Creates a new prototype");
	wxMenuItem* open_item = file_menu->Append(wxID_ANY, "Open...\tCtrl+O", "Opens an existing prototype");
	file_menu->AppendSeparator();

	wxMenuItem* save_item = file_menu->Append(wxID_ANY, "Save", "Saves the prototype to its current file");
	wxMenuItem* save_as_item = file_menu->Append(wxID_ANY, "Save As...\tCtrl+Shift+S",
												 "Saves the prototype under a new name");

	menu_bar->Append(file_menu, "&File");

	wxMenu* world_menu = new wxMenu;

	wxMenuItem* reload_world_item = world_menu->Append(wxID_ANY, "Reload World", "Reloads the world from disk");
	wxMenuItem* reload_prototype_item = world_menu->Append(wxID_ANY, "Reload Prototype",
														   "Reloads prototype geometry from disk");
	wxMenuItem* reload_scripts_item = world_menu->Append(wxID_ANY, "Reload Scripts", "Reloads all loaded scripts");

	menu_bar->Append(world_menu, "&World");

	wxMenu* window_menu = new wxMenu;
	wxMenuItem* object_list_item = window_menu->Append(wxID_ANY, "Open Object List",
													   "View all objects currently in ObjectManager");
	wxMenuItem* cvar_list_item = window_menu->Append(wxID_ANY, "Open CVar List",
													 "View and edit all registered console variables");

	wxMenuItem* material_picker_item = window_menu->Append(wxID_ANY, "Open Material Picker",
														   "Search materials and preview their albedo");

	menu_bar->Append(window_menu, "&Tools");

	SetMenuBar(menu_bar);

	Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPrototype(); }, open_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SavePrototype(); }, save_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveProtoTypeAs(); }, save_as_item->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { gEditor->InvokeReloadHandler(eReloadTarget::World); },
		reload_world_item->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { gEditor->InvokeReloadHandler(eReloadTarget::Prototype); },
		reload_prototype_item->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { gEditor->InvokeReloadHandler(eReloadTarget::Scripts); },
		reload_scripts_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowObjectListWindow(); }, object_list_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowCVarListWindow(); }, cvar_list_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowMaterialPickerWindow(); }, material_picker_item->GetId());

	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

	// Tools
	wxBoxSizer* tool_sizer = new wxBoxSizer(wxVERTICAL);

	for (uint32 i = 0; i < static_cast<uint32>(eEditorTool::Count); i++) {
		wxToggleButton* button = MakeToolButton(root, scToolButtons[i]);

		const eEditorTool tool = static_cast<eEditorTool>(i);
		button->Bind(wxEVT_TOGGLEBUTTON, [tool](wxCommandEvent&) { gEditor->SetTool(tool); });

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

	// The tool settings slot starts empty - RaptorEditor::SetTool swaps in a panel for tools that have one
	mpComponentSizer = component_panel;
	mpComponentParent = root;

	body_sizer->Add(component_panel, wxSizerFlags().Expand());

	root_sizer->Add(body_sizer, wxSizerFlags(1).Expand());

	root->SetSizer(root_sizer);

	// Size the frame so the viewport starts at the requested size, then let it shrink
	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizerAndFit(frame_sizer);

	mpViewport->SetMinSize(wxSize(64, 64));
	SetMinSize(wxDefaultSize);

	Bind(wxEVT_CLOSE_WINDOW, &EditorFrame::OnClose, this);
	Bind(wxEVT_ACTIVATE, &EditorFrame::OnActivate, this);
	Bind(wxEVT_ICONIZE, &EditorFrame::OnIconize, this);
}

void EditorFrame::SetToolSettingsPanel(ToolSettingsBasePanel* new_panel)
{
	if (mpToolSettingsPanel != nullptr) {
		mpComponentSizer->Detach(mpToolSettingsPanel);
		mpToolSettingsPanel->Destroy();
	}

	mpToolSettingsPanel = new_panel;

	if (mpToolSettingsPanel != nullptr) {
		mpToolSettingsPanel->Create(mpComponentParent, wxID_ANY);

		wxBoxSizer* inner_sizer = new wxBoxSizer(wxVERTICAL);
		mpToolSettingsPanel->Construct(inner_sizer);
		mpToolSettingsPanel->SetSizer(inner_sizer);

		mpComponentSizer->Add(mpToolSettingsPanel, wxSizerFlags().Expand());
	}

	mpComponentSizer->Layout();
}

void EditorFrame::ShowSelectedTool(const eEditorTool tool)
{
	// Only one tool at a time, and clicking the selected one again keeps it selected
	for (uint32 i = 0; i < mToolButtons.Size; i++) {
		mToolButtons[i]->SetValue(i == static_cast<uint32>(tool));
	}

	// Give the keyboard back to the viewport
	if (mpViewport != nullptr) {
		mpViewport->SetFocus();
	}
}

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

void EditorFrame::ShowCVarListWindow()
{
	if (mpCVarListWindow == nullptr) {
		mpCVarListWindow = new CVarListWindow(this);
	}
	else {
		mpCVarListWindow->RefreshList();
	}

	mpCVarListWindow->Show();
	mpCVarListWindow->Raise();
}

void EditorFrame::ShowMaterialPickerWindow()
{
	if (mpMaterialPickerWindow == nullptr) {
		mpMaterialPickerWindow = new MaterialPickerWindow(this);
	}
	else {
		mpMaterialPickerWindow->RefreshList();
	}

	mpMaterialPickerWindow->Show();
	mpMaterialPickerWindow->Raise();
}

void EditorFrame::OnClose(wxCloseEvent& event) { mbCloseRequested = true; }

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

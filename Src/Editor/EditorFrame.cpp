#include "EditorFrame.hpp"

#include "AtlasPackerWindow.hpp"
#include "CVarListWindow.hpp"
#include "EditorThread.hpp"
#include "EditorViewport.hpp"
#include "MaterialPickerWindow.hpp"
#include "ObjectListWindow.hpp"
#include "ObjectPropertiesPanel.hpp"
#include "OutlineToggleButton.hpp"
#include "RaptorEditor.hpp"
#include "ToolSettingsPanel.hpp"
#include "WorldPropertiesPanel.hpp"

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statline.h>

#include <Blockout.hpp>
#include <Controls.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/LightProbe.hpp>
#include <World.hpp>
#include <algorithm>
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
	{ "Simulate", "Textures/editor_new/simulate.png" }, { "Transform", "Textures/editor_new/translate.png" },
	{ "Face", "Textures/editor_new/face.png" },			{ "Rotate", "Textures/editor_new/rotate.png" },
	{ "Create", "Textures/editor_new/brush.png" },		{ "Clip", "Textures/editor_new/cut.png" },
	{ "Light", "Textures/editor_new/light.png" },		{ "Bounds", "Textures/editor_new/bounds.png" },
	{ "Grab", "Textures/editor_new/grab.png" },			{ "Subtract", "Textures/editor_new/subtract.png" },
};

static_assert(std::size(scToolButtons) == static_cast<size_t>(eEditorTool::Count));

static constexpr const char* scDataFilterNames[] = {
	"All", "Probe Volumes", "Reflection Probes", "Spawn Points", "Volumes", "Lights",
};

static_assert(std::size(scDataFilterNames) == static_cast<size_t>(eDataFilter::Count));

static const wxColor scPanelColor = wxColor(30, 30, 30);

static const wxColor scSelectedColor = wxColor(168, 50, 50);

/// Creates an icon button for the tool, or a text button if its icon can't be loaded
static OutlineToggleButton* MakeToolButton(wxWindow* parent, const ToolButtonInfo& info)
{
	const String icon_path = FilesystemIO::ResolvePath(info.pIconPath);

	// wx shows an error dialog for images it can't load, so check the icon is there first
	wxBitmap icon;
	if (FilesystemIO::FileExists(icon_path)) {
		icon.LoadFile(wxString::FromUTF8(icon_path.CStr()), wxBITMAP_TYPE_PNG);
	}

	if (!icon.IsOk()) {
		LogWarning(LC_CORE, "Could not load editor icon '{}'", icon_path);
	}

	OutlineToggleButton* button = new OutlineToggleButton(parent, icon, info.pName);
	button->SetToolTip(info.pName);

	return button;
}


static std::string ToBlockoutPath(const std::filesystem::path& chosen)
{
	std::error_code error;
	const std::filesystem::path relative = std::filesystem::relative(chosen, std::filesystem::current_path(), error);

	return (error || relative.empty()) ? chosen.string() : relative.string();
}

static std::filesystem::path GetBlockoutDialogStart(const std::string& blockout_path)
{
	return std::filesystem::absolute(blockout_path.empty() ? std::string(scDefaultBlockoutPath) : blockout_path);
}

void EditorFrame::OpenPrototype()
{
	const std::filesystem::path current = GetBlockoutDialogStart(mState.BlockoutPath);

	wxFileDialog dialog(this, "Open blockout", wxString::FromUTF8(current.parent_path().lexically_normal().string()),
						wxString::FromUTF8(current.filename().string()), "Blockout files (*.prx)|*.prx",
						wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const std::string path = ToBlockoutPath(std::filesystem::path(dialog.GetPath().ToStdString()));

	thread::PostToGame(
		[this, path]
		{
			if (gWorld == nullptr || gWorld->pBlockout == nullptr) {
				return;
			}

			LogInfo("Opening blockout '{}'", path);

			const String previous_path = gWorld->BlockoutPath;
			const String new_path(path.c_str());

			if (gWorld->pBlockout->Load(new_path)) {
				gWorld->BlockoutPath = new_path;

				gProbeManager->LoadProbes();
				gWorld->RespawnPlayer();
				return;
			}

			gWorld->BlockoutPath = previous_path;

			thread::PostToUI(
				[this]
				{
					wxMessageBox("That file could not be loaded as a blockout.", "Open blockout", wxOK | wxICON_ERROR,
								 this);
				});
		});
}

void EditorFrame::NewPrototype()
{
	thread::PostToGame(
		[]
		{
			if (gWorld != nullptr && gWorld->pBlockout != nullptr) {
				gWorld->pBlockout->ResetToTemplate();
			}
		});
}

void EditorFrame::SavePrototype()
{
	if (mState.BlockoutPath.empty()) {
		SaveProtoTypeAs();
		return;
	}

	thread::PostToGame(
		[]
		{
			if (gWorld == nullptr || gWorld->pBlockout == nullptr || gWorld->BlockoutPath.GetLength() == 0) {
				return;
			}

			LogInfo("Saving blockout to '{}'", gWorld->BlockoutPath);
			gWorld->pBlockout->Save(gWorld->BlockoutPath);
		});
}

void EditorFrame::SaveProtoTypeAs()
{
	const std::filesystem::path current = GetBlockoutDialogStart(mState.BlockoutPath);

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

	thread::PostToGame(
		[path]
		{
			if (gWorld == nullptr || gWorld->pBlockout == nullptr) {
				return;
			}

			LogInfo("Saving blockout as '{}'", path);

			gWorld->BlockoutPath = String(path.c_str());
			gWorld->pBlockout->Save(gWorld->BlockoutPath);
		});
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

	wxMenuItem* clean_item = world_menu->Append(wxID_ANY, "Clean", "Removes all decals and ragdolls from the world");

	wxMenuItem* rebase_probes_item = world_menu->Append(wxID_ANY, "Rebake Probes",
														"Rebakes all irradiance and reflection probes for the world");


	menu_bar->Append(world_menu, "&World");

	wxMenu* window_menu = new wxMenu;
	wxMenuItem* object_list_item = window_menu->Append(wxID_ANY, "Open Object List",
													   "View all objects currently in ObjectManager");
	wxMenuItem* cvar_list_item = window_menu->Append(wxID_ANY, "Open CVar List",
													 "View and edit all registered console variables");

	wxMenuItem* material_picker_item = window_menu->Append(wxID_ANY, "Open Material Picker",
														   "Search materials and preview their albedo");
	wxMenuItem* atlas_packer_item = window_menu->Append(wxID_ANY, "Open Atlas Packer",
														"Pack images into a tile atlas and export its config");


	menu_bar->Append(window_menu, "&Tools");

	SetMenuBar(menu_bar);

	Bind(wxEVT_MENU, [this](wxCommandEvent&) { NewPrototype(); }, new_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPrototype(); }, open_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SavePrototype(); }, save_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveProtoTypeAs(); }, save_as_item->GetId());
	const auto bind_reload = [this](wxMenuItem* item, eReloadTarget target)
	{
		Bind(
			wxEVT_MENU, [target](wxCommandEvent&)
			{ thread::PostToGame([target] { gEditor->InvokeReloadHandler(target); }); }, item->GetId());
	};

	bind_reload(reload_world_item, eReloadTarget::World);
	bind_reload(reload_prototype_item, eReloadTarget::Prototype);
	bind_reload(reload_scripts_item, eReloadTarget::Scripts);
	bind_reload(clean_item, eReloadTarget::Clean);

	Bind(
		wxEVT_MENU,
		[](wxCommandEvent&)
		{
			thread::PostToGame(
				[]
				{
					gProbeManager->RebuildVolumesFromWorld();
					gProbeManager->BeginBake();
				});
		},
		rebase_probes_item->GetId());

	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowObjectListWindow(); }, object_list_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowCVarListWindow(); }, cvar_list_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowMaterialPickerWindow(); }, material_picker_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowAtlasPackerWindow(); }, atlas_packer_item->GetId());

	SetBackgroundColour(scPanelColor);

	wxPanel* root = new wxPanel(this, wxID_ANY);
	root->SetBackgroundColour(scPanelColor);
	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

	// Modes
	wxBoxSizer* mode_sizer = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* mode_buttons_sizer = new wxBoxSizer(wxHORIZONTAL);

	mpVisButton = new OutlineToggleButton(root, wxBitmap(), "V");
	mpVisButton->SetToolTip("Select and edit visible brushes and models");
	mpVisButton->Bind(wxEVT_TOGGLEBUTTON,
					  [](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetMode(eEditorMode::Vis); }); });

	mpDataButton = new OutlineToggleButton(root, wxBitmap(), "D");
	mpDataButton->SetToolTip("Select and edit probe volumes, reflection probes, volumes, lights and spawn points");
	mpDataButton->Bind(wxEVT_TOGGLEBUTTON,
					   [](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetMode(eEditorMode::Data); }); });

	mode_buttons_sizer->Add(mpVisButton, wxSizerFlags(1).Expand());
	mode_buttons_sizer->Add(mpDataButton, wxSizerFlags(1).Expand().Border(wxLEFT, 2));
	mode_sizer->Add(mode_buttons_sizer, wxSizerFlags().CenterVertical().Border(wxALL, 2));

	mpDataFilterChoice = new wxChoice(root, wxID_ANY);
	for (const char* name : scDataFilterNames) {
		mpDataFilterChoice->Append(name);
	}
	mpDataFilterChoice->SetSelection(0);
	mpDataFilterChoice->SetToolTip("Which data brushes can be selected");
	mpDataFilterChoice->Bind(wxEVT_CHOICE,
							 [this](wxCommandEvent&)
							 {
								 const int32 selection = mpDataFilterChoice->GetSelection();

								 if (selection != wxNOT_FOUND) {
									 gEditor->SetDataFilter(static_cast<eDataFilter>(selection));
								 }
							 });
	mode_sizer->Add(mpDataFilterChoice, wxSizerFlags().CenterVertical().Border(wxALL, 2));

	// Tools
	wxBoxSizer* tool_sizer = new wxBoxSizer(wxHORIZONTAL);

	for (uint32 i = 0; i < static_cast<uint32>(eEditorTool::Count); i++) {
		OutlineToggleButton* button = MakeToolButton(root, scToolButtons[i]);

		const eEditorTool tool = static_cast<eEditorTool>(i);
		button->Bind(wxEVT_TOGGLEBUTTON,
					 [tool](wxCommandEvent&) { thread::PostToGame([tool] { gEditor->SetTool(tool); }); });

		tool_sizer->Add(button, wxSizerFlags().Border(wxALL, 2));
		mToolButtons.Insert(button);
	}

	// root_sizer->Add(tool_sizer, wxSizerFlags().Border(wxALL, 6));

	// Viewport and component panel
	wxBoxSizer* body_sizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* top_bar = new wxBoxSizer(wxHORIZONTAL);
	top_bar->Add(mode_sizer, wxSizerFlags().CenterVertical());
	top_bar->Add(new wxStaticLine(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL),
				 wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, 6));
	top_bar->Add(tool_sizer, wxSizerFlags().CenterVertical());

	root_sizer->Add(top_bar, wxSizerFlags().Border(wxALL, 4));

	mpViewport = new EditorViewport(root, viewport_size);
	mpViewport->SetMinSize(viewport_size);
	body_sizer->Add(mpViewport, wxSizerFlags(1).Expand());


	mpSideColumn = new wxBoxSizer(wxVERTICAL);

	mpSideToggleButton = new wxButton(root, wxID_ANY, wxString::FromUTF8("Hide"), wxDefaultPosition, wxDefaultSize,
									  wxBU_EXACTFIT);
	mpSideToggleButton->SetToolTip("Minimize the side panel");
	mpSideToggleButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetSidePanelCollapsed(!mbSidePanelCollapsed); });
	mpSideColumn->Add(mpSideToggleButton, wxSizerFlags().Right().Border(wxALL, 2));

	mpSideScroller = new wxScrolledWindow(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	mpSideScroller->SetScrollRate(0, 12);

	wxBoxSizer* component_panel = new wxBoxSizer(wxVERTICAL);

	mpWorldPropertiesPanel = new WorldPropertiesPanel(mpSideScroller);
	component_panel->Add(mpWorldPropertiesPanel, wxSizerFlags().Expand());

	mpObjectPropertiesPanel = new ObjectPropertiesPanel(mpSideScroller);
	mpObjectPropertiesPanel->SetMinSize(wxSize(scObjectPropertyPanelWidth, -1));
	component_panel->Add(mpObjectPropertiesPanel, wxSizerFlags().Expand());

	// The tool settings slot starts empty - RaptorEditor::SetTool swaps in a panel for tools that have one
	mpComponentSizer = component_panel;
	mpSideScroller->SetSizer(component_panel);
	component_panel->FitInside(mpSideScroller);

	mpSideScroller->Bind(wxEVT_SIZE,
						 [this](wxSizeEvent& event)
						 {
							 mpComponentSizer->FitInside(mpSideScroller);
							 event.Skip();
						 });

	// Only the height scrolls, so reserve room for the scroll bar next to the widest content
	const int32 scroller_width = std::max(component_panel->GetMinSize().x, scObjectPropertyPanelWidth) +
								 wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, mpSideScroller);
	mpSideScroller->SetMinSize(wxSize(scroller_width, 64));

	mpSideColumn->Add(mpSideScroller, wxSizerFlags(1).Expand());

	body_sizer->Add(mpSideColumn, wxSizerFlags().Expand());

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

void EditorFrame::SetInteractive(bool interactive)
{
	Enable(interactive);

	if (wxMenuBar* menu_bar = GetMenuBar(); menu_bar != nullptr) {
		for (size_t i = 0; i < menu_bar->GetMenuCount(); i++) {
			menu_bar->EnableTop(i, interactive);
		}
	}

	if (interactive && mpViewport != nullptr) {
		mpViewport->SetFocus();
	}
}

void EditorFrame::SetSidePanelCollapsed(bool collapsed)
{
	mbSidePanelCollapsed = collapsed;

	mpSideColumn->Show(mpSideScroller, !collapsed);

	mpSideToggleButton->SetLabel(collapsed ? wxString::FromUTF8("Open") : wxString::FromUTF8("Hide"));
	mpSideToggleButton->SetToolTip(collapsed ? "Restore the side panel" : "Minimize the side panel");

	mpSideColumn->Layout();
	Layout();

	if (mpViewport != nullptr) {
		mpViewport->SetFocus();
	}
}

void EditorFrame::SetToolSettingsPanel(ToolSettingsBasePanel* new_panel)
{
	if (mpToolSettingsPanel != nullptr) {
		mpComponentSizer->Detach(mpToolSettingsPanel);
		mpToolSettingsPanel->Destroy();
	}

	mpToolSettingsPanel = new_panel;

	if (mpToolSettingsPanel != nullptr) {
		mpToolSettingsPanel->Create(mpSideScroller, wxID_ANY);

		wxBoxSizer* inner_sizer = new wxBoxSizer(wxVERTICAL);
		mpToolSettingsPanel->Construct(inner_sizer);
		mpToolSettingsPanel->SetSizer(inner_sizer);
		mpToolSettingsPanel->ApplyState(mState);

		mpComponentSizer->Add(mpToolSettingsPanel, wxSizerFlags().Expand());
	}

	mpComponentSizer->FitInside(mpSideScroller);
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

void EditorFrame::ApplyState(const EditorPanelState& state)
{
	mState = state;

	mpWorldPropertiesPanel->ApplyState(mState.World);
	mpObjectPropertiesPanel->ApplyState(mState.Object, mState.Materials);

	if (mpToolSettingsPanel != nullptr) {
		mpToolSettingsPanel->ApplyState(mState);
	}

	if (mpMaterialPickerWindow != nullptr) {
		mpMaterialPickerWindow->OnStateChanged();
	}
}

void EditorFrame::ShowMode(const eEditorMode mode, const eDataFilter filter, const uint32 available_tools)
{
	const bool data_mode = (mode == eEditorMode::Data);

	mpVisButton->SetValue(!data_mode);
	mpDataButton->SetValue(data_mode);

	mpDataFilterChoice->SetSelection(static_cast<int32>(filter));
	mpDataFilterChoice->Enable(data_mode);

	for (uint32 i = 0; i < mToolButtons.Size; i++) {
		mToolButtons[i]->Enable((available_tools & (1u << i)) != 0);
	}

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

void EditorFrame::ShowAtlasPackerWindow()
{
	if (mpAtlasPackerWindow == nullptr) {
		mpAtlasPackerWindow = new AtlasPackerWindow(this);
	}

	mpAtlasPackerWindow->Show();
	mpAtlasPackerWindow->Raise();
}

void EditorFrame::OnClose(wxCloseEvent& event) { mbCloseRequested = true; }

void EditorFrame::OnActivate(wxActivateEvent& event)
{
	mbIsActive = event.GetActive();

	// Don't keep the cursor locked while another app is in front
	if (!mbIsActive) {
		ControlManager::PostFocusLost();
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

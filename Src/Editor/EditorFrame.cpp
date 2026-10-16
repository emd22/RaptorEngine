#include "EditorFrame.hpp"

#include "AtlasPackerWindow.hpp"
#include "CVarListWindow.hpp"
#include "EditorTheme.hpp"
#include "EditorThread.hpp"
#include "EditorToolbar.hpp"
#include "EditorViewport.hpp"
#include "MaterialPickerPanel.hpp"
#include "ObjectListWindow.hpp"
#include "ObjectPropertiesPanel.hpp"
#include "RaptorEditor.hpp"
#include "ToolSettingsPanel.hpp"
#include "TopViewPanel.hpp"
#include "WorldPropertiesPanel.hpp"

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/choicdlg.h>
#include <wx/textdlg.h>

#include <Blockout.hpp>
#include <Controls.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Core/Path.hpp>
#include <Engine.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/LightProbe.hpp>
#include <World.hpp>
#include <algorithm>
#include <filesystem>

namespace fx::editor {

static constexpr int32 scSidePanelWidth = 300;
static constexpr int32 scMinimumSidePanelWidth = 220;
static constexpr size_t scToolSettingsSlotIndex = 3;
static constexpr int32 scMinimumPaneSize = 160;

static constexpr const char* scDefaultBlockoutPath = "RaptorData/Data/blockouts/btemp.prx";

class EditorSplitter : public wxSplitterWindow
{
public:
	EditorSplitter(wxWindow* parent, const wxSize& size)
		: wxSplitterWindow(parent, wxID_ANY, wxDefaultPosition, size, wxSP_NOBORDER)
	{
	}

	void DrawSash(wxDC& dc) override
	{
		if (!IsSplit()) {
			return;
		}

		const wxSize size = GetClientSize();
		const bool vertical = (GetSplitMode() == wxSPLIT_VERTICAL);
		const int position = GetSashPosition();
		const int thickness = GetSashSize();

		const EditorTheme& theme = GetEditorTheme();

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(ToWxColour(theme.Frame.Sash)));

		if (vertical) {
			dc.DrawRectangle(position, 0, thickness, size.y);
			dc.SetBrush(wxBrush(ToWxColour(theme.Frame.SashGrip)));
			dc.DrawRectangle(position + thickness / 2 - 1, 0, 2, size.y);
		}
		else {
			dc.DrawRectangle(0, position, size.x, thickness);
			dc.SetBrush(wxBrush(ToWxColour(theme.Frame.SashGrip)));
			dc.DrawRectangle(0, position + thickness / 2 - 1, size.x, 2);
		}
	}
};


static String ToBlockoutPath(const std::filesystem::path& chosen)
{
	std::error_code error;
	const std::filesystem::path relative = std::filesystem::relative(chosen, std::filesystem::current_path(), error);

	if (error || relative.empty()) {
		return String(chosen.string());
	}

	return String(relative.string());
}

static std::filesystem::path GetBlockoutDialogStart(const String& blockout_path)
{
	if (blockout_path.IsEmpty()) {
		return std::filesystem::absolute(scDefaultBlockoutPath);
	}

	return std::filesystem::absolute(blockout_path.CStr());
}

void EditorFrame::OpenPrototype()
{
	const std::filesystem::path current = GetBlockoutDialogStart(mState.BlockoutPath);

	wxFileDialog dialog(this, "Open map", wxString::FromUTF8(current.parent_path().lexically_normal().string()),
						wxString::FromUTF8(current.filename().string()),
						"Maps (*.prx;*.fxmap)|*.prx;*.fxmap|Prototypes (*.prx)|*.prx|Compiled maps (*.fxmap)|*.fxmap",
						wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const String path = ToBlockoutPath(std::filesystem::path(dialog.GetPath().ToStdString()));

	RunAfterUnsavedCheck(
		"Save changes to the prototype before opening another map?",
		[this, path]
		{
			if (gWorld == nullptr || gWorld->pPrototype == nullptr) {
				return;
			}

			LogInfo("Opening map '{}'", path);

			const String previous_path = gWorld->PrototypePath;

			ePrototypeLoadStatus load_status = gWorld->pPrototype->Load(path);

			// We are requesting a compiled version but that is not what is loaded, notify the user
			if (load_status == ePrototypeLoadStatus::NotCompiled) {
				thread::PostToUI(
					[this]
					{
						wxMessageBox("Compiled map could not be loaded, try rebaking with mapc.", "Open map",
									 wxOK | wxICON_ERROR, this);
					});
			}
			// The prototype could not be loaded, or there missing prototype files when loading a compiled map.
			else if (load_status == ePrototypeLoadStatus::HasErrors) {
				gWorld->PrototypePath = previous_path;

				thread::PostToUI(
					[this]
					{
						wxMessageBox(
							"That file could not be loaded. A compiled map (.fxmap) needs its prototype (.prx) "
							"next to it, or there are errors in that prototype.",
							"Open map", wxOK | wxICON_ERROR, this);
					});

				return;
			}


			if (load_status != ePrototypeLoadStatus::HasErrors) {
				gWorld->PrototypePath = path;

				gProbeManager->LoadProbes();
				gWorld->RespawnPlayer();
				return;
			}
		});
}


void EditorFrame::SwitchBakedOrProto()
{
	RunAfterUnsavedCheck(
		"Save changes to the prototype before switching to its linked map?",
		[this]
		{
			if (gWorld == nullptr || gWorld->pPrototype == nullptr || gWorld->PrototypePath.IsEmpty()) {
				return;
			}

			const bool to_prototype = gWorld->pPrototype->IsCompiledMap();
			const String previous_path = gWorld->PrototypePath;

			Path path(previous_path);
			if (to_prototype) {
				path.SetExtension(".prx");
			}
			else {
				path.SetExtension(".fxmap");
			}

			const String linked_path = path.Str();

			if (!FilesystemIO::FileExists(linked_path)) {
				LogWarning(LC_CORE, "There is no linked map at '{}'", linked_path);

				thread::PostToUI(
					[this, to_prototype]
					{
						if (to_prototype) {
							wxMessageBox("There is no prototype (.prx) next to this compiled map.", "Open map",
										 wxOK | wxICON_ERROR, this);
						}
						else {
							wxMessageBox("There is no compiled map (.fxmap) next to this prototype, bake it with mapc.",
										 "Open map", wxOK | wxICON_ERROR, this);
						}
					});

				return;
			}

			LogInfo("Opening linked map '{}'...", linked_path);

			const ePrototypeLoadStatus load_status = gWorld->pPrototype->Load(linked_path);

			if (load_status == ePrototypeLoadStatus::HasErrors) {
				gWorld->PrototypePath = previous_path;

				thread::PostToUI(
					[this, to_prototype]
					{
						if (to_prototype) {
							wxMessageBox("Linked prototype could not be loaded.", "Open map", wxOK | wxICON_ERROR,
										 this);
						}
						else {
							wxMessageBox("Linked compiled map could not be loaded.", "Open map", wxOK | wxICON_ERROR,
										 this);
						}
					});

				return;
			}

			if (load_status == ePrototypeLoadStatus::NotCompiled) {
				thread::PostToUI(
					[this]
					{
						wxMessageBox("Compiled map could not be loaded, try rebaking with mapc.", "Open map",
									 wxOK | wxICON_ERROR, this);
					});
			}

			gWorld->PrototypePath = linked_path;
			gProbeManager->LoadProbes();
			// No need to respawn/move the player as we are going to be in the same map structurally
		});
}

void EditorFrame::NewPrototype()
{
	RunAfterUnsavedCheck("Save changes to the prototype before starting a new one?",
						 []
						 {
							 if (gWorld != nullptr && gWorld->pPrototype != nullptr) {
								 gWorld->pPrototype->ResetToTemplate();
								 gWorld->PrototypePath = String("");
							 }
						 });
}

void EditorFrame::ReloadPrototype()
{
	RunAfterUnsavedCheck("Save changes to the prototype before reloading it from disk?",
						 [] { gEditor->InvokeReloadHandler(eReloadTarget::Prototype); });
}

void EditorFrame::RunAfterUnsavedCheck(const wxString& question, std::function<void()> game_task)
{
	if (mbAskingAboutChanges) {
		return;
	}

	bool save_first = false;

	if (mState.bUnsavedChanges) {
		wxMessageDialog dialog(this, question, "Unsaved changes", wxYES_NO | wxCANCEL | wxICON_WARNING);
		dialog.SetYesNoCancelLabels("Save", "Don't Save", "Cancel");
		dialog.SetExtendedMessage("Your changes will be lost if you don't save them.");

		mbAskingAboutChanges = true;
		const int choice = dialog.ShowModal();
		mbAskingAboutChanges = false;

		if (choice == wxID_CANCEL) {
			return;
		}

		save_first = (choice == wxID_YES);
	}

	if (save_first && !SavePrototype()) {
		return;
	}

	thread::PostToGame(
		[this, save_first, task = std::move(game_task)]
		{
			if (save_first && gEditor->HasUnsavedChanges()) {
				thread::PostToUI(
					[this]
					{
						wxMessageBox("The prototype could not be saved, so it has been left open. The log has the "
									 "details.",
									 "Save", wxOK | wxICON_ERROR, this);
					});
				return;
			}

			task();
		});
}

bool EditorFrame::SavePrototype()
{
	if (mState.BlockoutPath.IsEmpty()) {
		return SaveProtoTypeAs();
	}

	thread::PostToGame(
		[]
		{
			if (gWorld == nullptr || gWorld->pPrototype == nullptr || gWorld->PrototypePath.GetLength() == 0) {
				return;
			}

			LogInfo("Saving blockout to '{}'", gWorld->PrototypePath);
			gWorld->pPrototype->Save(gWorld->PrototypePath);
		});

	return true;
}

bool EditorFrame::SaveProtoTypeAs()
{
	std::filesystem::path current = GetBlockoutDialogStart(mState.BlockoutPath);
	current.replace_extension(".prx");

	wxFileDialog dialog(this, "Save blockout as", wxString::FromUTF8(current.parent_path().lexically_normal().string()),
						wxString::FromUTF8(current.filename().string()), "Blockout files (*.prx)|*.prx",
						wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	std::filesystem::path chosen(dialog.GetPath().ToStdString());

	if (chosen.extension().empty()) {
		chosen += ".prx";
	}

	const String path = ToBlockoutPath(chosen);

	thread::PostToGame(
		[path]
		{
			if (gWorld == nullptr || gWorld->pPrototype == nullptr) {
				return;
			}

			LogInfo("Saving blockout as '{}'", path);

			gWorld->PrototypePath = path;
			gWorld->pPrototype->Save(gWorld->PrototypePath);
		});

	return true;
}

EditorFrame::~EditorFrame() = default;

EditorFrame::EditorFrame(const wxString& title, const wxSize& viewport_size) : wxFrame(nullptr, wxID_ANY, title)
{
	GetThemeLibrary().Load();

	// Top bar
	wxMenuBar* menu_bar = new wxMenuBar;

	wxMenu* file_menu = new wxMenu;

	wxMenuItem* new_item = file_menu->Append(wxID_ANY, "New", "Creates a new prototype");
	wxMenuItem* open_item = file_menu->Append(wxID_ANY, "Open...\tCtrl+O", "Opens an existing prototype");
	file_menu->AppendSeparator();

	wxMenuItem* load_baked_or_proto = file_menu->Append(
		wxID_ANY, "Load baked/proto",
		"Switch to the prototype version if on baked, or the baked version if on prototype");

	file_menu->AppendSeparator();
	wxMenuItem* save_item = file_menu->Append(wxID_ANY, "Save\tCtrl+S", "Saves the prototype to its current file");
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

	wxMenu* view_menu = new wxMenu;

	mpPerspectiveMenuItem = view_menu->AppendRadioItem(wxID_ANY, "3D View\tCtrl+1", "Fly around the level in 3D");
	mpTopMenuItem = view_menu->AppendRadioItem(wxID_ANY, "2D Views\tCtrl+2",
											   "Top, front and side wireframe views for laying out brushes");
	mpSplitMenuItem = view_menu->AppendRadioItem(wxID_ANY, "Split View\tCtrl+3",
												 "The 3D view and the 2D views side by side, with a draggable divider");
	view_menu->AppendSeparator();
	mpStackMenuItem = view_menu->AppendCheckItem(wxID_ANY, "Stack Split Panes",
												 "Put the 2D view below the 3D view instead of beside it");
	view_menu->AppendSeparator();
	wxMenuItem* reload_theme_item = view_menu->Append(wxID_ANY, "Reload Theme",
													  "Re-reads the current theme from Config/EditorThemes");
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ReloadTheme(); }, reload_theme_item->GetId());

	menu_bar->Append(view_menu, "&View");

	wxMenu* window_menu = new wxMenu;
	wxMenuItem* object_list_item = window_menu->Append(wxID_ANY, "Open Object List",
													   "View all objects currently in ObjectManager");
	wxMenuItem* cvar_list_item = window_menu->Append(wxID_ANY, "Open CVar List",
													 "View and edit all registered console variables");

	mpMaterialPickerMenuItem = window_menu->AppendCheckItem(wxID_ANY, "Show Material Picker",
															 "Show the material search and preview in the side panel");
	mpMaterialPickerMenuItem->Check(true);
	wxMenuItem* atlas_packer_item = window_menu->Append(wxID_ANY, "Open Atlas Packer",
														"Pack images into a tile atlas and export its config");

	window_menu->AppendSeparator();

	wxMenu* theme_menu = new wxMenu;
	const std::vector<EditorTheme>& themes = GetThemeLibrary().GetThemes();

	for (uint32 index = 0; index < themes.size(); index++) {
		wxMenuItem* theme_item = theme_menu->AppendRadioItem(wxID_ANY, wxString::FromUTF8(themes[index].Name.CStr()),
															 "Recolours the editor");
		theme_item->Check(index == GetThemeLibrary().GetActiveIndex());

		Bind(wxEVT_MENU, [this, index](wxCommandEvent&) { SelectTheme(index); }, theme_item->GetId());
	}

	window_menu->AppendSubMenu(theme_menu, "Theme", "Picks the editor colour theme from Config/EditorThemes");

	menu_bar->Append(window_menu, "&Tools");

	wxMenu* subview_menu = new wxMenu;

	wxMenuItem* create_subview_item = subview_menu->Append(
		wxID_ANY, "Create from Selection...", "Groups the selected brushes into a new subview (Ctrl+G)");
	wxMenuItem* place_subview_item = subview_menu->Append(wxID_ANY, "Place Subview...",
														  "Places another copy of a subview at the crosshair");
	subview_menu->AppendSeparator();
	wxMenuItem* enter_subview_item = subview_menu->Append(
		wxID_ANY, "Enter Subview", "Edits the selected subview on its own, leaving the level alone (G)");
	wxMenuItem* leave_subview_item = subview_menu->Append(
		wxID_ANY, "Leave Subview", "Goes back to the level and updates every copy of the subview (G)");
	subview_menu->AppendSeparator();
	wxMenuItem* dissolve_subview_item = subview_menu->Append(
		wxID_ANY, "Dissolve into Level", "Turns the selected subview back into brushes of the level");

	menu_bar->Append(subview_menu, "&Subview");

	SetMenuBar(menu_bar);

	Bind(wxEVT_MENU, [this](wxCommandEvent&) { CreateSubview(); }, create_subview_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { PlaceSubview(); }, place_subview_item->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { thread::PostToGame([] { gEditor->EnterSubview(); }); },
		enter_subview_item->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { thread::PostToGame([] { gEditor->LeaveSubview(); }); },
		leave_subview_item->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { thread::PostToGame([] { gEditor->DissolveSelectedSubviews(); }); },
		dissolve_subview_item->GetId());

	Bind(wxEVT_MENU, [this](wxCommandEvent&) { NewPrototype(); }, new_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenPrototype(); }, open_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SavePrototype(); }, save_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveProtoTypeAs(); }, save_as_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SwitchBakedOrProto(); }, load_baked_or_proto->GetId());

	const auto bind_reload = [this](wxMenuItem* item, eReloadTarget target)
	{
		Bind(
			wxEVT_MENU, [target](wxCommandEvent&)
			{ thread::PostToGame([target] { gEditor->InvokeReloadHandler(target); }); }, item->GetId());
	};

	bind_reload(reload_world_item, eReloadTarget::World);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ReloadPrototype(); }, reload_prototype_item->GetId());
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

	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetView(eEditorView::Perspective); }); },
		mpPerspectiveMenuItem->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetView(eEditorView::Top); }); },
		mpTopMenuItem->GetId());
	Bind(
		wxEVT_MENU, [](wxCommandEvent&) { thread::PostToGame([] { gEditor->SetView(eEditorView::Split); }); },
		mpSplitMenuItem->GetId());
	Bind(
		wxEVT_MENU,
		[this](wxCommandEvent&)
		{
			mbStackedSplit = mpStackMenuItem->IsChecked();

			if (mView == eEditorView::Split) {
				ApplySplitLayout(mView);
				mpRootPanel->Layout();
			}
		},
		mpStackMenuItem->GetId());

	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowObjectListWindow(); }, object_list_item->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowCVarListWindow(); }, cvar_list_item->GetId());
	Bind(
		wxEVT_MENU, [this](wxCommandEvent& event) { SetMaterialPickerShown(event.IsChecked()); },
		mpMaterialPickerMenuItem->GetId());
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowAtlasPackerWindow(); }, atlas_packer_item->GetId());

	wxPanel* root = new wxPanel(this, wxID_ANY);
	mpRootPanel = root;
	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* body_sizer = new wxBoxSizer(wxHORIZONTAL);

	mpSideSplitter = new EditorSplitter(root, viewport_size);
	mpSideSplitter->SetMinimumPaneSize(scMinimumSidePanelWidth);
	mpSideSplitter->SetSashGravity(1.0);

	mpSplitter = new EditorSplitter(mpSideSplitter, viewport_size);
	mpSplitter->SetMinimumPaneSize(scMinimumPaneSize);
	mpSplitter->SetSashGravity(0.5);

	mpViewport = new EditorViewport(mpSplitter, viewport_size);
	mpViewport->SetMinSize(viewport_size);

	mpTopViewPanel = new TopViewPanel(mpSplitter);
	mpTopViewPanel->Hide();

	mpSplitter->Initialize(mpViewport);

	mpToolbar = std::make_unique<EditorToolbar>(root, mpTopViewPanel->GetCanvas());
	root_sizer->Add(mpToolbar->GetSizer(), wxSizerFlags().Expand().Border(wxALL, 4));

	body_sizer->Add(mpSideSplitter, wxSizerFlags(1).Expand());

	mpViewport->Bind(wxEVT_SET_FOCUS,
					 [this](wxFocusEvent& event)
					 {
						 mbSplitFocusOn2D = false;
						 event.Skip();
					 });
	mpTopViewPanel->GetCanvas()->Bind(wxEVT_SET_FOCUS,
									  [this](wxFocusEvent& event)
									  {
										  mbSplitFocusOn2D = true;
										  event.Skip();
									  });

	mpSidePanel = new wxPanel(mpSideSplitter, wxID_ANY);
	wxBoxSizer* side_sizer = new wxBoxSizer(wxVERTICAL);

	mpSideToggleButton = new wxButton(mpSidePanel, wxID_ANY, wxString::FromUTF8("Hide"), wxDefaultPosition,
									  wxDefaultSize, wxBU_EXACTFIT);
	mpSideToggleButton->SetToolTip("Minimize the side panel");
	mpSideToggleButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetSidePanelCollapsed(true); });
	side_sizer->Add(mpSideToggleButton, wxSizerFlags().Right().Border(wxALL, 2));

	mpSideScroller = new wxScrolledWindow(mpSidePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	mpSideScroller->SetScrollRate(0, 12);

	wxBoxSizer* component_panel = new wxBoxSizer(wxVERTICAL);

	mpMaterialPickerPanel = new MaterialPickerPanel(mpSideScroller, this);
	component_panel->Add(mpMaterialPickerPanel, wxSizerFlags().Expand());

	mpWorldPropertiesPanel = new WorldPropertiesPanel(mpSideScroller);
	component_panel->Add(mpWorldPropertiesPanel, wxSizerFlags().Expand());

	mpObjectPropertiesPanel = new ObjectPropertiesPanel(mpSideScroller);
	component_panel->Add(mpObjectPropertiesPanel, wxSizerFlags().Expand());

	// The tool settings slot starts empty - RaptorEditor::SetTool swaps in a panel for tools that have one. It sits
	// right below Object Properties
	mpComponentSizer = component_panel;
	mpSideScroller->SetSizer(component_panel);
	component_panel->FitInside(mpSideScroller);

	mpSideScroller->Bind(wxEVT_SIZE,
						 [this](wxSizeEvent& event)
						 {
							 mpComponentSizer->FitInside(mpSideScroller);
							 event.Skip();
						 });

	side_sizer->Add(mpSideScroller, wxSizerFlags(1).Expand());
	mpSidePanel->SetSizer(side_sizer);

	// Only the height scrolls, so reserve room for the scroll bar next to the widest content
	mSidePanelWidth = std::max(component_panel->GetMinSize().x, scSidePanelWidth) +
					  wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, mpSideScroller);

	mpSideSplitter->SplitVertically(mpSplitter, mpSidePanel, -mSidePanelWidth);
	mpSideSplitter->SetMinSize(
		wxSize(viewport_size.x + mpSideSplitter->GetSashSize() + mSidePanelWidth, viewport_size.y));

	mpSideExpandButton = new wxButton(root, wxID_ANY, wxString::FromUTF8("<"), wxDefaultPosition, wxDefaultSize,
									  wxBU_EXACTFIT);
	mpSideExpandButton->SetToolTip("Show the side panel");
	mpSideExpandButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetSidePanelCollapsed(false); });
	mpSideExpandButton->Hide();
	body_sizer->Add(mpSideExpandButton, wxSizerFlags().Top().Border(wxALL, 2));

	root_sizer->Add(body_sizer, wxSizerFlags(1).Expand());

	root->SetSizer(root_sizer);

	// Size the frame so the viewport starts at the requested size, then let it shrink
	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizerAndFit(frame_sizer);

	mpViewport->SetMinSize(wxSize(64, 64));
	mpSplitter->SetMinSize(wxSize(64, 64));
	mpSideSplitter->SetMinSize(wxSize(64, 64));

	mpSideSplitter->SetSashPosition(-(mSidePanelWidth + mpSideSplitter->GetSashSize()));
	SetMinSize(wxDefaultSize);

	ApplyTheme();

	Bind(wxEVT_CLOSE_WINDOW, &EditorFrame::OnClose, this);
	Bind(wxEVT_ACTIVATE, &EditorFrame::OnActivate, this);
	Bind(wxEVT_ICONIZE, &EditorFrame::OnIconize, this);
}

static void ApplyThemeToTree(wxWindow* window, const wxColour& text)
{
	if (wxDynamicCast(window, wxStaticText) || wxDynamicCast(window, wxCheckBox) ||
		wxDynamicCast(window, wxRadioButton)) {
		window->SetForegroundColour(text);
	}

	window->Refresh();

	for (wxWindow* child : window->GetChildren()) {
		if (!child->IsTopLevel()) {
			ApplyThemeToTree(child, text);
		}
	}
}

static wxApp::Appearance ToWxAppearance(EditorTheme::eAppearance appearance)
{
	switch (appearance) {
	case EditorTheme::eAppearance::Light:
		return wxApp::Appearance::Light;
	case EditorTheme::eAppearance::System:
		return wxApp::Appearance::System;
	case EditorTheme::eAppearance::Dark:
		break;
	}

	return wxApp::Appearance::Dark;
}

void EditorFrame::ApplyTheme()
{
	const EditorTheme& theme = GetEditorTheme();

	if (wxTheApp->SetAppearance(ToWxAppearance(theme.Appearance)) != wxApp::AppearanceResult::Ok) {
		LogWarning(LC_CORE, "Could not set the editor appearance for theme '{}'", theme.ID.CStr());
	}
	const wxColour background = ToWxColour(theme.Frame.Background);

	SetBackgroundColour(background);
	mpRootPanel->SetBackgroundColour(background);
	mpSideSplitter->SetBackgroundColour(background);
	mpSplitter->SetBackgroundColour(background);
	mpTopViewPanel->ApplyTheme();

	ApplyThemeToTree(this, ToWxColour(theme.Frame.Text));

	thread::PostToGame([theme] { gEditor->SetTheme(theme); });
}

void EditorFrame::SelectTheme(uint32 index)
{
	EditorThemeLibrary& library = GetThemeLibrary();
	library.Select(index);

	ApplyTheme();

	thread::PostToGame([id = library.GetActive().ID] { StoreThemeChoice(id); });
}

void EditorFrame::ReloadTheme()
{
	if (!GetThemeLibrary().ReloadActive()) {
		LogWarning(LC_CORE, "Could not reload editor theme '{}'", GetEditorTheme().ID.CStr());
		return;
	}

	ApplyTheme();
}

void EditorFrame::CreateSubview()
{
	if (!mState.Object.bHasObject) {
		wxMessageBox("Select the brushes to make a subview from first.", "Subview", wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxTextEntryDialog dialog(this, "Name of the new subview:", "Create Subview", "subview");

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const String name(dialog.GetValue().ToStdString());

	thread::PostToGame([name] { gEditor->CreateSubviewFromSelection(name); });
}

void EditorFrame::PlaceSubview()
{
	static const wxString scBrowseChoice = "Browse for a map...";

	wxArrayString names = wxSplit(wxString::FromUTF8(mState.SubviewNames.CStr()), '\n');
	names.Add(scBrowseChoice);

	wxSingleChoiceDialog dialog(this, "Subview to place at the crosshair:", "Place Subview", names);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	if (dialog.GetStringSelection() != scBrowseChoice) {
		const String name(dialog.GetStringSelection().ToStdString());

		thread::PostToGame([name] { gEditor->PlaceSubview(name); });
		return;
	}

	if (mState.BlockoutPath.IsEmpty()) {
		wxMessageBox("Save the prototype first, a subview is found from where the prototype is.", "Subview",
					 wxOK | wxICON_INFORMATION, this);
		return;
	}

	const std::filesystem::path folder = GetBlockoutDialogStart(mState.BlockoutPath).parent_path();

	wxFileDialog file_dialog(this, "Place Map as Subview", folder.string(), "", "Prototype files (*.prx)|*.prx",
							 wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (file_dialog.ShowModal() != wxID_OK) {
		return;
	}

	std::error_code error;
	const std::filesystem::path relative = std::filesystem::relative(
		std::filesystem::path(file_dialog.GetPath().ToStdString()), folder, error);

	if (error || relative.empty()) {
		wxMessageBox("That map can't be reached from the folder of this prototype.", "Subview",
					 wxOK | wxICON_INFORMATION, this);
		return;
	}

	const String name(relative.generic_string());

	thread::PostToGame([name] { gEditor->PlaceSubview(name); });
}

void EditorFrame::SetInteractive(bool interactive)
{
	Enable(interactive);

	if (wxMenuBar* menu_bar = GetMenuBar(); menu_bar != nullptr) {
		for (size_t i = 0; i < menu_bar->GetMenuCount(); i++) {
			menu_bar->EnableTop(i, interactive);
		}
	}

	if (interactive) {
		FocusActiveView();
	}
}

void EditorFrame::SetSidePanelCollapsed(bool collapsed)
{
	if (collapsed == mbSidePanelCollapsed) {
		return;
	}

	mbSidePanelCollapsed = collapsed;

	if (collapsed) {
		mSidePanelWidth = mpSidePanel->GetSize().x;
		mpSideSplitter->Unsplit(mpSidePanel);
	}
	else {
		mpSideSplitter->SplitVertically(mpSplitter, mpSidePanel,
										-(mSidePanelWidth + mpSideSplitter->GetSashSize()));
		mpSidePanel->Show();
	}

	mpSideExpandButton->Show(collapsed);
	mpRootPanel->Layout();

	FocusActiveView();
}

void EditorFrame::FocusActiveView()
{
	const bool use_2d = (mView == eEditorView::Top) || (mView == eEditorView::Split && mbSplitFocusOn2D);

	if (use_2d && mpTopViewPanel != nullptr) {
		mpTopViewPanel->GetCanvas()->SetFocus();
	}
	else if (mpViewport != nullptr) {
		mpViewport->SetFocus();
	}
}

void EditorFrame::ShowView(const eEditorView view)
{
	mView = view;

	mpToolbar->ShowView(view);

	switch (view) {
	case eEditorView::Top:
		mpTopMenuItem->Check(true);
		break;
	case eEditorView::Split:
		mpSplitMenuItem->Check(true);
		break;
	default:
		mpPerspectiveMenuItem->Check(true);
		break;
	}

	ApplySplitLayout(view);

	mpRootPanel->Layout();

	FocusActiveView();
}

void EditorFrame::ApplySplitLayout(const eEditorView view)
{
	const wxSplitMode wanted_mode = mbStackedSplit ? wxSPLIT_HORIZONTAL : wxSPLIT_VERTICAL;

	const auto extent = [this](wxSplitMode mode)
	{
		const wxSize size = mpSplitter->GetClientSize();
		return std::max((mode == wxSPLIT_HORIZONTAL) ? size.y : size.x, 1);
	};

	if (mpSplitter->IsSplit()) {
		mSashFraction = std::clamp(static_cast<float32>(mpSplitter->GetSashPosition()) /
									   static_cast<float32>(extent(mpSplitter->GetSplitMode())),
								   0.1f, 0.9f);

		if (view != eEditorView::Split || mpSplitter->GetSplitMode() != wanted_mode) {
			wxWindow* removed = (view == eEditorView::Top) ? static_cast<wxWindow*>(mpViewport)
														   : static_cast<wxWindow*>(mpTopViewPanel);

			mpSplitter->Unsplit(removed);
		}
	}

	if (view == eEditorView::Split) {
		if (!mpSplitter->IsSplit()) {
			const int position = static_cast<int>(mSashFraction * static_cast<float32>(extent(wanted_mode)));

			if (mbStackedSplit) {
				mpSplitter->SplitHorizontally(mpViewport, mpTopViewPanel, position);
			}
			else {
				mpSplitter->SplitVertically(mpViewport, mpTopViewPanel, position);
			}

			mpViewport->Show();
			mpTopViewPanel->Show();
		}

		return;
	}

	wxWindow* wanted = (view == eEditorView::Top) ? static_cast<wxWindow*>(mpTopViewPanel)
												  : static_cast<wxWindow*>(mpViewport);
	wxWindow* current = mpSplitter->GetWindow1();

	if (current != wanted) {
		mpSplitter->ReplaceWindow(current, wanted);
		current->Hide();
	}

	wanted->Show();
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

		mpComponentSizer->Insert(scToolSettingsSlotIndex, mpToolSettingsPanel, wxSizerFlags().Expand());
	}

	mpComponentSizer->FitInside(mpSideScroller);
}

void EditorFrame::ShowSelectedTool(const eEditorTool tool) { mpTopViewPanel->GetCanvas()->ShowTool(tool); }

void EditorFrame::ShowSimulating(bool simulating)
{
	mpToolbar->ShowSimulating(simulating);

	FocusActiveView();
}

void EditorFrame::ApplyState(const EditorPanelState& state)
{
	mState = state;

	mpWorldPropertiesPanel->ApplyState(mState.World);
	mpObjectPropertiesPanel->ApplyState(mState.Object, mState.Materials);

	if (mpToolSettingsPanel != nullptr) {
		mpToolSettingsPanel->ApplyState(mState);
	}

	mpMaterialPickerPanel->OnStateChanged();

	if (mState.TopView.bActive) {
		mpTopViewPanel->ApplyState(mState.TopView);
		mpToolbar->ApplyState(mState.TopView);
	}
}

void EditorFrame::ShowMode(const eEditorMode mode, const eDataFilter filter, const uint32 available_tools)
{
	mpToolbar->ShowMode(mode, filter, available_tools);
	mpTopViewPanel->GetCanvas()->SetAvailableTools(available_tools);

	FocusActiveView();
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

void EditorFrame::SetMaterialPickerShown(bool shown)
{
	mpMaterialPickerMenuItem->Check(shown);
	mpComponentSizer->Show(mpMaterialPickerPanel, shown);
	mpComponentSizer->FitInside(mpSideScroller);
}

void EditorFrame::ShowMaterialPicker()
{
	if (mbSidePanelCollapsed) {
		SetSidePanelCollapsed(false);
	}

	SetMaterialPickerShown(true);

	int32 scroll_unit_y = 1;
	mpSideScroller->GetScrollPixelsPerUnit(nullptr, &scroll_unit_y);

	const int32 panel_top = mpSideScroller->CalcUnscrolledPosition(mpMaterialPickerPanel->GetPosition()).y;
	mpSideScroller->Scroll(-1, panel_top / std::max(scroll_unit_y, 1));

	mpMaterialPickerPanel->FocusSearch();
}

void EditorFrame::ShowAtlasPackerWindow()
{
	if (mpAtlasPackerWindow == nullptr) {
		mpAtlasPackerWindow = new AtlasPackerWindow(this);
	}

	mpAtlasPackerWindow->Show();
	mpAtlasPackerWindow->Raise();
}

void EditorFrame::OnClose(wxCloseEvent& event)
{
	if (!event.CanVeto() || !mState.bUnsavedChanges) {
		mbCloseRequested = true;
		return;
	}

	event.Veto();

	RunAfterUnsavedCheck("Save changes to the prototype before closing?", [this] { mbCloseRequested = true; });
}

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

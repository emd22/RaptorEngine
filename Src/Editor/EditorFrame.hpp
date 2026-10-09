#pragma once

#include "EditorPanelState.hpp"
#include "EditorTool.hpp"

#include <wx/frame.h>

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <atomic>

class wxBoxSizer;
class wxButton;
class wxChoice;
class wxMenuItem;
class wxPanel;
class wxScrolledWindow;
class wxSplitterWindow;

namespace fx::editor {

class ObjectPropertiesPanel;
class WorldPropertiesPanel;
class ToolSettingsBasePanel;
class ObjectListWindow;
class CVarListWindow;
class MaterialPickerWindow;
class AtlasPackerWindow;

class EditorViewport;
class OutlineToggleButton;
class TopViewPanel;

class EditorFrame : public wxFrame
{
public:
	EditorFrame(const wxString& title, const wxSize& viewport_size);

	FX_FORCE_INLINE EditorViewport* GetViewport() { return mpViewport; }
	FX_FORCE_INLINE TopViewPanel* GetTopViewPanel() { return mpTopViewPanel; }
	FX_FORCE_INLINE ObjectPropertiesPanel* GetObjectPropertiesPanel() { return mpObjectPropertiesPanel; }
	FX_FORCE_INLINE WorldPropertiesPanel* GetWorldPropertiesPanel() { return mpWorldPropertiesPanel; }
	FX_FORCE_INLINE ToolSettingsBasePanel* GetToolSettingsPanel() { return mpToolSettingsPanel; }

	/**
	 * @brief Swaps in `new_panel` as the tool settings slot below Object Properties, destroying whatever was shown
	 * there before. Pass nullptr to leave the slot empty. Takes ownership of `new_panel`.
	 */
	void SetToolSettingsPanel(ToolSettingsBasePanel* new_panel);

	FX_FORCE_INLINE bool IsCloseRequested() const { return mbCloseRequested; }

	void SetInteractive(bool interactive);

	/// Highlights the button of the selected tool
	void ShowSelectedTool(const eEditorTool tool);

	void ShowMode(const eEditorMode mode, const eDataFilter filter, const uint32 available_tools);

	void ShowView(const eEditorView view);

	void ApplyState(const EditorPanelState& state);

	FX_FORCE_INLINE const EditorPanelState& GetState() const { return mState; }

	FX_FORCE_INLINE bool IsActive() const { return mbIsActive; }

	void ShowObjectListWindow();
	void ShowCVarListWindow();
	void ShowMaterialPickerWindow();
	void ShowAtlasPackerWindow();

	void NewPrototype();
	void SavePrototype();
	void SaveProtoTypeAs();
	void OpenPrototype();

private:
	void OnClose(wxCloseEvent& event);
	void OnActivate(wxActivateEvent& event);
	void OnIconize(wxIconizeEvent& event);
	void SetSidePanelCollapsed(bool collapsed);
	void FocusActiveView();
	void ApplySplitLayout(const eEditorView view);

private:
	EditorViewport* mpViewport = nullptr;
	TopViewPanel* mpTopViewPanel = nullptr;
	ObjectPropertiesPanel* mpObjectPropertiesPanel = nullptr;
	WorldPropertiesPanel* mpWorldPropertiesPanel = nullptr;
	ObjectListWindow* mpObjectListWindow = nullptr;
	CVarListWindow* mpCVarListWindow = nullptr;
	MaterialPickerWindow* mpMaterialPickerWindow = nullptr;
	AtlasPackerWindow* mpAtlasPackerWindow = nullptr;

	/// The tool settings slot: whatever ToolSettingsBasePanel is currently swapped in, below Object Properties
	ToolSettingsBasePanel* mpToolSettingsPanel = nullptr;
	wxBoxSizer* mpComponentSizer = nullptr;
	wxScrolledWindow* mpSideScroller = nullptr;
	wxButton* mpSideToggleButton = nullptr;
	wxBoxSizer* mpSideColumn = nullptr;

	StackArray<OutlineToggleButton*, static_cast<uint32>(eEditorTool::Count)> mToolButtons;

	OutlineToggleButton* mpVisButton = nullptr;
	OutlineToggleButton* mpDataButton = nullptr;
	wxChoice* mpDataFilterChoice = nullptr;

	OutlineToggleButton* mpPerspectiveButton = nullptr;
	OutlineToggleButton* mpTopButton = nullptr;
	OutlineToggleButton* mpSplitButton = nullptr;
	wxMenuItem* mpPerspectiveMenuItem = nullptr;
	wxMenuItem* mpTopMenuItem = nullptr;
	wxMenuItem* mpSplitMenuItem = nullptr;
	wxMenuItem* mpStackMenuItem = nullptr;

	wxSplitterWindow* mpSplitter = nullptr;
	float32 mSashFraction = 0.5f;
	bool mbStackedSplit = false;
	bool mbSplitFocusOn2D = false;

	wxPanel* mpRootPanel = nullptr;
	wxBoxSizer* mpTopBarSizer = nullptr;
	wxBoxSizer* mpToolsGroup = nullptr;

	eEditorView mView = eEditorView::Perspective;

	std::atomic<bool> mbCloseRequested = false;
	std::atomic<bool> mbIsActive = true;
	bool mbSidePanelCollapsed = false;

	EditorPanelState mState;
};

} // namespace fx::editor

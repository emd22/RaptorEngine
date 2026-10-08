#pragma once

#include "EditorTool.hpp"

#include <wx/frame.h>

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>

class wxBoxSizer;
class wxButton;
class wxChoice;
class wxPanel;
class wxScrolledWindow;

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

class EditorFrame : public wxFrame
{
public:
	EditorFrame(const wxString& title, const wxSize& viewport_size);

	FX_FORCE_INLINE EditorViewport* GetViewport() { return mpViewport; }
	FX_FORCE_INLINE ObjectPropertiesPanel* GetObjectPropertiesPanel() { return mpObjectPropertiesPanel; }
	FX_FORCE_INLINE WorldPropertiesPanel* GetWorldPropertiesPanel() { return mpWorldPropertiesPanel; }
	FX_FORCE_INLINE ToolSettingsBasePanel* GetToolSettingsPanel() { return mpToolSettingsPanel; }

	/**
	 * @brief Swaps in `new_panel` as the tool settings slot below Object Properties, destroying whatever was shown
	 * there before. Pass nullptr to leave the slot empty. Takes ownership of `new_panel`.
	 */
	void SetToolSettingsPanel(ToolSettingsBasePanel* new_panel);

	FX_FORCE_INLINE bool IsCloseRequested() const { return mbCloseRequested; }

	/// Highlights the button of the selected tool
	void ShowSelectedTool(const eEditorTool tool);

	void ShowMode(const eEditorMode mode, const eDataFilter filter);

	/// False while the frame is minimized or another app is in front, so the render loop can throttle itself
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

private:
	EditorViewport* mpViewport = nullptr;
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

	bool mbCloseRequested = false;
	bool mbIsActive = true;
	bool mbSidePanelCollapsed = false;
};

} // namespace fx::editor

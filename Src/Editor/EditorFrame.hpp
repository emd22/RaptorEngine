#pragma once

#include "EditorTool.hpp"

#include <wx/frame.h>

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>

class wxToggleButton;
class wxBoxSizer;
class wxPanel;

namespace fx::editor {

class ObjectPropertiesPanel;
class WorldPropertiesPanel;
class ToolSettingsBasePanel;
class ObjectListWindow;
class CVarListWindow;
class MaterialPickerWindow;

class EditorViewport;

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

	/// False while the frame is minimized or another app is in front, so the render loop can throttle itself
	FX_FORCE_INLINE bool IsActive() const { return mbIsActive; }

	void ShowObjectListWindow();
	void ShowCVarListWindow();
	void ShowMaterialPickerWindow();

	void SaveBlockout();
	void SaveBlockoutAs();
	void OpenBlockout();

private:
	void OnClose(wxCloseEvent& event);
	void OnActivate(wxActivateEvent& event);
	void OnIconize(wxIconizeEvent& event);

private:
	EditorViewport* mpViewport = nullptr;
	ObjectPropertiesPanel* mpObjectPropertiesPanel = nullptr;
	WorldPropertiesPanel* mpWorldPropertiesPanel = nullptr;
	ObjectListWindow* mpObjectListWindow = nullptr;
	CVarListWindow* mpCVarListWindow = nullptr;
	MaterialPickerWindow* mpMaterialPickerWindow = nullptr;

	/// The tool settings slot: whatever ToolSettingsBasePanel is currently swapped in, below Object Properties
	ToolSettingsBasePanel* mpToolSettingsPanel = nullptr;
	wxBoxSizer* mpComponentSizer = nullptr;
	wxPanel* mpComponentParent = nullptr;

	StackArray<wxToggleButton*, static_cast<uint32>(eEditorTool::Count)> mToolButtons;

	bool mbCloseRequested = false;
	bool mbIsActive = true;
};

} // namespace fx::editor

#pragma once

#include <wx/frame.h>

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <InGameEditor.hpp>

class wxToggleButton;

namespace fx::editor {

class ObjectPropertiesPanel;
class WorldPropertiesPanel;

class EditorViewport;

class EditorFrame : public wxFrame
{
public:
	EditorFrame(const wxString& title, const wxSize& viewport_size);

	FX_FORCE_INLINE EditorViewport* GetViewport() { return mpViewport; }
	FX_FORCE_INLINE ObjectPropertiesPanel* GetObjectPropertiesPanel() { return mpObjectPropertiesPanel; }
	FX_FORCE_INLINE WorldPropertiesPanel* GetWorldPropertiesPanel() { return mpWorldPropertiesPanel; }

	FX_FORCE_INLINE eEditorTool GetSelectedTool() const { return mSelectedTool; }
	FX_FORCE_INLINE bool IsCloseRequested() const { return mbCloseRequested; }

	void SetEditorTool(const eEditorTool tool);
	void SetDefaultTool();

	FX_FORCE_INLINE bool IsSimulationMode() const { return mSelectedTool == eEditorTool::None; }

	/// False while the frame is minimized or another app is in front, so the render loop can throttle itself
	FX_FORCE_INLINE bool IsActive() const { return mbIsActive; }

private:
	void OnClose(wxCloseEvent& event);
	void OnActivate(wxActivateEvent& event);
	void OnIconize(wxIconizeEvent& event);

private:
	EditorViewport* mpViewport = nullptr;
	ObjectPropertiesPanel* mpObjectPropertiesPanel = nullptr;
	WorldPropertiesPanel* mpWorldPropertiesPanel = nullptr;

	StackArray<wxToggleButton*, static_cast<uint32>(eEditorTool::Count)> mToolButtons;
	eEditorTool mSelectedTool = eEditorTool::Translate;

	bool mbCloseRequested = false;
	bool mbIsActive = true;
};

} // namespace fx::editor

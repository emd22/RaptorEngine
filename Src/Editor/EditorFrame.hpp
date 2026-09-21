#pragma once

#include <wx/frame.h>

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>

class wxToggleButton;

namespace fx::editor {

class ObjectPropertiesPanel;
class EditorViewport;

enum class eEditorTool : uint32
{
	Transform,
	Face,
	Rotate,

	Count,
};

class EditorFrame : public wxFrame
{
public:
	EditorFrame(const wxString& title, const wxSize& viewport_size);

	FX_FORCE_INLINE EditorViewport* GetViewport() { return mpViewport; }
	FX_FORCE_INLINE ObjectPropertiesPanel* GetComponentPanel() { return mpComponentPanel; }

	FX_FORCE_INLINE eEditorTool GetSelectedTool() const { return mSelectedTool; }
	FX_FORCE_INLINE bool IsCloseRequested() const { return mbCloseRequested; }

private:
	void SelectTool(eEditorTool tool);

	void OnClose(wxCloseEvent& event);
	void OnActivate(wxActivateEvent& event);

private:
	EditorViewport* mpViewport = nullptr;
	ObjectPropertiesPanel* mpComponentPanel = nullptr;

	StackArray<wxToggleButton*, static_cast<uint32>(eEditorTool::Count)> mToolButtons;
	eEditorTool mSelectedTool = eEditorTool::Transform;

	bool mbCloseRequested = false;
};

} // namespace fx::editor

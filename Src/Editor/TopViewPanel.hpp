#pragma once

#ifdef FX_IS_EDITOR

#include "EditorPanelState.hpp"
#include "TopViewCanvas.hpp"
#include "TopViewPlane.hpp"

#include <wx/panel.h>

#include <Core/Types.hpp>

class wxBoxSizer;
class wxStaticText;
class wxWrapSizer;

namespace fx::editor {

class FloatField;
class OutlineToggleButton;

class TopViewPanel : public wxPanel
{
public:
	explicit TopViewPanel(wxWindow* parent);

	FX_FORCE_INLINE TopViewCanvas* GetCanvas() { return mpCanvas; }

	void ApplyState(const TopViewState& state);

private:
	void ShowTool(eTopViewTool tool);
	void ShowPlane(eViewPlane plane);

private:
	TopViewCanvas* mpCanvas = nullptr;
	OutlineToggleButton* mpToolButtons[static_cast<uint32>(eTopViewTool::Count)] = {};
	OutlineToggleButton* mpPlaneButtons[scViewPlaneCount] = {};
	wxWrapSizer* mpToolbar = nullptr;
	wxBoxSizer* mpVolumeRows[scViewPlaneCount] = {};
	wxStaticText* mpStatus = nullptr;

	float32 mBase[scViewPlaneCount] = { 0.0f, 0.0f, 0.0f };
	float32 mSize[scViewPlaneCount] = { 1.0f, 1.0f, 1.0f };
};

} // namespace fx::editor

#endif

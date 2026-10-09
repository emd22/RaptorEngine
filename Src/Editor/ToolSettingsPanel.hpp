#pragma once

#include "EditorPanelState.hpp"

#include <wx/panel.h>
#include <wx/string.h>

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>

class wxStaticText;
class wxColourPickerCtrl;
class wxColourPickerEvent;
class wxBoxSizer;
class wxSpinCtrl;
class wxSpinCtrlDouble;

namespace fx::editor {

class Vector3Field;
class FloatField;

class ToolSettingsBasePanel : public wxPanel
{
public:
	ToolSettingsBasePanel() = default;

	virtual void Construct(wxBoxSizer* tool_panel) = 0;
	virtual void ApplyState(const EditorPanelState& state) = 0;

	virtual ~ToolSettingsBasePanel() = default;
};


class LightToolSettingsPanel : public ToolSettingsBasePanel
{
public:
	void Construct(wxBoxSizer* tool_panel) override;
	void ApplyState(const EditorPanelState& state) override;

private:
	void OnColorChange(wxColourPickerEvent& event);

private:
	wxStaticText* mpNameLabel = nullptr;
	wxColourPickerCtrl* mpColorPicker = nullptr;

	Vector3Field* mpPositionField = nullptr;
	FloatField* mpRadiusField = nullptr;
	FloatField* mpIntensityField = nullptr;
	FloatField* mpLumensField = nullptr;
	FloatField* mpOuterAngleField = nullptr;
	FloatField* mpInnerAngleField = nullptr;

	bool mbShowingAnything = false;
};

} // namespace fx::editor

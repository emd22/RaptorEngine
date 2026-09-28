#pragma once

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

namespace fx {

class LightSpot;

} // namespace fx

namespace fx::editor {

class Vector3Field;

class ToolSettingsBasePanel : public wxPanel
{
public:
	ToolSettingsBasePanel() = default;

	virtual void Construct(wxBoxSizer* tool_panel) = 0;
	virtual void Refresh() = 0;

	virtual ~ToolSettingsBasePanel() = default;
};


class LightToolSettingsPanel : public ToolSettingsBasePanel
{
public:
	void Construct(wxBoxSizer* tool_panel) override;
	void Refresh() override;

private:
	void OnColorChange(wxColourPickerEvent& event);

private:
	wxStaticText* mpNameLabel = nullptr;
	wxColourPickerCtrl* mpColorPicker = nullptr;
	Vector3Field* mpPositionField = nullptr;
	wxSpinCtrlDouble* mpRadiusField = nullptr;


	LightSpot* mpShownLight = nullptr;
	bool mbShowingAnything = false;
};

} // namespace fx::editor

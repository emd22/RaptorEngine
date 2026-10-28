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
class wxChoice;
class wxCommandEvent;

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


class SoundToolSettingsPanel : public ToolSettingsBasePanel
{
public:
	void Construct(wxBoxSizer* tool_panel) override;
	void ApplyState(const EditorPanelState& state) override;

private:
	void OnSoundChoice(wxCommandEvent& event);
	void SetControlsEnabled(bool enabled);

private:
	wxStaticText* mpNameLabel = nullptr;
	wxChoice* mpSoundChoice = nullptr;

	Vector3Field* mpPositionField = nullptr;
	FloatField* mpVolumeField = nullptr;
	FloatField* mpMinIntervalField = nullptr;
	FloatField* mpMaxIntervalField = nullptr;

	String mSoundNames;
	bool mbShowingAnything = true;
};

} // namespace fx::editor

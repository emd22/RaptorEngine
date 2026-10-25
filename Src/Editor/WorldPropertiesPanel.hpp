#pragma once

#include "EditorPanelState.hpp"

#include <wx/panel.h>
#include <wx/string.h>

#include <Core/StackArray.hpp>
#include <Object/Object.hpp>

class wxCheckBox;
class wxChoice;
class wxColourPickerCtrl;
class wxStaticText;
class wxSizer;

namespace fx::editor {

class Vector3Field;
class FloatField;

class WorldPropertiesPanel : public wxPanel
{
public:
	explicit WorldPropertiesPanel(wxWindow* parent);

	void ApplyState(const WorldPanelState& state);

private:
	void BuildSunSection(wxSizer* sizer);
	void ApplySunState(const WorldPanelState& state);

	void BuildFogSection(wxSizer* sizer);
	void ApplyFogState(const WorldPanelState& state);
	void PostFogSettings();

	void BuildReflectionSection(wxSizer* sizer);
	void ApplyReflectionState(const WorldPanelState& state);

private:
	wxStaticText* mpNameLabel = nullptr;
	Vector3Field* mpPositionField = nullptr;

	wxChoice* mpDebugLayerChoice = nullptr;
	int64 mShownDebugMask = -1;

	FloatField* mpApertureField = nullptr;
	FloatField* mpShutterField = nullptr;
	FloatField* mpIsoField = nullptr;
	FloatField* mpCompensationField = nullptr;

	wxCheckBox* mpSunEnabledCheck = nullptr;
	wxColourPickerCtrl* mpSunColorPicker = nullptr;
	FloatField* mpSunIntensityField = nullptr;
	FloatField* mpSunAzimuthField = nullptr;
	FloatField* mpSunElevationField = nullptr;
	wxStaticText* mpSunStatus = nullptr;
	bool mbShowingSun = true;
	bool mbShowingSunStale = true;

	wxCheckBox* mpFogEnabledCheck = nullptr;
	wxChoice* mpFogQualityChoice = nullptr;
	wxColourPickerCtrl* mpFogColorPicker = nullptr;
	FloatField* mpFogDensityField = nullptr;
	FloatField* mpFogHeightField = nullptr;
	FloatField* mpFogFalloffField = nullptr;
	FloatField* mpFogHazeField = nullptr;
	FloatField* mpFogAnisotropyField = nullptr;
	FloatField* mpFogAmbientField = nullptr;
	int64 mShownFogQuality = -1;

	wxStaticText* mpReflectionStatus = nullptr;
	wxCheckBox* mpReflectionEnabledCheck = nullptr;
	wxCheckBox* mpReflectionFallbackCheck = nullptr;
	wxCheckBox* mpShowProbeVolumesCheck = nullptr;
	wxChoice* mpReflectionDebugChoice = nullptr;
	wxString mShownReflectionStatus;
	int64 mShownReflectionDebug = -1;

	Vec3f mShownPosition = Vec3f::sZero;
	bool mbShowingAnything = true;
};

} // namespace fx::editor

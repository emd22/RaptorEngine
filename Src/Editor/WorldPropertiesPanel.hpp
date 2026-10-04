#pragma once

#include <wx/collpane.h>
#include <wx/panel.h>
#include <wx/string.h>

#include <Core/StackArray.hpp>
#include <Object/Object.hpp>

class wxCheckBox;
class wxChoice;
class wxStaticText;
class wxSizer;

namespace fx::editor {

class Vector3Field;
class FloatField;

class WorldPropertiesPanel : public wxPanel
{
public:
	explicit WorldPropertiesPanel(wxWindow* parent);

	void Update();

private:
	void BuildReflectionPane(wxSizer* sizer);
	void UpdateReflectionPane();
	void OnPaneChanged();

private:
	wxStaticText* mpNameLabel = nullptr;
	Vector3Field* mpPositionField = nullptr;

	wxChoice* mpDebugLayerChoice = nullptr;
	int64 mShownDebugMask = -1;

	wxCollapsiblePane* mpCameraPane = nullptr;
	FloatField* mpApertureField = nullptr;
	FloatField* mpShutterField = nullptr;
	FloatField* mpIsoField = nullptr;
	FloatField* mpCompensationField = nullptr;

	wxCollapsiblePane* mpReflectionPane = nullptr;
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

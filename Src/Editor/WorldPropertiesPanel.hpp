#pragma once

#include <wx/collpane.h>
#include <wx/panel.h>
#include <wx/string.h>

#include <Core/StackArray.hpp>
#include <Object/Object.hpp>

class wxCheckBox;
class wxChoice;
class wxStaticText;

namespace fx::editor {

class Vector3Field;
class FloatField;

class WorldPropertiesPanel : public wxPanel
{
public:
	explicit WorldPropertiesPanel(wxWindow* parent);

	void Update();

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

	Vec3f mShownPosition = Vec3f::sZero;
	bool mbShowingAnything = true;
};

} // namespace fx::editor

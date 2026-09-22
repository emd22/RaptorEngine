#pragma once

#include <wx/panel.h>
#include <wx/string.h>

#include <Core/StackArray.hpp>
#include <Object/Object.hpp>

class wxCheckBox;
class wxStaticText;

namespace fx::editor {

class EditableVector3Field;

class WorldPropertiesPanel : public wxPanel
{
public:
	explicit WorldPropertiesPanel(wxWindow* parent);

	void Update();

private:
	wxStaticText* mpNameLabel = nullptr;
	EditableVector3Field* mpPositionField = nullptr;

	Vec3f mShownPosition = Vec3f::sZero;
	bool mbShowingAnything = true;
};

} // namespace fx::editor

#pragma once

#include <wx/panel.h>
#include <wx/string.h>

#include <Core/StackArray.hpp>
#include <Object/Object.hpp>

class wxCheckBox;
class wxChoice;
class wxCommandEvent;
class wxStaticText;

namespace fx::editor {

/**
 * @brief Properties panel for blockout objects
 */
class ObjectPropertiesPanel : public wxPanel
{
public:
	static constexpr uint32 scMaxRows = 16;

	struct FlagRow
	{
		uint32 Bit = 0;
		wxCheckBox* pCheckBox = nullptr;
	};

public:
	explicit ObjectPropertiesPanel(wxWindow* parent);

	/**
	 * @brief Show a really basic properties panel for a selected blockout object. Should expand this to normal objects
	 * as well, but I need to add better object picking.
	 */
	void ShowObject(Object* object);

private:
	void SetRows(StackArray<FlagRow, scMaxRows>& rows, uint32 value, bool has_object);
	void OnMaterialChoice(wxCommandEvent& event);

private:
	wxStaticText* mpNameLabel = nullptr;
	wxChoice* mpMaterialChoice = nullptr;

	StackArray<FlagRow, scMaxRows> mTagRows;
	StackArray<FlagRow, scMaxRows> mFlagRows;

	// What is currently shown, to skip redundant updates
	Object* mpShownObject = nullptr;
	wxString mShownName;
	uint32 mShownTags = 0;
	uint32 mShownFlags = 0;
	int32 mShownMaterialSlot = -1;

	// Show Anything ...is a real bool
	bool mbShowingAnything = true;
};

} // namespace fx::editor

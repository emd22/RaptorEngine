#pragma once

#include "EditorPanelState.hpp"

#include <wx/panel.h>
#include <wx/string.h>

#include <Core/StackArray.hpp>
#include <Object/Object.hpp>
#include <string>
#include <vector>

class wxCheckBox;
class wxChoice;
class wxButton;
class wxCommandEvent;
class wxStaticText;
class wxTextCtrl;

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

	void ApplyState(const ObjectPanelState& state, const MaterialLibraryState& materials);

private:
	void BindRows(StackArray<FlagRow, scMaxRows>& rows, bool is_tag);
	void OnRowToggled(bool is_tag, uint32 bit, bool checked);
	void SetRows(StackArray<FlagRow, scMaxRows>& rows, uint32 value, uint32 editable, bool has_object);
	void OnMaterialChoice(wxCommandEvent& event);
	void RefreshMaterialChoices(const MaterialLibraryState& materials);
	void CommitScript();
	void BrowseScript();
	void OnEnterDirectionChoice(wxCommandEvent& event);

private:
	wxStaticText* mpNameLabel = nullptr;
	wxChoice* mpMaterialChoice = nullptr;
	wxTextCtrl* mpScriptText = nullptr;
	wxButton* mpScriptBrowse = nullptr;
	wxButton* mpScriptClear = nullptr;
	wxStaticText* mpScriptStatus = nullptr;
	wxChoice* mpEnterDirChoice = nullptr;

	StackArray<FlagRow, scMaxRows> mTagRows;
	StackArray<FlagRow, scMaxRows> mFlagRows;

	std::vector<std::string> mShownMaterialNames;
	std::string mShownScript;

	bool mbHasObject = false;
	bool mbCanAttachScript = false;
	bool mbIsTrigger = false;
	bool mbShownScriptErrors = false;
};

} // namespace fx::editor

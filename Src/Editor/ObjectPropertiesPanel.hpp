#pragma once

#include "EditorPanelState.hpp"

#include <wx/panel.h>
#include <wx/string.h>

#include <Core/DynArray.hpp>
#include <Core/StackArray.hpp>
#include <Object/ObjectFlags.hpp>


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

	template <typename TState>
	struct FlagRow
	{
		TState Bit = TState::None;
		wxCheckBox* pCheckBox = nullptr;
	};

	template <typename TState>
	using FlagRows = StackArray<FlagRow<TState>, scMaxRows>;

public:
	explicit ObjectPropertiesPanel(wxWindow* parent);

	void ApplyState(const ObjectPanelState& state, const MaterialLibraryState& materials);

private:
	template <typename TState>
	void BindRows(FlagRows<TState>& rows);

	template <typename TState>
	void OnRowToggled(TState bit, bool checked);

	template <typename TState>
	void SetRows(FlagRows<TState>& rows, TState value, TState editable, bool has_object);

	void OnMaterialChoice(wxCommandEvent& event);
	void RefreshMaterialChoices(const MaterialLibraryState& materials);
	void CommitName();
	void CommitScript();
	void BrowseScript();
	void OnEnterDirectionChoice(wxCommandEvent& event);

private:
	wxStaticText* mpNameLabel = nullptr;
	wxTextCtrl* mpNameText = nullptr;
	wxChoice* mpMaterialChoice = nullptr;
	wxTextCtrl* mpScriptText = nullptr;
	wxButton* mpScriptBrowse = nullptr;
	wxButton* mpScriptClear = nullptr;
	wxStaticText* mpScriptStatus = nullptr;
	wxChoice* mpEnterDirChoice = nullptr;

	FlagRows<eObjectTag> mTagRows;
	FlagRows<eObjectFlags> mFlagRows;

	DynArray<String> mShownMaterialNames;
	String mShownScript;
	String mShownName;

	bool mbHasObject = false;
	bool mbCanAttachScript = false;
	bool mbCanRename = false;
	bool mbIsTrigger = false;
	bool mbShownScriptErrors = false;
};

} // namespace fx::editor

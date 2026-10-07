#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <vector>

class wxButton;
class wxListCtrl;
class wxListEvent;
class wxStaticText;
class wxTextCtrl;

namespace fx {

class CVarValue;

namespace editor {

class CVarListWindow : public wxFrame
{
public:
	explicit CVarListWindow(wxWindow* parent);

	void RefreshList();

private:
	void RebuildRows(const CVarValue* previously_selected);
	void UpdateValues();
	void ShowSelected();
	void ApplyValue();

	CVarValue* GetSelectedCVar() const;

	void OnShow(wxShowEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnClose(wxCloseEvent& event);

private:
	wxTextCtrl* mpFilter = nullptr;
	wxListCtrl* mpList = nullptr;
	wxStaticText* mpSelectedLabel = nullptr;
	wxTextCtrl* mpValueField = nullptr;
	wxButton* mpSetButton = nullptr;
	wxStaticText* mpStatus = nullptr;

	wxTimer mRefreshTimer;

	/// CVars currently shown, in row order
	std::vector<CVarValue*> mShown;
};

} // namespace editor
} // namespace fx

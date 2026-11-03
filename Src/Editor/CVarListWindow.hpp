#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <Core/DynArray.hpp>
#include <Core/String.hpp>

class wxButton;
class wxListCtrl;
class wxListEvent;
class wxStaticText;
class wxTextCtrl;

namespace fx::editor {

class CVarListWindow : public wxFrame
{
public:
	explicit CVarListWindow(wxWindow* parent);

	void RefreshList();

	struct Row
	{
		String Name;
		String Type;
		String Value;
	};

	using RowArray = DynArray<Row, GrowthFunctions::Double>;

private:
	void ShowRows(RowArray rows);
	void RebuildRows(const String& previously_selected);
	void UpdateValues();
	void ShowSelected();
	void ApplyValue();

	const Row* GetSelectedRow() const;

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

	RowArray mShown;
};

} // namespace fx::editor

#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <string>
#include <vector>

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
		std::string Name;
		std::string Type;
		std::string Value;
	};

private:
	void ShowRows(std::vector<Row> rows);
	void RebuildRows(const std::string& previously_selected);
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

	std::vector<Row> mShown;
};

} // namespace fx::editor

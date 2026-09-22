#pragma once

#include <wx/frame.h>

class wxListCtrl;
class wxListEvent;

namespace fx::editor {

class ObjectListWindow : public wxFrame
{
public:
	explicit ObjectListWindow(wxWindow* parent);

	void RefreshList();

private:
	void OnRefreshButton(wxCommandEvent& event);
	void OnItemActivated(wxListEvent& event);
	void OnClose(wxCloseEvent& event);

private:
	wxListCtrl* mpList = nullptr;
};

} // namespace fx::editor

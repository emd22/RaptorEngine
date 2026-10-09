#pragma once

#include <wx/frame.h>

#include <Core/Types.hpp>
#include <string>
#include <vector>

class wxListCtrl;
class wxListEvent;

namespace fx::editor {

class ObjectListWindow : public wxFrame
{
public:
	explicit ObjectListWindow(wxWindow* parent);

	void RefreshList();

	struct Row
	{
		std::string Name;
		std::string Tags;
		std::string Position;
		uint32 ID = 0;
	};

private:
	void ShowRows(const std::vector<Row>& rows);

	void OnRefreshButton(wxCommandEvent& event);
	void OnItemActivated(wxListEvent& event);
	void OnClose(wxCloseEvent& event);

private:
	wxListCtrl* mpList = nullptr;
};

} // namespace fx::editor

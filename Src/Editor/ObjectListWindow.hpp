#pragma once

#include <wx/frame.h>

#include <Core/DynArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>

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
		String Name;
		String Tags;
		String Position;
		uint32 ID = 0;
	};

private:
	void ShowRows(const DynArray<Row>& rows);

	void OnRefreshButton(wxCommandEvent& event);
	void OnItemActivated(wxListEvent& event);
	void OnClose(wxCloseEvent& event);

private:
	wxListCtrl* mpList = nullptr;
};

} // namespace fx::editor

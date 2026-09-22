#include "ObjectListWindow.hpp"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include <Core/SizedArray.hpp>
#include <Engine.hpp>
#include <InGameEditor.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>

namespace fx::editor {

enum eColumn : int32
{
	Column_Name = 0,
	Column_Id,
	Column_Tags,
	Column_Position,
};

struct TagName
{
	eObjectTag Tag;
	const char* pName;
};

static constexpr TagName scTagNames[] = {
	{ eObjectTag::Blockout, "Blockout" },
	{ eObjectTag::LockTransform, "Lock Transform" },
	{ eObjectTag::ProbeVolume, "Probe Volume" },
};

static wxString BuildTagsList(eObjectTag tags)
{
	wxString description;

	for (const TagName& tag_name : scTagNames) {
		if (HasFlag(tags, tag_name.Tag)) {
			if (!description.IsEmpty()) {
				description += ", ";
			}

			description += tag_name.pName;
		}
	}

	return description;
}

ObjectListWindow::ObjectListWindow(wxWindow* parent)
	: wxFrame(parent, wxID_ANY, "Object List", wxDefaultPosition, wxSize(480, 400))
{
	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	mpList = new wxListCtrl(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->InsertColumn(Column_Name, "Name", wxLIST_FORMAT_LEFT, 160);
	mpList->InsertColumn(Column_Id, "ID", wxLIST_FORMAT_LEFT, 80);
	mpList->InsertColumn(Column_Tags, "Tags", wxLIST_FORMAT_LEFT, 140);
	mpList->InsertColumn(Column_Position, "Position", wxLIST_FORMAT_LEFT, 160);

	sizer->Add(mpList, wxSizerFlags(1).Expand().Border(wxALL, 6));

	wxButton* refresh_button = new wxButton(root, wxID_ANY, "Refresh");
	sizer->Add(refresh_button, wxSizerFlags().Right().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	root->SetSizer(sizer);

	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizer(frame_sizer);

	refresh_button->Bind(wxEVT_BUTTON, &ObjectListWindow::OnRefreshButton, this);
	mpList->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ObjectListWindow::OnItemActivated, this);
	Bind(wxEVT_CLOSE_WINDOW, &ObjectListWindow::OnClose, this);

	RefreshList();
}

void ObjectListWindow::RefreshList()
{
	if (gObjectManager == nullptr) {
		return;
	}

	SizedArray<Object*> objects = gObjectManager->CollectObjects();

	mpList->Freeze();
	mpList->DeleteAllItems();

	for (uint32 i = 0; i < objects.Size; i++) {
		Object* object = objects[i];

		if (object == nullptr) {
			continue;
		}

		const wxString name = wxString::FromUTF8(object->Name.Get());
		const Vec3f position = object->GetPosition();

		const long row = mpList->InsertItem(mpList->GetItemCount(), name.IsEmpty() ? wxString("(unnamed)") : name);

		mpList->SetItem(row, Column_Id, wxString::Format("%u", object->ID()));
		mpList->SetItem(row, Column_Tags, BuildTagsList(object->Tags));
		mpList->SetItem(row, Column_Position,
						wxString::Format("%.2f, %.2f, %.2f", static_cast<double>(position.X),
										 static_cast<double>(position.Y), static_cast<double>(position.Z)));

		// Retrieved in OnItemActivated() to select the object it corresponds to
		mpList->SetItemPtrData(row, reinterpret_cast<wxUIntPtr>(object));
	}

	mpList->Thaw();

	SetTitle(wxString::Format("Object List (%u)", objects.Size));
}

void ObjectListWindow::OnRefreshButton(wxCommandEvent& event) { RefreshList(); }

void ObjectListWindow::OnItemActivated(wxListEvent& event)
{
	Object* object = reinterpret_cast<Object*>(event.GetItem().GetData());

	if (object != nullptr && gPrototypeEditor != nullptr) {
		gPrototypeEditor->SelectObject(object, false);
	}
}

void ObjectListWindow::OnClose(wxCloseEvent& event) { Hide(); }

} // namespace fx::editor

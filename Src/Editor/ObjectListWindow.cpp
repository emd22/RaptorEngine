#include "ObjectListWindow.hpp"

#include "EditorThread.hpp"
#include "RaptorEditor.hpp"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include <Core/SizedArray.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <format>

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
	{ eObjectTag::ReflectionProbe, "Reflection Probe" },
	{ eObjectTag::Spawn, "Spawn" },
	{ eObjectTag::Trigger, "Trigger" },
	{ eObjectTag::NvBrush, "nvbrush" },
	{ eObjectTag::DBound, "dbound" },
};

static String BuildTagsList(eObjectTag tags)
{
	String description;

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
	thread::PostToGame(
		[this]
		{
			if (gObjectManager == nullptr) {
				return;
			}

			SizedArray<Object*> objects = gObjectManager->CollectObjects();

			DynArray<Row> rows;

			for (uint32 i = 0; i < objects.Size; i++) {
				Object* object = objects[i];

				if (object == nullptr) {
					continue;
				}

				const Vec3f position = object->GetPosition();

				rows.Insert(Row {
					.Name = object->Name.Get(),
					.Tags = BuildTagsList(object->Tags),
					.Position = String::Fmt("{:.2f}, {:.2f}, {:.2f}", position.X, position.Y, position.Z),
					.ID = object->ID(),
				});
			}

			thread::PostToUI([this, rows = std::move(rows)] { ShowRows(rows); });
		});
}

void ObjectListWindow::ShowRows(const DynArray<Row>& rows)
{
	mpList->Freeze();
	mpList->DeleteAllItems();

	for (const Row& entry : rows) {
		wxString name = wxString::FromUTF8(entry.Name.CStr());

		if (name.IsEmpty()) {
			name = "(unnamed)";
		}

		const long row = mpList->InsertItem(mpList->GetItemCount(), name);

		mpList->SetItem(row, Column_Id, wxString::Format("%u", entry.ID));
		mpList->SetItem(row, Column_Tags, wxString::FromUTF8(entry.Tags.CStr()));
		mpList->SetItem(row, Column_Position, wxString::FromUTF8(entry.Position.CStr()));

		mpList->SetItemData(row, static_cast<long>(entry.ID));
	}

	mpList->Thaw();

	SetTitle(wxString::Format("Object List (%u)", rows.Size));
}

void ObjectListWindow::OnRefreshButton(wxCommandEvent& event) { RefreshList(); }

void ObjectListWindow::OnItemActivated(wxListEvent& event)
{
	const uint32 id = static_cast<uint32>(event.GetItem().GetData());

	thread::PostToGame(
		[id]
		{
			if (Object* object = gObjectManager->GetObject(ObjectID(id)); object != nullptr) {
				gEditor->SelectObject(object, false);
			}
		});
}

void ObjectListWindow::OnClose(wxCloseEvent& event) { Hide(); }

} // namespace fx::editor

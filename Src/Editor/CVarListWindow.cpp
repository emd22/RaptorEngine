#include "CVarListWindow.hpp"

#include "EditorThread.hpp"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <CVar.hpp>
#include <Engine.hpp>
#include <cctype>

namespace fx::editor {

enum eColumn : int32
{
	Column_Name = 0,
	Column_Type,
	Column_Value,
};

static constexpr int scRefreshIntervalMs = 500;

CVarListWindow::CVarListWindow(wxWindow* parent)
	: wxFrame(parent, wxID_ANY, "CVar List", wxDefaultPosition, wxSize(520, 480)), mRefreshTimer(this)
{
	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	mpFilter = new wxTextCtrl(root, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	mpFilter->SetHint("Filter...");
	sizer->Add(mpFilter, wxSizerFlags().Expand().Border(wxALL, 6));

	mpList = new wxListCtrl(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->InsertColumn(Column_Name, "Name", wxLIST_FORMAT_LEFT, 200);
	mpList->InsertColumn(Column_Type, "Type", wxLIST_FORMAT_LEFT, 70);
	mpList->InsertColumn(Column_Value, "Value", wxLIST_FORMAT_LEFT, 200);
	sizer->Add(mpList, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, 6));

	wxBoxSizer* edit_sizer = new wxBoxSizer(wxHORIZONTAL);

	mpSelectedLabel = new wxStaticText(root, wxID_ANY, "(select a CVar)");
	edit_sizer->Add(mpSelectedLabel, wxSizerFlags().CenterVertical().Border(wxRIGHT, 6));

	mpValueField = new wxTextCtrl(root, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	mpValueField->Disable();
	edit_sizer->Add(mpValueField, wxSizerFlags(1).CenterVertical().Border(wxRIGHT, 6));

	mpSetButton = new wxButton(root, wxID_ANY, "Set");
	mpSetButton->Disable();
	edit_sizer->Add(mpSetButton, wxSizerFlags().CenterVertical());

	sizer->Add(edit_sizer, wxSizerFlags().Expand().Border(wxALL, 6));

	mpStatus = new wxStaticText(root, wxID_ANY, "");
	sizer->Add(mpStatus, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	root->SetSizer(sizer);

	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizer(frame_sizer);

	mpFilter->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RefreshList(); });
	mpList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { ShowSelected(); });
	mpList->Bind(wxEVT_LIST_ITEM_ACTIVATED,
				 [this](wxListEvent&)
				 {
					 ShowSelected();
					 mpValueField->SetFocus();
					 mpValueField->SelectAll();
				 });
	mpValueField->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { ApplyValue(); });
	mpSetButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplyValue(); });
	Bind(wxEVT_SHOW, &CVarListWindow::OnShow, this);
	Bind(wxEVT_TIMER, &CVarListWindow::OnTimer, this);
	Bind(wxEVT_CLOSE_WINDOW, &CVarListWindow::OnClose, this);

	RefreshList();
}

const CVarListWindow::Row* CVarListWindow::GetSelectedRow() const
{
	const long row = mpList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	if (row < 0 || static_cast<size_t>(row) >= mShown.size()) {
		return nullptr;
	}

	return &mShown[static_cast<size_t>(row)];
}

static std::string ToLowerAscii(std::string text)
{
	for (char& ch : text) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}

	return text;
}

void CVarListWindow::RefreshList()
{
	thread::PostToGame(
		[this, filter = ToLowerAscii(mpFilter->GetValue().utf8_string())]
		{
			if (gCVars == nullptr) {
				return;
			}

			std::vector<Row> rows;

			for (CVarValue* cvar : gCVars->CollectCVars()) {
				std::string name = cvar->GetName().Str();

				if (!filter.empty() && ToLowerAscii(name).find(filter) == std::string::npos) {
					continue;
				}

				rows.push_back(Row {
					.Name = std::move(name),
					.Type = GetCVarTypeName(cvar->Type),
					.Value = cvar->AsString().Str(),
				});
			}

			thread::PostToUI([this, rows = std::move(rows)]() mutable { ShowRows(std::move(rows)); });
		});
}

void CVarListWindow::ShowRows(std::vector<Row> rows)
{
	bool same_rows = rows.size() == mShown.size();

	for (size_t i = 0; same_rows && i < rows.size(); i++) {
		same_rows = rows[i].Name == mShown[i].Name;
	}

	const Row* selected = GetSelectedRow();
	const std::string selected_name = (selected != nullptr) ? selected->Name : std::string();

	mShown = std::move(rows);

	if (same_rows) {
		UpdateValues();
	}
	else {
		RebuildRows(selected_name);
	}

	SetTitle(wxString::Format("CVar List (%zu)", mShown.size()));
}

void CVarListWindow::RebuildRows(const std::string& previously_selected)
{
	mpList->Freeze();
	mpList->DeleteAllItems();

	for (const Row& entry : mShown) {
		const long row = mpList->InsertItem(mpList->GetItemCount(), wxString::FromUTF8(entry.Name));
		mpList->SetItem(row, Column_Type, wxString::FromUTF8(entry.Type));
		mpList->SetItem(row, Column_Value, wxString::FromUTF8(entry.Value));

		if (!previously_selected.empty() && entry.Name == previously_selected) {
			mpList->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		}
	}

	mpList->Thaw();

	ShowSelected();
}

void CVarListWindow::UpdateValues()
{
	for (size_t i = 0; i < mShown.size(); i++) {
		const wxString value = wxString::FromUTF8(mShown[i].Value);

		if (mpList->GetItemText(i, Column_Value) != value) {
			mpList->SetItem(i, Column_Value, value);
		}
	}
}

void CVarListWindow::ShowSelected()
{
	const Row* selected = GetSelectedRow();

	mpValueField->Enable(selected != nullptr);
	mpSetButton->Enable(selected != nullptr);

	if (selected == nullptr) {
		mpSelectedLabel->SetLabel("(select a CVar)");
		mpValueField->Clear();
	}
	else {
		mpSelectedLabel->SetLabel(wxString::FromUTF8(selected->Name));
		mpValueField->ChangeValue(wxString::FromUTF8(selected->Value));
	}

	mpStatus->SetLabel("");
	GetSizer()->Layout();
}

void CVarListWindow::ApplyValue()
{
	const Row* selected = GetSelectedRow();

	if (selected == nullptr) {
		return;
	}

	thread::PostToGame(
		[this, name = selected->Name, text = mpValueField->GetValue().utf8_string()]
		{
			if (gCVars == nullptr) {
				return;
			}

			for (CVarValue* cvar : gCVars->CollectCVars()) {
				if (cvar->GetName().Str() != name) {
					continue;
				}

				const bool ok = cvar->SetFromString(String(text));
				std::string type = GetCVarTypeName(cvar->Type);
				std::string value = cvar->AsString().Str();

				thread::PostToUI(
					[this, name, ok, type = std::move(type), value = std::move(value)]
					{
						mpStatus->SetLabel(ok ? wxString() : wxString::Format("Invalid %s value", type));

						const Row* current = GetSelectedRow();

						if (current != nullptr && current->Name == name) {
							mpValueField->ChangeValue(wxString::FromUTF8(value));
						}

						RefreshList();
					});

				return;
			}
		});
}

void CVarListWindow::OnShow(wxShowEvent& event)
{
	if (event.IsShown()) {
		RefreshList();
		mRefreshTimer.Start(scRefreshIntervalMs);
	}
	else {
		mRefreshTimer.Stop();
	}

	event.Skip();
}

void CVarListWindow::OnTimer(wxTimerEvent& event) { RefreshList(); }

void CVarListWindow::OnClose(wxCloseEvent& event) { Hide(); }

} // namespace fx::editor

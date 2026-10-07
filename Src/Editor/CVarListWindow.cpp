#include "CVarListWindow.hpp"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <CVar.hpp>
#include <Engine.hpp>

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

CVarValue* CVarListWindow::GetSelectedCVar() const
{
	const long row = mpList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	if (row < 0 || static_cast<size_t>(row) >= mShown.size()) {
		return nullptr;
	}

	return mShown[row];
}

void CVarListWindow::RefreshList()
{
	if (gCVars == nullptr) {
		return;
	}

	const wxString filter = mpFilter->GetValue().Lower();

	std::vector<CVarValue*> visible;

	for (CVarValue* cvar : gCVars->CollectCVars()) {
		if (filter.IsEmpty() || wxString::FromUTF8(cvar->GetName().CStr()).Lower().Contains(filter)) {
			visible.push_back(cvar);
		}
	}

	if (visible != mShown) {
		const CVarValue* selected = GetSelectedCVar();

		mShown = std::move(visible);
		RebuildRows(selected);
	}
	else {
		UpdateValues();
	}

	SetTitle(wxString::Format("CVar List (%zu)", mShown.size()));
}

void CVarListWindow::RebuildRows(const CVarValue* previously_selected)
{
	mpList->Freeze();
	mpList->DeleteAllItems();

	for (size_t i = 0; i < mShown.size(); i++) {
		CVarValue* cvar = mShown[i];

		const long row = mpList->InsertItem(mpList->GetItemCount(), wxString::FromUTF8(cvar->GetName().CStr()));
		mpList->SetItem(row, Column_Type, GetCVarTypeName(cvar->Type));
		mpList->SetItem(row, Column_Value, wxString::FromUTF8(cvar->AsString().CStr()));

		if (cvar == previously_selected) {
			mpList->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		}
	}

	mpList->Thaw();

	ShowSelected();
}

void CVarListWindow::UpdateValues()
{
	for (size_t i = 0; i < mShown.size(); i++) {
		const wxString value = wxString::FromUTF8(mShown[i]->AsString().CStr());

		if (mpList->GetItemText(i, Column_Value) != value) {
			mpList->SetItem(i, Column_Value, value);
		}
	}
}

void CVarListWindow::ShowSelected()
{
	CVarValue* cvar = GetSelectedCVar();

	mpValueField->Enable(cvar != nullptr);
	mpSetButton->Enable(cvar != nullptr);

	if (cvar == nullptr) {
		mpSelectedLabel->SetLabel("(select a CVar)");
		mpValueField->Clear();
	}
	else {
		mpSelectedLabel->SetLabel(wxString::FromUTF8(cvar->GetName().CStr()));
		mpValueField->ChangeValue(wxString::FromUTF8(cvar->AsString().CStr()));
	}

	mpStatus->SetLabel("");
	GetSizer()->Layout();
}

void CVarListWindow::ApplyValue()
{
	CVarValue* cvar = GetSelectedCVar();

	if (cvar == nullptr) {
		return;
	}

	const std::string text = mpValueField->GetValue().utf8_string();

	if (cvar->SetFromString(String(text))) {
		mpStatus->SetLabel("");
	}
	else {
		mpStatus->SetLabel(wxString::Format("Invalid %s value", GetCVarTypeName(cvar->Type)));
	}

	mpValueField->ChangeValue(wxString::FromUTF8(cvar->AsString().CStr()));
	UpdateValues();
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

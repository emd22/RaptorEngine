#include "TopViewPanel.hpp"

#ifdef FX_IS_EDITOR

#include "EditorTheme.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

namespace fx::editor {

TopViewPanel::TopViewPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

	mpCanvas = new TopViewCanvas(this);
	root_sizer->Add(mpCanvas, wxSizerFlags(1).Expand());

	mpStatus = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
								wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
	root_sizer->Add(mpStatus, wxSizerFlags().Expand().Border(wxALL, 4));

	SetSizer(root_sizer);

	mpCanvas->SetOnStatus([this](const wxString& text) { mpStatus->SetLabel(text); });

	ApplyTheme();
}

void TopViewPanel::ApplyTheme()
{
	const EditorTheme& theme = GetEditorTheme();

	SetBackgroundColour(ToWxColour(theme.Frame.Background));
	mpStatus->SetForegroundColour(ToWxColour(theme.Frame.StatusText));

	Refresh();
	mpCanvas->Refresh();
}

void TopViewPanel::ApplyState(const TopViewState& state) { mpCanvas->ApplyState(state); }

} // namespace fx::editor

#endif

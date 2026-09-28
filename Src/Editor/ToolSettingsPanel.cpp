#include "ToolSettingsPanel.hpp"

#include "Common.hpp"
#include "RaptorEditor.hpp"

#include <wx/clrpicker.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <Engine.hpp>
#include <Renderer/Light.hpp>

namespace fx::editor {

/////////////////////////////////////
// Light Tool
/////////////////////////////////////

void LightToolSettingsPanel::Construct(wxBoxSizer* tool_panel)
{
	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Light");
	title->SetFont(title->GetFont().Bold());
	tool_panel->Add(title, wxSizerFlags().Border(wxALL, 6));

	mpNameLabel = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	tool_panel->Add(mpNameLabel, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	wxBoxSizer* color_row = new wxBoxSizer(wxHORIZONTAL);
	color_row->Add(new wxStaticText(this, wxID_ANY, "Colour"), wxSizerFlags().CenterVertical());

	mpColorPicker = new wxColourPickerCtrl(this, wxID_ANY, *wxWHITE);
	color_row->Add(mpColorPicker, wxSizerFlags().Border(wxLEFT, 6));

	tool_panel->Add(color_row, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	mpColorPicker->Bind(wxEVT_COLOURPICKER_CHANGED, &LightToolSettingsPanel::OnColorChange, this);

	mpPositionField = new Vector3Field(this, "Position");
	tool_panel->Add(mpPositionField->GetSizer(), wxSizerFlags().Border(wxALL, 6));

	mpPositionField->SetOnChange(
		[this](const Vec3f& value)
		{
			if (mpShownLight != nullptr) {
				mpShownLight->SetPosition(value);
			}
		});

	mpRadiusField = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
										 wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER, 0.1, 200.0, 0.1, 0.1);

	auto UpdateRadiusField = [&](wxCommandEvent& event)
	{
		if (mpShownLight == nullptr) {
			return;
		}

		mpShownLight->SetRadius(mpRadiusField->GetValue());
	};
	mpRadiusField->Bind(wxEVT_SPINCTRLDOUBLE, UpdateRadiusField);

	tool_panel->Add(mpRadiusField, wxSizerFlags().Border(wxALL, 6));

	Refresh();
}

void LightToolSettingsPanel::OnColorChange(wxColourPickerEvent& event)
{
	if (mpShownLight == nullptr) {
		return;
	}

	const wxColour colour = event.GetColour();

	// The alpha is the brightness of the light, don't change it here.
	mpShownLight->Color = Color::FromRGBA(colour.Red(), colour.Green(), colour.Blue(), mpShownLight->Color.A);
}

void LightToolSettingsPanel::Refresh()
{
	LightSpot* light = gEditor->GetLightEditor().GetSelected();

	if (light == nullptr) {
		if (!mbShowingAnything) {
			return;
		}

		mbShowingAnything = false;
		mpShownLight = nullptr;

		mpNameLabel->SetLabel("No light selected");
		mpColorPicker->Disable();
		mpPositionField->SetEnabled(false);
		return;
	}

	mbShowingAnything = true;
	mpShownLight = light;

	mpNameLabel->SetLabel(wxString::Format("Selected '%s'", wxString::FromUTF8(light->Name.Get())));

	mpColorPicker->Enable();
	mpPositionField->SetEnabled(true);

	mpColorPicker->SetColour(wxColour(light->Color.R, light->Color.G, light->Color.B));

	// Skip the refresh while editing so typed keystrokes aren't clobbered
	if (!mpPositionField->HasFocus()) {
		mpPositionField->SetValue(light->GetPosition());
	}

	if (!mpRadiusField->HasFocus()) {
		mpRadiusField->SetValue(light->GetRadius());
	}
}

} // namespace fx::editor

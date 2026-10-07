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

	{
		mpPositionField = new Vector3Field(this, "Position", Vec2f(-100000.0f, 100000.0f));
		mpPositionField->SetOnChange(
			[this](const Vec3f value)
			{
				if (mpShownLight != nullptr) {
					mpShownLight->SetPosition(value);
				}
			});

		tool_panel->Add(mpPositionField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpRadiusField = new FloatField(this, "Radius", Vec2f(3.0f, 150.0f));
		mpRadiusField->SetOnChange(
			[&](const float value)
			{
				if (mpShownLight == nullptr) {
					return;
				}
				mpShownLight->SetRadius(value);
			});

		tool_panel->Add(mpRadiusField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpIntensityField = new FloatField(this, "Intensity (cd)", Vec2f(0.0f, 100000000.0f));
		mpIntensityField->SetOnChange(
			[this](const float value)
			{
				if (mpShownLight != nullptr) {
					mpShownLight->Intensity = value;
				}
			});

		tool_panel->Add(mpIntensityField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpLumensField = new FloatField(this, "Lumens", Vec2f(0.0f, 100000000.0f));
		mpLumensField->SetOnChange(
			[this](const float value)
			{
				if (mpShownLight != nullptr) {
					mpShownLight->SetLumens(value);
				}
			});

		tool_panel->Add(mpLumensField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpOuterAngleField = new FloatField(this, "Outer angle", Vec2f(1.0f, 90.0f));
		mpOuterAngleField->SetOnChange(
			[this](const float value)
			{
				if (mpShownLight != nullptr) {
					mpShownLight->SetConeAngles(mpShownLight->GetInnerAngle(), MathUtil::DegreesToRadians(value));
				}
			});

		tool_panel->Add(mpOuterAngleField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpInnerAngleField = new FloatField(this, "Inner angle", Vec2f(0.0f, 90.0f));
		mpInnerAngleField->SetOnChange(
			[this](const float value)
			{
				if (mpShownLight != nullptr) {
					mpShownLight->SetConeAngles(MathUtil::DegreesToRadians(value), mpShownLight->GetOuterAngle());
				}
			});

		tool_panel->Add(mpInnerAngleField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	Refresh();
}

void LightToolSettingsPanel::OnColorChange(wxColourPickerEvent& event)
{
	if (mpShownLight == nullptr) {
		return;
	}

	const wxColour colour = event.GetColour();

	mpShownLight->Color = Color::FromRGBA(colour.Red(), colour.Green(), colour.Blue(), 255);
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

	mpNameLabel->SetLabel(wxString::Format("Selected '%s'", wxString::FromUTF8(light->Name.Get().CStr())));

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

	if (!mpIntensityField->HasFocus()) {
		mpIntensityField->SetValue(light->Intensity);
	}

	if (!mpLumensField->HasFocus()) {
		mpLumensField->SetValue(light->GetLumens());
	}

	if (!mpOuterAngleField->HasFocus()) {
		mpOuterAngleField->SetValue(MathUtil::RadiansToDegrees(light->GetOuterAngle()));
	}

	if (!mpInnerAngleField->HasFocus()) {
		mpInnerAngleField->SetValue(MathUtil::RadiansToDegrees(light->GetInnerAngle()));
	}
}

} // namespace fx::editor

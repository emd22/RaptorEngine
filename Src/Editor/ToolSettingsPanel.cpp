#include "ToolSettingsPanel.hpp"

#include "Common.hpp"
#include "EditorThread.hpp"
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
			[](const Vec3f value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetPosition(value);
						}
					});
			});

		tool_panel->Add(mpPositionField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpRadiusField = new FloatField(this, "Radius", Vec2f(3.0f, 150.0f));
		mpRadiusField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetRadius(value);
						}
					});
			});

		tool_panel->Add(mpRadiusField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpIntensityField = new FloatField(this, "Intensity (cd)", Vec2f(0.0f, 100000000.0f));
		mpIntensityField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->Intensity = value;
						}
					});
			});

		tool_panel->Add(mpIntensityField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpLumensField = new FloatField(this, "Lumens", Vec2f(0.0f, 100000000.0f));
		mpLumensField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetLumens(value);
						}
					});
			});

		tool_panel->Add(mpLumensField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpOuterAngleField = new FloatField(this, "Outer angle", Vec2f(1.0f, 90.0f));
		mpOuterAngleField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetConeAngles(light->GetInnerAngle(), MathUtil::DegreesToRadians(value));
						}
					});
			});

		tool_panel->Add(mpOuterAngleField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	{
		mpInnerAngleField = new FloatField(this, "Inner angle", Vec2f(0.0f, 90.0f));
		mpInnerAngleField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetConeAngles(MathUtil::DegreesToRadians(value), light->GetOuterAngle());
						}
					});
			});

		tool_panel->Add(mpInnerAngleField->GetSizer(), wxSizerFlags().Border(wxALL, 6));
	}

	ApplyState(EditorPanelState {});
}

void LightToolSettingsPanel::OnColorChange(wxColourPickerEvent& event)
{
	if (!mbShowingAnything) {
		return;
	}

	const wxColour colour = event.GetColour();

	thread::PostToGame(
		[red = colour.Red(), green = colour.Green(), blue = colour.Blue()]
		{
			if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
				light->Color = Color::FromRGBA(red, green, blue, 255);
			}
		});
}

void LightToolSettingsPanel::ApplyState(const EditorPanelState& editor_state)
{
	const LightPanelState& state = editor_state.Light;

	if (!state.bHasLight) {
		if (!mbShowingAnything) {
			return;
		}

		mbShowingAnything = false;

		mpNameLabel->SetLabel("No light selected");
		mpColorPicker->Disable();
		mpPositionField->SetEnabled(false);
		return;
	}

	mbShowingAnything = true;

	mpNameLabel->SetLabel(wxString::Format("Selected '%s'", wxString::FromUTF8(state.Name)));

	mpColorPicker->Enable();
	mpPositionField->SetEnabled(true);

	mpColorPicker->SetColour(wxColour(state.ColorR, state.ColorG, state.ColorB));

	if (!mpPositionField->HasFocus()) {
		mpPositionField->SetValue(Vec3f(state.PositionX, state.PositionY, state.PositionZ));
	}

	if (!mpRadiusField->HasFocus()) {
		mpRadiusField->SetValue(state.Radius);
	}

	if (!mpIntensityField->HasFocus()) {
		mpIntensityField->SetValue(state.Intensity);
	}

	if (!mpLumensField->HasFocus()) {
		mpLumensField->SetValue(state.Lumens);
	}

	if (!mpOuterAngleField->HasFocus()) {
		mpOuterAngleField->SetValue(state.OuterAngleDegrees);
	}

	if (!mpInnerAngleField->HasFocus()) {
		mpInnerAngleField->SetValue(state.InnerAngleDegrees);
	}
}

} // namespace fx::editor

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
	PropertySection* section = new PropertySection(this, "Light");
	wxWindow* body = section->GetBody();
	tool_panel->Add(section, wxSizerFlags().Expand());

	mpNameLabel = new wxStaticText(body, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	section->AddWide(mpNameLabel);

	wxBoxSizer* color_row = new wxBoxSizer(wxHORIZONTAL);
	mpColorPicker = new wxColourPickerCtrl(body, wxID_ANY, *wxWHITE);
	color_row->Add(mpColorPicker, wxSizerFlags().CenterVertical());

	section->AddRow("Colour", color_row);

	mpColorPicker->Bind(wxEVT_COLOURPICKER_CHANGED, &LightToolSettingsPanel::OnColorChange, this);

	{
		mpPositionField = new Vector3Field(body, "Position", Vec2f(-100000.0f, 100000.0f));
		mpPositionField->SetOnChange(
			[](const Vec3f value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetPosition(value);
							gEditor->MarkModified();
						}
					});
			});

		section->AddRow(*mpPositionField);
	}

	{
		mpRadiusField = new FloatField(body, "Radius", Vec2f(3.0f, 150.0f));
		mpRadiusField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetRadius(value);
							gEditor->MarkModified();
						}
					});
			});

		section->AddRow(*mpRadiusField);
	}

	{
		mpIntensityField = new FloatField(body, "Intensity (cd)", Vec2f(0.0f, 100000000.0f));
		mpIntensityField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->Intensity = value;
							gEditor->MarkModified();
						}
					});
			});

		section->AddRow(*mpIntensityField);
	}

	{
		mpLumensField = new FloatField(body, "Lumens", Vec2f(0.0f, 100000000.0f));
		mpLumensField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetLumens(value);
							gEditor->MarkModified();
						}
					});
			});

		section->AddRow(*mpLumensField);
	}

	{
		mpOuterAngleField = new FloatField(body, "Outer angle", Vec2f(1.0f, 90.0f));
		mpOuterAngleField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetConeAngles(light->GetInnerAngle(), MathUtil::DegreesToRadians(value));
							gEditor->MarkModified();
						}
					});
			});

		section->AddRow(*mpOuterAngleField);
	}

	{
		mpInnerAngleField = new FloatField(body, "Inner angle", Vec2f(0.0f, 90.0f));
		mpInnerAngleField->SetOnChange(
			[](const float value)
			{
				thread::PostToGame(
					[value]
					{
						if (LightSpot* light = gEditor->GetLightEditor().GetSelected(); light != nullptr) {
							light->SetConeAngles(MathUtil::DegreesToRadians(value), light->GetOuterAngle());
							gEditor->MarkModified();
						}
					});
			});

		section->AddRow(*mpInnerAngleField);
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
				gEditor->MarkModified();
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

	mpNameLabel->SetLabel(wxString::Format("Selected '%s'", wxString::FromUTF8(state.Name.CStr())));

	mpColorPicker->Enable();
	mpPositionField->SetEnabled(true);

	mpColorPicker->SetColour(wxColour(state.Color.R, state.Color.G, state.Color.B));

	if (!mpPositionField->HasFocus()) {
		mpPositionField->SetValue(state.Position);
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

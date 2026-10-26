#include "ToolSettingsPanel.hpp"

#include "Common.hpp"
#include "EditorThread.hpp"
#include "RaptorEditor.hpp"

#include <wx/choice.h>
#include <wx/clrpicker.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <Audio/SoundEmitters.hpp>
#include <Engine.hpp>
#include <Renderer/Light.hpp>

#include <functional>

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

static void EditSelectedSound(std::function<void(SoundEmitter&)> edit)
{
	thread::PostToGame(
		[edit = std::move(edit)]
		{
			SoundEditor& sounds = gEditor->GetSoundEditor();

			if (const SoundEmitter* emitter = sounds.GetSelected(); emitter != nullptr) {
				SoundEmitter after = *emitter;
				edit(after);
				sounds.EditSelected(after);
			}
		});
}

void SoundToolSettingsPanel::Construct(wxBoxSizer* tool_panel)
{
	PropertySection* section = new PropertySection(this, "Sound");
	wxWindow* body = section->GetBody();
	tool_panel->Add(section, wxSizerFlags().Expand());

	mpNameLabel = new wxStaticText(body, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	section->AddWide(mpNameLabel);

	mpSoundChoice = new wxChoice(body, wxID_ANY);
	mpSoundChoice->Bind(wxEVT_CHOICE, &SoundToolSettingsPanel::OnSoundChoice, this);
	section->AddRow("Sound", mpSoundChoice);

	mpPositionField = new Vector3Field(body, "Position", Vec2f(-100000.0f, 100000.0f));
	mpPositionField->SetOnChange([](const Vec3f value)
								 { EditSelectedSound([value](SoundEmitter& emitter) { emitter.Position = value; }); });
	section->AddRow(*mpPositionField);

	mpVolumeField = new FloatField(body, "Volume", Vec2f(0.0f, 4.0f));
	mpVolumeField->SetOnChange([](const float value)
							   { EditSelectedSound([value](SoundEmitter& emitter) { emitter.Volume = value; }); });
	section->AddRow(*mpVolumeField);

	mpMinIntervalField = new FloatField(body, "Interval min (s)", Vec2f(0.0f, 3600.0f));
	mpMinIntervalField->SetOnChange(
		[](const float value) { EditSelectedSound([value](SoundEmitter& emitter) { emitter.MinInterval = value; }); });
	section->AddRow(*mpMinIntervalField);

	mpMaxIntervalField = new FloatField(body, "Interval max (s)", Vec2f(0.0f, 3600.0f));
	mpMaxIntervalField->SetOnChange(
		[](const float value) { EditSelectedSound([value](SoundEmitter& emitter) { emitter.MaxInterval = value; }); });
	section->AddRow(*mpMaxIntervalField);

	ApplyState(EditorPanelState {});
}

void SoundToolSettingsPanel::OnSoundChoice(wxCommandEvent& event)
{
	if (!mbShowingAnything || mpSoundChoice->GetSelection() == wxNOT_FOUND) {
		return;
	}

	const String sound(mpSoundChoice->GetStringSelection().utf8_str().data());

	EditSelectedSound([sound](SoundEmitter& emitter) { emitter.Sound = sound; });
}

void SoundToolSettingsPanel::SetControlsEnabled(bool enabled)
{
	mpSoundChoice->Enable(enabled);
	mpPositionField->SetEnabled(enabled);
	mpVolumeField->SetEnabled(enabled);
	mpMinIntervalField->SetEnabled(enabled);
	mpMaxIntervalField->SetEnabled(enabled);
}

void SoundToolSettingsPanel::ApplyState(const EditorPanelState& editor_state)
{
	const SoundPanelState& state = editor_state.Sound;

	if (state.SoundNames != mSoundNames) {
		mSoundNames = state.SoundNames;

		mpSoundChoice->Clear();

		const wxString names = wxString::FromUTF8(mSoundNames.CStr());

		for (const wxString& name : wxSplit(names, '\n')) {
			if (!name.IsEmpty()) {
				mpSoundChoice->Append(name);
			}
		}
	}

	if (!state.bHasSound) {
		if (!mbShowingAnything) {
			return;
		}

		mbShowingAnything = false;

		mpNameLabel->SetLabel("No sound selected, double-click to add one");
		mpSoundChoice->SetSelection(wxNOT_FOUND);
		SetControlsEnabled(false);
		return;
	}

	mbShowingAnything = true;

	mpNameLabel->SetLabel(wxString::Format("Selected '%s'", wxString::FromUTF8(state.Name.CStr())));
	SetControlsEnabled(true);

	mpSoundChoice->SetSelection(mpSoundChoice->FindString(wxString::FromUTF8(state.Sound.CStr()), true));

	if (!mpPositionField->HasFocus()) {
		mpPositionField->SetValue(state.Position);
	}

	if (!mpVolumeField->HasFocus()) {
		mpVolumeField->SetValue(state.Volume);
	}

	if (!mpMinIntervalField->HasFocus()) {
		mpMinIntervalField->SetValue(state.MinInterval);
	}

	if (!mpMaxIntervalField->HasFocus()) {
		mpMaxIntervalField->SetValue(state.MaxInterval);
	}
}

} // namespace fx::editor

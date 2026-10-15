#include "WorldPropertiesPanel.hpp"

#include "Common.hpp"
#include "EditorThread.hpp"
#include "RaptorEditor.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valnum.h>

#include <CVar.hpp>
#include <Engine.hpp>
#include <Renderer/Exposure.hpp>
#include <Renderer/LightProbe.hpp>
#include <World.hpp>
#include <algorithm>

namespace fx::editor {

namespace {

struct DebugLayer
{
	const char* pcName;
	uint32 Mask;
};

constexpr DebugLayer scDebugLayers[] = {
	{ "None", 0 },
	{ "Object Bounds", World::scDebugBoundsObjects },
	{ "Light Bounds", World::scDebugBoundsLights },
	{ "Physics Bounds", World::scDebugBoundsPhysics },
	{ "Ragdolls", World::scDebugBoundsRagdolls },
	{ "All Bounds", World::scDebugBoundsObjects | World::scDebugBoundsLights | World::scDebugBoundsPhysics |
						World::scDebugBoundsRagdolls },
};

constexpr const char* scReflectionDebugViews[] = {
	"Off",
	"Mirror Reflections",
	"Probe Coverage",
};

} // namespace

static void SetCameraCVar(const char* name, float32 value)
{
	gCVars->Set(name, value);
	gEditor->MarkModified();
}

WorldPropertiesPanel::WorldPropertiesPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	PropertySection* world_section = new PropertySection(this, "World");
	wxWindow* world_body = world_section->GetBody();
	sizer->Add(world_section, wxSizerFlags().Expand());

	mpPositionField = new Vector3Field(world_body, "Player", Vec2f(-100000.0f, 100000.0f));
	world_section->AddRow(*mpPositionField);

	mpPositionField->SetOnChange(
		[this](const Vec3f value)
		{
			mShownPosition = value;
			thread::PostToGame([value] { gWorld->Player.TeleportTo(value); });
		});

	mpDebugLayerChoice = new wxChoice(world_body, wxID_ANY);

	for (const DebugLayer& layer : scDebugLayers) {
		mpDebugLayerChoice->Append(wxString::FromUTF8(layer.pcName));
	}

	world_section->AddRow("Debug Layer", mpDebugLayerChoice);

	mpDebugLayerChoice->Bind(wxEVT_CHOICE,
							 [this](wxCommandEvent&)
							 {
								 const int32 selection = mpDebugLayerChoice->GetSelection();
								 if (selection < 0 || selection >= static_cast<int32>(std::size(scDebugLayers))) {
									 return;
								 }

								 mShownDebugMask = static_cast<int64>(scDebugLayers[selection].Mask);
								 thread::PostToGame([mask = mShownDebugMask] { gCVars->Set("r_debug_bounds", mask); });
							 });

	{
		PropertySection* camera_section = new PropertySection(this, "Camera", false);
		wxWindow* camera_body = camera_section->GetBody();
		sizer->Add(camera_section, wxSizerFlags().Expand());

		mpApertureField = new FloatField(camera_body, "Aperture (f/)", Vec2f(0.7f, 64.0f));
		mpApertureField->SetOnChange([](const float value)
									 { thread::PostToGame([value] { SetCameraCVar("r_aperture", value); }); });
		camera_section->AddRow(*mpApertureField);

		mpShutterField = new FloatField(camera_body, "Shutter (1/s)", Vec2f(1.0f, 8000.0f));
		mpShutterField->SetOnChange([](const float value)
									{ thread::PostToGame([value] { SetCameraCVar("r_shutter", 1.0f / value); }); });
		camera_section->AddRow(*mpShutterField);

		mpIsoField = new FloatField(camera_body, "ISO", Vec2f(25.0f, 25600.0f));
		mpIsoField->SetOnChange([](const float value)
								{ thread::PostToGame([value] { SetCameraCVar("r_iso", value); }); });
		camera_section->AddRow(*mpIsoField);

		mpCompensationField = new FloatField(camera_body, "Compensation (EV)", Vec2f(-10.0f, 10.0f));
		mpCompensationField->SetOnChange([](const float value)
										 { thread::PostToGame([value] { SetCameraCVar("r_exposure_ev", value); }); });
		camera_section->AddRow(*mpCompensationField);
	}

	BuildReflectionSection(sizer);

	SetSizer(sizer);

	ApplyState(WorldPanelState {});
}

void WorldPropertiesPanel::BuildReflectionSection(wxSizer* sizer)
{
	PropertySection* section = new PropertySection(this, "Probes", false);
	wxWindow* body = section->GetBody();
	sizer->Add(section, wxSizerFlags().Expand());

	mpReflectionStatus = new wxStaticText(body, wxID_ANY, wxEmptyString);
	section->AddWide(mpReflectionStatus);

	mpReflectionEnabledCheck = new wxCheckBox(body, wxID_ANY, "Enabled");
	section->AddWide(mpReflectionEnabledCheck);

	mpReflectionEnabledCheck->Bind(wxEVT_CHECKBOX,
								   [this](wxCommandEvent&)
								   {
									   thread::PostToGame([value = mpReflectionEnabledCheck->GetValue() ? 1 : 0]
														  { gCVars->Set("r_reflection_probes", value); });
								   });

	mpReflectionFallbackCheck = new wxCheckBox(body, wxID_ANY, "Reflection fallback probe");
	section->AddWide(mpReflectionFallbackCheck);

	mpReflectionFallbackCheck->Bind(wxEVT_CHECKBOX,
									[this](wxCommandEvent&)
									{
										thread::PostToGame(
											[value = mpReflectionFallbackCheck->GetValue() ? 1 : 0]
											{
												gCVars->Set("r_reflection_level_probe", value);
												gProbeManager->RebuildReflectionProbesFromWorld();
											});
									});

	mpShowProbeVolumesCheck = new wxCheckBox(body, wxID_ANY, "Show probe volumes");
	section->AddWide(mpShowProbeVolumesCheck);

	mpShowProbeVolumesCheck->Bind(wxEVT_CHECKBOX,
								  [this](wxCommandEvent&)
								  {
									  thread::PostToGame([value = mpShowProbeVolumesCheck->GetValue() ? 1 : 0]
														 { gCVars->Set("r_show_volumes", value); });
								  });

	mpReflectionDebugChoice = new wxChoice(body, wxID_ANY);

	for (const char* view : scReflectionDebugViews) {
		mpReflectionDebugChoice->Append(wxString::FromUTF8(view));
	}

	section->AddRow("Reflection View", mpReflectionDebugChoice);

	mpReflectionDebugChoice->Bind(wxEVT_CHOICE,
								  [this](wxCommandEvent&)
								  {
									  const int32 selection = mpReflectionDebugChoice->GetSelection();
									  if (selection < 0) {
										  return;
									  }

									  mShownReflectionDebug = selection;
									  thread::PostToGame([selection] { gCVars->Set("r_reflection_debug", selection); });
								  });

	wxGridSizer* buttons = new wxGridSizer(2, 4, 4);

	const auto add_button = [&](const char* label, std::function<void()> action)
	{
		wxButton* button = new wxButton(body, wxID_ANY, label);
		button->Bind(wxEVT_BUTTON, [action = std::move(action)](wxCommandEvent&) { thread::PostToGame(action); });
		buttons->Add(button, wxSizerFlags().Expand());
	};

	add_button("New At Player", [] { gEditor->CreateReflectionProbeAtPlayer(); });
	add_button("Mark Selection", [] { gEditor->SetSelectionReflectionProbe(true); });
	add_button("Unmark Selection", [] { gEditor->SetSelectionReflectionProbe(false); });
	add_button("Bake Reflections",
			   []
			   {
				   gProbeManager->RebuildReflectionProbesFromWorld();
				   gProbeManager->BeginReflectionBake();
			   });
	add_button("Bake All",
			   []
			   {
				   gProbeManager->RebuildVolumesFromWorld();
				   gProbeManager->BeginBake();
			   });
	add_button("Save Probes", [] { gProbeManager->SaveProbes(); });

	section->AddWide(buttons);
}

void WorldPropertiesPanel::ApplyReflectionState(const WorldPanelState& state)
{
	const wxString status = wxString::FromUTF8(state.ReflectionStatus.CStr());

	if (status != mShownReflectionStatus) {
		mShownReflectionStatus = status;
		mpReflectionStatus->SetLabel(status);
	}

	if (mpReflectionEnabledCheck->GetValue() != state.bReflectionsEnabled) {
		mpReflectionEnabledCheck->SetValue(state.bReflectionsEnabled);
	}

	if (mpReflectionFallbackCheck->GetValue() != state.bReflectionFallback) {
		mpReflectionFallbackCheck->SetValue(state.bReflectionFallback);
	}

	if (state.ReflectionDebugView != mShownReflectionDebug) {
		mShownReflectionDebug = state.ReflectionDebugView;
		mpReflectionDebugChoice->SetSelection(static_cast<int32>(state.ReflectionDebugView));
	}
}

void WorldPropertiesPanel::ApplyState(const WorldPanelState& state)
{
	if (!mbShowingAnything) {
		mbShowingAnything = true;
		mpPositionField->SetEnabled(true);
	}

	if (state.DebugMask != mShownDebugMask) {
		mShownDebugMask = state.DebugMask;

		int32 selection = wxNOT_FOUND;

		for (uint32 i = 0; i < std::size(scDebugLayers); i++) {
			if (static_cast<int64>(scDebugLayers[i].Mask) == state.DebugMask) {
				selection = static_cast<int32>(i);
				break;
			}
		}

		mpDebugLayerChoice->SetSelection(selection);
	}

	ApplyReflectionState(state);

	if (!mpApertureField->HasFocus()) {
		mpApertureField->SetValue(state.Aperture);
	}

	if (!mpShutterField->HasFocus()) {
		mpShutterField->SetValue(1.0f / std::max(state.ShutterTime, ExposureSettings::scMinValue));
	}

	if (!mpIsoField->HasFocus()) {
		mpIsoField->SetValue(state.ISO);
	}

	if (!mpCompensationField->HasFocus()) {
		mpCompensationField->SetValue(state.Compensation);
	}
}

} // namespace fx::editor

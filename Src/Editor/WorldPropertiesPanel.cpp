#include "WorldPropertiesPanel.hpp"

#include "Common.hpp"
#include "RaptorEditor.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/collpane.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
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
	{ "All Bounds", World::scDebugBoundsObjects | World::scDebugBoundsLights | World::scDebugBoundsPhysics },
};

constexpr const char* scReflectionDebugViews[] = {
	"Off",
	"Mirror Reflections",
	"Probe Coverage",
};

} // namespace

WorldPropertiesPanel::WorldPropertiesPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "World");
	title->SetFont(title->GetFont().Bold());
	sizer->Add(title, wxSizerFlags().Border(wxALL, 6));

	mpPositionField = new Vector3Field(this, "Player", Vec2f(-100000.0f, 100000.0f));
	sizer->Add(mpPositionField->GetSizer(), wxSizerFlags().Border(wxALL, 6));

	mpPositionField->SetOnChange(
		[this](const Vec3f& value)
		{
			gWorld->Player.TeleportTo(value);
			mShownPosition = value;
		});

	{
		wxBoxSizer* debug_row = new wxBoxSizer(wxHORIZONTAL);
		debug_row->Add(new wxStaticText(this, wxID_ANY, "Debug Layer"), wxSizerFlags().CenterVertical());

		mpDebugLayerChoice = new wxChoice(this, wxID_ANY);

		for (const DebugLayer& layer : scDebugLayers) {
			mpDebugLayerChoice->Append(wxString::FromUTF8(layer.pcName));
		}

		debug_row->Add(mpDebugLayerChoice, wxSizerFlags().Border(wxLEFT, 6));
		sizer->Add(debug_row, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

		mpDebugLayerChoice->Bind(wxEVT_CHOICE,
								 [this](wxCommandEvent&)
								 {
									 const int32 selection = mpDebugLayerChoice->GetSelection();
									 if (selection < 0 || selection >= static_cast<int32>(std::size(scDebugLayers))) {
										 return;
									 }

									 mShownDebugMask = static_cast<int64>(scDebugLayers[selection].Mask);
									 gCVars->Set("r_debug_bounds", mShownDebugMask);
								 });
	}

	{
		mpCameraPane = new wxCollapsiblePane(this, wxID_ANY, "Camera", wxDefaultPosition, wxDefaultSize,
											 wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);

		wxWindow* cam_pane_win = mpCameraPane->GetPane();
		wxSizer* cam_pane_sizer = new wxBoxSizer(wxVERTICAL);

		sizer->Add(mpCameraPane, wxSizerFlags().Expand().Border());

		mpApertureField = new FloatField(cam_pane_win, "Aperture (f/)", Vec2f(0.7f, 64.0f));
		mpApertureField->SetOnChange([](const float value) { gCVars->Set("r_aperture", value); });
		cam_pane_sizer->Add(mpApertureField->GetSizer(), wxSizerFlags().Border(wxALL, 6));

		mpShutterField = new FloatField(cam_pane_win, "Shutter (1/s)", Vec2f(1.0f, 8000.0f));
		mpShutterField->SetOnChange([](const float value) { gCVars->Set("r_shutter", 1.0f / value); });
		cam_pane_sizer->Add(mpShutterField->GetSizer(), wxSizerFlags().Border(wxALL, 6));

		mpIsoField = new FloatField(cam_pane_win, "ISO", Vec2f(25.0f, 25600.0f));
		mpIsoField->SetOnChange([](const float value) { gCVars->Set("r_iso", value); });
		cam_pane_sizer->Add(mpIsoField->GetSizer(), wxSizerFlags().Border(wxALL, 6));

		mpCompensationField = new FloatField(cam_pane_win, "Compensation (EV)", Vec2f(-10.0f, 10.0f));
		mpCompensationField->SetOnChange([](const float value) { gCVars->Set("r_exposure_ev", value); });
		cam_pane_sizer->Add(mpCompensationField->GetSizer(), wxSizerFlags().Border(wxALL, 6));

		cam_pane_win->SetSizer(cam_pane_sizer);
		cam_pane_sizer->SetSizeHints(cam_pane_win);

		mpCameraPane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this](wxCollapsiblePaneEvent&) { OnPaneChanged(); });
	}

	BuildReflectionPane(sizer);

	SetSizer(sizer);

	Update();
}

void WorldPropertiesPanel::OnPaneChanged()
{
	Layout();

	if (GetParent() != nullptr) {
		GetParent()->Layout();
	}

	Update();
}

void WorldPropertiesPanel::BuildReflectionPane(wxSizer* sizer)
{
	mpReflectionPane = new wxCollapsiblePane(this, wxID_ANY, "Probes", wxDefaultPosition, wxDefaultSize,
											 wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);

	wxWindow* pane = mpReflectionPane->GetPane();
	wxSizer* pane_sizer = new wxBoxSizer(wxVERTICAL);

	sizer->Add(mpReflectionPane, wxSizerFlags().Expand().Border());

	mpReflectionStatus = new wxStaticText(pane, wxID_ANY, wxEmptyString);
	pane_sizer->Add(mpReflectionStatus, wxSizerFlags().Expand().Border(wxALL, 6));

	mpReflectionEnabledCheck = new wxCheckBox(pane, wxID_ANY, "Enabled");
	pane_sizer->Add(mpReflectionEnabledCheck, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	mpReflectionEnabledCheck->Bind(
		wxEVT_CHECKBOX,
		[this](wxCommandEvent&) { gCVars->Set("r_reflection_probes", mpReflectionEnabledCheck->GetValue() ? 1 : 0); });

	mpReflectionFallbackCheck = new wxCheckBox(pane, wxID_ANY, "Reflection fallback probe");
	pane_sizer->Add(mpReflectionFallbackCheck, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	mpReflectionFallbackCheck->Bind(wxEVT_CHECKBOX,
									[this](wxCommandEvent&)
									{
										gCVars->Set("r_reflection_level_probe",
													mpReflectionFallbackCheck->GetValue() ? 1 : 0);
										gProbeManager->RebuildReflectionProbesFromWorld();
									});

	mpShowProbeVolumesCheck = new wxCheckBox(pane, wxID_ANY, "Show probe volumes");
	pane_sizer->Add(mpShowProbeVolumesCheck, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	mpShowProbeVolumesCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&)
								  { gCVars->Set("r_show_volumes", mpShowProbeVolumesCheck->GetValue() ? 1 : 0); });


	{
		wxBoxSizer* debug_row = new wxBoxSizer(wxHORIZONTAL);
		debug_row->Add(new wxStaticText(pane, wxID_ANY, "Reflection View"), wxSizerFlags().CenterVertical());

		mpReflectionDebugChoice = new wxChoice(pane, wxID_ANY);

		for (const char* view : scReflectionDebugViews) {
			mpReflectionDebugChoice->Append(wxString::FromUTF8(view));
		}

		debug_row->Add(mpReflectionDebugChoice, wxSizerFlags().Border(wxLEFT, 6));
		pane_sizer->Add(debug_row, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));


		mpReflectionDebugChoice->Bind(wxEVT_CHOICE,
									  [this](wxCommandEvent&)
									  {
										  const int32 selection = mpReflectionDebugChoice->GetSelection();
										  if (selection < 0) {
											  return;
										  }

										  mShownReflectionDebug = selection;
										  gCVars->Set("r_reflection_debug", selection);
									  });
	}

	wxGridSizer* buttons = new wxGridSizer(2, 4, 4);

	const auto add_button = [&](const char* label, std::function<void()> action)
	{
		wxButton* button = new wxButton(pane, wxID_ANY, label);
		button->Bind(wxEVT_BUTTON, [action = std::move(action)](wxCommandEvent&) { action(); });
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

	pane_sizer->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	pane->SetSizer(pane_sizer);
	pane_sizer->SetSizeHints(pane);

	mpReflectionPane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [this](wxCollapsiblePaneEvent&) { OnPaneChanged(); });
}

void WorldPropertiesPanel::UpdateReflectionPane()
{
	if (!mpReflectionPane->IsExpanded()) {
		return;
	}

	const uint32 probe_count = gProbeManager->GetReflectionProbeCount();

	wxString status;

	if (gProbeManager->IsBakingReflections()) {
		status = wxString::Format("Baking %u probe(s)...", probe_count);
	}
	else if (gProbeManager->IsBaking()) {
		status = "Baking irradiance...";
	}
	else if (probe_count == 0) {
		status = "No reflection probes";
	}
	else if (gProbeManager->AreReflectionsBaked()) {
		status = wxString::Format("%u probe(s), baked", probe_count);
	}
	else {
		status = wxString::Format("%u probe(s), needs a bake", probe_count);
	}

	if (status != mShownReflectionStatus) {
		mShownReflectionStatus = status;
		mpReflectionStatus->SetLabel(status);
	}

	const bool enabled = gCVars->Get("r_reflection_probes", int64 { 1 }) != 0;

	if (mpReflectionEnabledCheck->GetValue() != enabled) {
		mpReflectionEnabledCheck->SetValue(enabled);
	}

	const bool level_probe = gCVars->Get("r_reflection_level_probe", int64 { 0 }) != 0;

	if (mpReflectionFallbackCheck->GetValue() != level_probe) {
		mpReflectionFallbackCheck->SetValue(level_probe);
	}

	const int64 debug_view = std::clamp<int64>(gCVars->Get("r_reflection_debug", int64 { 0 }), 0, 2);

	if (debug_view != mShownReflectionDebug) {
		mShownReflectionDebug = debug_view;
		mpReflectionDebugChoice->SetSelection(static_cast<int32>(debug_view));
	}
}

void WorldPropertiesPanel::Update()
{
	if (!mbShowingAnything) {
		mbShowingAnything = true;
		mpPositionField->SetEnabled(true);
	}

	const Vec3f position = gWorld->Player.Position;

	// if (true) {
	// 	mpPositionField->SetValue(position);
	// 	mShownPosition = position;
	// }

	const int64 debug_mask = gCVars->Get("r_debug_bounds", int64 { 0 });

	if (debug_mask != mShownDebugMask) {
		mShownDebugMask = debug_mask;

		int32 selection = wxNOT_FOUND;

		for (uint32 i = 0; i < std::size(scDebugLayers); i++) {
			if (static_cast<int64>(scDebugLayers[i].Mask) == debug_mask) {
				selection = static_cast<int32>(i);
				break;
			}
		}

		mpDebugLayerChoice->SetSelection(selection);
	}

	UpdateReflectionPane();

	if (!mpCameraPane->IsExpanded()) {
		return;
	}

	ExposureSettings exposure;
	exposure.Aperture = gCVars->Get("r_aperture", exposure.Aperture);
	exposure.ShutterTime = gCVars->Get("r_shutter", exposure.ShutterTime);
	exposure.ISO = gCVars->Get("r_iso", exposure.ISO);
	exposure.Compensation = gCVars->Get("r_exposure_ev", exposure.Compensation);

	if (!mpApertureField->HasFocus()) {
		mpApertureField->SetValue(exposure.Aperture);
	}

	if (!mpShutterField->HasFocus()) {
		mpShutterField->SetValue(1.0f / std::max(exposure.ShutterTime, ExposureSettings::scMinValue));
	}

	if (!mpIsoField->HasFocus()) {
		mpIsoField->SetValue(exposure.ISO);
	}

	if (!mpCompensationField->HasFocus()) {
		mpCompensationField->SetValue(exposure.Compensation);
	}
}

} // namespace fx::editor

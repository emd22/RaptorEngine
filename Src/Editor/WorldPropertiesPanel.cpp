#include "WorldPropertiesPanel.hpp"

#include "Common.hpp"

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

		mpCameraPane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED,
						   [this](wxCollapsiblePaneEvent&)
						   {
							   Layout();

							   if (GetParent() != nullptr) {
								   GetParent()->Layout();
							   }

							   Update();
						   });
	}

	SetSizer(sizer);

	Update();
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

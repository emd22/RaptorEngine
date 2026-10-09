#include "ObjectPropertiesPanel.hpp"

#include "EditOperation.hpp"
#include "EditorFrame.hpp"
#include "EditorThread.hpp"
#include "RaptorEditor.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <Blockout.hpp>
#include <Engine.hpp>
#include <Script/ObjectScripts.hpp>
#include <World.hpp>
#include <filesystem>

namespace fx::editor {

struct EnterDirectionChoice
{
	const char* pName;
	float32 X;
	float32 Y;
	float32 Z;
};

static constexpr EnterDirectionChoice scEnterDirections[] = {
	{ "Any", 0.0f, 0.0f, 0.0f },
	{ "+X (right)", 1.0f, 0.0f, 0.0f },
	{ "-X (left)", -1.0f, 0.0f, 0.0f },
	{ "+Y (up)", 0.0f, 1.0f, 0.0f },
	{ "-Y (down)", 0.0f, -1.0f, 0.0f },
	{ "+Z (forward)", 0.0f, 0.0f, 1.0f },
	{ "-Z (back)", 0.0f, 0.0f, -1.0f },
};

static constexpr int32 scNumEnterDirections = static_cast<int32>(std::size(scEnterDirections));
static constexpr int32 scCustomEnterDirectionIndex = scNumEnterDirections;

static int32 GetEnterDirectionIndex(const ObjectPanelState& state)
{
	static constexpr float32 scAxisMatchDot = 0.9999f;

	if (!state.bHasEnterDirection) {
		return 0;
	}

	for (int32 i = 1; i < scNumEnterDirections; i++) {
		const EnterDirectionChoice& choice = scEnterDirections[i];
		const float32 dot = choice.X * state.EnterDirection[0] + choice.Y * state.EnterDirection[1] +
							choice.Z * state.EnterDirection[2];

		if (dot > scAxisMatchDot) {
			return i;
		}
	}

	return scCustomEnterDirectionIndex;
}

struct FlagName
{
	uint32 Bit;
	const char* pName;
	const char* pHint;
};

static constexpr FlagName scTagNames[] = {
	{ static_cast<uint32>(eObjectTag::Blockout), "Blockout", "Marks level geometry. Set when the brush is created" },
	{ static_cast<uint32>(eObjectTag::LockTransform), "Lock Transform", nullptr },
	{ static_cast<uint32>(eObjectTag::ProbeVolume), "Probe Volume", "Blockout brushes only" },
	{ static_cast<uint32>(eObjectTag::ReflectionProbe), "Reflection Probe", "Blockout brushes only" },
	{ static_cast<uint32>(eObjectTag::Bleeds), "Bleeds", nullptr },
	{ static_cast<uint32>(eObjectTag::Spawn), "Spawn", nullptr },
	{ static_cast<uint32>(eObjectTag::Trigger), "Trigger",
	  "Blockout brushes only. Calls object_trigger_enter and object_trigger_exit in the object's script when the "
	  "player enters and leaves it" },
};

static constexpr FlagName scFlagNames[] = {
	{ static_cast<uint32>(eObjectFlags::ReadyToRender), "Ready To Render",
	  "Set by the renderer once the mesh and material have loaded" },
	{ static_cast<uint32>(eObjectFlags::PhysicsEnabled), "Physics Enabled",
	  "Blockout brushes: on is a dynamic body, off is static" },
	{ static_cast<uint32>(eObjectFlags::IsInstance), "Is Instance", "Set for instances of another object" },
	{ static_cast<uint32>(eObjectFlags::ShadowCaster), "Shadow Caster", "Probe volumes never cast shadows" },
	{ static_cast<uint32>(eObjectFlags::Unlit), "Unlit",
	  "Applied to the shared material when the object loads, so it can't be changed here" },
	{ static_cast<uint32>(eObjectFlags::DisableCulling), "Disable Culling", nullptr },
	{ static_cast<uint32>(eObjectFlags::NotProbeVisible), "Not Probe Visible",
	  "Probe volumes are never visible to probes" },
};

template <size_t TCount>
static wxStaticBoxSizer*
MakeFlagGroup(wxWindow* parent, const char* title, const FlagName (&names)[TCount],
			  StackArray<ObjectPropertiesPanel::FlagRow, ObjectPropertiesPanel::scMaxRows>& out_rows)
{
	static_assert(TCount <= ObjectPropertiesPanel::scMaxRows);

	wxStaticBoxSizer* group = new wxStaticBoxSizer(wxVERTICAL, parent, title);

	for (const FlagName& name : names) {
		wxCheckBox* check_box = new wxCheckBox(group->GetStaticBox(), wxID_ANY, name.pName);

		if (name.pHint != nullptr) {
			check_box->SetToolTip(name.pHint);
		}

		group->Add(check_box, wxSizerFlags().Border(wxALL, 2));
		out_rows.Insert(ObjectPropertiesPanel::FlagRow { .Bit = name.Bit, .pCheckBox = check_box });
	}

	return group;
}

ObjectPropertiesPanel::ObjectPropertiesPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "Properties");
	title->SetFont(title->GetFont().Bold());
	sizer->Add(title, wxSizerFlags().Border(wxALL, 6));

	mpNameLabel = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	sizer->Add(mpNameLabel, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	wxBoxSizer* material_row = new wxBoxSizer(wxHORIZONTAL);
	material_row->Add(new wxStaticText(this, wxID_ANY, "Material"), wxSizerFlags().CenterVertical());

	mpMaterialChoice = new wxChoice(this, wxID_ANY);
	material_row->Add(mpMaterialChoice, wxSizerFlags().Border(wxLEFT, 6));

	wxButton* browse_button = new wxButton(this, wxID_ANY, "Browse...", wxDefaultPosition, wxDefaultSize,
										   wxBU_EXACTFIT);
	material_row->Add(browse_button, wxSizerFlags().Border(wxLEFT, 6));

	browse_button->Bind(wxEVT_BUTTON,
						[this](wxCommandEvent&)
						{
							if (EditorFrame* frame = dynamic_cast<EditorFrame*>(wxGetTopLevelParent(this))) {
								frame->ShowMaterialPickerWindow();
							}
						});

	sizer->Add(material_row, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	mpMaterialChoice->Bind(wxEVT_CHOICE, &ObjectPropertiesPanel::OnMaterialChoice, this);

	wxBoxSizer* script_row = new wxBoxSizer(wxHORIZONTAL);
	script_row->Add(new wxStaticText(this, wxID_ANY, "Script"), wxSizerFlags().CenterVertical());

	mpScriptText = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	mpScriptText->SetHint("Scripts/objects/example.strata");
	script_row->Add(mpScriptText, wxSizerFlags(1).Border(wxLEFT, 6));

	mpScriptBrowse = new wxButton(this, wxID_ANY, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	script_row->Add(mpScriptBrowse, wxSizerFlags().Border(wxLEFT, 6));

	mpScriptClear = new wxButton(this, wxID_ANY, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	script_row->Add(mpScriptClear, wxSizerFlags().Border(wxLEFT, 6));

	sizer->Add(script_row, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	mpScriptStatus = new wxStaticText(this, wxID_ANY, wxEmptyString);
	sizer->Add(mpScriptStatus, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	wxBoxSizer* enter_direction_row = new wxBoxSizer(wxHORIZONTAL);
	enter_direction_row->Add(new wxStaticText(this, wxID_ANY, "Enter direction"), wxSizerFlags().CenterVertical());

	mpEnterDirChoice = new wxChoice(this, wxID_ANY);

	for (const EnterDirectionChoice& choice : scEnterDirections) {
		mpEnterDirChoice->Append(choice.pName);
	}

	mpEnterDirChoice->SetSelection(0);
	mpEnterDirChoice->SetToolTip("Triggers only. When set, the trigger only fires if the player enters it moving along "
								 "this direction in the brush's local space (within 90 degrees). Any fires from every "
								 "side");
	enter_direction_row->Add(mpEnterDirChoice, wxSizerFlags(1).Border(wxLEFT, 6));

	sizer->Add(enter_direction_row, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	mpEnterDirChoice->Bind(wxEVT_CHOICE, &ObjectPropertiesPanel::OnEnterDirectionChoice, this);

	mpScriptText->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { CommitScript(); });
	mpScriptText->Bind(wxEVT_KILL_FOCUS,
					   [this](wxFocusEvent& event)
					   {
						   CommitScript();
						   event.Skip();
					   });
	mpScriptBrowse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { BrowseScript(); });
	mpScriptClear->Bind(wxEVT_BUTTON,
						[this](wxCommandEvent&)
						{
							mpScriptText->ChangeValue(wxEmptyString);
							CommitScript();
						});

	sizer->Add(MakeFlagGroup(this, "Tags", scTagNames, mTagRows), wxSizerFlags().Expand().Border(wxALL, 6));
	sizer->Add(MakeFlagGroup(this, "Flags", scFlagNames, mFlagRows), wxSizerFlags().Expand().Border(wxALL, 6));

	BindRows(mTagRows, true);
	BindRows(mFlagRows, false);

	SetSizer(sizer);

	ApplyState(ObjectPanelState {}, MaterialLibraryState {});
}

void ObjectPropertiesPanel::BindRows(StackArray<FlagRow, scMaxRows>& rows, bool is_tag)
{
	for (FlagRow& row : rows) {
		const uint32 bit = row.Bit;

		row.pCheckBox->Bind(wxEVT_CHECKBOX, [this, is_tag, bit](wxCommandEvent& event)
							{ OnRowToggled(is_tag, bit, event.IsChecked()); });
	}
}

void ObjectPropertiesPanel::OnRowToggled(bool is_tag, uint32 bit, bool checked)
{
	if (!mbHasObject) {
		return;
	}

	thread::PostToGame([is_tag, bit, checked] { gEditor->SetSelectionObjectBit(is_tag, bit, checked); });
}

void ObjectPropertiesPanel::SetRows(StackArray<FlagRow, scMaxRows>& rows, uint32 value, uint32 editable,
									bool has_object)
{
	for (FlagRow& row : rows) {
		const bool checked = has_object && (value & row.Bit) != 0;
		const bool can_edit = has_object && (editable & row.Bit) != 0;

		if (row.pCheckBox->GetValue() != checked) {
			row.pCheckBox->SetValue(checked);
		}

		if (row.pCheckBox->IsEnabled() != can_edit) {
			row.pCheckBox->Enable(can_edit);
		}
	}
}

void ObjectPropertiesPanel::OnMaterialChoice(wxCommandEvent& event)
{
	const int32 slot = mpMaterialChoice->GetSelection();

	if (!mbHasObject || slot < 0) {
		return;
	}

	thread::PostToGame(
		[slot]
		{
			if (gWorld->pBlockout == nullptr) {
				return;
			}

			const MaterialLibraryID library_slot = MaterialLibraryID(slot);

			if (!library_slot.IsValid() || static_cast<int32>(library_slot.ID) >=
											   static_cast<int32>(gWorld->pBlockout->GetMaterialLibrary().GetCount())) {
				return;
			}

			const MaterialID material = gWorld->pBlockout->GetMaterialForID(library_slot);
			const EditorSelection& selection = gEditor->GetSelection();

			if (Object* last = selection.GetLast(); last != nullptr) {
				gEditor->SetStoredMaterial(last, material);
			}
		});
}

void ObjectPropertiesPanel::CommitScript()
{
	if (!mbHasObject || !mbCanAttachScript) {
		return;
	}

	const std::string path = mpScriptText->GetValue().Trim().ToStdString();

	if (path == mShownScript) {
		return;
	}

	mShownScript = path;

	thread::PostToGame([path] { gEditor->SetSelectionScript(String(path)); });
}

void ObjectPropertiesPanel::OnEnterDirectionChoice(wxCommandEvent& event)
{
	const int32 selection = event.GetSelection();

	if (!mbHasObject || !mbIsTrigger || selection < 0 || selection >= scNumEnterDirections) {
		return;
	}

	const EnterDirectionChoice choice = scEnterDirections[selection];

	thread::PostToGame([choice] { gEditor->SetSelectionEnterDirection(Vec3f(choice.X, choice.Y, choice.Z)); });
}

void ObjectPropertiesPanel::BrowseScript()
{
	if (!mbHasObject || !mbCanAttachScript) {
		return;
	}

	std::error_code error;
	const std::filesystem::path root = std::filesystem::current_path(error) / "Scripts";

	wxFileDialog dialog(this, "Attach script", wxString::FromUTF8(root.string()), wxEmptyString,
						"Strata scripts (*.strata)|*.strata", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	const std::filesystem::path chosen(dialog.GetPath().ToStdString());
	const std::filesystem::path relative = std::filesystem::relative(chosen, std::filesystem::current_path(), error);

	mpScriptText->ChangeValue(wxString::FromUTF8((error || relative.empty()) ? chosen.string() : relative.string()));

	CommitScript();
}

void ObjectPropertiesPanel::RefreshMaterialChoices(const MaterialLibraryState& materials)
{
	if (materials.Names == mShownMaterialNames) {
		return;
	}

	mpMaterialChoice->Clear();

	for (const std::string& name : materials.Names) {
		mpMaterialChoice->Append(wxString::FromUTF8(name));
	}

	mShownMaterialNames = materials.Names;
}

void ObjectPropertiesPanel::ApplyState(const ObjectPanelState& state, const MaterialLibraryState& materials)
{
	RefreshMaterialChoices(materials);

	mbHasObject = state.bHasObject;
	mbCanAttachScript = state.bHasObject && state.bCanAttachScript;
	mbIsTrigger = state.bHasObject && state.bIsTrigger;

	if (!state.bHasObject) {
		mpNameLabel->SetLabel("No block selected");
	}
	else {
		wxString name = wxString::Format("Selected '%s'", wxString::FromUTF8(state.Name));

		if (state.SelectedCount > 1) {
			name += wxString::Format(" (+%u more)", state.SelectedCount - 1);
		}

		mpNameLabel->SetLabel(name);
	}

	SetRows(mTagRows, state.Tags, state.EditableTags, state.bHasObject);
	SetRows(mFlagRows, state.Flags, state.EditableFlags, state.bHasObject);

	const int32 material_selection = state.bHasObject ? state.MaterialSlot : wxNOT_FOUND;

	if (mpMaterialChoice->GetSelection() != material_selection) {
		mpMaterialChoice->SetSelection(material_selection);
	}

	mpMaterialChoice->Enable(state.bHasObject && state.bIsBlockout);

	mpScriptText->Enable(mbCanAttachScript);
	mpScriptBrowse->Enable(mbCanAttachScript);
	mpScriptClear->Enable(mbCanAttachScript && !state.ScriptPath.empty());

	if (state.ScriptPath != mShownScript || !mbCanAttachScript) {
		if (!mpScriptText->HasFocus() || !mbCanAttachScript) {
			mpScriptText->ChangeValue(wxString::FromUTF8(mbCanAttachScript ? state.ScriptPath : std::string()));
			mShownScript = mbCanAttachScript ? state.ScriptPath : std::string();
		}
	}

	mpEnterDirChoice->Enable(mbIsTrigger);

	const int32 enter_direction_index = mbIsTrigger ? GetEnterDirectionIndex(state) : 0;
	const bool has_custom_item = (static_cast<int32>(mpEnterDirChoice->GetCount()) > scNumEnterDirections);

	if (enter_direction_index == scCustomEnterDirectionIndex) {
		const wxString label = wxString::Format("Custom (%.2f, %.2f, %.2f)", state.EnterDirection[0],
												state.EnterDirection[1], state.EnterDirection[2]);

		if (!has_custom_item) {
			mpEnterDirChoice->Append(label);
		}
		else if (mpEnterDirChoice->GetString(scCustomEnterDirectionIndex) != label) {
			mpEnterDirChoice->SetString(scCustomEnterDirectionIndex, label);
		}
	}
	else if (has_custom_item) {
		if (mpEnterDirChoice->GetSelection() == scCustomEnterDirectionIndex) {
			mpEnterDirChoice->SetSelection(enter_direction_index);
		}

		mpEnterDirChoice->Delete(scCustomEnterDirectionIndex);
	}

	if (mpEnterDirChoice->GetSelection() != enter_direction_index) {
		mpEnterDirChoice->SetSelection(enter_direction_index);
	}

	const bool has_errors = mbCanAttachScript && state.bScriptErrors;

	if (has_errors != mbShownScriptErrors) {
		mbShownScriptErrors = has_errors;
		mpScriptStatus->SetLabel(has_errors ? wxString("Script failed to compile, see the log") : wxString());
		Layout();
	}
}

} // namespace fx::editor

#include "ObjectPropertiesPanel.hpp"

#include "Common.hpp"
#include "EditOperation.hpp"
#include "EditorFrame.hpp"
#include "EditorThread.hpp"
#include "RaptorEditor.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
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
	{ "Any", 0.0f, 0.0f, 0.0f },		{ "+X (right)", 1.0f, 0.0f, 0.0f }, { "-X (left)", -1.0f, 0.0f, 0.0f },
	{ "+Y (up)", 0.0f, 1.0f, 0.0f },	{ "-Y (down)", 0.0f, -1.0f, 0.0f }, { "+Z (forward)", 0.0f, 0.0f, 1.0f },
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
		const float32 dot = Vec3f(choice.X, choice.Y, choice.Z).Dot(state.EnterDirection);

		if (dot > scAxisMatchDot) {
			return i;
		}
	}

	return scCustomEnterDirectionIndex;
}

template <typename TState>
struct FlagName
{
	TState Bit;
	const char* pName;
	const char* pHint;
};

static constexpr FlagName<eObjectTag> scTagNames[] = {
	{ eObjectTag::Blockout, "Blockout", "Marks level geometry. Set when the brush is created" },
	{ eObjectTag::LockTransform, "Lock Transform", nullptr },
	{ eObjectTag::ProbeVolume, "Probe Volume", "Blockout brushes only" },
	{ eObjectTag::ReflectionProbe, "Reflection Probe", "Blockout brushes only" },
	{ eObjectTag::Spawn, "Spawn", nullptr },
	{ eObjectTag::Trigger, "Trigger",
	  "Blockout brushes only. Calls object_trigger_enter and object_trigger_exit in the object's script when the "
	  "player enters and leaves it" },
	{ eObjectTag::NvBrush, "nvbrush",
	  "Blockout brushes only. Collides but is never drawn. The map compiler keeps only its collider" },
	{ eObjectTag::DBound, "dbound",
	  "Blockout brushes only. Seals the map for the map compiler: no faces, no collision" },
	{ eObjectTag::Patch, "Patch", "Marks a curve patch. Set when the patch is created" },
};

static constexpr FlagName<eObjectFlags> scFlagNames[] = {
	{ eObjectFlags::ReadyToRender, "Ready To Render", "Set by the renderer once the mesh and material have loaded" },
	{ eObjectFlags::PhysicsEnabled, "Physics Enabled", "Blockout brushes: on is a dynamic body, off is static" },
	{ eObjectFlags::ShadowCaster, "Shadow Caster", "Probe volumes never cast shadows" },
	{ eObjectFlags::Unlit, "Unlit",
	  "Applied to the shared material when the object loads, so it can't be changed here" },
	{ eObjectFlags::DisableCulling, "Disable Culling", nullptr },
	{ eObjectFlags::NotProbeVisible, "Not Probe Visible", "Probe volumes are never visible to probes" },
};

template <typename TState, size_t TCount>
static PropertySection* MakeFlagSection(wxWindow* parent, const char* title, const FlagName<TState> (&names)[TCount],
										ObjectPropertiesPanel::FlagRows<TState>& out_rows)
{
	static_assert(TCount <= ObjectPropertiesPanel::scMaxRows);

	PropertySection* section = new PropertySection(parent, title);

	for (const FlagName<TState>& name : names) {
		wxCheckBox* check_box = new wxCheckBox(section->GetBody(), wxID_ANY, name.pName);

		if (name.pHint != nullptr) {
			check_box->SetToolTip(name.pHint);
		}

		section->AddWide(check_box);
		out_rows.Insert(ObjectPropertiesPanel::FlagRow<TState> { .Bit = name.Bit, .pCheckBox = check_box });
	}

	return section;
}

ObjectPropertiesPanel::ObjectPropertiesPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	PropertySection* section = new PropertySection(this, "Object");
	wxWindow* body = section->GetBody();
	sizer->Add(section, wxSizerFlags().Expand());

	mpNameLabel = new wxStaticText(body, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	section->AddWide(mpNameLabel);

	wxBoxSizer* material_row = new wxBoxSizer(wxHORIZONTAL);

	mpMaterialChoice = new wxChoice(body, wxID_ANY);
	material_row->Add(mpMaterialChoice, wxSizerFlags(1).CenterVertical());

	wxButton* browse_button = new wxButton(body, wxID_ANY, "...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	browse_button->SetToolTip("Browse materials");
	material_row->Add(browse_button, wxSizerFlags().CenterVertical().Border(wxLEFT, 4));

	browse_button->Bind(wxEVT_BUTTON,
						[this](wxCommandEvent&)
						{
							if (EditorFrame* frame = dynamic_cast<EditorFrame*>(wxGetTopLevelParent(this))) {
								frame->ShowMaterialPicker();
							}
						});

	section->AddRow("Material", material_row);

	mpMaterialChoice->Bind(wxEVT_CHOICE, &ObjectPropertiesPanel::OnMaterialChoice, this);

	wxBoxSizer* script_row = new wxBoxSizer(wxHORIZONTAL);

	mpScriptText = new wxTextCtrl(body, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(40, -1),
								  wxTE_PROCESS_ENTER);
	mpScriptText->SetHint("Scripts/objects/example.strata");
	script_row->Add(mpScriptText, wxSizerFlags(1).CenterVertical());

	mpScriptBrowse = new wxButton(body, wxID_ANY, "...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	mpScriptBrowse->SetToolTip("Browse for a script");
	script_row->Add(mpScriptBrowse, wxSizerFlags().CenterVertical().Border(wxLEFT, 4));

	mpScriptClear = new wxButton(body, wxID_ANY, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	mpScriptClear->SetToolTip("Detach the script");
	script_row->Add(mpScriptClear, wxSizerFlags().CenterVertical().Border(wxLEFT, 4));

	section->AddRow("Script", script_row);

	mpScriptStatus = new wxStaticText(body, wxID_ANY, wxEmptyString);
	mpScriptStatus->Hide();
	section->AddWide(mpScriptStatus);

	mpEnterDirChoice = new wxChoice(body, wxID_ANY);

	for (const EnterDirectionChoice& choice : scEnterDirections) {
		mpEnterDirChoice->Append(choice.pName);
	}

	mpEnterDirChoice->SetSelection(0);
	mpEnterDirChoice->SetToolTip("Triggers only. When set, the trigger only fires if the player enters it moving along "
								 "this direction in the brush's local space (within 90 degrees). Any fires from every "
								 "side");
	section->AddRow("Enter direction", mpEnterDirChoice);

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

	sizer->Add(MakeFlagSection(this, "Tags", scTagNames, mTagRows), wxSizerFlags().Expand());
	sizer->Add(MakeFlagSection(this, "Flags", scFlagNames, mFlagRows), wxSizerFlags().Expand());

	BindRows(mTagRows);
	BindRows(mFlagRows);

	SetSizer(sizer);

	ApplyState(ObjectPanelState {}, MaterialLibraryState {});
}

template <typename TState>
void ObjectPropertiesPanel::BindRows(FlagRows<TState>& rows)
{
	for (FlagRow<TState>& row : rows) {
		const TState bit = row.Bit;

		row.pCheckBox->Bind(wxEVT_CHECKBOX,
							[this, bit](wxCommandEvent& event) { OnRowToggled(bit, event.IsChecked()); });
	}
}

template <typename TState>
void ObjectPropertiesPanel::OnRowToggled(TState bit, bool checked)
{
	if (!mbHasObject) {
		return;
	}

	thread::PostToGame([bit, checked] { gEditor->SetSelectionObjectState(bit, checked); });
}

template <typename TState>
void ObjectPropertiesPanel::SetRows(FlagRows<TState>& rows, TState value, TState editable, bool has_object)
{
	for (FlagRow<TState>& row : rows) {
		const bool checked = has_object && fx::HasFlag(value, row.Bit);
		const bool can_edit = has_object && fx::HasFlag(editable, row.Bit);

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
			if (gWorld->pPrototype == nullptr) {
				return;
			}

			const MaterialLibraryID library_slot = MaterialLibraryID(slot);

			if (!library_slot.IsValid() ||
				static_cast<int32>(library_slot.ID) >=
					static_cast<int32>(gWorld->pPrototype->GetMaterialLibrary().GetCount())) {
				return;
			}

			const MaterialID material = gWorld->pPrototype->GetMaterialForID(library_slot);
			const EditorSelection& selection = gEditor->GetSelection();

			if (Object* last = selection.GetLast(); last != nullptr) {
				gEditor->SetStoredMaterial(last, material);
				gEditor->MarkModified();
			}
		});
}

void ObjectPropertiesPanel::CommitScript()
{
	if (!mbHasObject || !mbCanAttachScript) {
		return;
	}

	const String path(mpScriptText->GetValue().Trim().ToStdString());

	if (path == mShownScript) {
		return;
	}

	mShownScript = path;

	thread::PostToGame([path] { gEditor->SetSelectionScript(path); });
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

	for (const String& name : materials.Names) {
		mpMaterialChoice->Append(wxString::FromUTF8(name.CStr()));
	}

	mShownMaterialNames = materials.Names.Clone();
}

void ObjectPropertiesPanel::ApplyState(const ObjectPanelState& state, const MaterialLibraryState& materials)
{
	RefreshMaterialChoices(materials);

	mbHasObject = state.bHasObject;
	mbCanAttachScript = state.bHasObject && state.bCanAttachScript;
	mbIsTrigger = state.bHasObject && state.bIsTrigger;

	if (!state.bHasObject) {
		if (state.ActiveSubview.IsEmpty()) {
			mpNameLabel->SetLabel("No block selected");
		}
		else {
			mpNameLabel->SetLabel(
				wxString::Format("Editing subview '%s'", wxString::FromUTF8(state.ActiveSubview.CStr())));
		}
	}
	else if (!state.SubviewName.IsEmpty()) {
		mpNameLabel->SetLabel(wxString::Format("Selected subview '%s' (%u brushes)",
											   wxString::FromUTF8(state.SubviewName.CStr()), state.SubviewBrushCount));
	}
	else {
		wxString name = wxString::Format("Selected '%s'", wxString::FromUTF8(state.Name.CStr()));

		if (state.SelectedCount > 1) {
			name += wxString::Format(" (+%u more)", state.SelectedCount - 1);
		}

		if (!state.ActiveSubview.IsEmpty()) {
			name += wxString::Format(" in subview '%s'", wxString::FromUTF8(state.ActiveSubview.CStr()));
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
	mpScriptClear->Enable(mbCanAttachScript && !state.ScriptPath.IsEmpty());

	if (state.ScriptPath != mShownScript || !mbCanAttachScript) {
		if (!mpScriptText->HasFocus() || !mbCanAttachScript) {
			if (mbCanAttachScript) {
				mpScriptText->ChangeValue(wxString::FromUTF8(state.ScriptPath.CStr()));
				mShownScript = state.ScriptPath;
			}
			else {
				mpScriptText->ChangeValue(wxEmptyString);
				mShownScript = String();
			}
		}
	}

	mpEnterDirChoice->Enable(mbIsTrigger);

	const int32 enter_direction_index = mbIsTrigger ? GetEnterDirectionIndex(state) : 0;
	const bool has_custom_item = (static_cast<int32>(mpEnterDirChoice->GetCount()) > scNumEnterDirections);

	if (enter_direction_index == scCustomEnterDirectionIndex) {
		const wxString label = wxString::Format("Custom (%.2f, %.2f, %.2f)", state.EnterDirection.X,
												state.EnterDirection.Y, state.EnterDirection.Z);

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
		mpScriptStatus->Show(has_errors);
		RelayoutToScroller(mpScriptStatus->GetParent());
	}
}

} // namespace fx::editor

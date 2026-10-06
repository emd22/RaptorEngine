#include "ObjectPropertiesPanel.hpp"

#include "EditOperation.hpp"
#include "EditorFrame.hpp"
#include "RaptorEditor.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include <Blockout.hpp>
#include <Engine.hpp>
#include <World.hpp>

namespace fx::editor {

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
};

static constexpr FlagName scFlagNames[] = {
	{ static_cast<uint32>(eObjectFlags::ReadyToRender), "Ready To Render",
	  "Set by the renderer once the mesh and material have loaded" },
	{ static_cast<uint32>(eObjectFlags::PhysicsEnabled), "Physics Enabled", "Blockout brushes: on is a dynamic body, off is static" },
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

	wxButton* browse_button = new wxButton(this, wxID_ANY, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
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

	sizer->Add(MakeFlagGroup(this, "Tags", scTagNames, mTagRows), wxSizerFlags().Expand().Border(wxALL, 6));
	sizer->Add(MakeFlagGroup(this, "Flags", scFlagNames, mFlagRows), wxSizerFlags().Expand().Border(wxALL, 6));

	BindRows(mTagRows, true);
	BindRows(mFlagRows, false);

	SetSizer(sizer);

	ShowObject(nullptr);
}

void ObjectPropertiesPanel::BindRows(StackArray<FlagRow, scMaxRows>& rows, bool is_tag)
{
	for (FlagRow& row : rows) {
		const uint32 bit = row.Bit;

		row.pCheckBox->Bind(wxEVT_CHECKBOX, [this, is_tag, bit](wxCommandEvent& event) {
			OnRowToggled(is_tag, bit, event.IsChecked());
		});
	}
}

void ObjectPropertiesPanel::OnRowToggled(bool is_tag, uint32 bit, bool checked)
{
	if (mpShownObject == nullptr) {
		return;
	}

	gEditor->SetSelectionObjectBit(is_tag, bit, checked);

	mbRowsStale = true;
	ShowObject(mpShownObject);
}

void ObjectPropertiesPanel::SetRows(StackArray<FlagRow, scMaxRows>& rows, uint32 value, Object* object, bool is_tag)
{
	for (FlagRow& row : rows) {
		const bool can_edit = is_tag ? CanEditObjectTag(object, row.Bit) : CanEditObjectFlag(object, row.Bit);

		row.pCheckBox->SetValue(object != nullptr && (value & row.Bit) != 0);
		row.pCheckBox->Enable(can_edit);
	}
}

void ObjectPropertiesPanel::OnMaterialChoice(wxCommandEvent& event)
{
	if (mpShownObject == nullptr || gWorld->pBlockout == nullptr) {
		return;
	}

	const int32 slot = mpMaterialChoice->GetSelection();
	if (slot < 0 || slot >= static_cast<int32>(gWorld->pBlockout->GetMaterialLibrary().GetCount())) {
		return;
	}

	mShownMaterialSlot = slot;

	gEditor->SetStoredMaterial(mpShownObject, gWorld->pBlockout->GetMaterialForID(slot));
}

void ObjectPropertiesPanel::RefreshMaterialChoices()
{
	if (gWorld->pBlockout == nullptr) {
		return;
	}

	const MaterialLibrary& library = gWorld->pBlockout->GetMaterialLibrary();

	if (library.GetCount() == mShownMaterialCount) {
		return;
	}

	mpMaterialChoice->Clear();

	for (uint32 id = 0; id < library.GetCount(); id++) {
		mpMaterialChoice->Append(wxString::FromUTF8(library.GetName(id).Str()));
	}

	mShownMaterialCount = library.GetCount();
	mShownMaterialSlot = -1;
}

void ObjectPropertiesPanel::ShowObject(Object* object)
{
	RefreshMaterialChoices();

	if (object == nullptr) {
		if (!mbShowingAnything) {
			return;
		}

		mbShowingAnything = false;
		mpShownObject = nullptr;

		mpNameLabel->SetLabel("No block selected");
		SetRows(mTagRows, 0, nullptr, true);
		SetRows(mFlagRows, 0, nullptr, false);

		mpMaterialChoice->SetSelection(wxNOT_FOUND);
		mpMaterialChoice->Disable();
		mShownMaterialSlot = -1;
		return;
	}

	const uint32 tags = static_cast<uint32>(object->Tags);
	const uint32 flags = static_cast<uint32>(object->GetFlags());
	wxString name = wxString::Format("Selected '%s'", wxString::FromUTF8(object->Name.Get()));

	const uint32 selected_count = gEditor->GetSelection().GetCount();

	if (selected_count > 1) {
		name += wxString::Format(" (+%u more)", selected_count - 1);
	}

	int32 material_slot = -1;

	if (gWorld->pBlockout != nullptr) {
		material_slot = gWorld->pBlockout->GetIDForMaterial(gEditor->GetSelection().GetStoredMaterial(object));
	}

	if (material_slot != mShownMaterialSlot) {
		mShownMaterialSlot = material_slot;
		mpMaterialChoice->SetSelection(material_slot);
	}

	mpMaterialChoice->Enable(object->HasTags(eObjectTag::Blockout));

	if (mbShowingAnything && !mbRowsStale && object == mpShownObject && tags == mShownTags && flags == mShownFlags &&
		name == mShownName) {
		return;
	}

	mbShowingAnything = true;
	mbRowsStale = false;
	mpShownObject = object;
	mShownTags = tags;
	mShownFlags = flags;
	mShownName = name;

	mpNameLabel->SetLabel(name.IsEmpty() ? wxString("(unnamed)") : name);
	SetRows(mTagRows, tags, object, true);
	SetRows(mFlagRows, flags, object, false);

}

} // namespace fx::editor

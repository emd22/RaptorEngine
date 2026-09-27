#include "ObjectPropertiesPanel.hpp"

#include "RaptorEditor.hpp"

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
};

static constexpr FlagName scTagNames[] = {
	{ static_cast<uint32>(eObjectTag::Blockout), "Blockout" },
	{ static_cast<uint32>(eObjectTag::LockTransform), "Lock Transform" },
	{ static_cast<uint32>(eObjectTag::ProbeVolume), "Probe Volume" },
};

static constexpr FlagName scFlagNames[] = {
	{ static_cast<uint32>(eObjectFlags::ReadyToRender), "Ready To Render" },
	{ static_cast<uint32>(eObjectFlags::PhysicsEnabled), "Physics Enabled" },
	{ static_cast<uint32>(eObjectFlags::IsInstance), "Is Instance" },
	{ static_cast<uint32>(eObjectFlags::ShadowCaster), "Shadow Caster" },
	{ static_cast<uint32>(eObjectFlags::Unlit), "Unlit" },
	{ static_cast<uint32>(eObjectFlags::DisableCulling), "Disable Culling" },
	{ static_cast<uint32>(eObjectFlags::NotProbeVisible), "Not Probe Visible" },
};

/// Dropdown items, in eCProtoMat order
static constexpr const char* scMaterialSlotNames[] = { "Gray", "Orange", "Blue", "Tile" };

static_assert(std::size(scMaterialSlotNames) == static_cast<size_t>(eCProtoMat::Count));

template <size_t TCount>
static wxStaticBoxSizer*
MakeFlagGroup(wxWindow* parent, const char* title, const FlagName (&names)[TCount],
			  StackArray<ObjectPropertiesPanel::FlagRow, ObjectPropertiesPanel::scMaxRows>& out_rows)
{
	static_assert(TCount <= ObjectPropertiesPanel::scMaxRows);

	wxStaticBoxSizer* group = new wxStaticBoxSizer(wxVERTICAL, parent, title);

	for (const FlagName& name : names) {
		wxCheckBox* check_box = new wxCheckBox(group->GetStaticBox(), wxID_ANY, name.pName);

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
	for (const char* slot_name : scMaterialSlotNames) {
		mpMaterialChoice->Append(slot_name);
	}
	material_row->Add(mpMaterialChoice, wxSizerFlags().Border(wxLEFT, 6));

	sizer->Add(material_row, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	mpMaterialChoice->Bind(wxEVT_CHOICE, &ObjectPropertiesPanel::OnMaterialChoice, this);

	sizer->Add(MakeFlagGroup(this, "Tags", scTagNames, mTagRows), wxSizerFlags().Expand().Border(wxALL, 6));
	sizer->Add(MakeFlagGroup(this, "Flags", scFlagNames, mFlagRows), wxSizerFlags().Expand().Border(wxALL, 6));

	SetSizer(sizer);

	ShowObject(nullptr);
}

void ObjectPropertiesPanel::SetRows(StackArray<FlagRow, scMaxRows>& rows, uint32 value, bool has_object)
{
	for (FlagRow& row : rows) {
		row.pCheckBox->SetValue(has_object && (value & row.Bit) != 0);
	}
}

void ObjectPropertiesPanel::OnMaterialChoice(wxCommandEvent& event)
{
	if (mpShownObject == nullptr || gWorld->pBlockout == nullptr) {
		return;
	}

	const int32 slot = mpMaterialChoice->GetSelection();
	if (slot < 0 || slot >= static_cast<int32>(eCProtoMat::Count)) {
		return;
	}

	mShownMaterialSlot = slot;

	const MaterialID material = gWorld->pBlockout->GetMaterialForSlot(static_cast<eCProtoMat>(slot));
	gEditor->SetStoredMaterial(mpShownObject, material);
}

void ObjectPropertiesPanel::ShowObject(Object* object)
{
	if (object == nullptr) {
		if (!mbShowingAnything) {
			return;
		}

		mbShowingAnything = false;
		mpShownObject = nullptr;

		mpNameLabel->SetLabel("No block selected");
		SetRows(mTagRows, 0, false);
		SetRows(mFlagRows, 0, false);

		mpMaterialChoice->SetSelection(wxNOT_FOUND);
		mpMaterialChoice->Disable();
		mShownMaterialSlot = -1;
		return;
	}

	const uint32 tags = static_cast<uint32>(object->Tags);
	const uint32 flags = static_cast<uint32>(object->GetFlags());
	const wxString name = wxString::Format("Selected '%s'", wxString::FromUTF8(object->Name.Get()));

	int32 material_slot = -1;

	if (gWorld->pBlockout != nullptr) {
		const MaterialID current_material = gEditor->GetSelection().GetStoredMaterial(object);

		for (uint32 slot = 0; slot < static_cast<uint32>(eCProtoMat::Count); slot++) {
			if (current_material == gWorld->pBlockout->GetMaterialForSlot(static_cast<eCProtoMat>(slot))) {
				material_slot = static_cast<int32>(slot);
				break;
			}
		}
	}

	if (material_slot != mShownMaterialSlot) {
		mShownMaterialSlot = material_slot;
		mpMaterialChoice->SetSelection(material_slot);
	}

	mpMaterialChoice->Enable();

	if (mbShowingAnything && object == mpShownObject && tags == mShownTags && flags == mShownFlags &&
		name == mShownName) {
		return;
	}

	mbShowingAnything = true;
	mpShownObject = object;
	mShownTags = tags;
	mShownFlags = flags;
	mShownName = name;

	mpNameLabel->SetLabel(name.IsEmpty() ? wxString("(unnamed)") : name);
	SetRows(mTagRows, tags, true);
	SetRows(mFlagRows, flags, true);
}

} // namespace fx::editor

#include "ObjectPropertiesPanel.hpp"

#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

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

template <size_t TCount>
static wxStaticBoxSizer*
MakeFlagGroup(wxWindow* parent, const char* title, const FlagName (&names)[TCount],
			  StackArray<ObjectPropertiesPanel::FlagRow, ObjectPropertiesPanel::scMaxRows>& out_rows)
{
	static_assert(TCount <= ObjectPropertiesPanel::scMaxRows);

	wxStaticBoxSizer* group = new wxStaticBoxSizer(wxVERTICAL, parent, title);

	for (const FlagName& name : names) {
		wxCheckBox* check_box = new wxCheckBox(group->GetStaticBox(), wxID_ANY, name.pName);

		// The panel only shows the flags for now
		check_box->Disable();

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

void ObjectPropertiesPanel::ShowObject(const Object* object)
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
		return;
	}

	const uint32 tags = static_cast<uint32>(object->Tags);
	const uint32 flags = static_cast<uint32>(object->GetFlags());
	const wxString name = wxString::FromUTF8(object->Name.Get());

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

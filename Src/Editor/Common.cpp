#include "Common.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valnum.h>

#include <Math/Vec3.hpp>

namespace fx::editor {


wxTextCtrl* EditableVector3Field::AddAxisField(wxWindow* parent, const wxString& axis_label)
{
	mpSizer->Add(new wxStaticText(parent, wxID_ANY, axis_label), wxSizerFlags().CenterVertical().Border(wxLEFT, 6));

	wxTextCtrl* field = new wxTextCtrl(parent, wxID_ANY, "0", wxDefaultPosition, wxSize(scFieldWidth, -1),
									   wxTE_PROCESS_ENTER, wxFloatingPointValidator<float32>(scPrecision, nullptr));

	// Commit on Enter, and also on focus-out so a typed value isn't lost by clicking straight to another field
	field->Bind(wxEVT_TEXT_ENTER, &EditableVector3Field::OnCommit, this);
	field->Bind(wxEVT_KILL_FOCUS, &EditableVector3Field::OnKillFocus, this);

	mpSizer->Add(field, wxSizerFlags().CenterVertical().Border(wxLEFT, 2));

	return field;
}

EditableVector3Field::EditableVector3Field(wxWindow* parent, const wxString& label)
{
	mpSizer = new wxBoxSizer(wxHORIZONTAL);

	mpSizer->Add(new wxStaticText(parent, wxID_ANY, label), wxSizerFlags().CenterVertical());

	mpFields[0] = AddAxisField(parent, "X");
	mpFields[1] = AddAxisField(parent, "Y");
	mpFields[2] = AddAxisField(parent, "Z");
}

void EditableVector3Field::SetValue(const Vec3f& value)
{
	mbUpdatingFields = true;

	mpFields[0]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.X)));
	mpFields[1]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.Y)));
	mpFields[2]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.Z)));

	mbUpdatingFields = false;
}

Vec3f EditableVector3Field::GetValue() const
{
	double axis_values[3] = { 0.0, 0.0, 0.0 };

	for (uint32 axis = 0; axis < 3; axis++) {
		// Left as 0 if the field is empty or mid-edit (e.g. just "-"), rather than rejecting the keystroke
		mpFields[axis]->GetValue().ToDouble(&axis_values[axis]);
	}

	return Vec3f(static_cast<float32>(axis_values[0]), static_cast<float32>(axis_values[1]),
				 static_cast<float32>(axis_values[2]));
}

void EditableVector3Field::Enable(bool enable)
{
	for (wxTextCtrl* field : mpFields) {
		field->Enable(enable);
	}
}

bool EditableVector3Field::HasFocus() const
{
	for (wxTextCtrl* field : mpFields) {
		if (field->HasFocus()) {
			return true;
		}
	}

	return false;
}

void EditableVector3Field::OnCommit(wxCommandEvent& event)
{
	if (!mbUpdatingFields && mOnChange) {
		mOnChange(GetValue());
	}

	event.Skip();
}

void EditableVector3Field::OnKillFocus(wxFocusEvent& event)
{
	if (!mbUpdatingFields && mOnChange) {
		mOnChange(GetValue());
	}

	// Required for focus to actually move on to the next field/control
	event.Skip();
}


} // namespace fx::editor

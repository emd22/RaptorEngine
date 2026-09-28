#include "Common.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valnum.h>

#include <Math/Vec3.hpp>

namespace fx::editor {

/////////////////////////////////////
// FieldBaseImpl
/////////////////////////////////////


FieldBaseImpl::FieldBaseImpl(wxWindow* parent, const wxString& label)
{
	/* */
	mpSizer = new wxBoxSizer(wxHORIZONTAL);
	mpSizer->Add(new wxStaticText(parent, wxID_ANY, label), wxSizerFlags().CenterVertical());
}


/////////////////////////////////////
// Vector3 field
/////////////////////////////////////


Vector3Field::Vector3Field(wxWindow* parent, const wxString& label) : FieldBase(parent, label)
{
	mpFields[0] = MakeAxisField(parent, "X");
	mpFields[1] = MakeAxisField(parent, "Y");
	mpFields[2] = MakeAxisField(parent, "Z");
}


void Vector3Field::OnCommit(wxCommandEvent& event)
{
	if (!mbUpdatingFields && mpOnChange) {
		mpOnChange(GetValue());
	}

	event.Skip();
}

void Vector3Field::OnFocusLost(wxFocusEvent& event)
{
	if (!mbUpdatingFields && mpOnChange) {
		mpOnChange(GetValue());
	}

	// Required for focus to actually move on to the next field/control
	event.Skip();
}

wxTextCtrl* Vector3Field::MakeAxisField(wxWindow* parent, const wxString& axis_label)
{
	mpSizer->Add(new wxStaticText(parent, wxID_ANY, axis_label), wxSizerFlags().CenterVertical().Border(wxLEFT, 6));

	wxTextCtrl* field = new wxTextCtrl(parent, wxID_ANY, "0", wxDefaultPosition, wxSize(scFieldWidth, -1),
									   wxTE_PROCESS_ENTER, wxFloatingPointValidator<float32>(scPrecision, nullptr));

	// Commit on enter
	field->Bind(wxEVT_TEXT_ENTER, &Vector3Field::OnCommit, this);
	// Commit when clicking off of the field
	field->Bind(wxEVT_KILL_FOCUS, &Vector3Field::OnFocusLost, this);

	mpSizer->Add(field, wxSizerFlags().CenterVertical().Border(wxLEFT, 2));

	return field;
}


Vector3Field::TType Vector3Field::GetValue() const
{
	double axis_values[3] = { 0.0, 0.0, 0.0 };

	for (uint32 axis = 0; axis < 3; axis++) {
		mpFields[axis]->GetValue().ToDouble(&axis_values[axis]);
	}

	return Vec3f(static_cast<float32>(axis_values[0]), static_cast<float32>(axis_values[1]),
				 static_cast<float32>(axis_values[2]));
}

void Vector3Field::SetValue(Vector3Field::TRefType value)
{
	mbUpdatingFields = true;

	mpFields[0]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.X)));
	mpFields[1]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.Y)));
	mpFields[2]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.Z)));

	mbUpdatingFields = false;
}

bool Vector3Field::HasFocus() const
{
	for (wxTextCtrl* field : mpFields) {
		if (field->HasFocus()) {
			return true;
		}
	}

	return false;
}

void Vector3Field::SetEnabled(bool enable)
{
	for (wxTextCtrl* field : mpFields) {
		field->Enable(enable);
	}
}


/////////////////////////////////////
//
/////////////////////////////////////
//
// wxTextCtrl* Vector3Field_OLD::AddAxisField(wxWindow* parent, const wxString& axis_label)
// {
// 	mpSizer->Add(new wxStaticText(parent, wxID_ANY, axis_label), wxSizerFlags().CenterVertical().Border(wxLEFT, 6));
//
// 	wxTextCtrl* field = new wxTextCtrl(parent, wxID_ANY, "0", wxDefaultPosition, wxSize(scFieldWidth, -1),
// 									   wxTE_PROCESS_ENTER, wxFloatingPointValidator<float32>(scPrecision, nullptr));
//
// 	// Commit on Enter, and also on focus-out so a typed value isn't lost by clicking straight to another field
// 	field->Bind(wxEVT_TEXT_ENTER, &Vector3Field_OLD::OnCommit, this);
// 	field->Bind(wxEVT_KILL_FOCUS, &Vector3Field_OLD::OnKillFocus, this);
//
// 	mpSizer->Add(field, wxSizerFlags().CenterVertical().Border(wxLEFT, 2));
//
// 	return field;
// }
//
// Vector3Field_OLD::Vector3Field_OLD(wxWindow* parent, const wxString& label)
// {
// 	mpSizer = new wxBoxSizer(wxHORIZONTAL);
//
// 	mpSizer->Add(new wxStaticText(parent, wxID_ANY, label), wxSizerFlags().CenterVertical());
//
// 	mpFields[0] = AddAxisField(parent, "X");
// 	mpFields[1] = AddAxisField(parent, "Y");
// 	mpFields[2] = AddAxisField(parent, "Z");
// }
//
// void Vector3Field_OLD::SetValue(const Vec3f& value)
// {
// 	mbUpdatingFields = true;
//
// 	mpFields[0]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.X)));
// 	mpFields[1]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.Y)));
// 	mpFields[2]->ChangeValue(wxString::Format("%.*f", scPrecision, static_cast<double>(value.Z)));
//
// 	mbUpdatingFields = false;
// }
//
// Vec3f Vector3Field_OLD::GetValue() const
// {
// 	double axis_values[3] = { 0.0, 0.0, 0.0 };
//
// 	for (uint32 axis = 0; axis < 3; axis++) {
// 		// Left as 0 if the field is empty or mid-edit (e.g. just "-"), rather than rejecting the keystroke
// 		mpFields[axis]->GetValue().ToDouble(&axis_values[axis]);
// 	}
//
// 	return Vec3f(static_cast<float32>(axis_values[0]), static_cast<float32>(axis_values[1]),
// 				 static_cast<float32>(axis_values[2]));
// }
//
// void Vector3Field_OLD::Enable(bool enable)
// {
// 	for (wxTextCtrl* field : mpFields) {
// 		field->Enable(enable);
// 	}
// }
//
//
// bool Vector3Field_OLD::HasFocus() const
// {
// 	for (wxTextCtrl* field : mpFields) {
// 		if (field->HasFocus()) {
// 			return true;
// 		}
// 	}
//
// 	return false;
// }
//
// void Vector3Field_OLD::OnCommit(wxCommandEvent& event)
// {
// 	if (!mbUpdatingFields && mpOnChange) {
// 		mpOnChange(GetValue());
// 	}
//
// 	event.Skip();
// }
//
// void Vector3Field_OLD::OnKillFocus(wxFocusEvent& event)
// {
// 	if (!mbUpdatingFields && mpOnChange) {
// 		mpOnChange(GetValue());
// 	}
//
// 	// Required for focus to actually move on to the next field/control
// 	event.Skip();
// }


/////////////////////////////////////
// Float field
/////////////////////////////////////


} // namespace fx::editor

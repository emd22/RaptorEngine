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
	mpSizer = new wxBoxSizer(wxHORIZONTAL);
	mpSizer->Add(new wxStaticText(parent, wxID_ANY, label), wxSizerFlags().CenterVertical());
}


/////////////////////////////////////
// Vector3 field
/////////////////////////////////////

Vector3Field::Vector3Field(wxWindow* parent, const wxString& label, const Vec2f& limits) : FieldBase(parent, label)
{
	mpFields[0] = MakeAxisField(parent, "X", limits);
	mpFields[1] = MakeAxisField(parent, "Y", limits);
	mpFields[2] = MakeAxisField(parent, "Z", limits);
}

wxTextCtrl* Vector3Field::MakeAxisField(wxWindow* parent, const wxString& axis_label, const Vec2f& limits)
{
	auto validator = wxFloatingPointValidator<float32>(scFloatPrecision, nullptr);
	validator.SetMin(limits.X);
	validator.SetMax(limits.Y);

	mpSizer->Add(new wxStaticText(parent, wxID_ANY, axis_label), wxSizerFlags().CenterVertical().Border(wxLEFT, 6));

	wxTextCtrl* field = new wxTextCtrl(parent, wxID_ANY, "0", wxDefaultPosition, wxSize(scFieldWidth, -1),
									   wxTE_PROCESS_ENTER, validator);

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

	mpFields[0]->ChangeValue(wxString::Format("%.*f", scFloatPrecision, static_cast<double>(value.X)));
	mpFields[1]->ChangeValue(wxString::Format("%.*f", scFloatPrecision, static_cast<double>(value.Y)));
	mpFields[2]->ChangeValue(wxString::Format("%.*f", scFloatPrecision, static_cast<double>(value.Z)));

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

Vector3Field::~Vector3Field()
{
	for (wxTextCtrl* field : mpFields) {
		if (field != nullptr) {
			delete field;
		}

		field = nullptr;
	}
}

/////////////////////////////////////
// Float field
/////////////////////////////////////

FloatField::FloatField(wxWindow* parent, const wxString& label, const Vec2f& limits) : FieldBase(parent, label)
{
	auto validator = wxFloatingPointValidator<float32>(scFloatPrecision, nullptr);
	validator.SetMin(limits.X);
	validator.SetMax(limits.Y);

	mpField = new wxTextCtrl(parent, wxID_ANY, "0", wxDefaultPosition, wxSize(scFieldWidth, -1), wxTE_PROCESS_ENTER,
							 validator);
	// Commit on enter
	mpField->Bind(wxEVT_TEXT_ENTER, &FloatField::OnCommit, this);
	// Commit when clicking off of the field
	mpField->Bind(wxEVT_KILL_FOCUS, &FloatField::OnFocusLost, this);

	mpSizer->Add(mpField, wxSizerFlags().CenterVertical().Border(wxLEFT, 2));
}


FloatField::TType FloatField::GetValue() const
{
	double double_value = 0.0;
	mpField->GetValue().ToDouble(&double_value);

	return static_cast<float32>(double_value);
}

void FloatField::SetValue(FloatField::TRefType value)
{
	mbUpdatingFields = true;
	mpField->ChangeValue(wxString::Format("%.*f", scFloatPrecision, static_cast<double>(value)));

	mbUpdatingFields = false;
}

bool FloatField::HasFocus() const { return mpField->HasFocus(); }

void FloatField::SetEnabled(bool enable) { mpField->Enable(enable); }

FloatField::~FloatField() { delete mpField; }


} // namespace fx::editor

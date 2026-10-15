#include "Common.hpp"

#include "EditorTheme.hpp"

#include <wx/dcbuffer.h>
#include <wx/gbsizer.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valnum.h>

#include <Math/Vec3.hpp>
#include <algorithm>

namespace fx::editor {

/////////////////////////////////////
// FieldBaseImpl
/////////////////////////////////////


FieldBaseImpl::FieldBaseImpl(wxWindow* parent, const wxString& label)
{
	mpLabel = new wxStaticText(parent, wxID_ANY, label);
	mpControls = new wxBoxSizer(wxHORIZONTAL);
}

wxSizer* FieldBaseImpl::GetSizer()
{
	if (mpSizer == nullptr) {
		mpSizer = new wxBoxSizer(wxHORIZONTAL);
		mpSizer->Add(mpLabel, wxSizerFlags().CenterVertical());
		mpSizer->Add(mpControls, wxSizerFlags(1).CenterVertical().Border(wxLEFT, 2));
	}

	return mpSizer;
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

	const int32 label_gap = mpControls->IsEmpty() ? 0 : 6;
	mpControls->Add(new wxStaticText(parent, wxID_ANY, axis_label),
					wxSizerFlags().CenterVertical().Border(wxLEFT, label_gap));

	wxTextCtrl* field = new wxTextCtrl(parent, wxID_ANY, "0", wxDefaultPosition, wxSize(scVectorFieldWidth, -1),
									   wxTE_PROCESS_ENTER, validator);

	// Commit on enter
	field->Bind(wxEVT_TEXT_ENTER, &Vector3Field::OnCommit, this);
	// Commit when clicking off of the field
	field->Bind(wxEVT_KILL_FOCUS, &Vector3Field::OnFocusLost, this);

	mpControls->Add(field, wxSizerFlags(1).CenterVertical().Border(wxLEFT, 2));

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

	RememberValue();
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

	mpControls->Add(mpField, wxSizerFlags(1).CenterVertical());
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

	RememberValue();
}

bool FloatField::HasFocus() const { return mpField->HasFocus(); }

void FloatField::SetEnabled(bool enable) { mpField->Enable(enable); }

FloatField::~FloatField() { delete mpField; }

/////////////////////////////////////
// Property sections
/////////////////////////////////////

void RelayoutToScroller(wxWindow* window)
{
	window->InvalidateBestSize();

	for (; window != nullptr && !window->IsTopLevel(); window = window->GetParent()) {
		if (wxScrolledWindow* scroller = dynamic_cast<wxScrolledWindow*>(window)) {
			scroller->FitInside();
			scroller->Layout();
			return;
		}

		window->Layout();
	}
}

PropertySection::PropertySection(wxWindow* parent, const wxString& title, bool expanded)
	: wxPanel(parent, wxID_ANY), mTitle(title), mbExpanded(expanded)
{
	mpHeader = new wxWindow(this, wxID_ANY);
	mpHeader->SetBackgroundStyle(wxBG_STYLE_PAINT);
	mpHeader->SetFont(GetFont().Bold());
	mpHeader->SetMinSize(wxSize(-1, mpHeader->GetCharHeight() + 10));

	mpHeader->Bind(wxEVT_PAINT, &PropertySection::OnHeaderPaint, this);
	mpHeader->Bind(wxEVT_LEFT_DOWN, &PropertySection::OnHeaderClick, this);
	mpHeader->Bind(wxEVT_LEFT_DCLICK, &PropertySection::OnHeaderClick, this);

	mpBody = new wxPanel(this, wxID_ANY);

	mpGrid = new wxGridBagSizer(4, 8);
	mpGrid->SetCols(2);
	mpGrid->SetEmptyCellSize(wxSize(0, 0));
	mpGrid->AddGrowableCol(1);

	wxBoxSizer* body_sizer = new wxBoxSizer(wxVERTICAL);
	body_sizer->Add(mpGrid, wxSizerFlags(1).Expand().Border(wxALL, 6));
	mpBody->SetSizer(body_sizer);
	mpBody->Show(mbExpanded);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(mpHeader, wxSizerFlags().Expand());
	sizer->Add(mpBody, wxSizerFlags().Expand());
	SetSizer(sizer);
}

void PropertySection::AddLabel(wxStaticText* label)
{
	label->SetMinSize(wxSize(std::max(label->GetBestSize().x, scMinLabelWidth), -1));
	mpGrid->Add(label, wxGBPosition(mRow, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
}

void PropertySection::AddRow(const wxString& label, wxWindow* control)
{
	AddLabel(new wxStaticText(mpBody, wxID_ANY, label));
	mpGrid->Add(control, wxGBPosition(mRow++, 1), wxDefaultSpan, wxEXPAND);
}

void PropertySection::AddRow(const wxString& label, wxSizer* controls)
{
	AddLabel(new wxStaticText(mpBody, wxID_ANY, label));
	mpGrid->Add(controls, wxGBPosition(mRow++, 1), wxDefaultSpan, wxEXPAND);
}

void PropertySection::AddRow(FieldBaseImpl& field)
{
	AddLabel(field.GetLabel());
	mpGrid->Add(field.GetControlSizer(), wxGBPosition(mRow++, 1), wxDefaultSpan, wxEXPAND);
}

void PropertySection::AddWide(wxWindow* control)
{
	mpGrid->Add(control, wxGBPosition(mRow++, 0), wxGBSpan(1, 2), wxEXPAND);
}

void PropertySection::AddWide(wxSizer* controls)
{
	mpGrid->Add(controls, wxGBPosition(mRow++, 0), wxGBSpan(1, 2), wxEXPAND);
}

void PropertySection::SetExpanded(bool expanded)
{
	if (expanded == mbExpanded) {
		return;
	}

	mbExpanded = expanded;

	mpBody->Show(mbExpanded);
	mpHeader->Refresh();

	RelayoutToScroller(this);
}

void PropertySection::OnHeaderClick(wxMouseEvent&) { SetExpanded(!mbExpanded); }

void PropertySection::OnHeaderPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(mpHeader);

	const EditorTheme& theme = GetEditorTheme();
	const wxColour background = ToWxColour(theme.Frame.Background);
	const bool is_dark = background.GetLuminance() < 0.25;

	const wxColour fill = background.ChangeLightness(is_dark ? 112 : 84);
	const wxColour edge = background.ChangeLightness(is_dark ? 70 : 72);
	const wxColour text = ToWxColour(theme.Frame.Text);

	const wxSize size = mpHeader->GetClientSize();

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(fill));
	dc.DrawRectangle(0, 0, size.x, size.y);

	dc.SetBrush(wxBrush(edge));
	dc.DrawRectangle(0, 0, size.x, 1);
	dc.DrawRectangle(0, size.y - 1, size.x, 1);

	const int32 center_x = 11;
	const int32 center_y = size.y / 2;
	const int32 arrow = 4;

	wxPoint points[3];

	if (mbExpanded) {
		points[0] = wxPoint(center_x - arrow, center_y - arrow / 2);
		points[1] = wxPoint(center_x + arrow, center_y - arrow / 2);
		points[2] = wxPoint(center_x, center_y + arrow / 2 + 1);
	}
	else {
		points[0] = wxPoint(center_x - arrow / 2, center_y - arrow);
		points[1] = wxPoint(center_x - arrow / 2, center_y + arrow);
		points[2] = wxPoint(center_x + arrow / 2 + 1, center_y);
	}

	dc.SetBrush(wxBrush(text));
	dc.DrawPolygon(3, points);

	dc.SetFont(mpHeader->GetFont());
	dc.SetTextForeground(text);
	dc.DrawText(mTitle, 22, (size.y - dc.GetCharHeight()) / 2);
}


} // namespace fx::editor

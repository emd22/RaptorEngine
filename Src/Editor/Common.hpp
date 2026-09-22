#pragma once

#include <wx/sizer.h>

#include <Core/Types.hpp>
#include <functional>

class wxTextCtrl;
class wxWindow;
class wxString;
class wxCommandEvent;
class wxFocusEvent;

namespace fx {
class Vec3f;
}

namespace fx::editor {


class EditableVector3Field
{
public:
	using OnChangeFunc = std::function<void(const Vec3f&)>;

	EditableVector3Field(wxWindow* parent, const wxString& label);

	wxSizer* GetSizer() { return mpSizer; }

	void SetValue(const Vec3f& value);
	Vec3f GetValue() const;

	void Enable(bool enable = true);

	/// True while the user has one of the three fields focused, mid-edit
	bool HasFocus() const;

	void SetOnChange(OnChangeFunc on_change) { mOnChange = std::move(on_change); }

	~EditableVector3Field() = default;

private:
	wxTextCtrl* AddAxisField(wxWindow* parent, const wxString& axis_label);

	void OnCommit(wxCommandEvent& event);
	void OnKillFocus(wxFocusEvent& event);

private:
	static constexpr int32 scFieldWidth = 56;
	static constexpr uint32 scPrecision = 3;

	wxBoxSizer* mpSizer = nullptr;
	wxTextCtrl* mpFields[3] = {};

	bool mbUpdatingFields = false;

	OnChangeFunc mOnChange = nullptr;
};

} // namespace fx::editor

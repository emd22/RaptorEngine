#pragma once

#include <wx/sizer.h>

#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <functional>

class wxTextCtrl;
class wxWindow;
class wxString;
class wxCommandEvent;
class wxFocusEvent;


namespace fx::editor {


class FieldBaseImpl
{
public:
	FieldBaseImpl(wxWindow* parent, const wxString& label);

	wxSizer* GetSizer() { return mpSizer; }
	const wxSizer* GetSizer() const { return mpSizer; }

protected:
	wxBoxSizer* mpSizer = nullptr;
	bool mbUpdatingFields = false;
};

template <typename T>
class FieldBase : public FieldBaseImpl
{
public:
	static constexpr int32 scFieldWidth = 56;
	static constexpr uint32 scPrecision = 3;

	using TRefType = TGetConstRef<T>;
	using TType = T;

	using OnChangeFunc = std::function<void(TRefType)>;

public:
	FieldBase(wxWindow* parent, const wxString& label) : FieldBaseImpl(parent, label) {};

	virtual TType GetValue() const = 0;
	virtual void SetValue(TRefType value) = 0;

	virtual bool HasFocus() const = 0;
	virtual void SetEnabled(bool enable) = 0;

	void SetOnChange(OnChangeFunc on_change) { mpOnChange = std::move(on_change); }

	virtual ~FieldBase() = default;

public:
	OnChangeFunc mpOnChange = nullptr;
};

/////////////////////////////////////
// Vector fields
/////////////////////////////////////


class Vector3Field final : public FieldBase<Vec3f>
{
public:
	Vector3Field(wxWindow* parent, const wxString& label);

	TType GetValue() const override;
	void SetValue(TRefType value) override;

	bool HasFocus() const override;
	void SetEnabled(bool enable) override;

	void OnCommit(wxCommandEvent& event);
	void OnFocusLost(wxFocusEvent& event);

private:
	wxTextCtrl* MakeAxisField(wxWindow* parent, const wxString& axis_label);

private:
	wxTextCtrl* mpFields[3] = {};
};

//
// class Vector3Field_OLD
// {
// public:
// 	using OnChangeFunc = std::function<void(const Vec3f&)>;
//
// 	static constexpr int32 scFieldWidth = 56;
// 	static constexpr uint32 scPrecision = 3;
//
// public:
// 	Vector3Field_OLD(wxWindow* parent, const wxString& label);
//
// 	wxSizer* GetSizer() { return mpSizer; }
//
// 	void SetValue(const Vec3f& value);
// 	Vec3f GetValue() const;
//
// 	void Enable(bool enable = true);
//
// 	/// True while the user has one of the three fields focused, mid-edit
// 	bool HasFocus() const;
//
// 	void SetOnChange(OnChangeFunc on_change) { mpOnChange = std::move(on_change); }
//
// 	~Vector3Field_OLD() = default;
//
// private:
// 	wxTextCtrl* AddAxisField(wxWindow* parent, const wxString& axis_label);
//
// 	void OnCommit(wxCommandEvent& event);
// 	void OnKillFocus(wxFocusEvent& event);
//
// private:
// 	wxBoxSizer* mpSizer = nullptr;
// 	wxTextCtrl* mpFields[3] = {};
//
// 	bool mbUpdatingFields = false;
//
// 	OnChangeFunc mpOnChange = nullptr;
// };

/////////////////////////////////////
// Numeric fields
/////////////////////////////////////

} // namespace fx::editor

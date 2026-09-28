#pragma once


#include <wx/sizer.h>

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
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
	static constexpr uint32 scFloatPrecision = 3;

	using TRefType = TGetConstRef<T>;
	using TType = T;

	using OnChangeFunc = std::function<void(TRefType)>;

public:
	FieldBase(wxWindow* parent, const wxString& label) : FieldBaseImpl(parent, label) {};

	virtual TType GetValue() const = 0;
	virtual void SetValue(TRefType value) = 0;

	virtual bool HasFocus() const = 0;
	virtual void SetEnabled(bool enable) = 0;

	void OnCommit(wxCommandEvent& event)
	{
		if (!mbUpdatingFields && mpOnChange) {
			mpOnChange(GetValue());
		}

		event.Skip();
	}

	void OnFocusLost(wxFocusEvent& event)
	{
		if (!mbUpdatingFields && mpOnChange) {
			mpOnChange(GetValue());
		}

		event.Skip();
	}

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
	Vector3Field(wxWindow* parent, const wxString& label, const Vec2f& limits);

	TType GetValue() const override;
	void SetValue(TRefType value) override;

	bool HasFocus() const override;
	void SetEnabled(bool enable) override;

	~Vector3Field() override;

private:
	wxTextCtrl* MakeAxisField(wxWindow* parent, const wxString& axis_label, const Vec2f& limits);

private:
	wxTextCtrl* mpFields[3] = {};
};

/////////////////////////////////////
// Numeric fields
/////////////////////////////////////

class FloatField final : public FieldBase<float32>
{
public:
	FloatField(wxWindow* parent, const wxString& label, const Vec2f& limits);

	TType GetValue() const override;
	void SetValue(TRefType value) override;

	bool HasFocus() const override;
	void SetEnabled(bool enable) override;

	~FloatField() override;

private:
	wxTextCtrl* MakeAxisField(wxWindow* parent, const wxString& axis_label);

private:
	wxTextCtrl* mpField = nullptr;
};


} // namespace fx::editor

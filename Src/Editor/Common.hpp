#pragma once


#include <wx/panel.h>
#include <wx/sizer.h>

#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <Math/Vec3.hpp>
#include <functional>


class wxGridBagSizer;
class wxMouseEvent;
class wxPaintEvent;
class wxStaticText;
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

	wxSizer* GetSizer();

	wxStaticText* GetLabel() { return mpLabel; }
	wxSizer* GetControlSizer() { return mpControls; }

protected:
	wxStaticText* mpLabel = nullptr;
	wxBoxSizer* mpControls = nullptr;
	wxBoxSizer* mpSizer = nullptr;
	bool mbUpdatingFields = false;
};

template <typename T>
class FieldBase : public FieldBaseImpl
{
public:
	static constexpr int32 scFieldWidth = 56;
	static constexpr int32 scVectorFieldWidth = 40;
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
		Commit();

		event.Skip();
	}

	void OnFocusLost(wxFocusEvent& event)
	{
		Commit();

		event.Skip();
	}

	void SetOnChange(OnChangeFunc on_change) { mpOnChange = std::move(on_change); }

	virtual ~FieldBase() = default;

protected:
	void RememberValue() { mLastValue = GetValue(); }

private:
	void Commit()
	{
		if (mbUpdatingFields || !mpOnChange) {
			return;
		}

		const TType value = GetValue();

		if (value == mLastValue) {
			return;
		}

		mLastValue = value;
		mpOnChange(value);
	}

public:
	OnChangeFunc mpOnChange = nullptr;

private:
	TType mLastValue {};
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

/////////////////////////////////////
// Property sections
/////////////////////////////////////

void RelayoutToScroller(wxWindow* window);

class PropertySection : public wxPanel
{
public:
	static constexpr int32 scMinLabelWidth = 96;

public:
	PropertySection(wxWindow* parent, const wxString& title, bool expanded = true);

	wxWindow* GetBody() { return mpBody; }

	void AddRow(const wxString& label, wxWindow* control);
	void AddRow(const wxString& label, wxSizer* controls);
	void AddRow(FieldBaseImpl& field);

	void AddWide(wxWindow* control);
	void AddWide(wxSizer* controls);

	bool IsExpanded() const { return mbExpanded; }
	void SetExpanded(bool expanded);

private:
	void AddLabel(wxStaticText* label);
	void OnHeaderPaint(wxPaintEvent& event);
	void OnHeaderClick(wxMouseEvent& event);

private:
	wxString mTitle;
	wxWindow* mpHeader = nullptr;
	wxPanel* mpBody = nullptr;
	wxGridBagSizer* mpGrid = nullptr;
	int32 mRow = 0;
	bool mbExpanded = true;
};

} // namespace fx::editor

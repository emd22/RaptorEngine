#pragma once

#include <wx/bitmap.h>
#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/tglbtn.h>

#include <Core/Types.hpp>

namespace fx::editor {

class OutlineToggleButton : public wxControl
{
public:
	static constexpr int32 scPadding = 6;
	static constexpr int32 scOutlineWidth = 2;

	OutlineToggleButton(wxWindow* parent, const wxBitmap& icon, const wxString& label)
		: wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), mIcon(icon), mLabel(label)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetCanFocus(false);

		Bind(wxEVT_PAINT, &OutlineToggleButton::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &OutlineToggleButton::OnLeftDown, this);
		Bind(wxEVT_LEFT_UP, &OutlineToggleButton::OnLeftUp, this);
		Bind(wxEVT_ENTER_WINDOW,
			 [this](wxMouseEvent&)
			 {
				 mbHovered = true;
				 Refresh();
			 });
		Bind(wxEVT_LEAVE_WINDOW,
			 [this](wxMouseEvent&)
			 {
				 mbHovered = false;
				 mbPressed = false;
				 Refresh();
			 });
	}

	bool GetValue() const { return mbValue; }

	void SetValue(bool value)
	{
		if (mbValue != value) {
			mbValue = value;
			Refresh();
		}
	}

	bool Enable(bool enable = true) override
	{
		const bool changed = wxControl::Enable(enable);
		Refresh();
		return changed;
	}

	bool AcceptsFocus() const override { return false; }

protected:
	wxSize DoGetBestSize() const override
	{
		wxSize content;

		if (mIcon.IsOk()) {
			content = mIcon.GetSize();
		}
		else {
			content = GetTextExtent(mLabel);
		}

		return wxSize(content.x + (scPadding * 2), content.y + (scPadding * 2));
	}

private:
	void OnPaint(wxPaintEvent&)
	{
		wxAutoBufferedPaintDC buffered(this);
		wxGCDC dc(buffered);

		const wxSize size = GetClientSize();
		const bool enabled = IsEnabled();

		dc.SetBackground(wxBrush(GetBackgroundColour()));
		dc.Clear();

		wxColour fill = GetBackgroundColour().ChangeLightness(mbPressed ? 85 : (mbHovered && enabled) ? 125 : 112);
		wxColour outline = GetBackgroundColour().ChangeLightness(75);

		const wxColour selection_colour = wxColour(100, 200, 120);

		dc.SetBrush(wxBrush(fill));
		dc.SetPen(wxPen(outline, 1));
		dc.DrawRoundedRectangle(wxRect(0, 0, size.x, size.y).Deflate(1), 5.0);

		if (mbValue) {
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(selection_colour, scOutlineWidth));
			dc.DrawRoundedRectangle(wxRect(0, 0, size.x, size.y).Deflate(1), 5.0);
		}

		if (mIcon.IsOk()) {
			const wxBitmap icon = enabled ? mIcon : mIcon.ConvertToDisabled(50);
			dc.DrawBitmap(icon, (size.x - icon.GetWidth()) / 2, (size.y - icon.GetHeight()) / 2, true);
		}
		else {
			const wxSize text = dc.GetTextExtent(mLabel);

			dc.SetTextForeground(enabled ? *wxWHITE : wxColour(150, 150, 150));
			dc.DrawText(mLabel, (size.x - text.x) / 2, (size.y - text.y) / 2);
		}
	}

	void OnLeftDown(wxMouseEvent&)
	{
		if (!IsEnabled()) {
			return;
		}

		mbPressed = true;
		Refresh();
	}

	void OnLeftUp(wxMouseEvent&)
	{
		const bool clicked = mbPressed && mbHovered && IsEnabled();

		mbPressed = false;
		Refresh();

		if (!clicked) {
			return;
		}

		mbValue = !mbValue;

		wxCommandEvent event(wxEVT_TOGGLEBUTTON, GetId());
		event.SetEventObject(this);
		event.SetInt(mbValue ? 1 : 0);
		ProcessWindowEvent(event);
	}

private:
	wxBitmap mIcon;
	wxString mLabel;

	bool mbValue = false;
	bool mbHovered = false;
	bool mbPressed = false;
};

} // namespace fx::editor

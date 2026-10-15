#pragma once

#include "EditorTheme.hpp"

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
	static constexpr int32 scPadding = 5;
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

	void SetButtonSize(const wxSize& size)
	{
		mButtonSize = size;
		InvalidateBestSize();
		SetMinSize(GetBestSize());
		Refresh();
	}

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

		wxSize best(content.x + (scPadding * 2), content.y + (scPadding * 2));

		if (mButtonSize.x != wxDefaultCoord) {
			best.x = mButtonSize.x;
		}

		if (mButtonSize.y != wxDefaultCoord) {
			best.y = mButtonSize.y;
		}

		return best;
	}

private:
	void OnPaint(wxPaintEvent&)
	{
		wxAutoBufferedPaintDC buffered(this);
		wxGCDC dc(buffered);

		const wxSize size = GetClientSize();
		const bool enabled = IsEnabled();

		const EditorTheme& theme = GetEditorTheme();
		const wxColour background = ToWxColour(theme.Frame.Background);

		dc.SetBackground(wxBrush(background));
		dc.Clear();

		int32 lightness = theme.Button.Lightness;

		if (mbPressed) {
			lightness = theme.Button.PressedLightness;
		}
		else if (mbHovered && enabled) {
			lightness = theme.Button.HoverLightness;
		}

		const wxColour fill = background.ChangeLightness(lightness);
		// const wxColour outline = GetBackgroundColour().ChangeLightness(125);
		const wxColour outline = background;

		const wxColour selection_colour = ToWxColour(theme.Button.Selected);

		dc.SetBrush(wxBrush(fill));
		dc.SetPen(wxPen(outline, scOutlineWidth));
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

			dc.SetTextForeground(ToWxColour(enabled ? theme.Button.Text : theme.Button.DisabledText));
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
	wxSize mButtonSize = wxDefaultSize;

	bool mbValue = false;
	bool mbHovered = false;
	bool mbPressed = false;
};

} // namespace fx::editor

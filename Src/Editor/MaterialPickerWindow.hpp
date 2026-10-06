#pragma once

#include <wx/bitmap.h>
#include <wx/frame.h>

#include <Core/Types.hpp>
#include <unordered_map>
#include <vector>

class wxButton;
class wxListBox;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

namespace fx::editor {

/**
 * @brief Window for searching the blockout material library and previewing each material's albedo.
 */
class MaterialPickerWindow : public wxFrame
{
public:
	explicit MaterialPickerWindow(wxWindow* parent);

	/// Rebuilds the list from the blockout's material library and highlights the selected object's material
	void RefreshList();

private:
	void RebuildRows();
	void ShowPreview(int32 material_slot);
	void ApplySelected();
	int32 GetSelectedSlot() const;

	const wxBitmap& GetAlbedoBitmap(int32 material_slot);

	void OnShow(wxShowEvent& event);
	void OnClose(wxCloseEvent& event);

private:
	wxTextCtrl* mpSearch = nullptr;
	wxListBox* mpList = nullptr;
	wxStaticBitmap* mpPreview = nullptr;
	wxStaticText* mpNameLabel = nullptr;
	wxStaticText* mpStatusLabel = nullptr;
	wxButton* mpApplyButton = nullptr;

	/// Material library slots currently shown, in row order
	std::vector<int32> mShownSlots;

	std::unordered_map<int32, wxBitmap> mBitmaps;
};

} // namespace fx::editor

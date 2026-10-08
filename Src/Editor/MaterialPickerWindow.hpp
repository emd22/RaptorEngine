#pragma once

#include <wx/bitmap.h>
#include <wx/frame.h>

#include <Core/HashMap.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialLibrary.hpp>
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
	void ShowPreview(MaterialLibraryID material_slot);
	void ApplySelected();
	MaterialLibraryID GetSelectedSlot() const;

	const wxBitmap& GetAlbedoBitmap(MaterialLibraryID material_slot);

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
	std::vector<MaterialLibraryID> mShownSlots;

	HashMap<MaterialLibraryID, wxBitmap> mBitmaps;
};

} // namespace fx::editor

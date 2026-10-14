#pragma once

#include "EditorPanelState.hpp"

#include <wx/bitmap.h>
#include <wx/panel.h>

#include <Core/DynArray.hpp>
#include <Core/HashMap.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialLibrary.hpp>

class wxButton;
class wxStaticText;
class wxTextCtrl;

namespace fx::editor {

class EditorFrame;
class MaterialThumbnailGrid;
class PaginatedView;

/**
 * @brief Side panel section for searching the blockout material library, shown as pages of albedo thumbnails.
 */
class MaterialPickerPanel : public wxPanel
{
public:
	MaterialPickerPanel(wxWindow* parent, EditorFrame* frame);

	void RefreshList();

	void OnStateChanged();

	void FocusSearch();

private:
	friend class MaterialThumbnailGrid;

	void RebuildRows();
	void ShowSelection();
	void ApplySelected();
	MaterialLibraryID GetSelectedSlot() const;

	void OnShow(wxShowEvent& event);
	void OnIdle(wxIdleEvent& event);

private:
	wxTextCtrl* mpSearch = nullptr;
	PaginatedView* mpPages = nullptr;
	MaterialThumbnailGrid* mpGrid = nullptr;
	wxStaticText* mpNameLabel = nullptr;
	wxButton* mpApplyButton = nullptr;

	EditorFrame* mpFrame = nullptr;

	MaterialLibraryState mShownLibrary;

	DynArray<MaterialLibraryID> mShownSlots;

	/// Thumbnails are loaded one per idle event, in library order
	HashMap<MaterialLibraryID, wxBitmap> mBitmaps;
	uint32 mNextThumbnailSlot = 0;
};

} // namespace fx::editor

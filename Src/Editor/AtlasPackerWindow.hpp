#pragma once

#include "AtlasPacker.hpp"

#include <wx/frame.h>

#include <Core/DynArray.hpp>
#include <Core/String.hpp>

class wxButton;
class wxListCtrl;
class wxListEvent;
class wxSpinCtrl;
class wxStaticText;

namespace fx::editor {

class AtlasPreviewPanel;

class AtlasPackerWindow : public wxFrame
{
public:
	explicit AtlasPackerWindow(wxWindow* parent);

private:
	void AddImages();
	void RemoveSelected();
	void MoveSelected(int32 direction);
	void Repack();
	void RebuildRows();
	void Export();

	int32 GetSelectedRow() const;

	void OnClose(wxCloseEvent& event);

private:
	wxSpinCtrl* mpTileSize = nullptr;
	wxSpinCtrl* mpColumns = nullptr;
	wxListCtrl* mpList = nullptr;
	wxButton* mpRemoveButton = nullptr;
	wxButton* mpUpButton = nullptr;
	wxButton* mpDownButton = nullptr;
	wxButton* mpExportButton = nullptr;
	wxStaticText* mpStatus = nullptr;
	AtlasPreviewPanel* mpPreview = nullptr;

	DynArray<AtlasImage> mImages;
	AtlasLayout mLayout;

	String mLastDirectory;
};

} // namespace fx::editor

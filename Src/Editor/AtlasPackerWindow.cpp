#include "AtlasPackerWindow.hpp"

#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <Core/Log.hpp>
#include <algorithm>

namespace fx::editor {

enum eColumn : int32
{
	Column_Name = 0,
	Column_Source,
	Column_Tiles,
	Column_Position,
};

static constexpr int32 scDefaultTileSize = 64;
static constexpr int32 scDefaultColumns = 16;
static constexpr int32 scMaxTileSize = 4096;
static constexpr int32 scMaxColumns = 1024;
static constexpr int32 scMaxAtlasDimension = 16384;

class AtlasPreviewPanel : public wxPanel
{
public:
	explicit AtlasPreviewPanel(wxWindow* parent)
		: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(480, 480), wxFULL_REPAINT_ON_RESIZE)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &AtlasPreviewPanel::OnPaint, this);
	}

	void SetAtlas(const AtlasLayout* layout, const SizedArray<uint8>& pixels, int32 selected_group)
	{
		mpLayout = layout;
		mSelectedGroup = selected_group;

		if (pixels.pData == nullptr || layout->GetWidth() <= 0 || layout->GetHeight() <= 0) {
			mBitmap = wxBitmap();
		}
		else {
			wxImage image(layout->GetWidth(), layout->GetHeight(), false);
			image.InitAlpha();

			uint8* rgb = image.GetData();
			uint8* alpha = image.GetAlpha();

			const size_t count = static_cast<size_t>(layout->GetWidth()) * layout->GetHeight();

			for (size_t i = 0; i < count; i++) {
				rgb[i * 3 + 0] = pixels[i * 4 + 0];
				rgb[i * 3 + 1] = pixels[i * 4 + 1];
				rgb[i * 3 + 2] = pixels[i * 4 + 2];
				alpha[i] = pixels[i * 4 + 3];
			}

			mBitmap = wxBitmap(image);
		}

		Refresh();
	}

	void SetSelectedGroup(int32 selected_group)
	{
		mSelectedGroup = selected_group;
		Refresh();
	}

private:
	void OnPaint(wxPaintEvent&)
	{
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(wxColour(40, 40, 40)));
		dc.Clear();

		if (!mBitmap.IsOk() || mpLayout == nullptr) {
			return;
		}

		const AtlasLayout& layout = *mpLayout;
		const wxSize client = GetClientSize();
		const double atlas_width = layout.GetWidth();
		const double atlas_height = layout.GetHeight();
		const double scale = std::min((client.x - 8) / atlas_width, (client.y - 8) / atlas_height);

		if (scale <= 0.0) {
			return;
		}

		const int draw_width = std::max(1, static_cast<int>(atlas_width * scale));
		const int draw_height = std::max(1, static_cast<int>(atlas_height * scale));
		const int origin_x = (client.x - draw_width) / 2;
		const int origin_y = (client.y - draw_height) / 2;

		constexpr int checker = 8;
		for (int y = 0; y < draw_height; y += checker) {
			for (int x = 0; x < draw_width; x += checker) {
				const bool light = (((x / checker) + (y / checker)) & 1) == 0;
				dc.SetBrush(wxBrush(light ? wxColour(90, 90, 90) : wxColour(70, 70, 70)));
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.DrawRectangle(origin_x + x, origin_y + y, std::min(checker, draw_width - x),
								 std::min(checker, draw_height - y));
			}
		}

		wxImage scaled = mBitmap.ConvertToImage();
		scaled.Rescale(draw_width, draw_height, wxIMAGE_QUALITY_NEAREST);
		dc.DrawBitmap(wxBitmap(scaled), origin_x, origin_y, true);

		const double tile = layout.TileSize * scale;

		dc.SetPen(wxPen(wxColour(255, 255, 255, 60), 1));
		for (int32 col = 0; col <= layout.Columns; col++) {
			const int x = origin_x + static_cast<int>(col * tile);
			dc.DrawLine(x, origin_y, x, origin_y + draw_height);
		}
		for (int32 row = 0; row <= layout.Rows; row++) {
			const int y = origin_y + static_cast<int>(row * tile);
			dc.DrawLine(origin_x, y, origin_x + draw_width, y);
		}

		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetTextForeground(wxColour(255, 220, 80));

		for (uint32 i = 0; i < layout.Groups.Size; i++) {
			const AtlasGroup& group = layout.Groups[i];
			const bool selected = static_cast<int32>(i) == mSelectedGroup;

			dc.SetPen(wxPen(selected ? wxColour(255, 200, 40) : wxColour(60, 200, 255), selected ? 3 : 2));

			const int x = origin_x + static_cast<int>(group.TileX * tile);
			const int y = origin_y + static_cast<int>(group.TileY * tile);
			const int w = static_cast<int>(group.TilesWide * tile);
			const int h = static_cast<int>(group.TilesHigh * tile);

			dc.DrawRectangle(x, y, w, h);
			dc.SetClippingRegion(x, y, w, h);
			dc.DrawText(wxString::FromUTF8(group.Name.CStr()), x + 3, y + 2);
			dc.DestroyClippingRegion();
		}
	}

private:
	wxBitmap mBitmap;
	const AtlasLayout* mpLayout = nullptr;
	int32 mSelectedGroup = -1;
};

AtlasPackerWindow::AtlasPackerWindow(wxWindow* parent)
	: wxFrame(parent, wxID_ANY, "Atlas Packer", wxDefaultPosition, wxSize(980, 620))
{
	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* settings = new wxBoxSizer(wxHORIZONTAL);
	settings->Add(new wxStaticText(root, wxID_ANY, "Tile size (px)"), wxSizerFlags().CenterVertical().Border(wxRIGHT, 6));
	mpTileSize = new wxSpinCtrl(root, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, scMaxTileSize,
								scDefaultTileSize);
	settings->Add(mpTileSize, wxSizerFlags().CenterVertical().Border(wxRIGHT, 14));
	settings->Add(new wxStaticText(root, wxID_ANY, "Atlas width (tiles)"),
				  wxSizerFlags().CenterVertical().Border(wxRIGHT, 6));
	mpColumns = new wxSpinCtrl(root, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, scMaxColumns,
							   scDefaultColumns);
	settings->Add(mpColumns, wxSizerFlags().CenterVertical());
	left->Add(settings, wxSizerFlags().Border(wxALL, 6));

	mpList = new wxListCtrl(root, wxID_ANY, wxDefaultPosition, wxSize(420, -1),
							wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_EDIT_LABELS);
	mpList->InsertColumn(Column_Name, "Group", wxLIST_FORMAT_LEFT, 140);
	mpList->InsertColumn(Column_Source, "Source", wxLIST_FORMAT_LEFT, 80);
	mpList->InsertColumn(Column_Tiles, "Tiles", wxLIST_FORMAT_LEFT, 60);
	mpList->InsertColumn(Column_Position, "At", wxLIST_FORMAT_LEFT, 70);
	left->Add(mpList, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, 6));

	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	wxButton* add_button = new wxButton(root, wxID_ANY, "Add Images...");
	mpRemoveButton = new wxButton(root, wxID_ANY, "Remove");
	mpUpButton = new wxButton(root, wxID_ANY, "Up");
	mpDownButton = new wxButton(root, wxID_ANY, "Down");
	buttons->Add(add_button, wxSizerFlags().Border(wxRIGHT, 4));
	buttons->Add(mpRemoveButton, wxSizerFlags().Border(wxRIGHT, 4));
	buttons->Add(mpUpButton, wxSizerFlags().Border(wxRIGHT, 4));
	buttons->Add(mpDownButton, wxSizerFlags());
	left->Add(buttons, wxSizerFlags().Border(wxALL, 6));

	mpExportButton = new wxButton(root, wxID_ANY, "Export Atlas...");
	left->Add(mpExportButton, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, 6));

	mpStatus = new wxStaticText(root, wxID_ANY, "");
	left->Add(mpStatus, wxSizerFlags().Expand().Border(wxALL, 6));

	sizer->Add(left, wxSizerFlags().Expand());

	mpPreview = new AtlasPreviewPanel(root);
	sizer->Add(mpPreview, wxSizerFlags(1).Expand().Border(wxALL, 6));

	root->SetSizer(sizer);

	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizer(frame_sizer);

	add_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AddImages(); });
	mpRemoveButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RemoveSelected(); });
	mpUpButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveSelected(-1); });
	mpDownButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { MoveSelected(1); });
	mpExportButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Export(); });
	mpTileSize->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { Repack(); });
	mpTileSize->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Repack(); });
	mpColumns->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { Repack(); });
	mpColumns->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Repack(); });
	mpList->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { mpPreview->SetSelectedGroup(GetSelectedRow()); });
	mpList->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { mpPreview->SetSelectedGroup(GetSelectedRow()); });
	mpList->Bind(wxEVT_LIST_END_LABEL_EDIT,
				 [this](wxListEvent& event)
				 {
					 event.Veto();

					 const long row = event.GetIndex();

					 if (event.IsEditCancelled() || row < 0 || static_cast<uint32>(row) >= mImages.Size) {
						 return;
					 }

					 mImages[row].Name = MakeGroupName(String(event.GetLabel().utf8_string()));
					 CallAfter([this] { Repack(); });
				 });
	Bind(wxEVT_CLOSE_WINDOW, &AtlasPackerWindow::OnClose, this);

	Repack();
}

int32 AtlasPackerWindow::GetSelectedRow() const
{
	return static_cast<int32>(mpList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED));
}

void AtlasPackerWindow::AddImages()
{
	wxFileDialog dialog(this, "Add images", wxString::FromUTF8(mLastDirectory.CStr()), "",
						"Images (*.png;*.jpg;*.jpeg;*.tga;*.bmp)|*.png;*.jpg;*.jpeg;*.tga;*.bmp",
						wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	wxArrayString paths;
	dialog.GetPaths(paths);

	String failed;

	for (const wxString& wx_path : paths) {
		const String path(wx_path.utf8_string());

		AtlasImage image;

		if (LoadAtlasImage(path, image)) {
			mImages.Emplace(std::move(image));
		}
		else {
			failed += String("\n") + GetPathFileName(path);
		}

		mLastDirectory = GetPathDirectory(path);
	}

	Repack();

	if (!failed.IsEmpty()) {
		wxMessageBox(wxString::FromUTF8((String("Could not load:") + failed).CStr()), "Atlas Packer", wxOK | wxICON_WARNING, this);
	}
}

void AtlasPackerWindow::RemoveSelected()
{
	const int32 row = GetSelectedRow();

	if (row < 0 || static_cast<uint32>(row) >= mImages.Size) {
		return;
	}

	for (uint32 i = row; i + 1 < mImages.Size; i++) {
		mImages[i] = std::move(mImages[i + 1]);
	}

	mImages.RemoveLast();
	Repack();
}

void AtlasPackerWindow::MoveSelected(int32 direction)
{
	const int32 row = GetSelectedRow();
	const int32 target = row + direction;

	if (row < 0 || target < 0 || static_cast<uint32>(target) >= mImages.Size) {
		return;
	}

	std::swap(mImages[row], mImages[target]);
	Repack();

	mpList->SetItemState(target, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

void AtlasPackerWindow::Repack()
{
	const int32 selected = GetSelectedRow();

	mLayout = PackAtlas(mImages, mpTileSize->GetValue(), mpColumns->GetValue());

	for (uint32 i = 0; i < mImages.Size; i++) {
		mImages[i].Name = mLayout.Groups[i].Name;
	}

	RebuildRows();

	const bool too_large = mLayout.GetWidth() > scMaxAtlasDimension || mLayout.GetHeight() > scMaxAtlasDimension;

	if (too_large) {
		mpPreview->SetAtlas(&mLayout, SizedArray<uint8>(), -1);
	}
	else {
		mpPreview->SetAtlas(&mLayout, ComposeAtlas(mImages, mLayout), selected);
	}

	if (selected >= 0 && static_cast<uint32>(selected) < mImages.Size) {
		mpList->SetItemState(selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	}

	const bool has_images = mImages.Size > 0;
	mpRemoveButton->Enable(has_images);
	mpUpButton->Enable(has_images);
	mpDownButton->Enable(has_images);
	mpExportButton->Enable(has_images && !too_large);

	if (too_large) {
		mpStatus->SetLabel(wxString::Format("Atlas would be %d x %d px, over the %d px limit", mLayout.GetWidth(),
											mLayout.GetHeight(), scMaxAtlasDimension));
	}
	else {
		mpStatus->SetLabel(wxString::Format("%u groups, %d x %d tiles, %d x %d px", mImages.Size, mLayout.Columns,
											mLayout.Rows, mLayout.GetWidth(), mLayout.GetHeight()));
	}
}

void AtlasPackerWindow::RebuildRows()
{
	mpList->Freeze();
	mpList->DeleteAllItems();

	for (uint32 i = 0; i < mImages.Size; i++) {
		const AtlasGroup& group = mLayout.Groups[i];

		const long row = mpList->InsertItem(static_cast<long>(i), wxString::FromUTF8(group.Name.CStr()));
		mpList->SetItem(row, Column_Source, wxString::Format("%d x %d", group.SourceWidth, group.SourceHeight));
		mpList->SetItem(row, Column_Tiles, wxString::Format("%d x %d", group.TilesWide, group.TilesHigh));
		mpList->SetItem(row, Column_Position, wxString::Format("%d, %d", group.TileX, group.TileY));
	}

	mpList->Thaw();
}

void AtlasPackerWindow::Export()
{
	if (mImages.Size == 0) {
		return;
	}

	wxFileDialog dialog(this, "Export atlas", wxString::FromUTF8(mLastDirectory.CStr()), "atlas.png",
						"PNG image (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	String png_path(dialog.GetPath().utf8_string());

	if (png_path.GetLength() < 4 || !(png_path.SubStr(png_path.GetLength() - 4, 4) == String(".png"))) {
		png_path = ReplacePathExtension(png_path, ".png");
	}

	const String config_path = ReplacePathExtension(png_path, ".conf");

	mLastDirectory = GetPathDirectory(png_path);

	if (ExportAtlas(mImages, mLayout, png_path, config_path)) {
		LogInfo("Exported atlas '{}' and '{}'", png_path, config_path);
		mpStatus->SetLabel(wxString::FromUTF8(
			(String("Exported ") + GetPathFileName(png_path) + " and " + GetPathFileName(config_path)).CStr()));
	}
	else {
		wxMessageBox("The atlas could not be written.", "Atlas Packer", wxOK | wxICON_ERROR, this);
	}
}

void AtlasPackerWindow::OnClose(wxCloseEvent& event)
{
	if (event.CanVeto()) {
		Hide();
		event.Veto();
		return;
	}

	event.Skip();
}

} // namespace fx::editor

#include "MaterialPickerPanel.hpp"

#include "Common.hpp"
#include "EditorFrame.hpp"
#include "EditorTheme.hpp"
#include "EditorThread.hpp"
#include "PaginatedView.hpp"
#include "RaptorEditor.hpp"
#include "TopViewCanvas.hpp"
#include "TopViewPanel.hpp"

#include <ktx.h>
#include <vulkan/vulkan.h>
#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <Blockout.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Material/SpecialMaterial.hpp>
#include <World.hpp>
#include <algorithm>
#include <cctype>

namespace fx::editor {

static constexpr int scThumbnailSize = 32;
static constexpr int scCellWidth = 74;
static constexpr int scCellPadding = 4;
static constexpr int scLabelGap = 2;
static constexpr int scRowsPerPage = 3;
static constexpr uint32 scGlRgba8 = 0x8058;
static constexpr uint32 scGlSrgb8Alpha8 = 0x8C43;

static String ToLower(String text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return std::tolower(ch); });
	return text;
}

/// Reads the smallest mip that is at least `pixel_size` wide (or the base level) of an 8-bit RGBA or Basis KTX, as a
/// bitmap that is `pixel_size / scale_factor` across on screen
static wxBitmap LoadKtxBitmap(const String& path, const int pixel_size, const double scale_factor)
{
	ktxTexture* texture = nullptr;
	const String resolved = FilesystemIO::ResolvePath(path);

	if (ktxTexture_CreateFromNamedFile(resolved.CStr(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture) !=
		KTX_SUCCESS) {
		return wxBitmap();
	}

	bool is_bgra = false;
	bool supported = false;

	if (texture->classId == ktxTexture2_c) {
		ktxTexture2* texture2 = reinterpret_cast<ktxTexture2*>(texture);
		supported = (!ktxTexture2_NeedsTranscoding(texture2) ||
					 ktxTexture2_TranscodeBasis(texture2, KTX_TTF_RGBA32, 0) == KTX_SUCCESS) &&
					(texture2->vkFormat == VK_FORMAT_R8G8B8A8_UNORM || texture2->vkFormat == VK_FORMAT_R8G8B8A8_SRGB ||
					 texture2->vkFormat == VK_FORMAT_B8G8R8A8_UNORM || texture2->vkFormat == VK_FORMAT_B8G8R8A8_SRGB);
		is_bgra = texture2->vkFormat == VK_FORMAT_B8G8R8A8_UNORM || texture2->vkFormat == VK_FORMAT_B8G8R8A8_SRGB;
	}
	else {
		ktxTexture1* texture1 = reinterpret_cast<ktxTexture1*>(texture);
		supported = texture1->glInternalformat == scGlRgba8 || texture1->glInternalformat == scGlSrgb8Alpha8;
	}

	wxBitmap bitmap;

	if (supported && ktxTexture_GetData(texture) != nullptr) {
		uint32 level = 0;

		while (level + 1 < texture->numLevels &&
			   (texture->baseWidth >> (level + 1)) >= static_cast<uint32>(pixel_size)) {
			level++;
		}

		const uint32 width = std::max(1u, texture->baseWidth >> level);
		const uint32 height = std::max(1u, texture->baseHeight >> level);

		size_t offset = 0;

		if (ktxTexture_GetImageOffset(texture, level, 0, 0, &offset) == KTX_SUCCESS) {
			const uint8* pixels = ktxTexture_GetData(texture) + offset;

			wxImage image(static_cast<int>(width), static_cast<int>(height), false);
			uint8* rgb = image.GetData();

			for (uint32 i = 0; i < width * height; i++) {
				rgb[i * 3 + 0] = pixels[i * 4 + (is_bgra ? 2 : 0)];
				rgb[i * 3 + 1] = pixels[i * 4 + 1];
				rgb[i * 3 + 2] = pixels[i * 4 + (is_bgra ? 0 : 2)];
			}

			const double scale = std::min(1.0, static_cast<double>(pixel_size) / std::max(width, height));

			if (scale < 1.0) {
				image.Rescale(std::max(1, static_cast<int>(width * scale)),
							  std::max(1, static_cast<int>(height * scale)), wxIMAGE_QUALITY_HIGH);
			}

			bitmap = wxBitmap(image, wxBITMAP_SCREEN_DEPTH, scale_factor);
		}
	}

	ktxTexture_Destroy(texture);

	return bitmap;
}

/**
 * @brief Draws one page of the materials that pass the picker's search, as rows of thumbnails with their names
 * underneath. The page holds as many columns as fit across it.
 */
class MaterialThumbnailGrid : public wxPanel
{
public:
	MaterialThumbnailGrid(PaginatedView* pages, MaterialPickerPanel* picker)
		: wxPanel(pages, wxID_ANY), mpPicker(picker), mpPages(pages)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);

		mLabelFont = GetFont().Smaller();

		int label_width = 0;
		int label_height = 0;
		GetTextExtent("Ag", &label_width, &label_height, nullptr, nullptr, &mLabelFont);

		mCellHeight = scCellPadding + scThumbnailSize + scLabelGap + label_height + scCellPadding;

		SetMinSize(wxSize(-1, scRowsPerPage * mCellHeight));

		Bind(wxEVT_PAINT, &MaterialThumbnailGrid::OnPaint, this);
		Bind(wxEVT_SIZE, &MaterialThumbnailGrid::OnSize, this);
		Bind(wxEVT_LEFT_DOWN, &MaterialThumbnailGrid::OnLeftDown, this);
		Bind(wxEVT_LEFT_DCLICK, &MaterialThumbnailGrid::OnLeftDoubleClick, this);
		Bind(wxEVT_MOTION, &MaterialThumbnailGrid::OnMotion, this);
	}

	FX_FORCE_INLINE int GetSelectedRow() const { return mSelectedRow; }

	void SetSelectedRow(const int row)
	{
		mSelectedRow = row;
		Refresh();
	}

private:
	int GetColumnCount() const { return std::max(1, GetClientSize().x / scCellWidth); }

	/// The cell of the `page_index`th material on the current page
	wxRect GetCellRect(const int page_index) const
	{
		const int columns = GetColumnCount();
		const int cell_width = GetClientSize().x / columns;

		return wxRect((page_index % columns) * cell_width, (page_index / columns) * mCellHeight, cell_width,
					  mCellHeight);
	}

	int FindRowAt(const wxPoint& position) const
	{
		const int count = static_cast<int>(mpPages->GetPageItemCount());

		for (int page_index = 0; page_index < count; page_index++) {
			if (GetCellRect(page_index).Contains(position)) {
				return static_cast<int>(mpPages->GetFirstItem()) + page_index;
			}
		}

		return wxNOT_FOUND;
	}

	void OnPaint(wxPaintEvent&)
	{
		wxPaintDC dc(this);

		const EditorTheme::MaterialPickerColors& colors = GetEditorTheme().MaterialPicker;

		dc.SetBackground(wxBrush(ToWxColour(colors.Background)));
		dc.Clear();
		dc.SetFont(mLabelFont);

		const wxColour text_colour = ToWxColour(colors.Text);
		const wxColour highlight_colour = ToWxColour(colors.Highlight);
		const wxColour highlight_text_colour = ToWxColour(colors.HighlightText);
		const wxColour placeholder_colour = ToWxColour(colors.Placeholder);

		const int first_row = static_cast<int>(mpPages->GetFirstItem());
		const int count = static_cast<int>(mpPages->GetPageItemCount());

		for (int page_index = 0; page_index < count; page_index++) {
			const int row = first_row + page_index;

			if (static_cast<uint32>(row) >= mpPicker->mShownSlots.Size) {
				break;
			}

			const MaterialLibraryID slot = mpPicker->mShownSlots[static_cast<uint32>(row)];
			const wxRect cell = GetCellRect(page_index);

			if (row == mSelectedRow) {
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(highlight_colour));
				dc.DrawRoundedRectangle(wxRect(cell).Deflate(1), 4.0);
				dc.SetTextForeground(highlight_text_colour);
			}
			else {
				dc.SetTextForeground(text_colour);
			}

			const wxRect thumbnail(cell.x + (cell.width - scThumbnailSize) / 2, cell.y + scCellPadding, scThumbnailSize,
								   scThumbnailSize);

			const wxBitmap* bitmap = mpPicker->mBitmaps.Find(slot);

			if (bitmap != nullptr && bitmap->IsOk()) {
				dc.DrawBitmap(*bitmap, thumbnail.GetTopLeft(), false);
			}
			else {
				dc.SetPen(wxPen(placeholder_colour));
				dc.SetBrush(*wxTRANSPARENT_BRUSH);
				dc.DrawRectangle(thumbnail);

				if (bitmap != nullptr) {
					dc.DrawLine(thumbnail.GetTopLeft(), thumbnail.GetBottomRight());
				}
			}

			const wxString name = wxString::FromUTF8(
				mpPicker->mShownLibrary.Names[static_cast<uint32>(slot.ID)].CStr());
			const wxString label = wxControl::Ellipsize(name, dc, wxELLIPSIZE_END, cell.width - scCellPadding);
			const wxSize label_size = dc.GetTextExtent(label);

			dc.DrawText(label, cell.x + (cell.width - label_size.x) / 2, thumbnail.GetBottom() + 1 + scLabelGap);
		}
	}

	void OnSize(wxSizeEvent& event)
	{
		mpPages->SetPageSize(static_cast<uint32>(GetColumnCount() * scRowsPerPage));

		Refresh();
		event.Skip();
	}

	void OnLeftDown(wxMouseEvent& event)
	{
		const int row = FindRowAt(event.GetPosition());

		if (row != wxNOT_FOUND) {
			SetSelectedRow(row);
			mpPicker->ShowSelection();
		}
	}

	void OnLeftDoubleClick(wxMouseEvent& event)
	{
		const int row = FindRowAt(event.GetPosition());

		if (row != wxNOT_FOUND) {
			SetSelectedRow(row);
			mpPicker->ShowSelection();
			mpPicker->ApplySelected();
		}
	}

	void OnMotion(wxMouseEvent& event)
	{
		const int row = FindRowAt(event.GetPosition());

		if (row != mHoveredRow) {
			mHoveredRow = row;

			if (row == wxNOT_FOUND) {
				UnsetToolTip();
			}
			else {
				const MaterialLibraryID slot = mpPicker->mShownSlots[static_cast<uint32>(row)];
				SetToolTip(wxString::FromUTF8(mpPicker->mShownLibrary.Names[static_cast<uint32>(slot.ID)].CStr()));
			}
		}

		event.Skip();
	}

private:
	MaterialPickerPanel* mpPicker = nullptr;
	PaginatedView* mpPages = nullptr;

	wxFont mLabelFont;
	int mCellHeight = 0;
	int mSelectedRow = wxNOT_FOUND;
	int mHoveredRow = wxNOT_FOUND;
};

MaterialPickerPanel::MaterialPickerPanel(wxWindow* parent, EditorFrame* frame)
	: wxPanel(parent, wxID_ANY), mpFrame(frame)
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	mpSection = new PropertySection(this, "Materials");
	wxWindow* body = mpSection->GetBody();
	sizer->Add(mpSection, wxSizerFlags().Expand());

	mpSearch = new wxTextCtrl(body, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	mpSearch->SetHint("Search materials...");
	mpSection->AddWide(mpSearch);

	mpPages = new PaginatedView(body);
	mpGrid = new MaterialThumbnailGrid(mpPages, this);
	mpPages->SetContent(mpGrid);
	mpPages->OnPageChanged = [this] { mpGrid->Refresh(); };
	mpSection->AddWide(mpPages);

	mpNameLabel = new wxStaticText(body, wxID_ANY, "(select a material)", wxDefaultPosition, wxDefaultSize,
								   wxST_ELLIPSIZE_END);
	mpSection->AddWide(mpNameLabel);

	mpApplyButton = new wxButton(body, wxID_ANY, "Apply to Selection");
	mpApplyButton->Disable();
	mpSection->AddWide(mpApplyButton);

	SetSizer(sizer);

	mpSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RebuildRows(); });
	mpSearch->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { ApplySelected(); });
	mpApplyButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplySelected(); });
	Bind(wxEVT_SHOW, &MaterialPickerPanel::OnShow, this);
	Bind(wxEVT_IDLE, &MaterialPickerPanel::OnIdle, this);

	RefreshList();
}

void MaterialPickerPanel::FocusSearch()
{
	mpSection->SetExpanded(true);
	mpSearch->SetFocus();
	mpSearch->SelectAll();
}

void MaterialPickerPanel::RefreshList()
{
	mBitmaps.Clear();
	mNextThumbnailSlot = 0;
	mShownLibrary = mpFrame->GetState().Materials;

	RebuildRows();
}

void MaterialPickerPanel::OnStateChanged()
{
	if (mpFrame->GetState().Materials != mShownLibrary) {
		RefreshList();
	}
}

void MaterialPickerPanel::RebuildRows()
{
	MaterialLibraryID previous_slot = MaterialLibraryID(GetSelectedSlot());

	if (!previous_slot.IsValid()) {
		const int32 object_slot = mpFrame->GetState().Object.MaterialSlot;

		if (object_slot >= 0) {
			previous_slot = MaterialLibraryID(object_slot);
		}
	}

	mShownSlots.Clear();

	const String filter = ToLower(String(mpSearch->GetValue().ToStdString()));

	int selected_row = wxNOT_FOUND;

	for (uint32 slot = 0; slot < mShownLibrary.Names.Size; slot++) {
		const String& name = mShownLibrary.Names[slot];

		if (IsSystemMaterialName(name.CStr()) || IsBrushMarkerMaterial(FindSpecialMaterial(name.CStr()))) {
			continue;
		}

		if (!filter.IsEmpty() && ToLower(name).FindNext(0, filter) == String::scNotFound) {
			continue;
		}

		if (static_cast<int32>(slot) == previous_slot.ID) {
			selected_row = static_cast<int>(mShownSlots.Size);
		}

		mShownSlots.Insert(MaterialLibraryID(slot));
	}

	mpPages->SetItemCount(mShownSlots.Size);
	mpGrid->SetSelectedRow(selected_row);

	if (selected_row != wxNOT_FOUND) {
		mpPages->ShowItem(static_cast<uint32>(selected_row));
	}

	ShowSelection();
}

MaterialLibraryID MaterialPickerPanel::GetSelectedSlot() const
{
	const int row = mpGrid->GetSelectedRow();

	if (row == wxNOT_FOUND || row < 0 || static_cast<uint32>(row) >= mShownSlots.Size) {
		return MaterialLibraryID::scNull;
	}

	return mShownSlots[static_cast<uint32>(row)];
}

void MaterialPickerPanel::ShowSelection()
{
	const MaterialLibraryID slot = GetSelectedSlot();

	if (!slot.IsValid() || static_cast<uint32>(slot.ID) >= mShownLibrary.Names.Size) {
		mpNameLabel->SetLabel("(select a material)");
		mpApplyButton->Disable();
		return;
	}

	mpNameLabel->SetLabel(wxString::FromUTF8(mShownLibrary.Names[static_cast<uint32>(slot.ID)].CStr()));
	mpApplyButton->Enable();
}

void MaterialPickerPanel::ApplySelected()
{
	const MaterialLibraryID slot = GetSelectedSlot();

	if (!slot.IsValid()) {
		return;
	}

	ObjectID face_object = ObjectID::scNull;
	Vec3f face_normal = Vec3f::sZero;

	if (mpFrame->GetTopViewPanel()->GetCanvas()->GetPickedFace(face_object, face_normal)) {
		thread::PostToGame(
			[slot, face_object, face_normal]
			{
				if (gEditor == nullptr || gWorld == nullptr || gWorld->pPrototype == nullptr) {
					return;
				}

				gEditor->TopViewSetFaceMaterial(face_object, face_normal,
												gWorld->pPrototype->GetMaterialForID(slot));
			});

		return;
	}

	thread::PostToGame(
		[slot]
		{
			if (gEditor == nullptr || gWorld == nullptr || gWorld->pPrototype == nullptr) {
				return;
			}

			const MaterialID material = gWorld->pPrototype->GetMaterialForID(slot);
			const EditorSelection& selection = gEditor->GetSelection();

			for (uint32 i = 0; i < selection.GetCount(); i++) {
				gEditor->SetStoredMaterial(selection.GetObject(i), material);
			}

			if (selection.GetCount() > 0) {
				gEditor->MarkModified();
			}
		});
}

void MaterialPickerPanel::OnShow(wxShowEvent& event)
{
	if (event.IsShown()) {
		RebuildRows();
	}

	event.Skip();
}

void MaterialPickerPanel::OnIdle(wxIdleEvent& event)
{
	event.Skip();

	if (!IsShownOnScreen()) {
		return;
	}

	while (mNextThumbnailSlot < mShownLibrary.DiffusePaths.Size &&
		   mBitmaps.Find(MaterialLibraryID(mNextThumbnailSlot)) != nullptr) {
		mNextThumbnailSlot++;
	}

	if (mNextThumbnailSlot >= mShownLibrary.DiffusePaths.Size) {
		return;
	}

	const String& path = mShownLibrary.DiffusePaths[mNextThumbnailSlot];
	const double scale_factor = GetContentScaleFactor();

	wxBitmap bitmap;

	if (!path.IsEmpty()) {
		bitmap = LoadKtxBitmap(path, static_cast<int>(scThumbnailSize * scale_factor), scale_factor);
	}

	mBitmaps.Insert(MaterialLibraryID(mNextThumbnailSlot), bitmap);
	mNextThumbnailSlot++;

	mpGrid->Refresh();
	event.RequestMore();
}

} // namespace fx::editor

#include "MaterialPickerWindow.hpp"

#include "EditorFrame.hpp"
#include "EditorThread.hpp"
#include "RaptorEditor.hpp"

#include <ktx.h>
#include <vulkan/vulkan.h>
#include <wx/button.h>
#include <wx/image.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <Blockout.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Engine.hpp>
#include <World.hpp>
#include <algorithm>
#include <cctype>

namespace fx::editor {

static constexpr int scPreviewSize = 256;
static constexpr uint32 scGlRgba8 = 0x8058;
static constexpr uint32 scGlSrgb8Alpha8 = 0x8C43;

static String ToLower(String text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return std::tolower(ch); });
	return text;
}

/// Reads the smallest mip that is at least the preview size (or the base level) of an uncompressed 8-bit KTX
static wxBitmap LoadKtxBitmap(const String& path)
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
		supported = !ktxTexture2_NeedsTranscoding(texture2) &&
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

		while (level + 1 < texture->numLevels && (texture->baseWidth >> (level + 1)) >= scPreviewSize) {
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

			const double scale = std::min(1.0, static_cast<double>(scPreviewSize) / std::max(width, height));

			if (scale < 1.0) {
				image.Rescale(std::max(1, static_cast<int>(width * scale)),
							  std::max(1, static_cast<int>(height * scale)), wxIMAGE_QUALITY_HIGH);
			}

			bitmap = wxBitmap(image);
		}
	}

	ktxTexture_Destroy(texture);

	return bitmap;
}

MaterialPickerWindow::MaterialPickerWindow(EditorFrame* parent)
	: wxFrame(parent, wxID_ANY, "Material Picker", wxDefaultPosition, wxSize(560, 440)), mpFrame(parent)
{
	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);

	mpSearch = new wxTextCtrl(root, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	mpSearch->SetHint("Search materials...");
	left->Add(mpSearch, wxSizerFlags().Expand().Border(wxALL, 6));

	mpList = new wxListBox(root, wxID_ANY, wxDefaultPosition, wxSize(200, -1), 0, nullptr, wxLB_SINGLE);
	left->Add(mpList, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	sizer->Add(left, wxSizerFlags(1).Expand());

	wxBoxSizer* right = new wxBoxSizer(wxVERTICAL);

	mpNameLabel = new wxStaticText(root, wxID_ANY, "(select a material)");
	mpNameLabel->SetFont(mpNameLabel->GetFont().Bold());
	right->Add(mpNameLabel, wxSizerFlags().Border(wxALL, 6));

	mpPreview = new wxStaticBitmap(root, wxID_ANY, wxBitmap(), wxDefaultPosition, wxSize(scPreviewSize, scPreviewSize));
	right->Add(mpPreview, wxSizerFlags().Border(wxLEFT | wxRIGHT, 6));

	mpStatusLabel = new wxStaticText(root, wxID_ANY, "");
	right->Add(mpStatusLabel, wxSizerFlags().Border(wxALL, 6));

	mpApplyButton = new wxButton(root, wxID_ANY, "Apply to Selection");
	mpApplyButton->Disable();
	right->Add(mpApplyButton, wxSizerFlags().Border(wxALL, 6));

	sizer->Add(right, wxSizerFlags().Expand());

	root->SetSizer(sizer);

	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizer(frame_sizer);

	mpSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RebuildRows(); });
	mpSearch->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { ApplySelected(); });
	mpList->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { ShowPreview(GetSelectedSlot()); });
	mpList->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { ApplySelected(); });
	mpApplyButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ApplySelected(); });
	Bind(wxEVT_SHOW, &MaterialPickerWindow::OnShow, this);
	Bind(wxEVT_CLOSE_WINDOW, &MaterialPickerWindow::OnClose, this);

	RefreshList();
}

void MaterialPickerWindow::RefreshList()
{
	mBitmaps.Clear();
	mShownLibrary = mpFrame->GetState().Materials;

	RebuildRows();
}

void MaterialPickerWindow::OnStateChanged()
{
	if (mpFrame->GetState().Materials != mShownLibrary) {
		RefreshList();
	}
}

void MaterialPickerWindow::RebuildRows()
{
	MaterialLibraryID previous_slot = MaterialLibraryID(GetSelectedSlot());

	if (!previous_slot.IsValid()) {
		const int32 object_slot = mpFrame->GetState().Object.MaterialSlot;

		if (object_slot >= 0) {
			previous_slot = MaterialLibraryID(object_slot);
		}
	}

	mpList->Clear();
	mShownSlots.clear();

	const String filter = ToLower(mpSearch->GetValue().ToStdString());

	int selected_row = wxNOT_FOUND;

	for (uint32 slot = 0; slot < mShownLibrary.Names.size(); slot++) {
		const std::string& name = mShownLibrary.Names[slot];

		if (!filter.IsEmpty() && ToLower(name).FindNext(0, filter) == String::scNotFound) {
			continue;
		}

		if (static_cast<int32>(slot) == previous_slot.ID) {
			selected_row = static_cast<int>(mShownSlots.size());
		}

		mpList->Append(wxString::FromUTF8(name));
		mShownSlots.push_back(MaterialLibraryID(slot));
	}

	if (selected_row != wxNOT_FOUND) {
		mpList->SetSelection(selected_row);
	}

	ShowPreview(GetSelectedSlot());
}

MaterialLibraryID MaterialPickerWindow::GetSelectedSlot() const
{
	const int row = mpList->GetSelection();

	if (row == wxNOT_FOUND || row < 0 || static_cast<size_t>(row) >= mShownSlots.size()) {
		return MaterialLibraryID::scNull;
	}

	return mShownSlots[static_cast<size_t>(row)];
}

const wxBitmap& MaterialPickerWindow::GetAlbedoBitmap(MaterialLibraryID material_slot)
{
	wxBitmap* found = mBitmaps.Find(material_slot);

	if (found != nullptr) {
		return *found;
	}

	const String path(mShownLibrary.DiffusePaths[static_cast<size_t>(material_slot.ID)]);

	return mBitmaps.Insert(material_slot, path.IsEmpty() ? wxBitmap() : LoadKtxBitmap(path));
}

void MaterialPickerWindow::ShowPreview(MaterialLibraryID material_slot)
{
	if (!material_slot.IsValid() || static_cast<size_t>(material_slot.ID) >= mShownLibrary.Names.size()) {
		mpNameLabel->SetLabel("(select a material)");
		mpStatusLabel->SetLabel("");
		mpPreview->SetBitmap(wxBitmap());
		mpApplyButton->Disable();
		return;
	}

	const wxBitmap& bitmap = GetAlbedoBitmap(material_slot);

	mpNameLabel->SetLabel(wxString::FromUTF8(mShownLibrary.Names[static_cast<size_t>(material_slot.ID)]));

	if (bitmap.IsOk()) {
		mpPreview->SetBitmap(bitmap);
		mpStatusLabel->SetLabel("");
	}
	else {
		mpPreview->SetBitmap(wxBitmap());

		if (mShownLibrary.DiffusePaths[static_cast<size_t>(material_slot.ID)].empty()) {
			mpStatusLabel->SetLabel("No albedo texture");
		}
		else {
			mpStatusLabel->SetLabel("Preview unavailable");
		}
	}

	mpApplyButton->Enable();

	Layout();
}

void MaterialPickerWindow::ApplySelected()
{
	const MaterialLibraryID slot = GetSelectedSlot();

	if (!slot.IsValid()) {
		return;
	}

	thread::PostToGame(
		[slot]
		{
			if (gEditor == nullptr || gWorld == nullptr || gWorld->pBlockout == nullptr) {
				return;
			}

			const MaterialID material = gWorld->pBlockout->GetMaterialForID(slot);
			const EditorSelection& selection = gEditor->GetSelection();

			for (uint32 i = 0; i < selection.GetCount(); i++) {
				gEditor->SetStoredMaterial(selection.GetObject(i), material);
			}
		});
}

void MaterialPickerWindow::OnShow(wxShowEvent& event)
{
	if (event.IsShown()) {
		RebuildRows();
	}

	event.Skip();
}

void MaterialPickerWindow::OnClose(wxCloseEvent& event)
{
	if (event.CanVeto()) {
		Hide();
		event.Veto();
		return;
	}

	event.Skip();
}

} // namespace fx::editor

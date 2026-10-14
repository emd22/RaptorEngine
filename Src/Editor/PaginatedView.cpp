#include "PaginatedView.hpp"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>

namespace fx::editor {

PaginatedView::PaginatedView(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	mpSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* pager = new wxBoxSizer(wxHORIZONTAL);

	mpPreviousButton = new wxButton(this, wxID_ANY, "<", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	mpPreviousButton->SetToolTip("Previous page");
	pager->Add(mpPreviousButton, wxSizerFlags().CenterVertical());

	mpPageLabel = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
								   wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
	pager->Add(mpPageLabel, wxSizerFlags(1).CenterVertical().Border(wxLEFT | wxRIGHT, 6));

	mpNextButton = new wxButton(this, wxID_ANY, ">", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	mpNextButton->SetToolTip("Next page");
	pager->Add(mpNextButton, wxSizerFlags().CenterVertical());

	mpSizer->Add(pager, wxSizerFlags().Expand().Border(wxTOP, 4));

	SetSizer(mpSizer);

	mpPreviousButton->Bind(wxEVT_BUTTON,
						   [this](wxCommandEvent&)
						   {
							   if (mPage > 0) {
								   SetPage(mPage - 1);
							   }
						   });
	mpNextButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetPage(mPage + 1); });

	ShowRange();
}

void PaginatedView::SetContent(wxWindow* content)
{
	mpSizer->Insert(0, content, wxSizerFlags(1).Expand());
	Layout();
}

uint32 PaginatedView::GetPageCount() const { return std::max(1u, (mItemCount + mPageSize - 1) / mPageSize); }

uint32 PaginatedView::GetPageItemCount() const
{
	const uint32 first = GetFirstItem();

	if (first >= mItemCount) {
		return 0;
	}

	return std::min(mPageSize, mItemCount - first);
}

void PaginatedView::SetItemCount(uint32 item_count)
{
	mItemCount = item_count;
	mPage = std::min(mPage, GetPageCount() - 1);

	ShowRange();
}

void PaginatedView::SetPageSize(uint32 page_size)
{
	page_size = std::max(page_size, 1u);

	if (page_size == mPageSize) {
		return;
	}

	const uint32 first = GetFirstItem();

	mPageSize = page_size;
	mPage = std::min(first / mPageSize, GetPageCount() - 1);

	ShowRange();
}

void PaginatedView::SetPage(uint32 page)
{
	page = std::min(page, GetPageCount() - 1);

	if (page == mPage) {
		return;
	}

	mPage = page;

	ShowRange();
}

void PaginatedView::ShowItem(uint32 item)
{
	if (item < mItemCount) {
		SetPage(item / mPageSize);
	}
}

void PaginatedView::ShowRange()
{
	const uint32 page_count = GetPageCount();

	mpPageLabel->SetLabel(wxString::Format("%u / %u", mPage + 1, page_count));
	mpPreviousButton->Enable(mPage > 0);
	mpNextButton->Enable(mPage + 1 < page_count);

	if (OnPageChanged) {
		OnPageChanged();
	}
}

} // namespace fx::editor

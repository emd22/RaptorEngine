#pragma once

#include <wx/panel.h>

#include <Core/Types.hpp>
#include <functional>

class wxBoxSizer;
class wxButton;
class wxStaticText;

namespace fx::editor {

/**
 * @brief Splits a list of items into a paged list
 */
class PaginatedView : public wxPanel
{
public:
	explicit PaginatedView(wxWindow* parent);

	void SetContent(wxWindow* content);

	void SetItemCount(uint32 item_count);
	void SetPageSize(uint32 page_size);

	void SetPage(uint32 page);

	/// Turns to the page that `item` is on
	void ShowItem(uint32 item);

	FX_FORCE_INLINE uint32 GetItemCount() const { return mItemCount; }
	FX_FORCE_INLINE uint32 GetPageSize() const { return mPageSize; }
	FX_FORCE_INLINE uint32 GetPage() const { return mPage; }
	uint32 GetPageCount() const;

	FX_FORCE_INLINE uint32 GetFirstItem() const { return mPage * mPageSize; }
	uint32 GetPageItemCount() const;

public:
	std::function<void()> OnPageChanged;

private:
	void ShowRange();

private:
	wxBoxSizer* mpSizer = nullptr;
	wxButton* mpPreviousButton = nullptr;
	wxButton* mpNextButton = nullptr;
	wxStaticText* mpPageLabel = nullptr;

	uint32 mItemCount = 0;
	uint32 mPageSize = 1;
	uint32 mPage = 0;
};

} // namespace fx::editor

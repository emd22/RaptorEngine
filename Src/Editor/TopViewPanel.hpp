#pragma once

#ifdef FX_IS_EDITOR

#include "EditorPanelState.hpp"
#include "TopViewCanvas.hpp"

#include <wx/panel.h>

#include <Core/Types.hpp>

class wxStaticText;

namespace fx::editor {

class TopViewPanel : public wxPanel
{
public:
	explicit TopViewPanel(wxWindow* parent);

	FX_FORCE_INLINE TopViewCanvas* GetCanvas() { return mpCanvas; }

	void ApplyState(const TopViewState& state);

	void ApplyTheme();

private:
	TopViewCanvas* mpCanvas = nullptr;
	wxStaticText* mpStatus = nullptr;
};

} // namespace fx::editor

#endif

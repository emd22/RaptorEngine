#include "WorldPropertiesPanel.hpp"

#include "Common.hpp"

#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valnum.h>

#include <Engine.hpp>
#include <World.hpp>

namespace fx::editor {

WorldPropertiesPanel::WorldPropertiesPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* title = new wxStaticText(this, wxID_ANY, "World");
	title->SetFont(title->GetFont().Bold());
	sizer->Add(title, wxSizerFlags().Border(wxALL, 6));

	mpPositionField = new EditableVector3Field(this, "Player Position");
	sizer->Add(mpPositionField->GetSizer(), wxSizerFlags().Border(wxALL, 6));

	mpPositionField->SetOnChange(
		[this](const Vec3f& value)
		{
			gWorld->Player.TeleportTo(value);
			mShownPosition = value;
		});


	SetSizer(sizer);

	Update();
}

void WorldPropertiesPanel::Update()
{
	if (!mbShowingAnything) {
		mbShowingAnything = true;
		mpPositionField->Enable(true);
	}

	const Vec3f position = gWorld->Player.Position;

	if (true) {
		mpPositionField->SetValue(position);
		mShownPosition = position;
	}
}

} // namespace fx::editor

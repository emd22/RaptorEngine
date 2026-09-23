#include "EditorApp.hpp"

#include "EditorFrame.hpp"
#include "EditorPlatform.hpp"
#include "EditorViewport.hpp"
#include "ObjectPropertiesPanel.hpp"
#include "WorldPropertiesPanel.hpp"

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/image.h>
#include <wx/init.h>

#include <Core/Log.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>

namespace fx::editor {

/// Raptor controls the frame loop, so the app has nothing to do on init
class EditorApp : public wxApp
{
public:
	bool OnInit() override
	{
		SetAppearance(wxApp::Appearance::Dark);
		return true;
	}
};

static wxGUIEventLoop* spEventLoop = nullptr;
static EditorFrame* spMainFrame = nullptr;

static std::function<void()> spReloadHandlers[static_cast<uint32>(eReloadTarget::Count)];

/// Upper bound on native events dispatched per frame, so a flood of them can't stall rendering
static constexpr uint32 scMaxEventsPerFrame = 512;

bool Init(int argc, char** argv)
{
	wxApp::SetInstance(new EditorApp);

	if (!wxEntryStart(argc, argv)) {
		LogError(LC_CORE, "Could not initialize the wxWidgets backend for the editor");
		return false;
	}

	// For the tool icons
	wxInitAllImageHandlers();

#ifdef FX_PLATFORM_MACOS
	platform::DisableWindowTabbing();
#endif

	if (!wxTheApp->CallOnInit()) {
		wxEntryCleanup();
		return false;
	}

	spEventLoop = new wxGUIEventLoop;
	wxEventLoopBase::SetActive(spEventLoop);

	return true;
}

void UpdateWorldPropertiesPanel() { spMainFrame->GetWorldPropertiesPanel()->Update(); }

EditorFrame* CreateMainFrame(const char* title, const Vec2u& viewport_size)
{
	spMainFrame = new EditorFrame(wxString::FromAscii(title),
								  wxSize(static_cast<int>(viewport_size.X), static_cast<int>(viewport_size.Y)));

	spMainFrame->Show();
	spMainFrame->Raise();

#ifdef FX_PLATFORM_MACOS
	// Bring window to front
	platform::ActivateApp();
#endif

	spMainFrame->GetViewport()->SetFocus();

	// Get the frame on screen and laid out before the renderer creates its surface on the viewport
	PumpEvents();

	return spMainFrame;
}

EditorFrame* GetMainFrame() { return spMainFrame; }

bool PumpEvents()
{
	if (spEventLoop == nullptr || spMainFrame == nullptr) {
		return false;
	}

	for (uint32 i = 0; i < scMaxEventsPerFrame; i++) {
		if (spEventLoop->DispatchTimeout(0) != 1) {
			break;
		}
	}

	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();

	EditorViewport* viewport = spMainFrame->GetViewport();

	viewport->PollRelativeMouse();

	// Rebuild once for however many size events arrived
	if (viewport->ConsumeResize() && renderer::gGraphics != nullptr && renderer::gGraphics->GetWindow() != nullptr) {
		const wxSize size = viewport->GetClientSize();

		if (size.x > 0 && size.y > 0) {
			renderer::gGraphics->RebuildToResizedWindow();
		}
	}

	return !spMainFrame->IsCloseRequested();
}

void UpdatePropertiesPanelForObject(Object* object)
{
	if (spMainFrame != nullptr) {
		spMainFrame->GetObjectPropertiesPanel()->ShowObject(object);
	}
}

bool IsSimulationMode() { return spMainFrame->IsSimulationMode(); }

eEditorTool GetEditorTool() { return spMainFrame->GetSelectedTool(); }

void SetReloadHandler(eReloadTarget target, std::function<void()> handler)
{
	spReloadHandlers[static_cast<uint32>(target)] = std::move(handler);
}

void InvokeReloadHandler(eReloadTarget target)
{
	const std::function<void()>& handler = spReloadHandlers[static_cast<uint32>(target)];

	if (handler) {
		handler();
	}
}

void Shutdown()
{
	if (spMainFrame != nullptr) {
		spMainFrame->Destroy();
		spMainFrame = nullptr;
	}

	// Runs the frame's delayed deletion
	if (wxTheApp != nullptr) {
		wxTheApp->ProcessIdle();
	}

	wxEventLoopBase::SetActive(nullptr);
	delete spEventLoop;
	spEventLoop = nullptr;

	wxEntryCleanup();
}

} // namespace fx::editor

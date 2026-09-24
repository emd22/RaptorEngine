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
#include <Core/StackArray.hpp>
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


struct AppContext
{
	wxGUIEventLoop* pEventLoop = nullptr;
	EditorFrame* pMainFrame = nullptr;

	std::function<void()> pReloadHandlers[static_cast<uint32>(eReloadTarget::Count)];

	StackArray<EditorTool, static_cast<uint32>(eEditorTool::Count)> Tools;

	EditorToolState ToolState;
	EditorToolSelection ToolSelection;
};

static AppContext* pCtx = nullptr;

static void AddTool(const eEditorTool tool, const char* path)
{
	uint32 tool_index = (static_cast<uint32>(tool));

	Assert((static_cast<uint32>(tool)) == pCtx->Tools.Size);

	pCtx->Tools.Insert(EditorTool(tool, path));
}


static void CreateEditorTools()
{
	Assert(pCtx != nullptr);

	AddTool(eEditorTool::None, nullptr);
	AddTool(eEditorTool::Translate, "./Scripts/editor/tools/tool_translate.strata");
	AddTool(eEditorTool::Face, "./Scripts/editor/tools/tool_face.strata");
}


/// Upper bound on native events dispatched per frame, so a flood of them can't stall rendering
static constexpr uint32 scMaxEventsPerFrame = 512;

bool Init(int argc, char** argv)
{
	pCtx = new AppContext;

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

	pCtx->pEventLoop = new wxGUIEventLoop;
	wxEventLoopBase::SetActive(pCtx->pEventLoop);

	CreateEditorTools();

	return true;
}


void UpdateWorldPropertiesPanel() { pCtx->pMainFrame->GetWorldPropertiesPanel()->Update(); }

EditorFrame* CreateMainFrame(const char* title, const Vec2u& viewport_size)
{
	pCtx->pMainFrame = new EditorFrame(wxString::FromAscii(title),
									   wxSize(static_cast<int>(viewport_size.X), static_cast<int>(viewport_size.Y)));

	pCtx->pMainFrame->Show();
	pCtx->pMainFrame->Raise();

#ifdef FX_PLATFORM_MACOS
	// Bring window to front
	platform::ActivateApp();
#endif

	pCtx->pMainFrame->GetViewport()->SetFocus();

	// Get the frame on screen and laid out before the renderer creates its surface on the viewport
	PumpEvents();

	return pCtx->pMainFrame;
}

EditorFrame* GetMainFrame() { return pCtx->pMainFrame; }

bool PumpEvents()
{
	if (pCtx->pEventLoop == nullptr || pCtx->pMainFrame == nullptr) {
		return false;
	}

	for (uint32 i = 0; i < scMaxEventsPerFrame; i++) {
		if (pCtx->pEventLoop->DispatchTimeout(0) != 1) {
			break;
		}
	}

	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();

	EditorViewport* viewport = pCtx->pMainFrame->GetViewport();

	viewport->PollRelativeMouse();

	// Rebuild once for however many size events arrived
	if (viewport->ConsumeResize() && renderer::gGraphics != nullptr && renderer::gGraphics->GetWindow() != nullptr) {
		const wxSize size = viewport->GetClientSize();

		if (size.x > 0 && size.y > 0) {
			renderer::gGraphics->RebuildToResizedWindow();
		}
	}

	return !pCtx->pMainFrame->IsCloseRequested();
}

void UpdatePropertiesPanelForObject(Object* object)
{
	if (pCtx->pMainFrame != nullptr) {
		pCtx->pMainFrame->GetObjectPropertiesPanel()->ShowObject(object);
	}
}

bool IsSimulationMode() { return pCtx->pMainFrame->IsSimulationMode(); }

eEditorTool GetEditorTool() { return pCtx->pMainFrame->GetSelectedTool(); }
EditorTool* GetEditorTool2(eEditorTool tool_type)
{
	uint32 tool_index = static_cast<uint32>(tool_type);
	if (tool_index > pCtx->Tools.Size || tool_index < 0) {
		LogError("Tool {} has not been registered", tool_index);
		return nullptr;
	}

	return &pCtx->Tools[tool_index];
}

editor::EditorToolState* GetEditorToolState() { return &pCtx->ToolState; }
editor::EditorToolSelection* GetEditorToolSelection() { return &pCtx->ToolSelection; }

void ReloadAllTools()
{
	for (EditorTool& tool : pCtx->Tools) {
		tool.ReloadHotFunctions();
	}
}

void SubmitToolConfig(const editor::EditorToolState* config) { pCtx->ToolState = (*config); }

void SetReloadHandler(eReloadTarget target, std::function<void()> handler)
{
	pCtx->pReloadHandlers[static_cast<uint32>(target)] = std::move(handler);
}

void InvokeReloadHandler(eReloadTarget target)
{
	const std::function<void()>& handler = pCtx->pReloadHandlers[static_cast<uint32>(target)];

	if (handler != nullptr) {
		handler();
	}
}

void Shutdown()
{
	if (pCtx->pMainFrame != nullptr) {
		pCtx->pMainFrame->Destroy();
		pCtx->pMainFrame = nullptr;
	}

	// Runs the frame's delayed deletion
	if (wxTheApp != nullptr) {
		wxTheApp->ProcessIdle();
	}

	wxEventLoopBase::SetActive(nullptr);

	delete pCtx->pEventLoop;
	pCtx->pEventLoop = nullptr;

	delete pCtx;
	pCtx = nullptr;

	wxEntryCleanup();
}

} // namespace fx::editor

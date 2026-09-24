#include "RaptorEditor.hpp"

#ifdef FX_IS_EDITOR

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

struct EditorContext
{
	EditorToolState ToolState;
	EditorToolSelection ToolSelection;
};

static EditorContext* pCtx = nullptr;

static void AddTool(const eEditorTool tool, const char* path) {}


static void CreateEditorTools()
{
	Assert(pCtx != nullptr);

	AddTool(eEditorTool::None, nullptr);
	AddTool(eEditorTool::Translate, "./Scripts/editor/tools/tool_translate.strata");
	AddTool(eEditorTool::Face, "./Scripts/editor/tools/tool_face.strata");
}


/// Upper bound on native events dispatched per frame, so a flood of them can't stall rendering
static constexpr uint32 scMaxEventsPerFrame = 256;

void RaptorEditor::AddTool(const eEditorTool tool_type, const char* path)
{
	uint32 tool_index = (static_cast<uint32>(tool_type));
	Assert((static_cast<uint32>(tool_type)) == mTools.Size);

	mTools.Insert(EditorTool(tool_type, path));
}

void RaptorEditor::AddTools() {}


bool RaptorEditor::InitGUI(int argc, char** argv)
{
	pCtx = new EditorContext;

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

	mpEventLoop = new wxGUIEventLoop;
	wxEventLoopBase::SetActive(mpEventLoop);

	CreateEditorTools();

	return true;
}

EditorTool* RaptorEditor::GetTool(const eEditorTool tool_type)
{
	uint32 tool_index = static_cast<uint32>(tool_type);

	if (tool_index > mTools.Size || tool_index < 0) {
		LogError("Tool {} has not been registered", tool_index);
		return nullptr;
	}

	return &mTools[tool_index];
}


void RaptorEditor::UpdateForSelectedObject(Object* object)
{
	if (mpMainFrame != nullptr) {
		mpMainFrame->GetObjectPropertiesPanel()->ShowObject(object);
	}
}

void RaptorEditor::RefreshValues() { mpMainFrame->GetWorldPropertiesPanel()->Update(); }

EditorFrame* RaptorEditor::CreateMainFrame(const char* title, const Vec2u& viewport_size)
{
	mpMainFrame = new EditorFrame(wxString::FromAscii(title),
								  wxSize(static_cast<int>(viewport_size.X), static_cast<int>(viewport_size.Y)));

	mpMainFrame->Show();
	mpMainFrame->Raise();

#ifdef FX_PLATFORM_MACOS
	// Bring window to front
	platform::ActivateApp();
#endif

	mpMainFrame->GetViewport()->SetFocus();

	// Get the frame on screen and laid out before the renderer creates its surface on the viewport
	PumpEvents();

	return mpMainFrame;
}

bool RaptorEditor::PumpEvents()
{
	if (mpEventLoop == nullptr || mpMainFrame == nullptr) {
		return false;
	}

	for (uint32 i = 0; i < scMaxEventsPerFrame; i++) {
		if (mpEventLoop->DispatchTimeout(0) != 1) {
			break;
		}
	}

	wxTheApp->ProcessPendingEvents();
	wxTheApp->ProcessIdle();

	EditorViewport* viewport = mpMainFrame->GetViewport();

	viewport->PollRelativeMouse();

	// Rebuild once for however many size events arrived
	if (viewport->ConsumeResize() && renderer::gGraphics != nullptr && renderer::gGraphics->GetWindow() != nullptr) {
		const wxSize size = viewport->GetClientSize();

		if (size.x > 0 && size.y > 0) {
			renderer::gGraphics->RebuildToResizedWindow();
		}
	}

	return !mpMainFrame->IsCloseRequested();
}


void UpdatePropertiesPanelForObject(Object* object) {}

bool RaptorEditor::IsSimulationMode() const { return mpMainFrame->IsSimulationMode(); }

editor::EditorToolState* GetEditorToolState() { return &pCtx->ToolState; }
editor::EditorToolSelection* GetEditorToolSelection() { return &pCtx->ToolSelection; }

void RaptorEditor::ReloadAllTools()
{
	for (EditorTool& tool : mTools) {
		tool.ReloadHotFunctions();
	}
}

void SubmitToolConfig(const editor::EditorToolState* config) { pCtx->ToolState = (*config); }

void RaptorEditor::SetReloadHandler(eReloadTarget target, std::function<void()> handler)
{
	mpReloadHandlers[static_cast<uint32>(target)] = std::move(handler);
}

void RaptorEditor::InvokeReloadHandler(eReloadTarget target)
{
	const std::function<void()>& handler = mpReloadHandlers[static_cast<uint32>(target)];

	if (handler != nullptr) {
		handler();
	}
}

void RaptorEditor::Destroy()
{
	if (mpMainFrame != nullptr) {
		mpMainFrame->Destroy();
		mpMainFrame = nullptr;
	}

	// Runs the frame's delayed deletion
	if (wxTheApp != nullptr) {
		wxTheApp->ProcessIdle();
	}

	wxEventLoopBase::SetActive(nullptr);

	delete mpEventLoop;
	mpEventLoop = nullptr;

	delete pCtx;
	pCtx = nullptr;

	wxEntryCleanup();
}

} // namespace fx::editor

#endif

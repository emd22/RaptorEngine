#pragma once

#ifdef FX_IS_EDITOR

#include "EditorTool.hpp"

#include <wx/evtloop.h>

#include <Core/StackArray.hpp>
#include <Core/Types.hpp>
#include <Math/Vec2.hpp>
#include <functional>

namespace fx {

class Object;

namespace editor {

enum class eEditorTool : uint32;

};

} // namespace fx

namespace fx::editor {

class EditorFrame;

// class wxGUIEventLoop;

/// The File > Reload menu items
enum class eReloadTarget : uint32
{
	World,
	Prototype,
	Scripts,

	Count,
};


class RaptorEditor
{
public:
	RaptorEditor() = default;

	bool InitGUI(int argc, char** argv);
	EditorFrame* CreateMainFrame(const char* title, const Vec2u& viewport_size);

	FX_FORCE_INLINE EditorFrame* GetMainFrame() { return mpMainFrame; }
	FX_FORCE_INLINE const EditorFrame* GetMainFrame() const { return mpMainFrame; }

	void SetReloadHandler(eReloadTarget target, std::function<void()> handler);
	void InvokeReloadHandler(eReloadTarget target);

	bool PumpEvents();

	void RefreshValues();
	void UpdateForSelectedObject(Object* object);

	eEditorTool GetCurrentToolType() const { return mCurrentToolType; }
	EditorTool* GetCurrentTool() { return mpEditorTool; };
	EditorTool* GetTool(const eEditorTool tool_type);

	bool IsSimulationMode() const;

	void ReloadAllTools();

	void Destroy();

	~RaptorEditor() = default;

private:
	void AddTools();
	void AddTool(const eEditorTool tool_type, const char* path);

private:
	EditorFrame* mpMainFrame = nullptr;

	StackArray<EditorTool, static_cast<uint32>(eEditorTool::Count)> mTools;

	eEditorTool mCurrentToolType = eEditorTool::None;
	EditorTool* mpEditorTool = nullptr;

	wxGUIEventLoop* mpEventLoop = nullptr;

	std::function<void()> mpReloadHandlers[static_cast<uint32>(eReloadTarget::Count)];
};


editor::EditorToolState* GetEditorToolState();
editor::EditorToolSelection* GetEditorToolSelection();


void SubmitToolConfig(const editor::EditorToolState* config);


} // namespace fx::editor

#endif

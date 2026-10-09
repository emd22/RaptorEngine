#define VMA_DEBUG_LOG(...) LogWarning(LC_MEMORY, __VA_ARGS__)

#include "RaptorGame.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/DataPack.hpp>
#include <Asset/Font/Font.hpp>
#include <Asset/ShaderCompiler.hpp>
#include <Asset/ShaderPreproc.hpp>
#include <Core/Defer.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/FreeArray.hpp>
#include <Core/Path.hpp>
#include <Core/Queue.hpp>
#include <Core/String.hpp>
#include <Core/Thread/SysThread.hpp>
#include <Engine.hpp>
#ifdef FX_IS_EDITOR
#include <Editor/RaptorEditor.hpp>
#endif
#include <FoxScript/FoxScript.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/Globals.hpp>


FX_SET_MODULE_NAME("Main")

using namespace fx;
using namespace fx::renderer;

#ifdef FX_IS_EDITOR
static constexpr size_t scGameThreadStackSize = 16 * 1024 * 1024;
#endif


static void RunGame()
{
	gScriptManager = new ScriptManager;

#ifdef FX_IS_EDITOR
	gEditor->InitTools();
#endif

	fx::renderer::Globals::Init();

	{
		fx::RaptorGame game {};
	}

	fx::Globals::Destroy();
	fx::renderer::Globals::Destroy();

	if (gAssetManager) {
		delete gAssetManager;
		gAssetManager = nullptr;
	}
}


int main(int argc, char** argv)
{
#ifdef FX_IS_EDITOR
	gEditor = new editor::RaptorEditor;

	// If there was an issue starting the editor, return with an error code
	if (!gEditor->InitGUI(argc, argv)) {
		return 1;
	}

	SysThread game_thread;
	game_thread.StackSize = scGameThreadStackSize;

	game_thread.Create(
		[]
		{
			struct FinishNotifier
			{
				~FinishNotifier() { gEditor->NotifyGameFinished(); }
			} notifier;

			RunGame();
		});

	gEditor->RunUILoop();

	game_thread.Join();

	// Destroy the editor after the renderer is gone as its surface presents to the editor frame
	gEditor->Destroy();

	delete gEditor;
	gEditor = nullptr;
#else
	RunGame();
#endif

	return 0;
}

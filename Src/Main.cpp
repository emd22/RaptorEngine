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
#include <Core/MemPool/MemPool.hpp>
#include <Core/Path.hpp>
#include <Core/Queue.hpp>
#include <Core/String.hpp>
#include <Engine.hpp>
#ifdef FX_IS_EDITOR
#include <Editor/EditorApp.hpp>
#endif
#include <FoxScript/FoxScript.hpp>
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/Globals.hpp>

// #define FX_RUN_TEST
// #define FX_TEST_SCRIPT

FX_SET_MODULE_NAME("Main")

using namespace fx;
using namespace fx::renderer;


int main(int argc, char** argv)
{
	fx::gEnginePool = new fx::MemPool;
	fx::gEnginePool->Create(FX_MEMORY_ENGINE_POOL_SIZE);

	fx::gScriptMemPool = new fx::MemPool;
	fx::gScriptMemPool->Create(1024 * 64);


#ifdef FX_TEST_SCRIPT
	script::FoxScript fs;
	fs.Load("./Scripts/GlobalTest.fox");

	script::FoxSymbol* sym = fs.GetSymbol("Default");
	if (!sym) {
		LogError("Cannot find symbol!");
	}

	script::FoxValue value = fs.CallProc(sym, {});

	LogInfo("Value: {}", value);
#endif

#ifndef FX_RUN_TEST

#ifdef FX_IS_EDITOR
	// If there was an issue starting the editor, return with an error code
	if (!fx::editor::Init(argc, argv)) {
		return 1;
	}
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

#ifdef FX_IS_EDITOR
	// Destroy the editor after the renderer is gone as its surface presents to the editor frame
	fx::editor::Shutdown();
#endif

	Defer(
		[]()
		{
			delete fx::gEnginePool;
			fx::gEnginePool = nullptr;
		});
#endif
	return 0;
}

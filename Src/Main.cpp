
#define VMA_DEBUG_LOG(...) LogWarning(LC_MEMORY, __VA_ARGS__)

#include "FoxtrotGame.hpp"

#include <Asset/AxManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <Asset/DataPack.hpp>
#include <Asset/Font/Font.hpp>
#include <Asset/MipmapGen.hpp>
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
#include <Math/MathConsts.hpp>
#include <Math/MathUtil.hpp>
#include <Renderer/Globals.hpp>
#include <Script/FoxScript.hpp>

// #define FX_RUN_TEST
// #define FX_TEST_SCRIPT

FX_SET_MODULE_NAME("Main")

using namespace fx;
using namespace fx::renderer;


enum class eTestFlags
{
	None = 0,
	A = (1 << 0),
	B = (1 << 1),
	C = (1 << 2),
	D = (1 << 3),
};

FxEnumFlags(eTestFlags);


int main()
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
	fx::renderer::Globals::Init();

	{
		fx::FoxtrotGame game {};
	}

	fx::Globals::Destroy();
	fx::renderer::Globals::Destroy();

	if (gAssetManager) {
		delete gAssetManager;
		gAssetManager = nullptr;
	}

	Defer(
		[]()
		{
			delete fx::gEnginePool;
			fx::gEnginePool = nullptr;
		});
#endif
	return 0;
}

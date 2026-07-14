#include "Assert.hpp"

#include <Asset/AssetManager.hpp>
#include <Engine.hpp>

namespace fx {

void Terminate()
{
	gAssetManager->GetInstance()->Shutdown();

	FX_BREAKPOINT;

	std::terminate();
}

} // namespace fx

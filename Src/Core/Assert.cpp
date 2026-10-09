#include "Assert.hpp"

#include <Asset/AssetManager.hpp>
#include <Engine.hpp>

namespace fx {

void Terminate()
{
	if (gAssetManager != nullptr) {
		gAssetManager->Abort();
	}

	FX_BREAKPOINT;
	std::terminate();
}

} // namespace fx

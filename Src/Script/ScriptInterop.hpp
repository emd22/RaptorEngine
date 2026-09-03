#pragma once

#include <Core/Slice.hpp>

namespace fx::script {
struct PredefExtern
{
	const char* pcName;
	void* pFunction;
};

Slice<const PredefExtern> GetInteropPredefs();


} // namespace fx::script

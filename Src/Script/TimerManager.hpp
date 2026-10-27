#pragma once

#include "TimerQueue.hpp"

#include <Core/Types.hpp>
#include <functional>

namespace fx {

namespace script {
class Script;
}

class TimerManager : public TimerQueue
{
public:
	TimerID After(float32 seconds, std::function<void()> callback);
	TimerID Every(float32 seconds, std::function<void()> callback);

	TimerID StartScript(script::Script* owner, float32 seconds, int64 fires, const char* fn_name);

	void Dump() const;
};

} // namespace fx

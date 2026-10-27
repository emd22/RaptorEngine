#include "TimerManager.hpp"

#include <Core/Log.hpp>
#include <Engine.hpp>
#include <Object/Object.hpp>
#include <Object/ObjectManager.hpp>
#include <Script/Script.hpp>
#include <string>

namespace fx {

TimerID TimerManager::After(float32 seconds, std::function<void()> callback)
{
	return Add(Desc { .Interval = seconds, .Fires = 1 }, [callback](TimerID) { callback(); });
}

TimerID TimerManager::Every(float32 seconds, std::function<void()> callback)
{
	return Add(Desc { .Interval = seconds, .Fires = -1 }, [callback](TimerID) { callback(); });
}

TimerID TimerManager::StartScript(script::Script* owner, float32 seconds, int64 fires, const char* fn_name)
{
	if (owner == nullptr) {
		LogError(LC_SCRIPT, "Timer '{}' was started outside of a script", fn_name);
		return TimerID {};
	}

	if (fires == 0 || fires < -1) {
		LogError(LC_SCRIPT, "Timer '{}' must fire at least once", fn_name);
		return TimerID {};
	}

	const script::ScriptFunctionType<void(Object*, int64)> callback = owner->GetFunction<void(Object*, int64)>(fn_name);

	if (callback == nullptr) {
		LogError(LC_SCRIPT, "Timer callback '{}' was not found in '{}'", fn_name, owner->GetPath());
		return TimerID {};
	}

	const ObjectID owner_id = owner->GetOwner();

	const Callback fire = [this, owner, owner_id, callback](TimerID id)
	{
		Object* self = nullptr;

		if (!owner_id.IsNull()) {
			self = gObjectManager->GetObject(owner_id);

			if (self == nullptr) {
				Cancel(id);
				return;
			}
		}

		owner->CallFunctionPtr<void(Object*, int64)>(callback, self, id.Pack());
	};

	return Add(Desc { .Interval = seconds, .Fires = fires, .pOwner = owner, .Label = fn_name }, fire);
}

void TimerManager::Dump() const
{
	const std::vector<Info> timers = List();

	LogInfo(LC_SCRIPT, "{} active timer(s)", timers.size());

	for (const Info& timer : timers) {
		std::string fires = std::to_string(timer.Fires);

		if (timer.Fires < 0) {
			fires = "until cancelled";
		}

		if (timer.pOwner == nullptr) {
			LogInfo(LC_SCRIPT, "  '{}' in {:.2f}s, fires {}", timer.Label, timer.Remaining, fires);
			continue;
		}

		const auto* owner = static_cast<const script::Script*>(timer.pOwner);
		LogInfo(LC_SCRIPT, "  '{}' from '{}' in {:.2f}s, fires {}", timer.Label, owner->GetPath(), timer.Remaining,
				fires);
	}
}

} // namespace fx

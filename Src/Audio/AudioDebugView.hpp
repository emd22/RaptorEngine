#pragma once

#include <Core/Types.hpp>

namespace fx {

class BaseCamera;
class CVarValue;

/// On-screen audio inspector, toggled with `s_debug`: 1 shows the voice, bus and snapshot panel, 2 also marks each
/// positioned voice in the world with its level and occlusion.
class AudioDebugView
{
public:
	void Create();

	bool IsEnabled() const;

	/// Queues the world markers. Call before the world renders, which flushes debug draws.
	void DrawWorld();

	/// Draws the panel and the world labels. Call while drawing HUD text.
	void DrawText(const BaseCamera& camera);

private:
	CVarValue* mpDebugCVar = nullptr;
};

} // namespace fx

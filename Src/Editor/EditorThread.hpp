#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <functional>

namespace fx::editor::thread {

void InitUIThread();

bool IsUIThread();

void SetUIWakeHandler(std::function<void()> handler);

void PostToUI(std::function<void()> task);

void RunOnUIAndWait(std::function<void()> task);

void RunUITasks();

void PostToGame(std::function<void()> task);

uint32 RunGameTasks();

} // namespace fx::editor::thread

#endif

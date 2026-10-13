#pragma once

#ifdef FX_IS_EDITOR

#include <Core/Types.hpp>
#include <functional>
#include <memory>

namespace fx::editor::thread {

class Task
{
public:
	Task() = default;

	template <typename TCallable>
		requires(!std::is_same_v<std::remove_cvref_t<TCallable>, Task>)
	Task(TCallable&& callable)
		: mpCallable(std::make_unique<Callable<std::remove_cvref_t<TCallable>>>(std::forward<TCallable>(callable)))
	{
	}

	Task(Task&& other) = default;
	Task& operator=(Task&& other) = default;

	FX_FORCE_INLINE bool IsValid() const { return mpCallable != nullptr; }

	void operator()() { mpCallable->Invoke(); }

private:
	struct CallableBase
	{
		virtual ~CallableBase() = default;
		virtual void Invoke() = 0;
	};

	template <typename TCallable>
	struct Callable final : CallableBase
	{
		template <typename TArg>
		explicit Callable(TArg&& callable) : Function(std::forward<TArg>(callable))
		{
		}

		void Invoke() override { Function(); }

		TCallable Function;
	};

private:
	std::unique_ptr<CallableBase> mpCallable;
};

void InitUIThread();

bool IsUIThread();

void SetUIWakeHandler(std::function<void()> handler);

void PostToUI(Task task);

void RunOnUIAndWait(Task task);

void RunUITasks();

void PostToGame(Task task);

uint32 RunGameTasks();

} // namespace fx::editor::thread

#endif

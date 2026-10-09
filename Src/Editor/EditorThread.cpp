#include "EditorThread.hpp"

#ifdef FX_IS_EDITOR

#include <Core/TSQueue.hpp>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace fx::editor::thread {

using Task = std::function<void()>;

static constexpr uint32 scMaxQueuedTasks = 8192;

static std::thread::id sUIThreadID;

static TSQueue<Task> sUITasks(scMaxQueuedTasks);
static TSQueue<Task> sGameTasks(scMaxQueuedTasks);

static std::mutex sWakeMutex;
static std::function<void()> sWakeHandler;


static void TakeAll(TSQueue<Task>& queue, std::vector<Task>& out)
{
	auto locked = queue.GetQueue();

	while (locked->GetSize() > 0) {
		out.push_back(locked->PopValue());
	}
}

void InitUIThread() { sUIThreadID = std::this_thread::get_id(); }

bool IsUIThread() { return std::this_thread::get_id() == sUIThreadID; }

void SetUIWakeHandler(std::function<void()> handler)
{
	std::lock_guard lock(sWakeMutex);
	sWakeHandler = std::move(handler);
}

void RunUITasks()
{
	std::vector<Task> batch;
	TakeAll(sUITasks, batch);

	for (Task& task : batch) {
		task();
	}
}

void PostToUI(Task task)
{
	if (IsUIThread()) {
		RunUITasks();
		task();
		return;
	}

	sUITasks.GetQueue()->Push(std::move(task));

	Task wake;

	{
		std::lock_guard lock(sWakeMutex);
		wake = sWakeHandler;
	}

	if (wake != nullptr) {
		wake();
	}
}

void RunOnUIAndWait(Task task)
{
	if (IsUIThread()) {
		RunUITasks();
		task();
		return;
	}

	struct Completion
	{
		std::mutex Mutex;
		std::condition_variable Cond;
		bool bDone = false;
	};

	std::shared_ptr<Completion> completion = std::make_shared<Completion>();

	PostToUI(
		[task = std::move(task), completion]
		{
			task();

			{
				std::lock_guard lock(completion->Mutex);
				completion->bDone = true;
			}

			completion->Cond.notify_all();
		});

	std::unique_lock lock(completion->Mutex);
	completion->Cond.wait(lock, [&completion] { return completion->bDone; });
}

void PostToGame(Task task) { sGameTasks.GetQueue()->Push(std::move(task)); }

uint32 RunGameTasks()
{
	std::vector<Task> batch;
	TakeAll(sGameTasks, batch);

	for (Task& task : batch) {
		task();
	}

	return static_cast<uint32>(batch.size());
}

} // namespace fx::editor::thread

#endif

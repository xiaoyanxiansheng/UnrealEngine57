// Copyright Epic Games, Inc. All Rights Reserved.

#include "Thread.h"

#include "HAL/Event.h"
#include "HAL/Runnable.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Misc/SingleThreadRunnable.h"
#include "PixelStreaming2Trace.h"
#include "TickableTask.h"

namespace UE::PixelStreaming2
{
	static TWeakPtr<FPixelStreamingRunnable> PixelStreamingRunnable;

	/**
	 * The runnable. Handles ticking of all tasks
	 */
	class FPixelStreamingRunnable : public FRunnable, public FSingleThreadRunnable
	{
	public:
		// Begin FRunnable
		virtual bool Init() override
		{
			return true;
		}

		virtual uint32 Run() override
		{
			bIsRunning = true;

			while (bIsRunning)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::Run LoopStart", PixelStreaming2Channel);
				Tick();

				// Sleep 1ms
				FPlatformProcess::Sleep(0.001f);
			}

			return 0;
		}

		virtual void Stop() override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::Stop", PixelStreaming2Channel);
			bIsRunning = false;

			TaskEvent->Trigger();
		}

		virtual void Exit() override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::Exit", PixelStreaming2Channel);
			bIsRunning = false;

			TaskEvent->Trigger();
		}

		virtual FSingleThreadRunnable* GetSingleThreadInterface() override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::GetSingleThreadInterface", PixelStreaming2Channel);
			return this;
		}
		// End FRunnable

		// Begin FSingleThreadRunnable
		virtual void Tick() override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::Tick", PixelStreaming2Channel);
			FScopeLock TaskLock(&TasksMutex);

			const uint64 NowCycles = FPlatformTime::Cycles64();
			const double DeltaMs = FPlatformTime::ToMilliseconds64(NowCycles - LastTickCycles);

			StartTicking();

			for (auto& Task : Tasks)
			{
				// A task may be nulled out due to deletion during our loop. Check for safety
				TSharedPtr<FPixelStreamingTickableTask> PinnedTask = Task.Pin();
				if (PinnedTask)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*PinnedTask->GetName(), PixelStreaming2Channel)
					PinnedTask->Tick(DeltaMs);
				}
			}

			FinishTicking();

			LastTickCycles = NowCycles;
		}
		// End FSingleThreadRunnable

		void StartTicking()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::StartTicking", PixelStreaming2Channel);
			FScopeLock NewTaskLock(&NewTasksMutex);
			for (auto& NewTask : NewTasks)
			{
				Tasks.Add(NewTask);
			}

			NewTasks.Empty();

			bIsTicking = true;
		}

		void FinishTicking()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::FinishTicking", PixelStreaming2Channel);
			if (bNeedsCleanup)
			{
				Tasks.RemoveAll([](const TWeakPtr<FPixelStreamingTickableTask> Entry) { return Entry == nullptr || !Entry.IsValid(); });
				bNeedsCleanup = false;
			}

			bIsTicking = false;

			if (Tasks.Num() == 0)
			{
				// Sleep the thread indefinitely because there are no tasks to tick.
				// Adding a new task will wake the thread
				TaskEvent->Wait();
			}
		}

		FPixelStreamingRunnable()
			: bIsTicking(false)
			, bNeedsCleanup(false)
			, bIsRunning(false)
			, LastTickCycles(FPlatformTime::Cycles64())
		{
		}

		virtual ~FPixelStreamingRunnable() = default;

	private:
		void AddTask(TWeakPtr<FPixelStreamingTickableTask> Task)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::AddTask", PixelStreaming2Channel);
			FScopeLock NewTaskLock(&NewTasksMutex);
			NewTasks.Add(Task);
			// We've added a new task. Wake the thread (if it was sleeping)
			TaskEvent->Trigger();
		}

		void RemoveTask(FPixelStreamingTickableTask* Task)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingRunnable::RemoveTask", PixelStreaming2Channel);
			if (Task == nullptr)
			{
				return;
			}
			
			// Lock TaskLock before NewTaskLock to ensure deadlock does not happen when Tick and StartTicking lock.
			// Locking matches FTickableObjectBase locking in Tickable.cpp
			FScopeLock TaskLock(&TasksMutex);
			FScopeLock NewTaskLock(&NewTasksMutex);

			// Remove from pending list if it hasn't been registered
			NewTasks.RemoveAll([Task](const TWeakPtr<FPixelStreamingTickableTask> Entry) { 
				const TSharedPtr<FPixelStreamingTickableTask> Pin = Entry.Pin();
				return Entry == nullptr || !Pin || Pin.Get() == Task; 
				});

			// During ticking it is not safe to modify the set so null and mark for later
			if (bIsTicking)
			{
				bNeedsCleanup = true;
				for (TWeakPtr<FPixelStreamingTickableTask>& LoopTask : Tasks)
				{
					if (TSharedPtr<FPixelStreamingTickableTask> Pin = LoopTask.Pin(); Pin && Pin.Get() == Task)
					{
						LoopTask = nullptr;
					}
				}
			}
			else
			{
				Tasks.RemoveAll([Task](const TWeakPtr<FPixelStreamingTickableTask> Entry) {
					TSharedPtr<FPixelStreamingTickableTask> Pin = Entry.Pin();
					return Entry == nullptr || !Pin || Pin.Get() == Task;
				});
			}
		}

	private:
		// Allow the FPixelStreamingTickableTask to access the private add and remove tasks
		friend FPixelStreamingTickableTask;

		// New tasks that have not yet been added to the Tasks list*/
		TArray<TWeakPtr<FPixelStreamingTickableTask>> NewTasks;
		// Lock for modifying new list */
		FCriticalSection NewTasksMutex;

		// Tasks to execute every tick
		TArray<TWeakPtr<FPixelStreamingTickableTask>> Tasks;
		// This critical section should be locked during entire tick process
		FCriticalSection TasksMutex;

		// Use this event to signal when we should wake.
		FEventRef TaskEvent;

		// Tasks can removed from any thread so this needs to be thread safe
		std::atomic<bool> bIsTicking;
		// Tasks can removed from any thread so this needs to be thread safe
		std::atomic<bool> bNeedsCleanup;
		// This thread can be stopped from another thread during shutdown so this needs to be thread safe
		std::atomic<bool> bIsRunning;
		uint64			  LastTickCycles;
	};

	FPixelStreamingThread::FPixelStreamingThread()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingThread Constructor", PixelStreaming2Channel)
		Runnable = MakeShared<FPixelStreamingRunnable>();
		PixelStreamingRunnable = Runnable;
		Thread = TSharedPtr<FRunnableThread>(FRunnableThread::Create(Runnable.Get(), TEXT("Pixel Streaming PixelStreaming Thread")));
	}

	FPixelStreamingThread::~FPixelStreamingThread()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingThread Destructor", PixelStreaming2Channel);
		if (Thread)
		{
			Thread->Kill();
			Thread.Reset();
		}

		if (Runnable)
		{
			Runnable->Stop();
			Runnable.Reset();
		}
	}

	/**
	 * ---------- FPixelStreamingTickableTask ---------------
	 */

	void FPixelStreamingTickableTask::Register()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingTickableTask::Register", PixelStreaming2Channel);
		TSharedPtr<FPixelStreamingRunnable> Runnable = PixelStreamingRunnable.Pin();
		if (Runnable)
		{
			Runnable->AddTask(AsWeak());
		}
	}

	void FPixelStreamingTickableTask::Unregister()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("FPixelStreamingTickableTask::Unregister", PixelStreaming2Channel);
		TSharedPtr<FPixelStreamingRunnable> Runnable = PixelStreamingRunnable.Pin();
		if (Runnable)
		{
			Runnable->RemoveTask(this);
		}
	}

} // namespace UE::PixelStreaming2
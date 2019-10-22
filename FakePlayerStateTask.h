#pragma once
#include <skse/GameThreads.h>
#include <skse/PluginAPI.h>
class TESObjectCELL;
class FakePlayerStateTask : TaskDelegate {
private:
	static FakePlayerStateTask* instance;
	static SKSETaskInterface* g_task;
	TESObjectCELL* lastCell;
	bool running = false;
	bool disposed = false;
public:
	static FakePlayerStateTask* InitTask(SKSETaskInterface* st);
	static void TaskLoop(FakePlayerStateTask* task);
	static void StartTask(TESObjectCELL* startingCell);
	static void EndTask();
	virtual void Run();
	virtual void Dispose();
};
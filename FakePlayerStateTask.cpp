#include "FakePlayerStateTask.h"
#include "MenuCloseWatcher.h"
#include "Utils.h"
#include <Common/IMemPool.h>
#include <skse/NiNodes.h>
#include <thread>
using namespace Utils;

IThreadSafeBasicMemPool<FakePlayerStateTask, 1> s_taskPool;
FakePlayerStateTask* FakePlayerStateTask::instance;
SKSETaskInterface* FakePlayerStateTask::g_task;
FakePlayerStateTask* FakePlayerStateTask::InitTask(SKSETaskInterface* st) {
	instance = s_taskPool.Allocate();
	g_task = st;
	return instance;
}

void FakePlayerStateTask::TaskLoop(FakePlayerStateTask* task) {
	std::this_thread::sleep_for(std::chrono::microseconds(500000));
	g_task->AddTask(task);
}

void FakePlayerStateTask::StartTask(TESObjectCELL* startingCell) {
	if (!instance) {
		InitTask(g_task);
	}
	instance->running = true;
	instance->lastCell = startingCell;
	g_task->AddTask(instance);
}

void FakePlayerStateTask::EndTask() {
	if (!instance)
		return;
	instance->running = false;
	s_taskPool.Free(instance);
	instance = nullptr;
}

void FakePlayerStateTask::Run() {
	MenuCloseWatcher* mcw = MenuCloseWatcher::GetInstance();
	PlayerCharacter* player = *g_thePlayer;
	Actor* fakePlayer = mcw->GetFakePlayer();
	if (!player || !player->GetNiNode() || !fakePlayer || !running)
		return;
	if (lastCell != player->parentCell) {
		_MESSAGE("Cell changed.");
		lastCell = player->parentCell;
		if (!fakePlayer->loadedState) {
			mcw->ForceLoadFakePlayer();
		}
	}
	else {
		if (fakePlayer->loadedState && fakePlayer->loadedState->node && fakePlayer->GetNiNode() && Scale(fakePlayer->loadedState->node->m_worldTransform.pos - player->pos) < 500) {
			mcw->HideFakePlayerIfNear();
		}
	}
}

void FakePlayerStateTask::Dispose() {
	if (!running) {
		s_taskPool.Free(instance);
		instance = nullptr;
		return;
	}
	std::thread t = std::thread(&TaskLoop, this);
	t.detach();
}

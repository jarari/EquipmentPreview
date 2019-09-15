#include "CellLoadWatcher.h"
#include "MenuCloseWatcher.h"
#include <skse/PapyrusVM.h>

CellLoadWatcher* CellLoadWatcher::instance = nullptr;

CellLoadWatcher::~CellLoadWatcher() {
}

void CellLoadWatcher::InitHook() {
	if (!instance)
		instance = new CellLoadWatcher();
	((EventDispatcher<TESCellFullyLoadedEvent>*)(0x012E4C30 + 0x120))->AddEventSink(instance);
	_MESSAGE("CellLoadWatcher added");
}

void CellLoadWatcher::RemoveHook() {
	((EventDispatcher<TESCellFullyLoadedEvent>*)(0x012E4C30 + 0x120))->RemoveEventSink(instance);
	_MESSAGE("CellLoadWatcher removed");
	delete(instance);
}

EventResult CellLoadWatcher::ReceiveEvent(TESCellFullyLoadedEvent* evn, EventDispatcher<TESCellFullyLoadedEvent>* src) {
	MenuCloseWatcher::GetInstance()->InitializeFakePlayer();
	RemoveHook();
	return kEvent_Continue;
}

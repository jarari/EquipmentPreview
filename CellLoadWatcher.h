#pragma once
#include <skse/GameEvents.h>

//from libSkyrim
class TESObjectCELL;
struct TESCellFullyLoadedEvent {
	// SendEvent: 00437663 <= 4CDD8E
	TESObjectCELL* cell;
};
template <>
class BSTEventSink <TESCellFullyLoadedEvent> {
public:
	virtual ~BSTEventSink() {}; // todo?
	virtual	EventResult ReceiveEvent(TESCellFullyLoadedEvent* evn, EventDispatcher<TESCellFullyLoadedEvent>* dispatcher) = 0;
};

class CellLoadWatcher : public BSTEventSink<TESCellFullyLoadedEvent> {
protected:
	static CellLoadWatcher* instance;
public:
	virtual ~CellLoadWatcher();

	static void InitHook();

	static void RemoveHook();

	virtual EventResult ReceiveEvent(TESCellFullyLoadedEvent* evn, EventDispatcher<TESCellFullyLoadedEvent>* src) override;
};

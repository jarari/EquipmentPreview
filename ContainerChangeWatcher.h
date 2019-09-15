#pragma once
#include <SKSE/GameEvents.h>
#include <vector>

class Actor;
class TESForm;
struct WatchData {
public:
	WatchData(Actor* t, TESForm* i, bool l) {
		target = t;
		item = i;
		left = l;
	}
	Actor* target;
	TESForm* item;
	bool left;
};
typedef std::vector<WatchData*> WatchList;

class ContainerChangeWatcher : public BSTEventSink<TESContainerChangedEvent> {
protected:
	int watchCount = 0;
	bool watching = false;
	WatchList watchList;
	static ContainerChangeWatcher* instance;
public:
	ContainerChangeWatcher() {
		if (instance)
			delete(instance);
		instance = this;
		_MESSAGE("ContainerChangeWatcher instance created.");
	}

	static ContainerChangeWatcher* GetInstance() {
		return instance;
	}
	virtual ~ContainerChangeWatcher();

	static void InitHook(WatchData* data);

	static void RemoveHook();

	virtual EventResult ReceiveEvent(TESContainerChangedEvent* evn, EventDispatcher<TESContainerChangedEvent>* src) override;
};

#include "ContainerChangeWatcher.h"
#include "InputWatcher.h"
#include "MenuCloseWatcher.h"
#include "Utils.h"
#include <skse/GameData.h>
#include <skse/GameRTTI.h>
using namespace Utils;
ContainerChangeWatcher* ContainerChangeWatcher::instance = nullptr;

ContainerChangeWatcher::~ContainerChangeWatcher() {
}

void ContainerChangeWatcher::InitHook(WatchData data) {
	if (!instance)
		instance = new ContainerChangeWatcher();
	if (!instance->watching) {
		g_containerChangedEventDispatcher->AddEventSink(instance);
		instance->watching = true;
	}
	instance->watchCount++;
	instance->watchList.push_back(data);
}

void ContainerChangeWatcher::RemoveHook() {
	if (!instance)
		return;
	g_containerChangedEventDispatcher->RemoveEventSink(instance);
	instance->watching = false;
	instance->watchCount = 0;
	instance->watchList.clear();
	Actor* fakePlayer = MenuCloseWatcher::GetInstance()->GetFakePlayer();
}

EventResult ContainerChangeWatcher::ReceiveEvent(TESContainerChangedEvent* evn, EventDispatcher<TESContainerChangedEvent>* src) {
	Actor* target = *(Actor**)((UInt32)evn + 0x30);
	Actor* fakePlayer = MenuCloseWatcher::GetInstance()->GetFakePlayer();
	if (target && (target == fakePlayer || target == *g_thePlayer)) {
		TESForm* item = LookupFormByID(evn->itemFormId);
		bool watch = false;
		for (WatchList::iterator it = watchList.begin(); it != watchList.end(); ++it) {
			WatchData data = *it;
			if (data.first == target && data.second == item) {
				watch = true;
				watchList.erase(it);
				break;
			}
		}
		if (watch) {
			EquipManager* em = EquipManager::GetSingleton();
			InventoryEntryData::EquipData state;
			ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(target->extraData.GetByType(0x15));
			if (containerChanges) {
				containerChanges->data->GetEquipItemData(state, item, item->formID);
			}
			BGSEquipType* equipType = DYNAMIC_CAST(item, TESForm, BGSEquipType);
			BGSEquipSlot* equipSlot = NULL;
			if (equipType)
				equipSlot = equipType->GetEquipSlot();
			CALL_MEMBER_FN(em, EquipItem)(target, item, state.itemExtraList, 1, equipSlot, false, false, false, NULL);
			InputWatcher::GetInstance()->shouldUpdate = true;
			watchCount--;
			if (watchCount == 0) {
				RemoveHook();
			}
		}
	}
	return kEvent_Continue;
}

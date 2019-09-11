#pragma once
#include <SKSE/GameEvents.h>
#include <skse/GameExtraData.h>
#include <skse/GameReferences.h>
#include <skse/GameThreads.h>
#include <SKSE/PluginAPI.h>

typedef void (*_Delete_Native)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* target);
extern const _Delete_Native Delete_Native;

typedef void (*_Disable_Native)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* target, bool fadeOut);
extern const _Disable_Native Disable_Native;

typedef void (*_Enable_Native)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* target, bool fadeIn);
extern const _Enable_Native Enable_Native;

typedef void (*_SetDontMove_Native)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* target, bool dontMove);
extern const _SetDontMove_Native SetDontMove_Native;

typedef void (*_SetScale_Native)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* target, float scale);
extern const _SetScale_Native SetScale_Native;

/*class WaitAddItemTask : public TaskDelegate {
private:
	ExtraContainerChanges* extraContainerChanges;
	TESForm* item;
	TESContainer* container;
	BaseExtraList* extraList;
	BGSEquipSlot* slot;
	int itemCount = 0;
public:
	static WaitAddItemTask* Create(ExtraContainerChanges* ecc, TESForm* i, TESContainer* c, BaseExtraList* el, BGSEquipSlot* s);
	virtual void Run();
	virtual void Dispose();
};*/

class ActorEx : public Actor {
public:
	MEMBER_FN_PREFIX(ActorEx);
	DEFINE_MEMBER_FN(GetScale, float, 0x004D5230);
};
class ActorProcessManagerEx : public ActorProcessManager {
public:
	MEMBER_FN_PREFIX(ActorProcessManagerEx);
	DEFINE_MEMBER_FN(SetAlphaValue, void, 0x0071F9B0, float alpha);
	DEFINE_MEMBER_FN(UpdateAnimationChannel, void, 0x007022A0, Actor* owner); //libSkyrim
	DEFINE_MEMBER_FN(UpdateEquipment, void, 0x007031A0, Actor* actor); //libSkyrim
};

class MenuCloseWatcher : public BSTEventSink<MenuOpenCloseEvent> {
protected:
	static MenuCloseWatcher* instance;
public:
	MenuCloseWatcher() {
		if (instance)
			delete(instance);
		instance = this;
		_MESSAGE("MenuCloseWatcher instance created.");
	}

	static MenuCloseWatcher* GetInstance() {
		return instance;
	}
	virtual ~MenuCloseWatcher();

	void UnequipAll(Actor* dest);

	void SyncEquipments(Actor* dest, Actor* src);

	void CheckBefore3DUpdate(InventoryEntryData* objDesc);
	
	void PreviewEquipment();

	static void InitHook(const SKSEInterface* skse);

	static void ResetHook();

	static void Save(SKSESerializationInterface* si);

	static void Load(SKSESerializationInterface* si);

	Actor* GetFakePlayer();

	virtual EventResult ReceiveEvent(MenuOpenCloseEvent* evn, EventDispatcher<MenuOpenCloseEvent>* src) override;
};

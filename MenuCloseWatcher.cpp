#include "ContainerChangeWatcher.h"
#include "MenuCloseWatcher.h"
#include "InputWatcher.h"
#include "Utils.h"
#define _USE_MATH_DEFINES
#include <SKSE/GameMenus.h>
#include <skse/PapyrusSpawnerTask.cpp>
#include <skse/GameData.h>
#include <skse/GameRTTI.h>
#include <skse/SafeWrite.h>
#include <common/IMemPool.h>
#include <unordered_set>
#include <math.h>
#include <inttypes.h>
#include <thread>
#include <TlHelp32.h>
using namespace Utils;

const _Delete_Native Delete_Native = (_Delete_Native)0x009084A0;
const _Disable_Native Disable_Native = (_Disable_Native)0x00908790;
const _Enable_Native Enable_Native = (_Enable_Native)0x00908AC0;
const _SetDontMove_Native SetDontMove_Native = (_SetDontMove_Native)0x008DAD20;
const _SetScale_Native SetScale_Native = (_SetScale_Native)0x0090A210;
const SKSETaskInterface* g_task;
const SKSESerializationInterface* g_serialization;

MenuCloseWatcher* MenuCloseWatcher::instance = nullptr;

Actor* fakePlayer = nullptr;
UInt64 fakePlayerHandle;
TESCameraState* oldState = nullptr;
float oldFadeOutLim;
float oldCamOffsetX;
float oldCamOffsetY;
float oldCamOffsetZ;
float oldCamPitch;
float oldCamZoom;
float oldPlayerPitch;
float camYaw;
UInt32 stack = 0;
bool activated = false;
typedef std::pair<TESForm*, int> ItemCount;
typedef std::vector<ItemCount> SimpleInventory;
typedef std::vector<ItemCount>::iterator SimpleInventoryCounter;
SimpleInventory storedItemList;

#pragma region HDTHook

DWORD GetHDTBaseAddr() {
	DWORD pid = GetCurrentProcessId();
	DWORD base = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
	if (snapshot != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 moduleEntry;
		moduleEntry.dwSize = sizeof(MODULEENTRY32);
		if (Module32First(snapshot, &moduleEntry)) {
			while (Module32Next(snapshot, &moduleEntry)) {
				if (strcmp("hdtPhysicsExtensions.dll", moduleEntry.szModule) == 0) {
					base = (DWORD)moduleEntry.modBaseAddr;
				}
			}
		}
		CloseHandle(snapshot);
	}
	return base;
}

class Lockable {
public:
	Lockable() { InitializeCriticalSectionAndSpinCount(&cs, 4000); }
	~Lockable() { DeleteCriticalSection(&cs); }

	void lock() { EnterCriticalSection(&cs); }
	void unlock() { LeaveCriticalSection(&cs); }

	class Locker {
	public:
		Locker(Lockable* obj) :object(obj) { object->lock(); }
		~Locker() { if (object)object->unlock(); }

		Locker(Locker&& rhs) :object(rhs.object) { rhs.object = 0; }
		void operator =(Locker&& rhs) { std::swap(object, rhs.object); }

	private:
		Locker(const Locker&);
		void operator =(const Locker&);

		Lockable* object;
	};

	Locker SmartLock() { return this; }
private:

	Lockable(const Lockable&) {}
	Lockable& operator =(const Lockable&) {}

	CRITICAL_SECTION cs;
};

class CWorld {
public:
	Lockable m_lock;							//0x0
	std::unordered_set<UINT32> m_threadInit;	//0x20 I think?
	UInt32 m_wind;								//0x28
	UInt32 m_pWorld;							//0x2C
	UInt32 m_pMemoryRouter;						//0x30
	UInt32 m_collisionFilter;					//0x34
	UInt32 m_jobQueue;							//0x38
	UInt32 m_jobThreadPool;						//0x3C
	float m_timeLastUpdate;						//0x40
	bool m_useSeperatedClock;					//0x44
};

static const float* timeStamp = (float*)0x12E355C;
class FreezeEventHandler : public BSTEventSink<MenuOpenCloseEvent> {
public:
	typedef EventResult(FreezeEventHandler::* FnReceiveEvent)(MenuOpenCloseEvent* evn, BSTEventSource<MenuOpenCloseEvent>* src);

	static FnReceiveEvent originalFunc;
	
	static UInt32 addr_cworld;

	UInt32 GetVPtr() const {
		return *(UInt32*)this;
	}

	EventResult ReceiveEvent_Hook(MenuOpenCloseEvent* evn, BSTEventSource<MenuOpenCloseEvent>* src) {
		UIStringHolder* uistr = UIStringHolder::GetSingleton();
		if (evn->opening) {
			if (evn->menuName == uistr->inventoryMenu || evn->menuName == uistr->containerMenu) {
				/*g_cworld->m_lock.lock();
				g_cworld->m_useSeperatedClock = true;
				g_cworld->m_timeLastUpdate = clock() * 0.001;
				g_cworld->m_lock.unlock();*/
				SafeWrite8(addr_cworld + 0x44, 1);
				SafeWrite32(addr_cworld + 0x40, clock() * 0.001);
				return kEvent_Continue;
			}
		}
		else {
			if (evn->menuName == uistr->inventoryMenu || evn->menuName == uistr->containerMenu) {
				/*g_cworld->m_lock.lock();
				g_cworld->m_useSeperatedClock = false;
				g_cworld->m_timeLastUpdate = *timeStamp;
				g_cworld->m_lock.unlock();*/
				SafeWrite8(addr_cworld + 0x44, 0);
				SafeWrite32(addr_cworld + 0x40, *timeStamp);
				return kEvent_Continue;
			}
		}

		return (originalFunc) ? (this->*originalFunc)(evn, src) : kEvent_Continue;
	}

	void InstallHook() {
		UInt32 vptr = GetVPtr();
		addr_cworld = (UInt32)GetHDTBaseAddr() + 0x9CBC18; //Latest HDT PE. HydrogensaysHDT is gone for long so it's unlikely to change..
		originalFunc = *(FnReceiveEvent*)(vptr + 4);
		_MESSAGE("hdt baseaddr 0x%08x, addr_cworld 0x%08x", (UInt32)GetHDTBaseAddr(), addr_cworld);
		SafeWrite32(vptr + 4, GetFnAddr(&FreezeEventHandler::ReceiveEvent_Hook));
	}
};
FreezeEventHandler::FnReceiveEvent FreezeEventHandler::originalFunc = NULL;
UInt32 FreezeEventHandler::addr_cworld;

class MenuOpenCloseEventSource : public EventDispatcher<MenuOpenCloseEvent> {
public:
	void ProcessHook() {
		lock.Lock();

		BSTEventSink<MenuOpenCloseEvent>* sink;
		UInt32 idx = 0;
		while (eventSinks.GetNthItem(idx, sink)) {
			const char* className = GetObjectClassName(sink);
			if (strcmp(className, "FreezeEventHandler@@") == 0) {
				FreezeEventHandler* freezeEventHandler = static_cast<FreezeEventHandler*>(sink);
				freezeEventHandler->InstallHook();
				_MESSAGE("HDT FreezeEvent hooked");
			}

			++idx;
		}

		lock.Release();
	}

	static void InitHook() {
		MenuManager* mm = MenuManager::GetSingleton();
		if (mm) {
			MenuOpenCloseEventSource* pThis = static_cast<MenuOpenCloseEventSource*>(mm->MenuOpenCloseEventDispatcher());
			pThis->ProcessHook();
		}
	}
};

#pragma endregion

MenuCloseWatcher::~MenuCloseWatcher() {
}

void SetActorAlpha(Actor* a, float alpha) {
	CALL_MEMBER_FN((ActorProcessManagerEx*)a->processManager, SetAlphaValue)(alpha + 2.0f);
	typedef void (Actor::*UnkFunc)();
	UnkFunc fn = *(UnkFunc*)(*(UInt32*)a + 0x37C);
	(a->*fn)();
}

bool CanDualWield(Actor* a, TESForm* item, BGSEquipSlot* equipSlot) {
	return item->IsWeapon() && (equipSlot == GetLeftHandSlot() || equipSlot == GetRightHandSlot()) &&
		(a->race->data.raceFlags & TESRace::kRace_CanDualWield);
}

void CountItems(Actor* a) {
	storedItemList.clear();
	TESContainer* container = DYNAMIC_CAST(a->baseForm, TESForm, TESContainer);
	ExtraContainerChanges* pXContainerChanges = static_cast<ExtraContainerChanges*>(a->extraData.GetByType(kExtraData_ContainerChanges));
	EntryDataList* objList = pXContainerChanges->data->objList;
	EntryDataList::Iterator it = objList->Begin();
	while (!it.End()) {
		InventoryEntryData* extraData = it.Get();
		if (extraData) {
			TESForm* item = extraData->type;
			int baseCount = 0;
			if (container)
				baseCount = container->CountItem(item);
			storedItemList.push_back(std::pair<TESForm*, int>(item, baseCount + extraData->countDelta));
		}
		++it;
	}
}

void MenuCloseWatcher::UnequipAll(Actor* dest) {
	EquipManager* em = EquipManager::GetSingleton();
	TESContainer* destcontainer = DYNAMIC_CAST(dest->baseForm, TESForm, TESContainer);
	ExtraContainerChanges* pXContainerChanges = static_cast<ExtraContainerChanges*>(dest->extraData.GetByType(kExtraData_ContainerChanges));
	EntryDataList* objList = pXContainerChanges->data->objList;
	EntryDataList::Iterator it = objList->Begin();
	while (!it.End()) {
		InventoryEntryData* extraData = it.Get();
		if (extraData) {
			TESForm* item = extraData->type;
			int baseCount = 0;
			if (destcontainer)
				baseCount = destcontainer->CountItem(item);
			InventoryEntryData::EquipData state;
			extraData->GetEquipItemData(state, item->formID, baseCount);
			BGSEquipType* equipType = DYNAMIC_CAST(item, TESForm, BGSEquipType);
			BGSEquipSlot* equipSlot = NULL;
			if (equipType)
				equipSlot = equipType->GetEquipSlot();
			if (state.isTypeWorn || state.isItemWorn) {
				CALL_MEMBER_FN(em, UnequipItem)(dest, item, state.wornExtraList, 1, equipSlot, false, false, true, false, NULL);
			}
			if (state.isTypeWornLeft || state.isItemWornLeft) {
				CALL_MEMBER_FN(em, UnequipItem)(dest, item, state.wornLeftExtraList, 1, equipSlot, false, false, true, false, NULL);
			}
		}
		++it;
	}
	InputWatcher::GetInstance()->shouldUpdate = true;
}

void MenuCloseWatcher::SyncEquipments(Actor* dest, Actor* src) {
	EquipManager* em = EquipManager::GetSingleton();
	SimpleInventory templist = storedItemList;
	UnequipAll(dest);
	CountItems(dest);
	TESContainer* srccontainer = DYNAMIC_CAST(src->baseForm, TESForm, TESContainer);
	ExtraContainerChanges* pXContainerChanges = static_cast<ExtraContainerChanges*>(src->extraData.GetByType(kExtraData_ContainerChanges));
	EntryDataList* objList = pXContainerChanges->data->objList;
	EntryDataList::Iterator it = objList->Begin();
	while (!it.End()) {
		InventoryEntryData* extraData = it.Get();
		if (extraData) {
			TESForm* item = extraData->type;
			int baseCount = 0;
			if (srccontainer)
				baseCount = srccontainer->CountItem(item);
			InventoryEntryData::EquipData state;
			extraData->GetEquipItemData(state, item->formID, baseCount);
			BGSEquipType* equipType = DYNAMIC_CAST(item, TESForm, BGSEquipType);
			BGSEquipSlot* equipSlot = NULL;
			if(equipType)
				equipSlot = equipType->GetEquipSlot();
			if (state.isTypeWornLeft || state.isItemWornLeft) {
				equipSlot = GetLeftHandSlot();
			}
			if (state.isTypeWorn || state.isTypeWornLeft || state.isItemWorn || state.isItemWornLeft) {
				bool canDualWield = CanDualWield(dest, item, equipSlot) && dest == fakePlayer;
				int itemCount = 0;
				size_t index = -1;
				for (SimpleInventoryCounter sc = storedItemList.begin(); sc != storedItemList.end(); ++sc) {
					ItemCount ic = *sc;
					index++;
					if (ic.first == item) {
						itemCount = ic.second;
						break;
					}
				}
				if (itemCount == 0 || (canDualWield && itemCount <= 1)) {
					int addCount = itemCount + 1;
					storedItemList.at(index).second += addCount;
					ContainerChangeWatcher::InitHook(WatchData(dest, item));
					VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
					AddItem_Native(registry, stack, dest, item, addCount, true);
				}
				else {
					CALL_MEMBER_FN(em, EquipItem)(dest, item, state.itemExtraList, 1, equipSlot, false, false, false, NULL);
				}
			}
		}
		++it;
	}
	if (dest != fakePlayer)
		storedItemList = templist;
	InputWatcher::GetInstance()->shouldUpdate = true;
}

/*IThreadSafeBasicMemPool<WaitAddItemTask, 32> s_WaitAddItemTaskPool;

void TaskLoop(WaitAddItemTask* task) {
	std::this_thread::sleep_for(std::chrono::microseconds(3000));
	g_task->AddTask(task);
}

WaitAddItemTask* WaitAddItemTask::Create(ExtraContainerChanges* ecc, TESForm* i, TESContainer* c, BaseExtraList* el, BGSEquipSlot* s) {
	WaitAddItemTask* cmd = s_WaitAddItemTaskPool.Allocate();
	if (cmd) {
		cmd->extraContainerChanges = ecc;
		cmd->item = i;
		cmd->container = c;
		cmd->extraList = el;
		cmd->slot = s;
	}
	return cmd;
}

void WaitAddItemTask::Run() {
	InventoryEntryData::EquipData state;
	extraContainerChanges->data->GetEquipItemData(state, item, item->formID);
	itemCount = 0;
	for (SimpleInventoryCounter it = storedItemList.begin(); it != storedItemList.end(); ++it) {
		ItemCount c = *it;
		if (c.first == item) {
			itemCount = c.second;
			it = storedItemList.end();
		}
	}
	if (itemCount == 0) {
		std::thread t = std::thread(&TaskLoop, this);
		t.detach();
		return;
	}
	EquipManager* em = EquipManager::GetSingleton();
	CALL_MEMBER_FN(em, EquipItem)(fakePlayer, item, state.itemExtraList, 1, slot, false, false, false, NULL);
	CALL_MEMBER_FN(fakePlayer, QueueNiNodeUpdate)(false);
	CALL_MEMBER_FN((ActorProcessManagerEx*)fakePlayer->processManager, UpdateEquipment)(fakePlayer);
	CALL_MEMBER_FN((ActorProcessManagerEx*)fakePlayer->processManager, UpdateAnimationChannel)(fakePlayer);
}

void WaitAddItemTask::Dispose() {
	if (extraContainerChanges && itemCount == 0)
		return;
	s_WaitAddItemTaskPool.Free(this);
}*/

void PositionFakePlayer(NiPoint3 offset = NiPoint3()) {
	PlayerCharacter* player = *g_thePlayer;
	TESObjectCELL* parentCell = player->parentCell;
	TESWorldSpace* worldspace = CALL_MEMBER_FN(player, GetWorldspace)();
	NiPoint3 finalPos = player->pos + offset;
	UInt32 nullHandle = *g_invalidRefHandle;
	float yaw = camYaw + M_PI;
	MoveRefrToPosition(fakePlayer, &nullHandle, parentCell, worldspace, &finalPos, &NiPoint3(0, 0, yaw));
	fakePlayer->animGraphHolder.SendAnimationEvent("IdleStudy");
	fakePlayer->rot.z = yaw;
}

void CreateFakePlayer() {
	VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
	PlayerCharacter* player = *g_thePlayer;
	fakePlayer = (Actor*)PlaceAtMe_Native(registry, stack, player, player->baseForm, 1, false, false);
	SetDontMove_Native(registry, stack, fakePlayer, true);
	IObjectHandlePolicy* policy = registry->GetHandlePolicy();
	fakePlayerHandle = policy->Create(kFormType_Character, fakePlayer);
	policy->AddRef(fakePlayerHandle);
	CountItems(fakePlayer);
	_MESSAGE("New fake player.");
}

void ShowFakePlayer() {
	if (!fakePlayer)
		return;
	VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
	//Enable_Native(registry, stack, fakePlayer, true);
	fakePlayer->flags1 |= Actor::kFlags_AIEnabled;
	SetActorAlpha(fakePlayer, 1);
	SetDontMove_Native(registry, stack, *g_thePlayer, true);
}

void HideFakePlayer() {
	if (!fakePlayer)
		return;
	VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
	fakePlayer->flags1 ^= Actor::kFlags_AIEnabled;
	fakePlayer->flags2 ^= Actor::kFlags_CanDoFavor;
	PositionFakePlayer(NiPoint3(0, 0, -10000));
	SetActorAlpha(fakePlayer, 0);
	SetDontMove_Native(registry, stack, *g_thePlayer, false);
}

void DeleteFakePlayer() {
	if (!fakePlayer)
		return;
	VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
	Disable_Native(registry, stack, fakePlayer, false);
	Delete_Native(registry, 0, fakePlayer);
	fakePlayer = nullptr;
}

void ScaleFakePlayer() {
	VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
	float scale = CALL_MEMBER_FN((ActorEx*)* g_thePlayer, GetScale)();
	SetScale_Native(registry, stack, fakePlayer, scale);
	InputWatcher::GetInstance()->AdjustToScale(scale);
}

void SetupCamera() {
	PlayerCharacter* player = *g_thePlayer;
	PlayerCamera* pCam = PlayerCamera::GetSingleton();
	ThirdPersonState* pCamState = (ThirdPersonState*)pCam->cameraStates[PlayerCamera::kCameraState_ThirdPerson2];
	oldState = pCam->cameraState;
	if(oldState != pCam->cameraStates[PlayerCamera::kCameraState_ThirdPerson2])
		CALL_MEMBER_FN(pCam, SetCameraState)(pCam->cameraStates[PlayerCamera::kCameraState_ThirdPerson2]);
	
	NiPoint3 camAng = GetEulerAngles(pCam->cameraNode->m_localTransform.rot);
	camYaw = -camAng.z;
	oldCamPitch = *(float*)((UInt32)pCamState + 0xB0);
	oldCamOffsetX = *(float*)((UInt32)pCamState + 0x3C);
	oldCamOffsetY = *(float*)((UInt32)pCamState + 0x40);
	oldCamOffsetZ = *(float*)((UInt32)pCamState + 0x44);
	oldCamZoom = *(float*)((UInt32)pCamState + 0x54);
	*(float*)((UInt32)pCamState + 0xB0) = -0.1f; //base pitch
	*(float*)((UInt32)pCamState + 0x54) = 0; //base zoom
	*(UInt8*)((UInt32)pCamState + 0xB4) = 1; //isStanding? anyway it disables the yaw control, so we set it to 1.
	oldFadeOutLim = GetINISetting("fActorFadeOutLimit:Camera")->data.f32;
	GetINISetting("fActorFadeOutLimit:Camera")->data.f32 = -5000;
	oldPlayerPitch = player->rot.x;
	player->rot.x = 0;
}

void RevertCamera() {
	PlayerCharacter* player = *g_thePlayer;
	PlayerCamera* pCam = PlayerCamera::GetSingleton();
	ThirdPersonState* pCamState = (ThirdPersonState*)pCam->cameraStates[PlayerCamera::kCameraState_ThirdPerson2];
	if (oldState != pCam->cameraStates[PlayerCamera::kCameraState_ThirdPerson2]) {
		CALL_MEMBER_FN(pCam, SetCameraState)(oldState);
	}
	*(float*)((UInt32)pCamState + 0xB0) = oldCamPitch;
	*(float*)((UInt32)pCamState + 0x3C) = oldCamOffsetX;
	*(float*)((UInt32)pCamState + 0x40) = oldCamOffsetY;
	*(float*)((UInt32)pCamState + 0x44) = oldCamOffsetZ;
	*(float*)((UInt32)pCamState + 0x54) = oldCamZoom;
	oldState = nullptr;
	GetINISetting("fActorFadeOutLimit:Camera")->data.f32 = oldFadeOutLim;
	player->rot.x = oldPlayerPitch;
}

InventoryEntryData* currentItem;
void MenuCloseWatcher::CheckBefore3DUpdate(InventoryEntryData* objDesc) {
	switch (objDesc->type->GetFormType()) {
		case kFormType_Armor:
		case kFormType_Ammo:
			currentItem = objDesc;
			CALL_MEMBER_FN(Inventory3DManager::GetSingleton(), Clear3D)();
			return;
	}
	CALL_MEMBER_FN(Inventory3DManager::GetSingleton(), UpdateItem3D)(objDesc);
}

void MenuCloseWatcher::PreviewEquipment() {
	if (!currentItem)
		return;
	TESForm* item = currentItem->type;
	TESContainer* container = DYNAMIC_CAST(fakePlayer, TESForm, TESContainer);
	int baseCount = 0;
	if (container)
		baseCount = container->CountItem(item);
	EquipManager* em = EquipManager::GetSingleton();
	InventoryEntryData::EquipData state;
	ExtraContainerChanges* containerChanges = static_cast<ExtraContainerChanges*>(fakePlayer->extraData.GetByType(0x15));
	if (containerChanges) {
		containerChanges->data->GetEquipItemData(state, item, item->formID);
	}
	BGSEquipType* equipType = DYNAMIC_CAST(item, TESForm, BGSEquipType);
	BGSEquipSlot* equipSlot = NULL;
	if (equipType)
		equipSlot = equipType->GetEquipSlot();
	if (state.isItemWorn || state.isItemWornLeft || state.isTypeWorn || state.isTypeWornLeft) {
		if (state.isTypeWorn || state.isItemWorn) {
			CALL_MEMBER_FN(em, UnequipItem)(fakePlayer, item, state.wornExtraList, 1, equipSlot, false, false, true, false, NULL);
		}
		if (state.isTypeWornLeft || state.isItemWornLeft) {
			CALL_MEMBER_FN(em, UnequipItem)(fakePlayer, item, state.wornLeftExtraList, 1, equipSlot, false, false, true, false, NULL);
		}
		InputWatcher::GetInstance()->shouldUpdate = true;
	}
	else {
		int itemCount = 0;
		size_t index = -1;
		for (SimpleInventoryCounter sc = storedItemList.begin(); sc != storedItemList.end(); ++sc) {
			ItemCount ic = *sc;
			index++;
			if (ic.first == item) {
				itemCount = ic.second;
				break;
			}
		}
		if (itemCount == 0) {
			storedItemList.at(index).second += 1;
			ContainerChangeWatcher::InitHook(WatchData(fakePlayer, item));
			VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
			AddItem_Native(registry, stack, fakePlayer, item, 1, true);
		}
		else {
			CALL_MEMBER_FN(em, EquipItem)(fakePlayer, item, state.itemExtraList, 1, equipSlot, false, false, false, NULL);
			InputWatcher::GetInstance()->shouldUpdate = true;
		}
	}
}

void MenuCloseWatcher::InitHook(const SKSEInterface* skse) {
	if (instance)
		delete(instance);
	g_task = (SKSETaskInterface*)skse->QueryInterface(kInterface_Task);
	g_serialization = (SKSESerializationInterface*)skse->QueryInterface(kInterface_Serialization);
	instance = new MenuCloseWatcher();
	MenuManager* mm = MenuManager::GetSingleton();
	if (mm) {
		mm->MenuOpenCloseEventDispatcher()->AddEventSink((BSTEventSink<MenuOpenCloseEvent>*)instance);
		_MESSAGE("MenuCloseWatcher added to the sink.");
	}
	g_serialization->SetUniqueID(skse->GetPluginHandle(), 0x14789632);
	g_serialization->SetSaveCallback(skse->GetPluginHandle(), Save);
	g_serialization->SetLoadCallback(skse->GetPluginHandle(), Load);
	WriteRelCall(0x869D00, GetFnAddr(&CheckBefore3DUpdate));    //Inventory
	WriteRelCall(0x8493BE, GetFnAddr(&CheckBefore3DUpdate));    //Container
}

bool hooked = false;
void MenuCloseWatcher::ResetHook() {
	fakePlayer = nullptr;
	oldState = nullptr;
	activated = false;
	storedItemList.clear();
	if (!hooked) {
		MenuOpenCloseEventSource::InitHook();
		hooked = true;
	}
}

const UInt32 kSerializationDataVersion = 1;
void MenuCloseWatcher::Save(SKSESerializationInterface* si) {
	if (si->OpenRecord('EQPV', kSerializationDataVersion)) {
		if (si->WriteRecordData(&fakePlayerHandle, sizeof(fakePlayerHandle))) {
			_MESSAGE("Saved fakePlayer handle");
		}
	}
}

void MenuCloseWatcher::Load(SKSESerializationInterface* si) {
	UInt32 type, version, length;

	while (si->GetNextRecordInfo(&type, &version, &length)) {
		if (type == 'EQPV') {
			UInt64 handle = 0;
			if (si->ReadRecordData(&handle, sizeof(handle))) {
				VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
				IObjectHandlePolicy* policy = registry->GetHandlePolicy();
				fakePlayer = (Actor*)policy->Resolve(kFormType_Character, handle);
				if (fakePlayer) {
					CountItems(fakePlayer);
					_MESSAGE("Loaded fakePlayer from data");
				}
			}
		}
	}
}

Actor* MenuCloseWatcher::GetFakePlayer() {
	return fakePlayer;
}

EventResult MenuCloseWatcher::ReceiveEvent(MenuOpenCloseEvent* evn, EventDispatcher<MenuOpenCloseEvent>* src) {
	UIStringHolder* uistr = UIStringHolder::GetSingleton();
	PlayerCamera* pCam = PlayerCamera::GetSingleton();
	PlayerCharacter* player = *g_thePlayer;
	if (!uistr || !pCam || !player || !player->GetNiNode()) {
		return kEvent_Continue;
	}
	if (evn->menuName == uistr->inventoryMenu ||
		evn->menuName == uistr->containerMenu) {
		if (evn->opening) {
			if (pCam->cameraState != pCam->cameraStates[PlayerCamera::kCameraState_Horse] &&
				pCam->cameraState != pCam->cameraStates[PlayerCamera::kCameraState_Bleedout]) {
				InputWatcher::InitHook();
				SetActorAlpha(player, 0);
				SetupCamera();
				if (!fakePlayer)
					CreateFakePlayer();
				PositionFakePlayer();
				ScaleFakePlayer();
				ShowFakePlayer();
				activated = true;
			}
		}
		else {
			if (activated) {
				SetActorAlpha(player, 1);
				InputWatcher::RemoveHook();
				HideFakePlayer();
				RevertCamera();
				activated = false;
			}
		}
	}
	else if (evn->menuName == uistr->raceSexMenu) {
		if (!evn->opening) {
			DeleteFakePlayer();
			CreateFakePlayer();
			HideFakePlayer();
		}
	}
	return kEvent_Continue;
}
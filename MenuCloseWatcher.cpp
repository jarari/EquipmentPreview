#include "ContainerChangeWatcher.h"
#include "MenuCloseWatcher.h"
#include "InputWatcher.h"
#include "Utils.h"
#include <SKSE/GameMenus.h>
#include <skse/PapyrusSpawnerTask.cpp>
#include <skse/GameData.h>
#include <skse/GameObjects.h>
#include <skse/GameRTTI.h>
#include <skse/NiExtraData.h>
#include <skse/NiGeometry.h>
#include <skse/SafeWrite.h>
#include <common/IMemPool.h>
#define _USE_MATH_DEFINES
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
const _SetPosition_Native SetPosition_Native = (_SetPosition_Native)0x00909E40;
const SKSETaskInterface* g_task;
const SKSESerializationInterface* g_serialization;

MenuCloseWatcher* MenuCloseWatcher::instance = nullptr;

IThreadSafeBasicMemPool<WaitFakePlayerMoveTask, 32> s_WaitMoveTaskPool;
VMClassRegistry* registry;
IObjectHandlePolicy* policy;
Actor* fakePlayer = nullptr;
UInt64 fakePlayerHandle;
TESObjectREFR* light = nullptr;
TESCameraState* oldState = nullptr;
float oldFadeOutLim;
float oldCamOffsetX;
float oldCamOffsetY;
float oldCamOffsetZ;
float oldCamPitch;
float oldCamZoom;
float oldPlayerPitch;
float camYaw;
UInt32 stack = -256;
bool activated = false;
bool lightEnabled = false;
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

struct Info {
	Info() :model(0), system(0) {}
	NiNode* model;
	std::shared_ptr<CSystemObject> system;
};
typedef Bucket<UINT, std::shared_ptr<CCharacter>> CHolder;
typedef Bucket<UINT32, Info> CSHolder;
typedef Bucket<UINT, std::shared_ptr<CSystemObject>> SHolder;
CWorld* g_cworld;
static const float* timeStamp = (float*)0x12E355C;
typedef void (hkpRigidBody::* _setPositionAndRotation)(const hkVector4& position, const hkQuaternion& rotation);
_setPositionAndRotation setPositionAndRotation;
typedef void (*_applyHardKeyFrame)(const hkVector4& nextPosition, const hkVector4& nextOrientation, float invDeltaTime, hkpRigidBody* body);
_applyHardKeyFrame applyHardKeyFrame;
class FreezeEventHandler : public BSTEventSink<MenuOpenCloseEvent> {
public:
	typedef EventResult(FreezeEventHandler::* FnReceiveEvent)(MenuOpenCloseEvent* evn, BSTEventSource<MenuOpenCloseEvent>* src);

	static FnReceiveEvent originalFunc;

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
				return kEvent_Continue;
			}
		}
		else {
			if (evn->menuName == uistr->inventoryMenu || evn->menuName == uistr->containerMenu) {
				/*g_cworld->m_lock.lock();
				g_cworld->m_useSeperatedClock = false;
				g_cworld->m_timeLastUpdate = *timeStamp;
				g_cworld->m_lock.unlock();*/
				return kEvent_Continue;
			}
		}

		return (originalFunc) ? (this->*originalFunc)(evn, src) : kEvent_Continue;
	}

	void InstallHook() {
		UInt32 vptr = GetVPtr();
		g_cworld = (CWorld*)(GetHDTBaseAddr() + 0x9CBC18); //Latest HDT PE. HydrogensaysHDT is gone for long so it's unlikely to change..
		originalFunc = *(FnReceiveEvent*)(vptr + 4);
		_MESSAGE("hdt baseaddr 0x%08x, addr_cworld 0x%08x", GetHDTBaseAddr(), g_cworld);
		SafeWrite32(vptr + 4, GetFnAddr(&FreezeEventHandler::ReceiveEvent_Hook));
		UInt32 setPosnRotfuncAddr = (GetHDTBaseAddr() + 0xB1030);
		setPositionAndRotation = *(_setPositionAndRotation*)& setPosnRotfuncAddr;
		UInt32 applyHardKeyframefuncAddr = (GetHDTBaseAddr() + 0x147B90);
		applyHardKeyFrame = (_applyHardKeyFrame)applyHardKeyframefuncAddr;
	}
};
FreezeEventHandler::FnReceiveEvent FreezeEventHandler::originalFunc = NULL;

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

void CWorld::RepositionHDT(Actor* a) {
	if (!a || !a->GetNiNode() || !a->loadedState || !a->loadedState->node)
		return;
	EnterCriticalSection((LPCRITICAL_SECTION)((UInt32)g_cworld + 0x8));
	CHolder* bucket = (*(CHolder * *)((UInt32)this + 0x48));
	CHolder* end = bucket;
	bucket = bucket->next;
	CHolder* head = bucket;
	size_t size = *(size_t*)((UInt32)this + 0x4C);
	bool characterExists = false;
	for (int i = 0; i < size; i++) {
		if (bucket->key == a->formID) {
			characterExists = true;
			break;
		}
		bucket = bucket->next;
	}
	if (characterExists) {
		CCharacter* c = bucket->value.get();
		EnterCriticalSection((LPCRITICAL_SECTION)((UInt32)c + 0x4));
		/*if (c->CreateIfValid()) {
			c->RemoveFromWorld();
			
			c->AddToWorld(g_cworld->m_pWorld);
		}*/
		c->RemoveFromWorld();
		CSHolder* s_bucket = (*(CSHolder * *)((UInt32)c + 0x20))->next;
		CSHolder* s_head = s_bucket;
		size_t s_size = *(int*)((UInt32)c + 0x24);
		for (int i = 0; i < s_size; i++) {
			CSystemObject* so = s_bucket->value.system.get();
			hkpPhysicsSystem* sys = so->m_system;
			/*if (so->CreateIfValid()) {
				so->RemoveFromWorld();
				
				so->AddToWorld(g_cworld->m_pWorld);
			}*/
			so->RemoveFromWorld();
			for (int j = 0; j < sys->rigidBodyCount; j++) {
				if (!so->m_bones[j])
					continue;
				hkpRigidBody* rb = sys->m_rigidBodies[j];
				if (!rb || *(UInt8*)((UInt32)rb + 0xE8) == 0x5)
					continue;
				hkMotionState* ms = *(hkMotionState * *)((UInt32)rb + 0x18);
				if (ms) {
					(rb->*setPositionAndRotation)(ms->m_sweptTransform.m_centerOfMass0, ms->m_sweptTransform.m_rotation0);
					applyHardKeyFrame(hkVector4(), hkVector4(), 1000, rb);
				}
			}
			so->ReadFromWorld();
			s_bucket = s_bucket->next;
		}
		c->ReadFromWorld();
		/*CHolder* prev = bucket->previous;
		CHolder* next = bucket->next;
		prev->next = next;
		next->previous = prev;
		*(size_t*)((UInt32)this + 0x4C) = size - 1;
		UInt32 hashend = **(UInt32 * *)((UInt32)this + 0x54);
		UInt32* hashlist = *(UInt32 * *)((UInt32)this + 0x50);
		int i = 0;
		while (hashlist[i] != hashend) {
			if (hashlist[i] == (UInt32)bucket) {
				hashlist[i] = (UInt32)end;
			}
			++i;
		}*/
		_MESSAGE("Deleted character from HDT list");
		LeaveCriticalSection((LPCRITICAL_SECTION)((UInt32)c + 0x4));
	}
	SHolder* s_bucket = (*(SHolder * *)((UInt32)this + 0x68))->next;
	SHolder* s_head = s_bucket;
	size_t s_size = *(size_t*)((UInt32)this + 0x6C);
	for (int i = 0; i < s_size; i++) {
		SHolder* tempbucket = s_bucket;
		CSystemObject* so = s_bucket->value.get();
		/*if (so->CreateIfValid()) {
			so->RemoveFromWorld();
			
			so->AddToWorld(g_cworld->m_pWorld);
		}*/
		so->RemoveFromWorld();
		if (so->m_skeleton == a->loadedState->node) {
			hkpPhysicsSystem* sys = so->m_system;
			for (int j = 0; j < sys->rigidBodyCount; j++) {
				if (!so->m_bones[j])
					continue;
				hkpRigidBody* rb = sys->m_rigidBodies[j];
				if (!rb || *(UInt8*)((UInt32)rb + 0xE8) == 0x5)
					continue;
				hkMotionState* ms = *(hkMotionState * *)((UInt32)rb + 0x18);
				if (ms) {
					(rb->*setPositionAndRotation)(ms->m_sweptTransform.m_centerOfMass0, ms->m_sweptTransform.m_rotation0);
					applyHardKeyFrame(hkVector4(), hkVector4(), 1000, rb);
				}
			}
		}
		so->ReadFromWorld();
		s_bucket = s_bucket->next;
	}
	LeaveCriticalSection((LPCRITICAL_SECTION)((UInt32)g_cworld + 0x8));
}


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
	return item->IsWeapon() && 
		(((equipSlot == GetLeftHandSlot() || equipSlot == GetRightHandSlot()) && (a->race->data.raceFlags & TESRace::kRace_CanDualWield)) ||
			equipSlot == GetEitherHandSlot());
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
			storedItemList.push_back(std::pair<TESForm*, int>(item, extraData->countDelta));
		}
		++it;
	}
}

void MenuCloseWatcher::UnequipAll(Actor* dest) {
	EquipManager* em = EquipManager::GetSingleton();
	ExtraContainerChanges* pXContainerChanges = static_cast<ExtraContainerChanges*>(dest->extraData.GetByType(kExtraData_ContainerChanges));
	EntryDataList* objList = pXContainerChanges->data->objList;
	EntryDataList::Iterator it = objList->Begin();
	while (!it.End()) {
		InventoryEntryData* extraData = it.Get();
		if (extraData) {
			TESForm* item = extraData->type;
			InventoryEntryData::EquipData state;
			extraData->GetEquipItemData(state, item->formID, 0);
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
			if (item->IsWeapon()) {
				if (state.isTypeWornLeft || state.isItemWornLeft) {
					equipSlot = GetLeftHandSlot();
				}
				else {
					equipSlot = GetRightHandSlot();
				}
			}
			if (state.isTypeWorn || state.isTypeWornLeft || state.isItemWorn || state.isItemWornLeft) {
				bool canDualWield = CanDualWield(dest, item, equipSlot) && dest == fakePlayer;
				int itemCount = baseCount;
				size_t index = -1;
				for (SimpleInventoryCounter sc = storedItemList.begin(); sc != storedItemList.end(); ++sc) {
					ItemCount ic = *sc;
					index++;
					if (ic.first == item) {
						itemCount += ic.second;
						break;
					}
				}
				if (itemCount == 0 || (canDualWield && itemCount <= 1)) {
					int addCount = 1;
					if (canDualWield)
						addCount = (2 - itemCount);
					storedItemList.at(index).second += addCount;
					ContainerChangeWatcher::InitHook(new WatchData(dest, item, state.isTypeWornLeft || state.isItemWornLeft));
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

void PlaceLight() {
	PlayerCharacter* player = *g_thePlayer;
	TESObjectLIGH* baselight = DYNAMIC_CAST(LookupFormByID(0x13234), TESForm, TESObjectLIGH);
	light = PlaceAtMe_Native(registry, stack, player, baselight, 1, false, false);
	UInt32 nullHandle = *g_invalidRefHandle;
	NiPoint3 pos, fwd;
	GetHeadPos(player, pos);
	GetCameraForward(PlayerCamera::GetSingleton(), fwd);
	pos -= fwd * 100.0f;
	MoveRefrToPosition(light, &nullHandle, player->parentCell, CALL_MEMBER_FN(player, GetWorldspace)(), &pos, &NiPoint3(0, 0, camYaw));
	lightEnabled = true;
}

void DeleteLight() {
	if (!light)
		return;
	Disable_Native(registry, stack, light, false);
	Delete_Native(registry, stack, light);
	lightEnabled = false;
}

void EnableLight() {
	if (!light)
		return;
	Enable_Native(registry, stack, light, false);
	lightEnabled = true;
}

void DisableLight() {
	if (!light)
		return;
	Disable_Native(registry, stack, light, false);
	lightEnabled = false;
}

void PositionFakePlayer(TESObjectCELL* cell, TESWorldSpace* worldspace, NiPoint3 pos) {
	UInt32 nullHandle = *g_invalidRefHandle;
	float yaw = camYaw + M_PI;
	MoveRefrToPosition(fakePlayer, &nullHandle, cell, worldspace, &pos, &NiPoint3(0, 0, yaw));
	if (fakePlayer->loadedState) {
		fakePlayer->animGraphHolder.SendAnimationEvent("IdleStudy");
	}
}

void CreateFakePlayer() {
	PlayerCharacter* player = *g_thePlayer;
	fakePlayer = (Actor*)PlaceAtMe_Native(registry, stack, player, player->baseForm, 1, false, false);
	SetDontMove_Native(registry, stack, fakePlayer, true);
	fakePlayerHandle = policy->Create(kFormType_Character, fakePlayer);
	policy->AddRef(fakePlayerHandle);
	CountItems(fakePlayer);
	_MESSAGE("New fake player 0x%08x", fakePlayer);
}

void ShowFakePlayer() {
	if (!fakePlayer)
		return;
	PlaceLight();
	PlayerCharacter* player = *g_thePlayer;
	fakePlayer->flags1 |= Actor::kFlags_AIEnabled;
	SetActorAlpha(fakePlayer, 1);
	PositionFakePlayer(player->parentCell, CALL_MEMBER_FN(player, GetWorldspace)(), player->pos);
	if (g_cworld) {
		WaitFakePlayerMoveTask* task = WaitFakePlayerMoveTask::Create(fakePlayer, fakePlayerHandle, NiPoint3(), [](Actor* a, UInt64 h) {
			g_cworld->RepositionHDT(a);
		}, "RepositionHDT");
		if (task)
			g_task->AddTask(task);
	}
}

void HideFakePlayer() {
	if (!fakePlayer)
		return;
	DeleteLight();
	PlayerCharacter* player = *g_thePlayer;
	fakePlayer->flags1 ^= Actor::kFlags_AIEnabled;
	PositionFakePlayer(player->parentCell, CALL_MEMBER_FN(player, GetWorldspace)(), player->pos - NiPoint3(0, 0,  1000));
	SetActorAlpha(fakePlayer, 0);
	SetScale_Native(registry, stack, fakePlayer, 1.0f);
}

void DeleteFakePlayer() {
	if (!fakePlayer)
		return;
	PlayerCharacter* player = *g_thePlayer;
	ShowFakePlayer();
	WaitFakePlayerMoveTask* task = WaitFakePlayerMoveTask::Create(fakePlayer, fakePlayerHandle, player->pos, [](Actor* a, UInt64 h) {
		_MESSAGE ("fakePlayer 0x%08x Deleted", fakePlayer);
		Disable_Native(registry, stack, a, false);
		Delete_Native(registry, stack, a);
		if (h != policy->GetInvalidHandle()) {
			policy->Release(h);
		}
	}, "Delete fakePlayer");
	if (task)
		g_task->AddTask(task);
	fakePlayer = nullptr;
	fakePlayerHandle = policy->GetInvalidHandle();
}

void ScaleFakePlayer() {
	float scale = CALL_MEMBER_FN((ActorEx*)* g_thePlayer, GetScale)();
	SetScale_Native(registry, stack, fakePlayer, scale);
	InputWatcher::GetInstance()->AdjustToScale(scale);
}

void FaceGenFakePlayer() {
	PlayerCharacter* player = *g_thePlayer;
	TESNPC* npc = DYNAMIC_CAST(player->baseForm, TESForm, TESNPC);
	if (npc) {
		BSFaceGenNiNode* srcfaceNode = player->GetFaceGenNiNode();
		BSFaceGenNiNode* destfaceNode = fakePlayer->GetFaceGenNiNode();
		if (!srcfaceNode || !destfaceNode)
			return;
		for (int i = 0; i < BGSHeadPart::kNumTypes; i++) {
			BGSHeadPart* headPart = npc->GetCurrentHeadPartByType(i);
			if (!headPart)
				continue;
			NiAVObject* srctobj = srcfaceNode->GetObjectByName(&headPart->partName.data);
			NiGeometry* srcgeo;
			BSFaceGenBaseMorphExtraData* srcextraData;
			if (srctobj) {
				srcgeo = srctobj->GetAsNiGeometry();
				if (srcgeo) {
					srcextraData = (BSFaceGenBaseMorphExtraData*)srcgeo->GetExtraData("FOD");
				}
			}
			NiAVObject* destobj = destfaceNode->GetObjectByName(&headPart->partName.data);
			NiGeometry* destgeo;
			BSFaceGenBaseMorphExtraData* destextraData;
			if (destobj) {
				destgeo = destobj->GetAsNiGeometry();
				if (destgeo) {
					destextraData = (BSFaceGenBaseMorphExtraData*)destgeo->GetExtraData("FOD");
				}
			}
			if (srcextraData && destextraData) {
				for (int i = 0; i < destextraData->vertexCount; i++) {
					destextraData->vertexData[i].x = srcextraData->vertexData[i].x;
					destextraData->vertexData[i].y = srcextraData->vertexData[i].y;
					destextraData->vertexData[i].z = srcextraData->vertexData[i].z;
				}
				UpdateModelFace(destfaceNode);
				_MESSAGE("Applied vertex edits to %s. src : 0x%08x, dest : 0x%08x", headPart->partName.data, srcextraData, destextraData);
			}
		}
	}
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

void MenuCloseWatcher::ToggleLight() {
	if (lightEnabled)
		DisableLight();
	else
		EnableLight();
}

InventoryEntryData* currentItem;
void MenuCloseWatcher::CheckBefore3DUpdate(InventoryEntryData* objDesc) {
	currentItem = objDesc;
	switch (objDesc->type->GetFormType()) {
		case kFormType_Armor:
		case kFormType_Ammo:
		case kFormType_Weapon:
			CALL_MEMBER_FN(Inventory3DManager::GetSingleton(), Clear3D)();
			return;
	}
	CALL_MEMBER_FN(Inventory3DManager::GetSingleton(), UpdateItem3D)(objDesc);
}

void MenuCloseWatcher::PreviewEquipment() {
	if (!currentItem)
		return;
	TESForm* item = currentItem->type;
	if (!item->IsWeapon() && !item->IsArmor() && !item->IsAmmo())
		return;
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
		int itemCount = baseCount;
		size_t index = -1;
		for (SimpleInventoryCounter sc = storedItemList.begin(); sc != storedItemList.end(); ++sc) {
			ItemCount ic = *sc;
			index++;
			if (ic.first == item) {
				itemCount += ic.second;
				break;
			}
		}
		if (itemCount == 0) {
			storedItemList.at(index).second += 1;
			ContainerChangeWatcher::InitHook(new WatchData(fakePlayer, item, false));
			VMClassRegistry* registry = (*g_skyrimVM)->GetClassRegistry();
			AddItem_Native(registry, stack, fakePlayer, item, 1, true);
		}
		else {
			CALL_MEMBER_FN(em, EquipItem)(fakePlayer, item, state.itemExtraList, 1, equipSlot, false, false, false, NULL);
			InputWatcher::GetInstance()->shouldUpdate = true;
		}
	}
}

void MenuCloseWatcher::InitializeFakePlayer() {
	if (fakePlayer)
		return;
	PlayerCharacter* player = *g_thePlayer;
	if (!player || !player->GetNiNode())
		return;
	_MESSAGE("Initialize fake player");
	CreateFakePlayer();
	HideFakePlayer();
}

void MenuCloseWatcher::ForceLoadFakePlayer() {
	if (!fakePlayer || fakePlayer->loadedState || activated)
		return;
	PlayerCharacter* player = *g_thePlayer;
	Disable_Native(registry, stack, fakePlayer, false);
	Enable_Native(registry, stack, fakePlayer, false);
	PositionFakePlayer(player->parentCell, CALL_MEMBER_FN(player, GetWorldspace)(), player->pos);
	WaitFakePlayerMoveTask* task = WaitFakePlayerMoveTask::Create(fakePlayer, fakePlayerHandle, NiPoint3(), [](Actor* a, UInt64 h) {
		PositionFakePlayer(a->parentCell, CALL_MEMBER_FN(a, GetWorldspace)(), a->pos - NiPoint3(0, 0, 1000));
		SetActorAlpha(fakePlayer, 0);
	}, "Hide after load");
	if (task)
		g_task->AddTask(task);
}

void MenuCloseWatcher::HideFakePlayerIfNear() {
	if (!fakePlayer || fakePlayer->loadedState || activated)
		return;
	PositionFakePlayer(fakePlayer->parentCell, CALL_MEMBER_FN(fakePlayer, GetWorldspace)(), fakePlayer->pos - NiPoint3(0, 0, 1000));
	_MESSAGE("FakePlayer too near! Hiding...");
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
		_MESSAGE("MenuCloseWatcher added");
	}
	g_serialization->SetUniqueID(skse->GetPluginHandle(), 0x14789632);
	g_serialization->SetSaveCallback(skse->GetPluginHandle(), Save);
	g_serialization->SetLoadCallback(skse->GetPluginHandle(), Load);
	WriteRelCall(0x869D00, GetFnAddr(&CheckBefore3DUpdate));    //Inventory
	WriteRelCall(0x8493BE, GetFnAddr(&CheckBefore3DUpdate));    //Container
}

void MenuCloseWatcher::ResetHook() {
	fakePlayer = nullptr;
	fakePlayerHandle = policy->GetInvalidHandle();
	oldState = nullptr;
	activated = false;
	storedItemList.clear();
	s_WaitMoveTaskPool.Reset();
}

void MenuCloseWatcher::FindCWorld() {
	MenuOpenCloseEventSource::InitHook();
}

void MenuCloseWatcher::GetSkyrimVM() {
	registry = (*g_skyrimVM)->GetClassRegistry();
	policy = registry->GetHandlePolicy();
}

const UInt32 kSerializationDataVersion = 1;
void MenuCloseWatcher::Save(SKSESerializationInterface* si) {
	if (si->OpenRecord('EQPV', kSerializationDataVersion)) {
		if (fakePlayerHandle != policy->GetInvalidHandle()) {
			if (si->WriteRecordData(&fakePlayerHandle, sizeof(fakePlayerHandle))) {
				_MESSAGE("Saved fakePlayer handle");
			}
		}
	}
}

void MenuCloseWatcher::Load(SKSESerializationInterface* si) {
	UInt32 type, version, length;
	while (si->GetNextRecordInfo(&type, &version, &length)) {
		if (type == 'EQPV') {
			UInt64 handle = 0;
			if (si->ReadRecordData(&handle, sizeof(handle))) {
				fakePlayerHandle = handle;
				instance->ResolveFakePlayer();
				if (fakePlayer) {
					instance->ForceLoadFakePlayer();
					CountItems(fakePlayer);
					_MESSAGE("Loaded fakePlayer from data 0x%08x", fakePlayer);
				}
			}
		}
	}
}

bool MenuCloseWatcher::ResolveFakePlayer() {
	if (!fakePlayerHandle || fakePlayerHandle == policy->GetInvalidHandle())
		return false;
	fakePlayer = (Actor*)policy->Resolve(kFormType_Character, fakePlayerHandle);
	if (!fakePlayer)
		return false;
	return true;
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
	if (!fakePlayer) {
		if (ResolveFakePlayer()) {
			if (!fakePlayer->loadedState || !fakePlayer->loadedState->node) {
				return kEvent_Continue;
			}
		}
		else {
			return kEvent_Continue;
		}
	}
	if (evn->menuName == uistr->inventoryMenu ||
		evn->menuName == uistr->containerMenu) {
		if (evn->opening) {
			if (pCam->cameraState != pCam->cameraStates[PlayerCamera::kCameraState_Horse] &&
				pCam->cameraState != pCam->cameraStates[PlayerCamera::kCameraState_Bleedout]) {
				InputWatcher::AddHook();
				SetActorAlpha(player, 0);
				SetupCamera();
				ScaleFakePlayer();
				ShowFakePlayer();
				InputWatcher::GetInstance()->shouldUpdate = true;
				if (g_cworld) {
					g_cworld->m_useSeperatedClock = true;
					g_cworld->m_timeLastUpdate = clock() * 0.001;
				}
				activated = true;
			}
		}
		else {
			if (activated) {
				SetActorAlpha(player, 1);
				InputWatcher::RemoveHook();
				HideFakePlayer();
				RevertCamera();
				if (g_cworld) {
					g_cworld->m_useSeperatedClock = false;
					g_cworld->m_timeLastUpdate = *timeStamp;
				}
				activated = false;
			}
		}
	}
	else if (evn->menuName == uistr->raceSexMenu) {
		if (!evn->opening) {
			DeleteFakePlayer();
			InitializeFakePlayer();
		}
	}
	return kEvent_Continue;
}

void WaitFakePlayerMoveTask::TaskLoop(WaitFakePlayerMoveTask* task) {
	std::this_thread::sleep_for(std::chrono::microseconds(8333));
	g_task->AddTask(task);
}

WaitFakePlayerMoveTask* WaitFakePlayerMoveTask::Create(Actor* f, UInt64 h, NiPoint3 p, Functor fn, const char* n) {
	WaitFakePlayerMoveTask* task = s_WaitMoveTaskPool.Allocate();
	if (task) {
		task->target = f;
		task->handle = h;
		task->targetPos = p;
		task->fn = fn;
		task->name = n;
	}
	if (strcmp(n, "") != 0)
		_MESSAGE("Task %s created.", n);
	return task;
}

void WaitFakePlayerMoveTask::Run() {
	if (time == 0 || !target || !target->loadedState || !target->loadedState->node || !target->GetNiNode() || !target->GetFaceGenNiNode() || (Scale(target->loadedState->node->m_worldTransform.pos - targetPos) > 100 && Scale(targetPos) != 0)) {
		time = *timeStamp;
		std::thread t = std::thread(&TaskLoop, this);
		t.detach();
		return;
	}
	else if (!target || !(*g_thePlayer)->loadedState) {
		dispose = true;
		return;
	}
	_MESSAGE("Running task %s", name);
	(fn)(target, handle);
	dispose = true;
}

void WaitFakePlayerMoveTask::Dispose() {
	if (!dispose)
		return;
	s_WaitMoveTaskPool.Free(this);
}



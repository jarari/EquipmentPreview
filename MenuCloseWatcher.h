#pragma once
#include "HavokDefinitions.h"
//#include <boost/unordered_map.hpp>
//#include <boost/unordered_set.hpp>
//#include <Common/Base/Container/Array/hkArray.h>
#include <SKSE/GameEvents.h>
#include <skse/GameExtraData.h>
#include <skse/GameReferences.h>
#include <skse/GameThreads.h>
#include <SKSE/PluginAPI.h>
#include <memory>
#include <Windows.h>

#pragma region HDTHook

class Lockable
{
public:
	Lockable() { InitializeCriticalSectionAndSpinCount(&cs, 4000); }
	~Lockable() { DeleteCriticalSection(&cs); }

	void lock() { EnterCriticalSection(&cs); }
	void unlock() { LeaveCriticalSection(&cs); }
private:
	CRITICAL_SECTION cs;
};

class CPhysObject : public Lockable {
public:
	virtual ~CPhysObject(void) {};

	virtual bool CreateIfValid() = 0;

	virtual void ReadFromWorld() = 0;
	virtual void WriteToWorld(float invDeltaTime) = 0;

	virtual void AddToWorld(hkpWorld* pWorld) = 0;
	virtual void RemoveFromWorld() = 0;
};

class CSystemObject : CPhysObject {
public:
	CSystemObject();
	virtual ~CSystemObject() {};

	virtual bool CreateIfValid();

	virtual void ReadFromWorld();
	virtual void WriteToWorld(float invDeltaTime);

	virtual void AddToWorld(hkpWorld* pWorld);
	virtual void RemoveFromWorld();

	//void * vtbl						//0x0
	hkpPhysicsSystem* m_system;			//0x1C
	NiNode** m_bones;			//0x20
	size_t boneCount;
	size_t boneArraySize;
	NiNode* m_skeleton;					//0x2C
	hkpWorld* m_world;					//0x30
};
STATIC_ASSERT(offsetof(CSystemObject, m_system) == 0x1C);
STATIC_ASSERT(offsetof(CSystemObject, m_skeleton) == 0x2C);
STATIC_ASSERT(sizeof(CSystemObject) == 0x34);

class CFormObject : public CPhysObject {
public:
	const int m_formID;
};

class CCharacter : CFormObject {
public:
	virtual ~CCharacter() {};

	virtual bool CreateIfValid();

	virtual void ReadFromWorld();
	virtual void WriteToWorld(float invDeltaTime);

	virtual void AddToWorld(hkpWorld* pWorld);
	virtual void RemoveFromWorld();
};

template<typename _KEY, typename _VAL>
struct Bucket {
	Bucket* next;
	Bucket* previous;
	_KEY key;
	_VAL value;
};

class CWorld {
public:
	Lockable m_lock;							//0x0
	//std::unordered_set<UINT32> m_threadInit;
	UInt8 m_threadInit[0x28 - 0x18];			//0x18
	void* m_wind;								//0x28
	hkpWorld* m_pWorld;							//0x2C
	void* m_pMemoryRouter;						//0x30
	void* m_collisionFilter;					//0x34
	void* m_jobQueue;							//0x38
	void* m_jobThreadPool;						//0x3C
	float m_timeLastUpdate;						//0x40
	bool m_useSeperatedClock;					//0x44
	//std::unordered_map<UINT, std::shared_ptr<CCharacter*>> m_characters;
	//std::unordered_multimap<UINT, std::shared_ptr<CSystemObject>> m_systems;
	void RepositionHDT(Actor* a);
};
STATIC_ASSERT(offsetof(CWorld, m_timeLastUpdate) == 0x40);
STATIC_ASSERT(offsetof(CWorld, m_useSeperatedClock) == 0x44);

#pragma endregion


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

typedef void (*_SetPosition_Native)(VMClassRegistry* registry, UInt32 stackId, TESObjectREFR* target, float x, float y, float z);
extern const _SetPosition_Native SetPosition_Native;

typedef void (*Functor)(Actor* t, UInt64 h);
class WaitFakePlayerMoveTask : public TaskDelegate {
public:
	static WaitFakePlayerMoveTask* Create(Actor* f, UInt64 h, NiPoint3 p, Functor fn);
	static void TaskLoop(WaitFakePlayerMoveTask* task);
	virtual void Run();
	virtual void Dispose();
private:
	Actor* target;
	UInt64 handle;
	NiPoint3 targetPos;
	Functor fn;
	float time = 0;
	bool dispose = false;
};

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

	void ToggleLight();

	void UnequipAll(Actor* dest);

	void SyncEquipments(Actor* dest, Actor* src);

	void CheckBefore3DUpdate(InventoryEntryData* objDesc);
	
	void PreviewEquipment();

	void InitializeFakePlayer();

	static void InitHook(const SKSEInterface* skse);

	static void ResetHook();

	static void FindCWorld();

	static void Save(SKSESerializationInterface* si);

	static void Load(SKSESerializationInterface* si);

	Actor* GetFakePlayer();

	virtual EventResult ReceiveEvent(MenuOpenCloseEvent* evn, EventDispatcher<MenuOpenCloseEvent>* src) override;
};

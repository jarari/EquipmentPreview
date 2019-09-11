#include "InputWatcher.h"
#include "MenuCloseWatcher.h"
#include "Utils.h"
#include <skse/GameInput.h>
#include <skse/GameReferences.h>
#include <skse/GameSettings.h>
#include <skse/NiNodes.h>
#include <skse/SafeWrite.h>
using namespace Utils;

InputWatcher* InputWatcher::instance = nullptr;
std::string InputWatcher::className = "InputWatcher";

void InputWatcher::InitHook() {
	if (!instance)
		instance = new InputWatcher();
	(*g_inputEventDispatcher)->AddEventSink((BSTEventSink<InputEvent>*)instance);
	instance->fakePlayer = MenuCloseWatcher::GetInstance()->GetFakePlayer();
	instance->mouseRightDown = false;
}

void InputWatcher::RemoveHook() {
	if (!instance)
		return;
	(*g_inputEventDispatcher)->RemoveEventSink((BSTEventSink<InputEvent>*)instance); 
	instance->mouseRightDown = false;
}

void InputWatcher::AdjustToScale(float scale) {
	PlayerCamera* pCam = PlayerCamera::GetSingleton();
	ThirdPersonState* pCamState = (ThirdPersonState*)pCam->cameraStates[PlayerCamera::kCameraState_ThirdPerson2];
	camDiffZHeight = pCamState->cameraNode->m_localTransform.pos.z * (scale - 1);
}

InputWatcher::~InputWatcher() {
}

EventResult InputWatcher::ReceiveEvent(InputEvent** evns, InputEventDispatcher* dispatcher) {
	if (!fakePlayer)
		fakePlayer = MenuCloseWatcher::GetInstance()->GetFakePlayer();
	if (!fakePlayer->GetNiNode())
		return kEvent_Continue;
	PlayerCamera* pCam = PlayerCamera::GetSingleton();
	ThirdPersonState* pCamState = (ThirdPersonState*)pCam->cameraStates[PlayerCamera::kCameraState_ThirdPerson2];
	float dCamYaw = 0;
	if (evns[0]) {
		for (InputEvent* ie = evns[0]; ie; ie = ie->next) {
			switch (ie->eventType) {
				case InputEvent::kEventType_Button: {
					MenuCloseWatcher* mcw = MenuCloseWatcher::GetInstance();
					ButtonEvent* be = (ButtonEvent*)ie;
					UInt32 deviceType = be->deviceType;
					UInt32 keyMask = be->keyMask;
					UInt32 keyCode;
					if (deviceType == kDeviceType_Mouse) {
						keyCode = InputMap::kMacro_MouseButtonOffset + keyMask;
						bool isDownOnce = be->flags != 0 && be->timer == 0.0;
						bool isUp = be->flags == 0 && be->timer != 0;
						if (keyCode == 257) {
							if (isDownOnce) {
								mouseRightDown = true;
							}
							else if (isUp) {
								mouseRightDown = false;
							}
						}
						else if (keyCode == 258 && isDownOnce) {
							mcw->PreviewEquipment();
						}
					}
					else if (deviceType == kDeviceType_Keyboard) {
						keyCode = keyMask;
						bool isDownOnce = be->flags != 0 && be->timer == 0.0;
						if (isDownOnce) {
							if (keyCode == 54) {
								mcw->SyncEquipments(*g_thePlayer, mcw->GetFakePlayer());
							}
							else if (keyCode == 53) {
								mcw->UnequipAll(mcw->GetFakePlayer());
							}
							else if (keyCode == 52) {
								mcw->SyncEquipments(mcw->GetFakePlayer(), *g_thePlayer);
							}
						}
					}
					break;
				}
				case InputEvent::kEventType_MouseMove: {
					MouseMoveEvent* me = (MouseMoveEvent*)ie;
					SInt32 pitch = *(SInt32*)((UInt32)me + 0x1C);
					SInt32 yaw = *(SInt32*)((UInt32)me + 0x18);
					if (mouseRightDown) {
						camZoom = min(max(camZoom + pitch / 50.0, 0), 1);
						dCamYaw += yaw / 100.0;
					}
					break;
				}
			}
		}
	}
	if (shouldUpdate) {
		CALL_MEMBER_FN((ActorProcessManagerEx*)fakePlayer->processManager, UpdateEquipment)(fakePlayer);
		CALL_MEMBER_FN(fakePlayer, QueueNiNodeUpdate)(false);
		shouldUpdate = false;
	}
	*(float*)((UInt32)pCamState + 0x3C) = LerpUnsafe(camOffsetXNear, camOffsetXFar, camZoom);
	*(float*)((UInt32)pCamState + 0x40) = LerpUnsafe(camOffsetYNear, camOffsetYFar, camZoom);
	*(float*)((UInt32)pCamState + 0x44) = LerpUnsafe(camOffsetZNear, camOffsetZFar, camZoom) + camDiffZHeight;
	*(float*)((UInt32)pCamState + 0xAC) += dCamYaw;
	pCam->Update();
	return kEvent_Continue;
}
